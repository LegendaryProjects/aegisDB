#include "recovery/log_recovery.h"
#include <iostream>
#include <utility>

namespace aegis {

namespace {

const char* LogRecordTypeName(LogRecordType type) {
    switch (type) {
        case LogRecordType::INVALID: return "INVALID";
        case LogRecordType::BEGIN: return "BEGIN";
        case LogRecordType::COMMIT: return "COMMIT";
        case LogRecordType::ABORT: return "ABORT";
        case LogRecordType::INSERT: return "INSERT";
        case LogRecordType::DELETE: return "DELETE";
        case LogRecordType::NEW_PAGE: return "NEW_PAGE";
    }
    return "UNKNOWN";
}

}  // namespace

LogRecovery::LogRecovery(const std::string& log_file_name,
                                                 BufferPool* buffer_pool,
                                                 std::function<void(const LogRecord&)> apply_callback)
        : log_file_name_(log_file_name),
            buffer_pool_(buffer_pool),
            apply_callback_(std::move(apply_callback)) {}

LogRecovery::~LogRecovery() = default;

// ==========================================================
// 1. DESERIALIZATION (Raw Bytes -> C++ Object)
// ==========================================================
bool LogRecovery::DeserializeLogRecord(const char* data, int32_t data_size, int32_t& offset, LogRecord* out_record) {
    int32_t start_offset = offset;

    if (offset + static_cast<int32_t>(sizeof(int32_t)) > data_size) {
        return false;
    }

    // 1. Read the Size. If it's 0 or garbage, we hit the end of the file!
    int32_t size = *reinterpret_cast<const int32_t*>(data + offset);
    if (size < 20) {
        return false;
    }
    if (start_offset + size > data_size) {
        return false;
    }
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
        if (offset + static_cast<int32_t>(sizeof(PageId)) > start_offset + size) {
            return false;
        }
        PageId page_id = *reinterpret_cast<const PageId*>(data + offset); 
        offset += sizeof(PageId);
        
        const int32_t tuple_size = size - 24;
        if (tuple_size < 0) {
            return false;
        }

        Tuple tuple;
        tuple.DeserializeFrom(data + offset, tuple_size); // The Tuple knows how to read itself!
        
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
    active_txns_.clear();
    committed_txns_.clear();
    parsed_records_.clear();
    lsn_mapping_.clear();

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
    
    // Demo mode: suppress verbose ARIES internals in terminal output.
    // Uncomment for detailed recovery tracing.
    // std::cout << "--- STARTING ARIES REDO PHASE ---" << std::endl;

    while (DeserializeLogRecord(log_buffer, static_cast<int32_t>(file_size), offset, &record)) {
        // Save the location of this record so we can jump back to it during UNDO
        lsn_mapping_[record.GetLSN()] = offset - record.GetSize();
        parsed_records_.push_back(record);
        
        // Track running transactions
        active_txns_[record.GetTxnId()] = record.GetLSN();

        if (record.GetLogRecordType() == LogRecordType::COMMIT) {
            committed_txns_.insert(record.GetTxnId());
            active_txns_.erase(record.GetTxnId());
        } else if (record.GetLogRecordType() == LogRecordType::ABORT) {
            // The transaction finished, so it is no longer active!
            active_txns_.erase(record.GetTxnId());
        }

        // Print out what we are replaying!
        // std::cout << "REDO: LSN " << record.GetLSN()
        //           << " | Txn: " << record.GetTxnId()
        //           << " | Type: " << LogRecordTypeName(record.GetLogRecordType())
        //           << " (" << static_cast<int>(record.GetLogRecordType()) << ")";
        //
        // if (record.GetLogRecordType() == LogRecordType::INSERT ||
        //     record.GetLogRecordType() == LogRecordType::DELETE) {
        //     std::cout << " | Page: " << record.GetTargetPageId();
        // }
        // std::cout << std::endl;
    }

    if (apply_callback_) {
        int replayed_actions = 0;
        for (const auto& parsed_record : parsed_records_) {
            if (committed_txns_.count(parsed_record.GetTxnId()) == 0) {
                continue;
            }

            if (parsed_record.GetLogRecordType() == LogRecordType::INSERT ||
                parsed_record.GetLogRecordType() == LogRecordType::DELETE) {
                apply_callback_(parsed_record);
                replayed_actions++;
            }
        }
        // std::cout << "REDO applied " << replayed_actions
        //           << " committed local-WAL actions (startup replay)." << std::endl;
    }

    delete[] log_buffer;
    // std::cout << "--- REDO PHASE COMPLETE ---" << std::endl;
}

// ==========================================================
// 3. THE UNDO PHASE (Rollback Failures Backward)
// ==========================================================
// ==========================================================
// 3. THE UNDO PHASE (Rollback Failures Backward)
// ==========================================================
void LogRecovery::Undo() {
    // std::cout << "--- STARTING ARIES UNDO PHASE ---" << std::endl;

    if (active_txns_.empty()) {
        // std::cout << "No active transactions to undo. We are clean!" << std::endl;
        // std::cout << "--- UNDO PHASE COMPLETE ---" << std::endl;
        return;
    }

    // In this implementation, loser transactions are never replayed during REDO,
    // so UNDO only reports what was skipped.
    for (auto const& [txn_id, last_lsn] : active_txns_) {
        // std::cout << "Skipping loser transaction " << txn_id
        //           << " (last LSN " << last_lsn
        //           << "): uncommitted actions were not replayed." << std::endl;
    }

    active_txns_.clear();
    // std::cout << "--- UNDO PHASE COMPLETE ---" << std::endl;
}

} // namespace aegis