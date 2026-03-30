#include "storage/table_page.h"
#include<cstring>
#include<iostream>

namespace aegis{

void TablePage::Init(PageId page_id,uint32_t page_size,PageId pre_page_id){
    page_id_=page_id;
    prev_page_id_=pre_page_id;
    next_page_id_=-1;
    free_space_pointer_=page_size;
    tuple_count_=0;
    deleted_tuple_count_=0;
}

//Inserting tuple

bool TablePage::InsertTuple(const Tuple& tuple,RecordId* rid){
    // Note: In a real DB, Tuples have a GetLength() and SerializeTo() method 
    // to convert the integers/strings into a raw byte array.
    uint32_t tuple_size = tuple.GetLength();

    uint32_t required_space = tuple_size + sizeof(Slot);

    uint32_t space_used = 24 + (tuple_count_ * sizeof(Slot));
    uint32_t available_space = free_space_pointer_ - space_used;

    if(available_space < required_space){
        return false; 
    }

    free_space_pointer_ -= tuple_size;

    slot_array_[tuple_count_].offset_= free_space_pointer_;
    slot_array_[tuple_count_].size_= tuple_size;

    //Physically copy the tuple data into the page's data area
    char* destination = reinterpret_cast<char*>(this) + free_space_pointer_;
    tuple.SerializeTo(destination);

    *rid = RecordId(page_id_, tuple_count_);
    
    tuple_count_++;
    return true;
}

//Get data
bool TablePage::GetTuple(const RecordId& rid, Tuple* tuple) {
    uint32_t slot_num = rid.GetSlotNum();

    if (slot_num >= tuple_count_) {
        return false; 
    }

    const Slot& slot = slot_array_[slot_num];

    if (slot.size_ == 0) {
        return false;
    }

    // Jump to the physical bytes and deserialize them back into a C++ Tuple object
    const char* source = reinterpret_cast<const char*>(this) + slot.offset_;
    tuple->DeserializeFrom(source, slot.size_);

    return true;
}

bool TablePage::DeleteTuple(const RecordId& rid) {
    uint32_t slot_num = rid.GetSlotNum();
    
    if (slot_num >= tuple_count_ || slot_array_[slot_num].size_ == 0) {
        return false;
    }
   //add thumbnail
    slot_array_[slot_num].size_ = 0;
    deleted_tuple_count_++;
    
    return true;
}

}