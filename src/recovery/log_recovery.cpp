#include "recovery/log_recovery.h"
#include <iostream>

namespace aegis {

LogRecovery::LogRecovery(const std::string& log_file_name, BufferPool* buffer_pool)
    : log_file_name_(log_file_name), buffer_pool_(buffer_pool) {}

LogRecovery::~LogRecovery() = default;

// ==========================================================
// 1. DESERIALIZATION (Raw Bytes -> C++ Object)
// ==========================================================
bool LogRecovery::DeserializeLogRecord(const char* data, int32_t& offset, LogRecord* out_record) {
    int32_t start_offset = offset;

    // 1. Read the Size. If it's 0 or garbage, we hit the end of the file!
    int32_t size = *reinterpret_cast<const int32_t*>(data + offset);
    if (size <= 0) return false; 
    offset += sizeof(int32_t);

    // 2. Read the rest of the 20-byte Header
    lsn_t lsn = *reinterpret_cast<const lsn_t*>(data + offset); 
    offset += sizeof(lsn_t);
    
    txn_id_t txn_id = *reinterpret_cast<const txn_id_t*>(data + offset); 
    offset += sizeof(txn_id_t);
    
    lsn_t prev_lsn = *reinterpret_cast<const lsn_t*>(data + offset); 
    offset += sizeof(lsn_t);
    
    LogRecordType type = *reinterpret_cast<const LogRecordType*>(data + offset); 
    offset += sizeof(LogRecordType);

    // 3. Read the Payload (If it's an action that modifies data)
    if (type == LogRecordType::INSERT || type == LogRecordType::DELETE) {
        PageId page_id = *reinterpret_cast<const PageId*>(data + offset); 
        offset += sizeof(PageId);
        
        Tuple tuple;
        tuple.DeserializeFrom(data + offset,size-24); // The Tuple knows how to read itself!
        
        *out_record = LogRecord(txn_id, prev_lsn, type, page_id, tuple);
    } else {
        // It's just a BEGIN, COMMIT, or ABORT
        *out_record = LogRecord(txn_id, prev_lsn, type);
    }
    
    // 4. Restore the internal variables
    out_record->SetSize(size);
    out_record->SetLSN(lsn);
    
    // 5. Jump the offset to the exact start of the NEXT record
    offset = start_offset + size;
    return true;
}

// ==========================================================
// 2. THE REDO PHASE (Replay History Forward)
// ==========================================================
void LogRecovery::Redo() {
    // 1. Open the file and jump to the very end to find out how big it is
    std::ifstream log_file(log_file_name_, std::ios::binary | std::ios::ate);
    if (!log_file.is_open()) return; // No log file exists yet!

    std::streamsize file_size = log_file.tellg();
    log_file.seekg(0, std::ios::beg); // Jump back to the beginning

    if (file_size <= 0) return; // File is empty

    // 2. Load the entire history into RAM
    char* log_buffer = new char[file_size];
    log_file.read(log_buffer, file_size);
    log_file.close();

    // 3. Scan through the history sequentially!
    int32_t offset = 0;
    LogRecord record;
    
    std::cout << "--- STARTING ARIES REDO PHASE ---" << std::endl;

    while (DeserializeLogRecord(log_buffer, offset, &record)) {
        // Save the location of this record so we can jump back to it during UNDO
        lsn_mapping_[record.GetLSN()] = offset - record.GetSize();
        
        // Track running transactions
        active_txns_[record.GetTxnId()] = record.GetLSN();

        if (record.GetLogRecordType() == LogRecordType::COMMIT || 
            record.GetLogRecordType() == LogRecordType::ABORT) {
            // The transaction finished, so it is no longer active!
            active_txns_.erase(record.GetTxnId());
        }

        // Print out what we are replaying!
        std::cout << "REDO: LSN " << record.GetLSN() 
                  << " | Txn: " << record.GetTxnId() 
                  << " | Type: " << static_cast<int>(record.GetLogRecordType());
                  
        if (record.GetLogRecordType() == LogRecordType::INSERT) {
            std::cout << " | Page: " << record.GetTargetPageId();
            // TODO: Actually fetch the page and insert the tuple!
        }
        std::cout << std::endl;
    }

    delete[] log_buffer;
    std::cout << "--- REDO PHASE COMPLETE ---" << std::endl;
}

// ==========================================================
// 3. THE UNDO PHASE (Rollback Failures Backward)
// ==========================================================
// ==========================================================
// 3. THE UNDO PHASE (Rollback Failures Backward)
// ==========================================================
void LogRecovery::Undo() {
    std::cout << "--- STARTING ARIES UNDO PHASE ---" << std::endl;

    // 1. Open the file to read the records we need to undo
    std::ifstream log_file(log_file_name_, std::ios::binary);
    if (!log_file.is_open() || active_txns_.empty()) {
        std::cout << "No active transactions to undo. We are clean!" << std::endl;
        std::cout << "--- UNDO PHASE COMPLETE ---" << std::endl;
        return;
    }

    // 2. Loop through every Loser Transaction
    for (auto const& [txn_id, last_lsn] : active_txns_) {
        std::cout << "Rolling back Loser Transaction: " << txn_id << std::endl;

        lsn_t current_lsn = last_lsn;

        // Follow the prev_lsn_ chain backwards until we hit -1 (the start of the transaction)
        while (current_lsn != -1) {
            // Find exactly where this record lives in the file
            int32_t offset = lsn_mapping_[current_lsn];
            log_file.seekg(offset, std::ios::beg);

            // Read the size of the record first
            int32_t size;
            log_file.read(reinterpret_cast<char*>(&size), sizeof(int32_t));

            // Now read the whole record into a buffer
            char* record_buffer = new char[size];
            log_file.seekg(offset, std::ios::beg);
            log_file.read(record_buffer, size);

            // Deserialize it
            LogRecord record;
            int32_t temp_offset = 0;
            DeserializeLogRecord(record_buffer, temp_offset, &record);

            // UNDO THE ACTION!
            if (record.GetLogRecordType() == LogRecordType::INSERT) {
                std::cout << "  -> UNDO: Reverting INSERT at LSN " << record.GetLSN() 
                          << " on Page " << record.GetTargetPageId() << std::endl;
                // TODO: Call buffer_pool_ -> FetchPage and actually DELETE the tuple
                
            } else if (record.GetLogRecordType() == LogRecordType::DELETE) {
                std::cout << "  -> UNDO: Reverting DELETE at LSN " << record.GetLSN() 
                          << " on Page " << record.GetTargetPageId() << std::endl;
                // TODO: Call buffer_pool_ -> FetchPage and actually INSERT the tuple back
            }

            // Move backwards down the chain!
            current_lsn = record.GetPrevLSN();
            delete[] record_buffer;
        }
        
        std::cout << "Transaction " << txn_id << " successfully rolled back." << std::endl;
    }

    log_file.close();
    std::cout << "--- UNDO PHASE COMPLETE ---" << std::endl;
}

} // namespace aegis