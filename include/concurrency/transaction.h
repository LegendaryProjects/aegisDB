#pragma once

#include "storage/page.h"
#include<deque>
#include<memory>

namespace aegis{

class Transaction{
public:
    Transaction()=default;
    ~Transaction()=default;

    Transaction(const Transaction&)=delete;
    Transaction& operator=(const Transaction&)=delete;

    void AddIntoPageSet(Page* page){
         page_set_.push_back(page);
    }

    std::deque<Page*>* GetPageSet(){
        return &page_set_;
    }

    void UnlockAndClearPageSet(){
        for(Page* page:page_set_){
            page->RUnlock();
        }
        page_set_.clear();
    }

    // --- Deleted Pages ---
    // When we delete a node from the tree, we can't actually destroy it in memory 
    // until the transaction is finished, just in case someone else is reading it!
   void AddIntoDeletedPageSet(PageId page_id){
        deleted_page_set_.push_back(page_id);
    }

    std::deque<PageId>* GetDeletedPageSet() {
        return &deleted_page_set_;
    }

    void ClearDeletedPageSet() {
        deleted_page_set_.clear();
    }

private:
    std::deque<Page*> page_set_;
    std::deque<PageId> deleted_page_set_;

};
}