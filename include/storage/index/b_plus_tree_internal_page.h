#pragma once
#include "storage/index/b_plus_tree_page.h"  // Fixed include path (removed < > and ./ for local include)
#include<utility>
#include "storage/buffer_pool.h"
#include "storage/tuple.h"                   // <--- ADD THIS LINE
#include "storage/index/tuple_comparator.h"  // <--- ADD THIS LINE
namespace aegis {


#define INDEX_TEMPLATE_ARGUMENTS template <typename KeyType, typename ValueType, typename KeyComparator>
#define B_PLUS_TREE_INTERNAL_PAGE_TYPE BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>

template <typename KeyType, typename ValueType, typename KeyComparator>
class BPlusTreeInternalPage : public BPlusTreePage {
public:  // Added public access specifier for methods
    void Init(PageId page_id, PageId parent_id = -1, int max_size = 0);

    KeyType KeyAt(int index) const {
        return array_[index].first;
    }

    void SetKeyAt(int index, const KeyType& key) {
        array_[index].first = key;
    }

    // ValueType is PageId of child
    ValueType ValueAt(int index) const {
        return array_[index].second;
    }

    void SetValueAt(int index, const ValueType& value) { array_[index].second = value; }

    // Core operations
    ValueType Lookup(const KeyType& key, const KeyComparator& comparator) const;
    int InsertNodeAfter(const ValueType& old_value, const KeyType& new_key, const ValueType& new_value);
    void MoveHalfTo(BPlusTreeInternalPage* recipient, BufferPool* buffer_pool);
    void Remove(int index);  // Added missing declaration
    int RemoveValue(const ValueType& value);  // Added missing declaration

private:
    std::pair<KeyType, ValueType> array_[0];
};

}  // namespace aegis