#include "recovery/log_manager.h"
#include <cstring>
#include <iostream>

namespace aegis {

LogManager::LogManager(const std::string& log_file_name) 
    : log_file_name_(log_file_name), enable_logging_(true) {
    
    // Open the log file in Append Mode (Create it if it doesn't exist)
    log_file_.open(log_file_name_, std::ios::app | std::ios::out | std::ios::binary);
    
    // Start the background disk-flushing thread!
    RunFlushThread();
}

LogManager::~LogManager() {
    StopFlushThread();
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

// ==========================================================
// 1. SERIALIZATION (C++ Object -> Raw Bytes)
// ==========================================================
lsn_t LogManager::AppendLogRecord(LogRecord* log_record) {
    std::unique_lock<std::mutex> lock(latch_);

    // 1. Assign this record a globally unique Log Sequence Number
    log_record->SetLSN(next_lsn_++);

    // 2. Calculate exactly how many bytes this record will take up
    int32_t record_size = 20; // The standard 20-byte Header
    
    if (log_record->GetLogRecordType() == LogRecordType::INSERT || 
        log_record->GetLogRecordType() == LogRecordType::DELETE) {
        // Add 4 bytes for the PageId, plus the length of the serialized Tuple
        record_size += sizeof(PageId) + log_record->GetPayloadTuple().GetLength();
    }
    log_record->SetSize(record_size);

    // 3. Double Buffering Check! 
    // Is the buffer too full for this new record?
    if (log_buffer_offset_ + record_size >= LOG_BUFFER_SIZE) {
        need_flush_ = true;
        cv_.notify_one(); // WAKE UP THE BACKGROUND THREAD!
        
        // Wait until the background thread empties the buffer
        cv_.wait(lock, [this]() { return log_buffer_offset_ == 0; });
    }

    // 4. Copy the bytes directly into the RAM buffer
    SerializeLogRecord(log_record, log_buffer_ + log_buffer_offset_);
    log_buffer_offset_ += record_size;

    return log_record->GetLSN();
}

void LogManager::SerializeLogRecord(LogRecord* log_record, char* out_buffer) {
    int offset = 0;

    // --- WRITE THE HEADER ---
    int32_t size = log_record->GetSize();
    lsn_t lsn = log_record->GetLSN();
    txn_id_t txn_id = log_record->GetTxnId();
    lsn_t prev_lsn = log_record->GetPrevLSN();
    LogRecordType type = log_record->GetLogRecordType();

    std::memcpy(out_buffer + offset, &size, sizeof(int32_t));     offset += sizeof(int32_t);
    std::memcpy(out_buffer + offset, &lsn, sizeof(lsn_t));        offset += sizeof(lsn_t);
    std::memcpy(out_buffer + offset, &txn_id, sizeof(txn_id_t));  offset += sizeof(txn_id_t);
    std::memcpy(out_buffer + offset, &prev_lsn, sizeof(lsn_t));   offset += sizeof(lsn_t);
    std::memcpy(out_buffer + offset, &type, sizeof(LogRecordType)); offset += sizeof(LogRecordType);

    // --- WRITE THE PAYLOAD (If it has one) ---
    if (type == LogRecordType::INSERT || type == LogRecordType::DELETE) {
        PageId page_id = log_record->GetTargetPageId();
        std::memcpy(out_buffer + offset, &page_id, sizeof(PageId)); offset += sizeof(PageId);
        
        // Serialize the Tuple directly into the buffer
        log_record->GetPayloadTuple().SerializeTo(out_buffer + offset);
    }
}

// ==========================================================
// 2. THE BACKGROUND FLUSH THREAD
// ==========================================================
void LogManager::RunFlushThread() {
    flush_thread_ = new std::thread([this]() {
        while (enable_logging_) {
            std::unique_lock<std::mutex> lock(latch_);
            
            // Go to sleep until we need to flush, or the DB shuts down
            cv_.wait(lock, [this]() { return need_flush_ || !enable_logging_; });

            if (log_buffer_offset_ > 0) {
                // 1. THE SWAP! Copy the active buffer into the flush buffer
                std::memcpy(flush_buffer_, log_buffer_, log_buffer_offset_);
                int bytes_to_flush = log_buffer_offset_;
                
                // 2. Reset the active buffer so threads can immediately keep writing
                log_buffer_offset_ = 0;
                need_flush_ = false;
                
                // 3. Let the main threads know the log_buffer_ is empty again
                cv_.notify_all();
                
                // 4. Unlock the latch! (We write to the SSD while UNLOCKED so we don't block the DB)
                lock.unlock();

                // 5. Write the copied buffer to the actual hard drive
                log_file_.write(flush_buffer_, bytes_to_flush);
                log_file_.flush();
            }
        }
    });
}

void LogManager::StopFlushThread() {
    if (enable_logging_) {
        enable_logging_ = false;
        need_flush_ = true;
        cv_.notify_one(); // Wake up the thread one last time to exit
        
        if (flush_thread_ != nullptr && flush_thread_->joinable()) {
            flush_thread_->join();
            delete flush_thread_;
            flush_thread_ = nullptr;
        }
    }
}

void LogManager::Flush() {
    std::unique_lock<std::mutex> lock(latch_);
    need_flush_ = true;
    cv_.notify_one();
    // Block until the flush is complete
    cv_.wait(lock, [this]() { return log_buffer_offset_ == 0; });
}

} // namespace aegis