#include "storage/index/b_plus_tree_internal_page.h"
#include "storage/buffer_pool.h"
#include "storage/tuple.h"                   // <--- ADD THIS LINE
#include "storage/index/tuple_comparator.h"

namespace aegis {

// ==========================================================
// 1. INITIALIZATION
// ==========================================================
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(PageId page_id, PageId parent_id, int max_size) {
    SetPageType(IndexPageType::INTERNAL_PAGE);
    SetSize(0);
    SetMaxSize(max_size);
    SetParentPageId(parent_id);
    SetPageId(page_id);
}

// ==========================================================
// 2. SEARCH / ROUTING (Binary Search)
// ==========================================================
INDEX_TEMPLATE_ARGUMENTS
ValueType B_PLUS_TREE_INTERNAL_PAGE_TYPE::Lookup(const KeyType& key, const KeyComparator& comparator) const {
    // We start searching at index 1, because index 0's key is ignored!
    int left = 1;
    int right = GetSize() - 1;
    int target_index = 0; // Default to the left-most pointer

    while (left <= right) {
        int mid = left + (right - left) / 2;
        int cmp = comparator(array_[mid].first, key);

        // We want the LAST key that is LESS THAN OR EQUAL to our search key
        if (cmp == 0 || cmp == -1) {
            target_index = mid; 
            left = mid + 1; // Keep searching right to see if there is a closer match
        } else {
            right = mid - 1; // We went too far, search left
        }
    }

    // Return the PageId of the child we need to travel down to
    return array_[target_index].second;
}

// ==========================================================
// 3. INSERTION (After a child splits)
// ==========================================================
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertNodeAfter(const ValueType& old_value, const KeyType& new_key, const ValueType& new_value) {
    // 1. Find the old pointer that just split
    int insert_idx = 0;
    for (int i = 0; i < GetSize(); i++) {
        if (array_[i].second == old_value) {
            insert_idx = i + 1; // We insert the new route immediately AFTER the old one
            break;
        }
    }

    // 2. Shift everything to the right to make a gap
    for (int i = GetSize(); i > insert_idx; i--) {
        array_[i] = array_[i - 1];
    }

    // 3. Insert the new routing key and child pointer
    array_[insert_idx].first = new_key;
    array_[insert_idx].second = new_value;
    
    IncreaseSize(1);
    return GetSize();
}

// ==========================================================
// 4. SPLITTING (The Node is Full)
// ==========================================================
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveHalfTo(BPlusTreeInternalPage* recipient, BufferPool* buffer_pool) {
    int split_idx = GetMinSize();
    int items_to_move = GetSize() - split_idx;

    // 1. Move the top half of the keys/pointers to the new recipient page
    for (int i = 0; i < items_to_move; i++) {
        recipient->array_[i].first = this->array_[split_idx + i].first;
        recipient->array_[i].second = this->array_[split_idx + i].second;
    }

    // 2. Update the sizes in the headers
    recipient->SetSize(items_to_move);
    this->SetSize(split_idx);

    // 3. ADOPT THE CHILDREN (The hardest part of Internal Nodes!)
    // The pointers we just moved point to child pages that still think WE are the parent.
    // We must fetch them into RAM and update their parent_page_id to the recipient's PageId.
    for (int i = 0; i < recipient->GetSize(); i++) {
        PageId child_page_id = recipient->ValueAt(i);
        
        // Ask Buffer Pool to fetch the child from the SSD
        Page* child_page = buffer_pool->FetchPage(child_page_id);
        char* child_raw_data = child_page->GetData();
        
        // Cast it as a generic Base Page (we just need to edit the 24-byte header)
        auto* child_node = reinterpret_cast<BPlusTreePage*>(child_raw_data);
        
        // "You belong to the recipient now."
        child_node->SetParentPageId(recipient->GetPageId());
        
        // Mark the child as dirty so the Buffer Pool saves the updated parent ID to SSD!
        buffer_pool->UnpinPage(child_page_id, true); 
    }
}

// ==========================================================
// 5. DELETION (When a child merges and we lose a pointer)
// ==========================================================
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Remove(int index) {
    // Shift all keys and pointers to the left to close the gap
    for (int i = index; i < GetSize() - 1; i++) {
        array_[i] = array_[i + 1];
    }
    IncreaseSize(-1);
}

// Removes the specific child pointer and its associated routing key
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveValue(const ValueType& value) {
    for (int i = 0; i < GetSize(); i++) {
        if (array_[i].second == value) {
            Remove(i);
            break;
        }
    }
    return GetSize();
}

// ==========================================================
// C++ TEMPLATE INSTANTIATION
// ==========================================================
template class BPlusTreeInternalPage<Tuple, PageId, TupleComparator>;

} // namespace aegis    