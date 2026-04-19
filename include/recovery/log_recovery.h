#pragma once

#include "recovery/log_record.h"
#include "storage/buffer_pool.h"
#include <functional>
#include <fstream>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace aegis {

class LogRecovery {
public:
    LogRecovery(const std::string& log_file_name,
                BufferPool* buffer_pool = nullptr,
                std::function<void(const LogRecord&)> apply_callback = nullptr);
    ~LogRecovery();

    // Phase 1: Replay the history forward to rebuild RAM!
    void Redo();

    // Phase 2: Any transaction that didn't COMMIT gets rolled back!
    void Undo();

private:
    // Helper to parse raw bytes back into a C++ LogRecord object
    bool DeserializeLogRecord(const char* data, int32_t data_size, int32_t& offset, LogRecord* out_record);

    std::string log_file_name_;
    BufferPool* buffer_pool_;
    
    // Keeps track of which transactions were currently running when the server crashed
    std::unordered_map<txn_id_t, lsn_t> active_txns_;

    // Transactions that are known winners.
    std::unordered_set<txn_id_t> committed_txns_;

    // Records parsed during REDO scan.
    std::vector<LogRecord> parsed_records_;

    // Optional callback to physically apply committed records.
    std::function<void(const LogRecord&)> apply_callback_;
    
    // Maps an LSN to its exact byte offset in the file (so we can jump backwards during Undo!)
    std::unordered_map<lsn_t, int32_t> lsn_mapping_;
};

} // namespace aegis