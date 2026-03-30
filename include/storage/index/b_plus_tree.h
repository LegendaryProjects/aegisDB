#pragma once

#include "storage/buffer_pool.h"
#include "storage/index/b_plus_tree_leaf_page.h"
#include "storage/index/b_plus_tree_internal_page.h"
#include "storage/index/index_iterator.h"
#include "concurrency/transaction.h"

namespace aegis {

enum class Operation { READ, INSERT, DELETE };

template <typename KeyType, typename ValueType, typename KeyComparator>
class BPlusTree {
public:
    // Initialize the tree with the Buffer Pool and our TupleComparator
    BPlusTree(BufferPool* buffer_pool, const KeyComparator& comparator);

    // Returns true if the tree has no data
    bool IsEmpty() const;

    // The public Search function
    bool GetValue(const KeyType& key, std::vector<ValueType>* result, Transaction* transaction = nullptr);
    // The public Insert function
    bool Insert(const KeyType& key, const ValueType& value, Transaction* transaction = nullptr);

   bool Remove(const KeyType& key, Transaction* transaction = nullptr);

    IndexIterator<KeyType, ValueType, KeyComparator> Begin();

    IndexIterator<KeyType, ValueType, KeyComparator> Begin(const KeyType& key);

private:
    // --- Helper Functions ---
    // Traverses down the tree to find the correct leaf page
    Page* FindLeafPage(const KeyType& key, bool leftMost = false, Operation op = Operation::READ, Transaction* transaction = nullptr);

    // Handles the recursive logic of pushing keys up to parents
    void InsertIntoParent(BPlusTreePage* old_node, const KeyType& key, BPlusTreePage* new_node, Transaction* transaction);

    // Creates a brand new root when the tree starts, or when the old root splits
    void StartNewTree(const KeyType& key, const ValueType& value);

    bool CoalesceOrRedistribute(BPlusTreePage* node);
    void Redistribute(BPlusTreePage* neighbor_node, BPlusTreePage* node, int index_in_parent);

    // Variables
    PageId root_page_id_;
    BufferPool* buffer_pool_;
    KeyComparator comparator_;
};

} // namespace aegis