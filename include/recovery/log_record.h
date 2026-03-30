#pragma once

#include<cstdint>
#include<string>
#include "storage/tuple.h" 
#include "storage/pager.h"

namespace aegis{

enum class LogRecordType{
 INVALID=0,
 BEGIN,
 COMMIT,
 ABORT,
 INSERT,
 DELETE,
 NEW_PAGE
};

using lsn_t=int32_t;
using txn_id_t=int32_t;

class LogRecord{

public:
   // 1. Default constructor (Used when reading from the disk back into RAM)
    LogRecord() 
        : size_(0), lsn_(-1), txn_id_(-1), prev_lsn_(-1), log_record_type_(LogRecordType::INVALID) {}

    // 2. Constructor for BEGIN, COMMIT, ABORT (These don't need a data payload)
    LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType log_record_type)
        : size_(20), // 20 bytes for the standard header
          lsn_(-1), txn_id_(txn_id), prev_lsn_(prev_lsn), log_record_type_(log_record_type) {}

    // 3. Constructor for INSERT or DELETE (Requires the PageId and the Data)
    LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType log_record_type, PageId page_id, const Tuple& tuple)
        : size_(0), // We will calculate this during serialization!
          lsn_(-1), txn_id_(txn_id), prev_lsn_(prev_lsn), log_record_type_(log_record_type),
          target_page_id_(page_id), payload_tuple_(tuple) {}

    lsn_t GetLSN() const { return lsn_; }
    void SetLSN(lsn_t lsn) { lsn_ = lsn; }
    
    txn_id_t GetTxnId() const { return txn_id_; }
    lsn_t GetPrevLSN() const { return prev_lsn_; }
    LogRecordType GetLogRecordType() const { return log_record_type_; }
    
    PageId GetTargetPageId() const { return target_page_id_; }
    Tuple GetPayloadTuple() const { return payload_tuple_; }

    int32_t GetSize() const { return size_; }
    void SetSize(int32_t size) { size_ = size; }

private:
    // =====================================
    // THE HEADER (20 Bytes)
    // =====================================
    int32_t size_;                  // 4 bytes: Total size of this record 
    lsn_t lsn_;                     // 4 bytes: The unique ID of this record
    txn_id_t txn_id_;               // 4 bytes: Which transaction did this?
    lsn_t prev_lsn_;                // 4 bytes: The LSN of the previous action in this transaction
    LogRecordType log_record_type_; // 4 bytes: Enum 

    // =====================================
    // THE PAYLOAD (Variable Bytes)
    // =====================================
    PageId target_page_id_{-1};
    Tuple payload_tuple_{};
};
}