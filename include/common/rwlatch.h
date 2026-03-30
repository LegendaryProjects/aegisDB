#pragma once

#include<shared_mutex>
#include<mutex>

namespace aegis{

class ReaderWriterLatch{
public:
   ReaderWriterLatch()=default;
   ~ReaderWriterLatch()=default;

   ReaderWriterLatch(const ReaderWriterLatch&)=delete;
   ReaderWriterLatch& operator=(const ReaderWriterLatch&)=delete;

   void RLock(){
         mutex_.lock_shared();
   }

   void RUnlock(){
         mutex_.unlock_shared();
   }

   void WLock(){
         mutex_.lock();
   }    
    void WUnlock(){
            mutex_.unlock();
    }

private:
    std::shared_mutex mutex_;
};
}