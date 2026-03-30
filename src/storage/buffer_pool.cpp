#include "storage/buffer_pool.h"
#include <stdexcept>
#include <iostream>

namespace aegis {

BufferPool::BufferPool(size_t pool_size, Pager* pager) : pool_size_(pool_size), pager_(pager) {
    // Allocate the exact number of Page objects we need in RAM
    pages_ = new Page[pool_size_];
    
    for(FrameId i = 0; i < pool_size_; i++) {
        free_list_.push_back(i);
    }
}

BufferPool::~BufferPool() {
    for(size_t i = 0; i < pool_size_; i++) {
        if(pages_[i].is_dirty_ && pages_[i].page_id_ != -1) {
            pager_->WritePage(pages_[i].page_id_, pages_[i].GetData());
        }
    }
    // Free the dynamically allocated array
    delete[] pages_;
}

Page* BufferPool::FetchPage(PageId page_id) {
    lock_guard<mutex> lock(latch_);

    // Check if the page is already in the cache
    if(page_table_.find(page_id) != page_table_.end()) {
        FrameId frame_id = page_table_[page_id];
        pages_[frame_id].pin_count_++;

        if(lru_map_.count(frame_id)) {
            lru_list_.erase(lru_map_[frame_id]);
            lru_map_.erase(frame_id);
        }

        // Return the actual PAGE pointer, not just the data!
        return &pages_[frame_id];
    }

    // If not in cache, find a victim frame
    FrameId victim_frame_id = FindVictim();

    if(victim_frame_id == -1) {
        return nullptr; // All pages pinned!
    }
    
    // If victim page has modified data, write back to SSD
    if(pages_[victim_frame_id].is_dirty_) {
        pager_->WritePage(pages_[victim_frame_id].page_id_, pages_[victim_frame_id].GetData());
        pages_[victim_frame_id].is_dirty_ = false;
    }

    // Cleanup entry of victim page in page_table
    if(pages_[victim_frame_id].page_id_ != -1) {
        page_table_.erase(pages_[victim_frame_id].page_id_);
    }

    // Read the new page from the SSD into the victim frame's data array
    pages_[victim_frame_id].ResetMemory(); // Clear out old data
    pager_->ReadPage(page_id, pages_[victim_frame_id].GetData());    
    
    pages_[victim_frame_id].page_id_ = page_id;
    pages_[victim_frame_id].pin_count_ = 1;
    page_table_[page_id] = victim_frame_id;   

    return &pages_[victim_frame_id];
}

void BufferPool::UnpinPage(PageId page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);

    if(page_table_.find(page_id) == page_table_.end()) return;

    FrameId frame_id = page_table_[page_id];

    pages_[frame_id].is_dirty_ |= is_dirty;

    if(pages_[frame_id].pin_count_ > 0) {
        pages_[frame_id].pin_count_--;
    }

    if(pages_[frame_id].pin_count_ == 0) {
        UpdateLRU(frame_id);
    }
}

void BufferPool::FlushPage(PageId page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    if(page_table_.find(page_id) != page_table_.end()) {
        FrameId frame_id = page_table_[page_id];
        if(pages_[frame_id].is_dirty_) {
            pager_->WritePage(page_id, pages_[frame_id].GetData());
            pages_[frame_id].is_dirty_ = false;
        }
    }
}

// --- Private Helper Functions ---
FrameId BufferPool::FindVictim() {
    if(!free_list_.empty()) {
        FrameId frame_id = free_list_.front();
        free_list_.pop_front();
        return frame_id;
    }

    if(!lru_list_.empty()) {
        FrameId victim_id = lru_list_.back();
        lru_list_.pop_back();
        lru_map_.erase(victim_id);
        return victim_id;
    }

    return -1;
}

void BufferPool::UpdateLRU(FrameId frame_id) {
    if(lru_map_.find(frame_id) != lru_map_.end()) {
        lru_list_.erase(lru_map_[frame_id]);
        lru_map_.erase(frame_id);
    }
   
    lru_list_.push_front(frame_id);
    lru_map_[frame_id] = lru_list_.begin();
}

} // namespace aegis