#include "storage/index/b_plus_tree.h"
#include "storage/buffer_pool.h"
#include "storage/pager.h"
#include "storage/tuple.h"
#include "storage/index/tuple_comparator.h"

// Define page sizes (adjust values as needed)
const int LEAF_PAGE_SIZE = 100;
const int INTERNAL_PAGE_SIZE = 100;

namespace aegis {

INDEX_TEMPLATE_ARGUMENTS
BPlusTree<KeyType, ValueType, KeyComparator>::BPlusTree(BufferPool* buffer_pool, const KeyComparator& comparator)
    : buffer_pool_(buffer_pool), comparator_(comparator), root_page_id_(-1) {}

INDEX_TEMPLATE_ARGUMENTS
bool BPlusTree<KeyType, ValueType, KeyComparator>::IsEmpty() const {
    return root_page_id_ == -1;
}

// ==========================================================
// 1. SEARCHING THE TREE
// ==========================================================
INDEX_TEMPLATE_ARGUMENTS
bool BPlusTree<KeyType, ValueType, KeyComparator>::GetValue(const KeyType& key, std::vector<ValueType>* result, Transaction* transaction) {
    if (IsEmpty()) return false;

    // 1. Find the leaf page (It comes back ALREADY READ-LOCKED!)
    Page* leaf_page = FindLeafPage(key, false, Operation::READ, transaction);
    char* raw_leaf = leaf_page->GetData();
    auto* leaf_node = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>*>(raw_leaf);

    // 2. Binary search the leaf!
    ValueType value;
    bool is_found = leaf_node->Lookup(key, &value, comparator_);
    
    if (is_found) {
        result->push_back(value);
    }

    // 3. We are done reading. Let go of the lock and unpin!
    leaf_page->RUnlock();
    buffer_pool_->UnpinPage(leaf_page->GetPageId(), false); 
    
    return is_found;
}

INDEX_TEMPLATE_ARGUMENTS
Page* BPlusTree<KeyType, ValueType, KeyComparator>::FindLeafPage(const KeyType& key, bool leftMost, Operation op, Transaction* transaction) {
    PageId current_page_id = root_page_id_;
    Page* current_page = buffer_pool_->FetchPage(current_page_id);
    
    if (op == Operation::READ) {
        current_page->RLock();
    } else {
        current_page->WLock();
    }

    while (true) {
        char* raw_page = current_page->GetData();
        auto* node = reinterpret_cast<BPlusTreePage*>(raw_page);
        
        if (node->isLeafPage()) {  
            return current_page; // Return the locked leaf!
        }
        
        auto* internal_node = reinterpret_cast<BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>*>(raw_page);
        PageId next_page_id = internal_node->Lookup(key, comparator_);
        Page* next_page = buffer_pool_->FetchPage(next_page_id);
        
        if (op == Operation::READ) {
            next_page->RLock();
            current_page->RUnlock();
            buffer_pool_->UnpinPage(current_page->GetPageId(), false);
        } else {
            // WRITE MODE!
            next_page->WLock();
            
            // 1. Put the parent in the backpack
            transaction->AddIntoPageSet(current_page); 

            // 2. Is the child safe? (Does it have room for at least 1 more key?)
            auto* next_node = reinterpret_cast<BPlusTreePage*>(next_page->GetData());
            bool is_safe = (op == Operation::INSERT && next_node->GetSize() < next_node->GetMaxSize());

            // 3. If safe, empty the backpack!
            if (is_safe) {
                for (Page* p : *transaction->GetPageSet()) {
                    p->WUnlock();
                    buffer_pool_->UnpinPage(p->GetPageId(), false);
                }
                transaction->GetPageSet()->clear();
            }
        }
        
        current_page = next_page;
    }
}

// ==========================================================
// 2. INSERTING INTO THE TREE
// ==========================================================
INDEX_TEMPLATE_ARGUMENTS
bool BPlusTree<KeyType, ValueType, KeyComparator>::Insert(const KeyType& key, const ValueType& value, Transaction* transaction) {
    if (IsEmpty()) {
        PageId new_page_id = buffer_pool_->pager_->AllocatePage();
        Page* fetched_page = buffer_pool_->FetchPage(new_page_id);
        
        fetched_page->WLock(); // Lock the new root!
        
        char* raw_page = fetched_page->GetData();
        auto* leaf_node = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>*>(raw_page);
        leaf_node->Init(new_page_id, -1, LEAF_PAGE_SIZE);
        leaf_node->Insert(key, value, comparator_);
        
        fetched_page->WUnlock(); // Unlock it
        buffer_pool_->UnpinPage(new_page_id, true);
        root_page_id_ = new_page_id;
        return true;
    }

    // 1. Find the target leaf (It comes back WRITE-LOCKED, and the backpack is full of locked parents!)
    Page* leaf_page = FindLeafPage(key, false, Operation::INSERT, transaction);
    char* raw_leaf = leaf_page->GetData();
    auto* leaf_node = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>*>(raw_leaf);

    // 2. Insert into the leaf
    int old_size = leaf_node->GetSize();
    leaf_node->Insert(key, value, comparator_);

    // If duplicate found and not inserted
    if (leaf_node->GetSize() == old_size) {
        leaf_page->WUnlock();
        buffer_pool_->UnpinPage(leaf_page->GetPageId(), false);
        
        // Empty the backpack
        for (Page* p : *transaction->GetPageSet()) {
            p->WUnlock();
            buffer_pool_->UnpinPage(p->GetPageId(), false);
        }
        transaction->GetPageSet()->clear();
        return false;
    }

    // 3. THE SPLIT CHECK
    if (leaf_node->GetSize() > leaf_node->GetMaxSize()) {
        PageId new_leaf_page_id = buffer_pool_->pager_->AllocatePage();
        Page* fetched_new_leaf = buffer_pool_->FetchPage(new_leaf_page_id);
        
        fetched_new_leaf->WLock(); // Lock the new sibling
        
        char* raw_new_leaf = fetched_new_leaf->GetData();
        auto* new_leaf_node = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>*>(raw_new_leaf);
        new_leaf_node->Init(new_leaf_page_id, leaf_node->GetParentPageId(), LEAF_PAGE_SIZE);
        
        leaf_node->MoveHalfTo(new_leaf_node);
        KeyType middle_key = new_leaf_node->KeyAt(0);
        
        // Pass the transaction down to the recursive split!
        InsertIntoParent(leaf_node, middle_key, new_leaf_node, transaction);
        
        fetched_new_leaf->WUnlock();
        buffer_pool_->UnpinPage(new_leaf_page_id, true);
    }

    // 4. WE ARE DONE! Drop the leaf lock and empty the backpack!
    leaf_page->WUnlock();
    buffer_pool_->UnpinPage(leaf_page->GetPageId(), true);

    for (Page* p : *transaction->GetPageSet()) {
        p->WUnlock();
        buffer_pool_->UnpinPage(p->GetPageId(), true); // Mark parents as dirty since pointers might have changed
    }
    transaction->GetPageSet()->clear();

    return true;
}

// Handles the domino effect of splitting all the way up the tree
INDEX_TEMPLATE_ARGUMENTS
void BPlusTree<KeyType, ValueType, KeyComparator>::InsertIntoParent(BPlusTreePage* old_node, const KeyType& key, BPlusTreePage* new_node, Transaction* transaction) {
    if (old_node->isRootPage()) { 
        PageId new_root_page_id = buffer_pool_->pager_->AllocatePage();
        Page* fetched_new_root = buffer_pool_->FetchPage(new_root_page_id);
        char* raw_new_root = fetched_new_root->GetData();
        auto* new_root = reinterpret_cast<BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>*>(raw_new_root);
        new_root->Init(new_root_page_id, -1, INTERNAL_PAGE_SIZE);
        
        // Populate new root manually
        new_root->SetValueAt(0, old_node->GetPageId());
        new_root->SetKeyAt(1, key);
        new_root->SetValueAt(1, new_node->GetPageId());
        new_root->SetSize(2);
        
        old_node->SetParentPageId(new_root_page_id);
        new_node->SetParentPageId(new_root_page_id);
        
        root_page_id_ = new_root_page_id;
        buffer_pool_->UnpinPage(new_root_page_id, true);
        return;
    }

    PageId parent_page_id = old_node->GetParentPageId();
    Page* fetched_parent = buffer_pool_->FetchPage(parent_page_id);
    char* raw_parent = fetched_parent->GetData();
    auto* parent_node = reinterpret_cast<BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>*>(raw_parent);
    
    parent_node->InsertNodeAfter(old_node->GetPageId(), key, new_node->GetPageId());
    new_node->SetParentPageId(parent_page_id);
    
    if (parent_node->GetSize() > parent_node->GetMaxSize()) {
        // Split the parent
        PageId new_parent_page_id = buffer_pool_->pager_->AllocatePage();
        Page* fetched_new_parent = buffer_pool_->FetchPage(new_parent_page_id);
        char* raw_new_parent = fetched_new_parent->GetData();
        auto* new_parent_node = reinterpret_cast<BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>*>(raw_new_parent);
        new_parent_node->Init(new_parent_page_id, parent_node->GetParentPageId(), INTERNAL_PAGE_SIZE);
        
        parent_node->MoveHalfTo(new_parent_node, buffer_pool_);
        
        KeyType middle_key = new_parent_node->KeyAt(0);
        InsertIntoParent(parent_node, middle_key, new_parent_node,transaction);
        
        buffer_pool_->UnpinPage(new_parent_page_id, true);
    }
    
    buffer_pool_->UnpinPage(parent_page_id, true);
}

// ==========================================================
// 3. DELETING FROM THE TREE
// ==========================================================
INDEX_TEMPLATE_ARGUMENTS
bool BPlusTree<KeyType, ValueType, KeyComparator>::Remove(const KeyType& key,Transaction* transaction) {
    if (IsEmpty()) return false;

    Page* fetched_leaf = FindLeafPage(key, false, Operation::DELETE, transaction);
    PageId leaf_page_id = fetched_leaf->GetPageId();
    char* raw_leaf = fetched_leaf->GetData();
    auto* leaf_node = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>*>(raw_leaf);

    int old_size = leaf_node->GetSize();
    leaf_node->Remove(key, comparator_);
    
    if (leaf_node->GetSize() == old_size) {
        // Key not found
        buffer_pool_->UnpinPage(leaf_page_id, false);
        return false;
    }

    // Check for underflow
    if (leaf_node->GetSize() < leaf_node->GetMinSize()) {
        bool node_should_be_deleted = CoalesceOrRedistribute(leaf_node);
        if (node_should_be_deleted) {
            // Handle root deletion if needed
        }
    }

    buffer_pool_->UnpinPage(leaf_page_id, true);
    return true;
}

// Stub for CoalesceOrRedistribute (implement full logic later)
INDEX_TEMPLATE_ARGUMENTS
bool BPlusTree<KeyType, ValueType, KeyComparator>::CoalesceOrRedistribute(BPlusTreePage* node) {
    return false;
}

// ==========================================================
// 4. RANGE SCANS (Iterators)
// ==========================================================

// --- Begin() : Start at the very beginning of the database ---
// --- Begin() ---
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE BPlusTree<KeyType, ValueType, KeyComparator>::Begin() {
    if (IsEmpty()) {
        return INDEXITERATOR_TYPE(buffer_pool_, -1, 0); 
    }

    KeyType dummy_key{}; 
    Page* leaf_page = FindLeafPage(dummy_key, true, Operation::READ);
    PageId left_most_leaf_id = leaf_page->GetPageId();
    
    leaf_page->RUnlock();
    buffer_pool_->UnpinPage(left_most_leaf_id, false);

    return INDEXITERATOR_TYPE(buffer_pool_, left_most_leaf_id, 0);
}


// --- Begin(key) ---
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE BPlusTree<KeyType, ValueType, KeyComparator>::Begin(const KeyType& key) {
    if (IsEmpty()) {
        return INDEXITERATOR_TYPE(buffer_pool_, -1, 0);
    }

    Page* leaf_page = FindLeafPage(key, false, Operation::READ);
    PageId leaf_page_id = leaf_page->GetPageId();
    char* raw_page = leaf_page->GetData();
    auto* leaf_node = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>*>(raw_page);

    int left = 0;
    int right = leaf_node->GetSize() - 1;
    int target_index = leaf_node->GetSize(); 

    while (left <= right) {
        int mid = left + (right - left) / 2;
        int cmp = comparator_(leaf_node->KeyAt(mid), key);

        if (cmp == 0 || cmp == 1) { 
            target_index = mid;
            right = mid - 1; 
        } else {
            left = mid + 1;
        }
    }

    leaf_page->RUnlock();
    buffer_pool_->UnpinPage(leaf_page_id, false);

    return INDEXITERATOR_TYPE(buffer_pool_, leaf_page_id, target_index);
}
// --- Explicit Template Instantiation ---
template class BPlusTree<Tuple, PageId, TupleComparator>;

} // namespace aegis