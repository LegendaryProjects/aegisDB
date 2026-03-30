#pragma once

#include "storage/buffer_pool.h"
#include "storage/index/b_plus_tree_leaf_page.h"

namespace aegis{
#define INDEXITERATOR_TYPE IndexIterator<KeyType,ValueType,KeyComparator>
template<typename KeyType, typename ValueType, typename KeyComparator>
class IndexIterator{
public:
    IndexIterator(BufferPool* buffer_pool,PageId currentpage_id,int current_index);

    ~IndexIterator();

    bool IsEnd() const;

    const std::pair<KeyType,ValueType>& operator*() const;

    IndexIterator& operator++();

    bool operator==(const IndexIterator& itr) const;
    bool operator!=(const IndexIterator& itr) const;
private:
    BufferPool* buffer_pool_;
    PageId current_page_id_;
    int current_index_;
    BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>* leaf_page_; // Cached pointer to current page
};
}