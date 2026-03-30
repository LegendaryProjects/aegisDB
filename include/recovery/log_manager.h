#pragma once

#include <mutex>
#include <condition_variable>
#include <fstream>
#include <thread>
#include <string>
#include "recovery/log_record.h"

namespace aegis {

// We will use a 16KB buffer. 
// (If an average log is 50 bytes, this holds ~320 actions before needing a disk flush)
const int LOG_BUFFER_SIZE = 16384; 

class LogManager {
public:
    LogManager(const std::string& log_file_name);
    ~LogManager();

    // Starts the background disk-flushing thread
    void RunFlushThread();
    
    // Stops the background thread (called when the database shuts down)
    void StopFlushThread();

    // Called by the B+ Tree whenever it modifies data.
    // Serializes the LogRecord into raw bytes and drops it in the buffer.
    lsn_t AppendLogRecord(LogRecord* log_record);

    // Forces the buffer to write to disk immediately (Used when a user types COMMIT)
    void Flush();

private:
    // Helper to turn a C++ Object into a raw array of bytes
    void SerializeLogRecord(LogRecord* log_record, char* out_buffer);

    std::string log_file_name_;
    std::fstream log_file_;
    
    // --- DOUBLE BUFFERING ---
    char log_buffer_[LOG_BUFFER_SIZE];
    char flush_buffer_[LOG_BUFFER_SIZE];
    
    int log_buffer_offset_{0};    // How full is the current log_buffer?
    lsn_t next_lsn_{0};           // Auto-incrementing ID for every new log
    lsn_t persistent_lsn_{-1};    // The highest LSN that has safely made it to the SSD

    // --- CONCURRENCY ---
    std::mutex latch_;
    std::condition_variable cv_;
    std::thread* flush_thread_{nullptr};
    
    bool enable_logging_{false};
    bool need_flush_{false};      // Flag to wake up the background thread
};

} // namespace aegis