#pragma once

#include "storage/pager.h"
#include "storage/tuple.h"
#include "storage/record_id.h"

namespace aegis{

class TablePage{

public:
void Init(PageId page_id,uint32_t page_size,PageId prev_page_id=-1);

bool InsertTuple(const Tuple& tuple,RecordId* rid);

bool GetTuple(const RecordId& rid,Tuple* tuple);

bool DeleteTuple(const RecordId& rid);

PageId GetNextPageId()const{
    return next_page_id_;
}

void SetNextPageId(PageId next_page_id){
    next_page_id_=next_page_id;
}

private:
PageId page_id_;
PageId prev_page_id_;       
PageId next_page_id_;
uint32_t free_space_pointer_;
uint32_t tuple_count_;
uint32_t deleted_tuple_count_;

struct Slot{
    uint32_t offset_;
    uint32_t size_;
};

Slot slot_array_[0];

};

}