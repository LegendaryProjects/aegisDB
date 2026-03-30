#pragma once
#include<./storage/pager.h>

namespace aegis{
enum class IndexPageType{
    Invalid=0,LEAF_PAGE,INTERNAL_PAGE
};

class BPlusTreePage{
    private:
      IndexPageType page_type_;
      int32_t size_;
      int32_t max_size_;
      PageId parent_page_id;
      PageId page_id_;
      int32_t padding_;

    public:
        IndexPageType GetPageType()const{
            return page_type_;

        }
        void SetPageType(IndexPageType page_type){
            page_type_=page_type;
        }   
        int32_t GetSize()const{
            return size_;
        }
        void SetSize(int32_t size){
            size_=size; 

        }
        
        void IncreaseSize(int32_t delta){
            size_+=delta;
        }   

        int GetMaxSize()const{
            return max_size_;
        }
        void SetMaxSize(int32_t max_size){
            max_size_=max_size;
        }

        
        PageId GetParentPageId()const{
            return parent_page_id;
        }
        void SetParentPageId(PageId parent_page_id){
            parent_page_id=parent_page_id;
        }

        PageId GetPageId()const{
            return page_id_;
        }
        void SetPageId(PageId page_id){
            page_id_=page_id;
        }

        bool isLeafPage() const{
            return page_type_==IndexPageType::LEAF_PAGE;
        }

        bool isRootPage() const{
            return parent_page_id==-1;
        }

        int GetMinSize() const { return isRootPage() ? (isLeafPage() ? 1 : 2) : max_size_ / 2; }


};
}