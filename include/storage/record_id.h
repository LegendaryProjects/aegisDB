#pragma once

#include "storage/pager.h"

namespace aegis {

class RecordId {
public:
    RecordId() : page_id_(-1), slot_num_(0) {}
    RecordId(PageId page_id, uint32_t slot_num) 
        : page_id_(page_id), slot_num_(slot_num) {}

    PageId GetPageId() const { return page_id_; }
    uint32_t GetSlotNum() const { return slot_num_; }

    // Overload the == operator so we can easily compare RecordIds
    bool operator==(const RecordId& other) const {
        return page_id_ == other.page_id_ && slot_num_ == other.slot_num_;
    }

private:
    PageId page_id_;
    uint32_t slot_num_;
};

} // namespace aegis