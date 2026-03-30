#include "storage/index/b_plus_tree_leaf_page.h"
#include "storage/index/tuple_comparator.h"
#include "storage/tuple.h"

namespace aegis {

// ==========================================================
// 1. INITIALIZATION
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(PageId page_id, PageId parent_id, int max_size) {
    SetPageType(IndexPageType::LEAF_PAGE);
    SetSize(0);
    SetMaxSize(max_size);
    SetParentPageId(parent_id);
    SetPageId(page_id);
    SetNextPageId(-1); 
}

// ==========================================================
// 2. SEARCH / LOOKUP (Binary Search)
// ==========================================================
INDEX_TEMPLATE_ARGUMENTS
    bool B_PLUS_TREE_LEAF_PAGE_TYPE::Lookup(const KeyType& key, ValueType* value, const KeyComparator& comparator) const {
    if (GetSize() == 0) return false;

    int left = 0;
    int right = GetSize() - 1;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        int cmp = comparator(array_[mid].first, key);

        if (cmp == 0) {
            *value = array_[mid].second;
            return true; // Found!
        } else if (cmp == -1) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return false;
}

// ==========================================================
// 3. INSERTION
// ==========================================================
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_LEAF_PAGE_TYPE::Insert(const KeyType& key, const ValueType& value, const KeyComparator& comparator) {
    int insert_idx = 0;
    
    // Find the correct sorted position
    while (insert_idx < GetSize() && comparator(array_[insert_idx].first, key) == -1) {
        insert_idx++;
    }

    // Optional: Prevent duplicate keys
    if (insert_idx < GetSize() && comparator(array_[insert_idx].first, key) == 0) {
        return GetSize(); 
    }

    // Shift everything to the right to make a gap
    for (int i = GetSize(); i > insert_idx; i--) {
        array_[i] = array_[i - 1];
    }

    // Insert the new tuple
    array_[insert_idx].first = key;
    array_[insert_idx].second = value;
    
    IncreaseSize(1);
    return GetSize();
}

// ==========================================================
// 4. SPLITTING (Node is Full)
// ==========================================================
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveHalfTo(BPlusTreeLeafPage* recipient) {
    int split_idx = GetMinSize();
    int items_to_move = GetSize() - split_idx;

    // Move the top half to the new page
    for (int i = 0; i < items_to_move; i++) {
        recipient->array_[i].first = this->array_[split_idx + i].first;
        recipient->array_[i].second = this->array_[split_idx + i].second;
    }

    // Fix the Linked List pointers
    recipient->SetNextPageId(this->GetNextPageId());
    this->SetNextPageId(recipient->GetPageId());

    // Update the sizes in the headers
    recipient->SetSize(items_to_move);
    this->SetSize(split_idx);
}

// ==========================================================
// 5. DELETION
// ==========================================================
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_LEAF_PAGE_TYPE::Remove(const KeyType& key, const KeyComparator& comparator) {
    if (GetSize() == 0) return 0;

    int target_idx = -1;
    int left = 0;
    int right = GetSize() - 1;

    // Binary search to find the key to delete
    while (left <= right) {
        int mid = left + (right - left) / 2;
        int cmp = comparator(array_[mid].first, key);

        if (cmp == 0) {
            target_idx = mid;
            break;
        } else if (cmp == -1) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    if (target_idx == -1) return GetSize(); // Key not found

    // Shift everything left to close the gap
    for (int i = target_idx; i < GetSize() - 1; i++) {
        array_[i] = array_[i + 1];
    }

    IncreaseSize(-1);
    return GetSize();
}

// ==========================================================
// 6. BORROWING (Node Underflowed)
// ==========================================================

// --- Borrowing from LEFT Sibling ---
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeLeafPage* recipient) {
    int last_idx = GetSize() - 1;
    auto item_to_steal = array_[last_idx];
    IncreaseSize(-1);
    recipient->CopyFirstFrom(item_to_steal);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::CopyFirstFrom(const std::pair<KeyType, ValueType>& item) {
    for (int i = GetSize(); i > 0; i--) {
        array_[i] = array_[i - 1];
    }
    array_[0] = item;
    IncreaseSize(1);
}

// --- Borrowing from RIGHT Sibling ---
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeLeafPage* recipient) {
    auto item_to_steal = array_[0];
    for (int i = 0; i < GetSize() - 1; i++) {
        array_[i] = array_[i + 1];
    }
    IncreaseSize(-1);
    recipient->CopyLastFrom(item_to_steal);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::CopyLastFrom(const std::pair<KeyType, ValueType>& item) {
    array_[GetSize()] = item;
    IncreaseSize(1);
}

// ==========================================================
// C++ TEMPLATE INSTANTIATION
// ==========================================================
template class BPlusTreeLeafPage<Tuple, PageId, TupleComparator>;

}