#pragma once

#include "common/rwlatch.h"
#include "storage/pager.h"

namespace aegis{

class Page{
    public:
    Page()=default;
    ~Page()=default;

    char* GetData(){
        return data_;
    }   

    PageId GetPageId() const{
        return page_id_;
    }

    //Concurrency
    void RLock(){
        latch_.RLock();
    }
    void RUnlock(){
        latch_.RUnlock();
    }
    void WLock(){
        latch_.WLock(); 

    }
    void WUnlock(){
        latch_.WUnlock();
    }

private:
    friend class BufferPool;

    void ResetMemory(){
        std::memset(data_,0,PAGE_SIZE);
    }

    char data_[PAGE_SIZE];
    PageId page_id_;
    ReaderWriterLatch latch_;
    int pin_count_{0};
    bool is_dirty_{false};


};
}