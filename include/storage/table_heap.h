#pragma once

#include "storage/buffer_pool.h"
#include "storage/table_page.h"

namespace aegis{

class TableHeap{
public:
    TableHeap(BufferPool* buffer_pool);

    //public API for inserting, deleting, and retrieving tuples
    bool InsertTuple(const Tuple& tuple,RecordId* rid);
    bool GetTuple(const RecordId& rid,Tuple* tuple);
    bool DeleteTuple(const RecordId& rid);

    PageId GetFirstPageId()const{
        return first_page_id_;
    }

private:
    BufferPool* buffer_pool_;
    PageId first_page_id_;
};
}