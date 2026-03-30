#pragma once

#include "storage/pager.h"
#include "storage/page.h" // <-- Include our new Page object!
#include <unordered_map>
#include <mutex>
#include <list>

using namespace std;

namespace aegis {

using FrameId = int32_t;

class BufferPool {
private:
    size_t pool_size_;
    
    // THE BIG CHANGE: We use a raw array of Pages instead of vector<Frame>
    // because locks (std::shared_mutex) cannot be copied by a vector!
    Page* pages_; 

    unordered_map<PageId, FrameId> page_table_;
    list<FrameId> free_list_;
    
    // The latch for the Buffer Pool mapping (Who is in what frame?)
    mutex latch_;

    unordered_map<FrameId, list<FrameId>::iterator> lru_map_;
    list<FrameId> lru_list_;

    FrameId FindVictim();
    void UpdateLRU(FrameId frame_id);

public:
    Pager* pager_;
    BufferPool(size_t pool_size, Pager* pager);
    ~BufferPool();
    
    // prevent copying
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    // fetching a page into the buffer pool
    Page* FetchPage(PageId page_id);

    // marking a page as dirty after modification
    void MarkDirty(PageId page_id);

    // unpinning a page when done
    void UnpinPage(PageId page_id, bool is_dirty);

    // force flushing all dirty pages to disk
    void FlushPage(PageId page_id);
};

} // namespace aegis