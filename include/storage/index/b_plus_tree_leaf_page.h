#pragma once
#include "b_plus_tree_page.h"
#include <utility>

namespace aegis{
#define INDEX_TEMPLATE_ARGUMENTS template <typename KeyType, typename ValueType, typename KeyComparator>
#define B_PLUS_TREE_LEAF_PAGE_TYPE BPlusTreeLeafPage<KeyType,ValueType,KeyComparator>

template<typename KeyType, typename ValueType, typename KeyComparator>

class BPlusTreeLeafPage : public BPlusTreePage{
public :

   void Init(PageId page_id, PageId parent_id = -1, int max_size = 0);

    PageId GetNextPageId()const{
        return next_page_id_;
    }

    void SetNextPageId(PageId next_page_id){
        next_page_id_=next_page_id;
    }

    //array access
    KeyType KeyAt(int index) const {
        return array_[index].first;
    }

    ValueType ValueAt(int index) const{
        return array_[index].second;
    }

    const std::pair<KeyType,ValueType>& GetItem(int index) const{
        return array_[index];
    }

    //core operations
    int Insert(const KeyType& key,const ValueType& value,const KeyComparator& comparator);
    bool Lookup(const KeyType& key, ValueType* value, const KeyComparator& comparator) const;
    int Remove(const KeyType& key,const KeyComparator& comparator);

    void MoveHalfTo(BPlusTreeLeafPage* recipient);
    void MoveLastToFrontOf(BPlusTreeLeafPage* recipient);
    void CopyFirstFrom(const std::pair<KeyType, ValueType>& item);
    void MoveFirstToEndOf(BPlusTreeLeafPage* recipient);
    void CopyLastFrom(const std::pair<KeyType, ValueType>& item);

private:
    PageId next_page_id_;
    std::pair<KeyType,ValueType> array_[0];
};
}