#include "storage/table_heap.h"

namespace aegis {

// ==========================================================
// 1. INITIALIZATION
// ==========================================================
TableHeap::TableHeap(BufferPool* buffer_pool) : buffer_pool_(buffer_pool) {
    // When a table is created, we immediately allocate Page 0
    first_page_id_ = buffer_pool_->pager_->AllocatePage();
    
    Page* fetched_page = buffer_pool_->FetchPage(first_page_id_);
    auto* first_page = reinterpret_cast<TablePage*>(fetched_page->GetData());
    first_page->Init(first_page_id_, PAGE_SIZE);
    
    // Unpin and mark dirty so it saves to the SSD
    buffer_pool_->UnpinPage(first_page_id_, true); 
}

// ==========================================================
// 2. INSERTION
// ==========================================================
bool TableHeap::InsertTuple(const Tuple& tuple, RecordId* rid) {
    PageId current_page_id = first_page_id_;

    while (current_page_id != -1) {
        // 1. Fetch the current page
      Page* fetched_page = buffer_pool_->FetchPage(current_page_id);
        auto* table_page = reinterpret_cast<TablePage*>(fetched_page->GetData());

        if (table_page->InsertTuple(tuple, rid)) {
            buffer_pool_->UnpinPage(current_page_id, true);
            return true;
        }

        PageId next_page_id = table_page->GetNextPageId();
        
        if (next_page_id == -1) {
            // Need a new page!
            PageId new_page_id = buffer_pool_->pager_->AllocatePage();
            Page* fetched_new_page = buffer_pool_->FetchPage(new_page_id);
            auto* new_table_page = reinterpret_cast<TablePage*>(fetched_new_page->GetData());
            
            new_table_page->Init(new_page_id, PAGE_SIZE, current_page_id);
            table_page->SetNextPageId(new_page_id);
            
            new_table_page->InsertTuple(tuple, rid);
            
            buffer_pool_->UnpinPage(current_page_id, true);
            buffer_pool_->UnpinPage(new_page_id, true);
            return true;
        }

        // Release the full page and move to the next one in the while loop
        buffer_pool_->UnpinPage(current_page_id, false);
        current_page_id = next_page_id;
    }

    return false;
}

// ==========================================================
// 3. RETRIEVAL
// ==========================================================
bool TableHeap::GetTuple(const RecordId& rid, Tuple* tuple) {
    // Because the B+ Tree gave us the exact PageId, we don't have to scan the linked list!
    // We instantly fetch the exact page we need from the SSD/RAM.
    PageId page_id = rid.GetPageId();
    Page* fetched_page = buffer_pool_->FetchPage(page_id);
    auto* table_page = reinterpret_cast<TablePage*>(fetched_page->GetData());

    bool success = table_page->GetTuple(rid, tuple);
    buffer_pool_->UnpinPage(page_id, false);
    return success;
}

// ==========================================================
// 4. DELETION
// ==========================================================
bool TableHeap::DeleteTuple(const RecordId& rid) {
    PageId page_id = rid.GetPageId();
    Page* fetched_page = buffer_pool_->FetchPage(page_id);
    auto* table_page = reinterpret_cast<TablePage*>(fetched_page->GetData());

    bool success = table_page->DeleteTuple(rid);
    buffer_pool_->UnpinPage(page_id, true);
    return success;
}

} // namespace aegis