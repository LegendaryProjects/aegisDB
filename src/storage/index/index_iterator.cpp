#include "storage/index/index_iterator.h"
#include "storage/tuple.h"                   
#include "storage/index/tuple_comparator.h"
#include "storage/page.h" // <--- Make sure we know what a Page object is!

namespace aegis {

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(BufferPool* buffer_pool, PageId current_page_id, int current_index)
    : buffer_pool_(buffer_pool), current_page_id_(current_page_id), current_index_(current_index), leaf_page_(nullptr) {
    
    if (current_page_id_ != -1) {
        // Fetch the page into memory and cast it to a Leaf Page
        Page* fetched_page = buffer_pool_->FetchPage(current_page_id_);
        char* raw_page = fetched_page->GetData();
        leaf_page_ = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>*>(raw_page);
    }
}

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() {
    if (current_page_id_ != -1 && leaf_page_ != nullptr) {
        // Unpin the page when the iterator is destroyed so we don't leak memory!
        buffer_pool_->UnpinPage(current_page_id_, false);
    }
}

INDEX_TEMPLATE_ARGUMENTS
bool INDEXITERATOR_TYPE::IsEnd() const {
    return current_page_id_ == -1; // -1 means we hit the absolute end of the tree
}

INDEX_TEMPLATE_ARGUMENTS
const std::pair<KeyType, ValueType>& INDEXITERATOR_TYPE::operator*() const {
    return leaf_page_->GetItem(current_index_); 
    // Note: You might need to add `const std::pair<KeyType, ValueType>& GetItem(int index) const { return array_[index]; }` to your leaf page header!
}

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE& INDEXITERATOR_TYPE::operator++() {
    current_index_++;

    // Did we walk off the edge of the current 4KB page?
    if (current_index_ >= leaf_page_->GetSize()) {
        PageId next_page_id = leaf_page_->GetNextPageId();
        
        // Let go of the old page
        buffer_pool_->UnpinPage(current_page_id_, false);
        
        current_page_id_ = next_page_id;
        current_index_ = 0;

        if (current_page_id_ != -1) {
            // Fetch the new page
            Page* fetched_page = buffer_pool_->FetchPage(current_page_id_);
            char* raw_page = fetched_page->GetData();
            leaf_page_ = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>*>(raw_page);
        } else {
            // We reached the very end of the database!
            leaf_page_ = nullptr;
        }
    }
    return *this;
}

INDEX_TEMPLATE_ARGUMENTS
bool INDEXITERATOR_TYPE::operator==(const IndexIterator& itr) const {
    return current_page_id_ == itr.current_page_id_ && current_index_ == itr.current_index_;
}

INDEX_TEMPLATE_ARGUMENTS
bool INDEXITERATOR_TYPE::operator!=(const IndexIterator& itr) const {
    return !(*this == itr);
}

// Explicit Template Instantiation
template class IndexIterator<Tuple, PageId, TupleComparator>;

} // namespace aegis