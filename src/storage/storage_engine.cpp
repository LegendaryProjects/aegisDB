#include "storage/storage_engine.h"
#include "storage/table_page.h" 
#include <iostream>
#include <filesystem>
#include <fstream>

namespace aegis {
StorageEngine::StorageEngine(const std::string& db_file_name, const std::string& log_file_name)
    : db_file_name_(db_file_name), log_file_name_(log_file_name) {
        InitializeSubsystem();
}

void StorageEngine::InitializeSubsystem() {
    pager_ = std::make_unique<Pager>(db_file_name_);
    buffer_pool_ = std::make_unique<BufferPool>(50, pager_.get()); 
    log_manager_ = std::make_unique<LogManager>(log_file_name_);
    TupleComparator cmp(nullptr);
    b_tree_ = std::make_unique<BPlusTree<Tuple, PageId, TupleComparator>>(buffer_pool_.get(), cmp);
}

StorageEngine::~StorageEngine() {
    Shutdown();
}

void StorageEngine::Boot() {
    std::cout << "[StorageEngine] Booting up database..." << std::endl;
    if (std::filesystem::exists(log_file_name_) && std::filesystem::file_size(log_file_name_) > 0) {
        std::cout << "[StorageEngine] Existing local WAL detected. Initiating ARIES startup recovery..." << std::endl;

        if (log_manager_) {
            log_manager_->StopFlushThread();
        }
        b_tree_.reset();
        buffer_pool_.reset();
        pager_.reset();
        log_manager_.reset();

        std::ofstream db_reset(db_file_name_, std::ios::binary | std::ios::trunc);
        db_reset.close();

        InitializeSubsystem();

        LogRecovery recovery(
            log_file_name_,
            buffer_pool_.get(),
            [this](const LogRecord& record) { this->ApplyRecoveredRecord(record); });
        recovery.Redo();
        recovery.Undo();
        std::cout << "[StorageEngine] Local WAL recovery complete. Engine is online (Raft catch-up may continue after join)." << std::endl;
    } else {
        std::cout << "[StorageEngine] Clean boot. Engine is online." << std::endl;
    }
}

void StorageEngine::Shutdown() {
    std::cout << "[StorageEngine] Shutting down..." << std::endl;
    if (log_manager_) {
        log_manager_->Flush();         
        log_manager_->StopFlushThread();
    }
    std::cout << "[StorageEngine] Shutdown complete." << std::endl;
}

// ==========================================================
// THE BRIDGE BETWEEN RAFT AND STORAGE
// ==========================================================
bool StorageEngine::Apply(const Command& cmd, std::string* out_value) {
    return ApplyInternal(cmd, out_value, true);
}

bool StorageEngine::ApplyInternal(const Command& cmd, std::string* out_value, bool write_log) {
    std::unique_lock<std::mutex> lock(execution_latch_);

    static txn_id_t current_txn_id = 1; 
    txn_id_t txn_id = current_txn_id++;
    Transaction txn;

    Tuple key_tuple({Value(cmd.key)});

    if (cmd.type == Command::Type::INSERT) {
        lsn_t op_lsn = -1;
        if (write_log && log_manager_) {
            Tuple log_payload({Value(cmd.key), Value(cmd.value)});
            LogRecord log_record(txn_id, -1, LogRecordType::INSERT, -1, log_payload);
            op_lsn = log_manager_->AppendLogRecord(&log_record);
            log_manager_->Flush();
        }

        // UPSERT semantics: remove old key route if it already exists.
        b_tree_->Remove(key_tuple, &txn);

        Tuple payload_tuple({Value(cmd.value)});
        
        PageId new_page_id = pager_->AllocatePage();
        Page* page = buffer_pool_->FetchPage(new_page_id);
        if (page == nullptr) {
            if (write_log && log_manager_) {
                LogRecord abort_record(txn_id, op_lsn, LogRecordType::ABORT);
                log_manager_->AppendLogRecord(&abort_record);
                log_manager_->Flush();
            }
            return false;
        }

        auto* table_page = reinterpret_cast<TablePage*>(page->GetData());
        table_page->Init(new_page_id, PAGE_SIZE);
        
        RecordId dummy_rid;
        const bool tuple_inserted = table_page->InsertTuple(payload_tuple, &dummy_rid);
        buffer_pool_->UnpinPage(new_page_id, tuple_inserted);

        bool success = tuple_inserted && b_tree_->Insert(key_tuple, new_page_id, &txn);

        if (write_log && log_manager_) {
            LogRecord final_record(txn_id, op_lsn, success ? LogRecordType::COMMIT : LogRecordType::ABORT);
            log_manager_->AppendLogRecord(&final_record);
            log_manager_->Flush();
        }
        
        return success;

    } else if (cmd.type == Command::Type::DELETE) {
        lsn_t op_lsn = -1;
        if (write_log && log_manager_) {
            Tuple log_payload({Value(cmd.key)});
            LogRecord log_record(txn_id, -1, LogRecordType::DELETE, -1, log_payload);
            op_lsn = log_manager_->AppendLogRecord(&log_record);
            log_manager_->Flush();
        }

        bool success = b_tree_->Remove(key_tuple, &txn); 

        if (write_log && log_manager_) {
            LogRecord final_record(txn_id, op_lsn, success ? LogRecordType::COMMIT : LogRecordType::ABORT);
            log_manager_->AppendLogRecord(&final_record);
            log_manager_->Flush();
        }
        
        return success;
        
    } else if (cmd.type == Command::Type::GET) {
        std::vector<PageId> result;
        bool found = b_tree_->GetValue(key_tuple, &result, &txn);
        
        if (found && !result.empty() && out_value) {
            PageId page_id = result[0];
            Page* page = buffer_pool_->FetchPage(page_id);
            auto* table_page = reinterpret_cast<TablePage*>(page->GetData());
            
            Tuple payload_tuple;
            if (table_page->GetTuple(RecordId(page_id, 0), &payload_tuple)) {
                *out_value = payload_tuple.GetValue(0).GetAsString();
            }
            buffer_pool_->UnpinPage(page_id, false);
        }
        return found;
    }
    return false;
}

void StorageEngine::ApplyRecoveredRecord(const LogRecord& record) {
    if (record.GetLogRecordType() != LogRecordType::INSERT &&
        record.GetLogRecordType() != LogRecordType::DELETE) {
        return;
    }

    Tuple payload = record.GetPayloadTuple();
    Command cmd;

    if (record.GetLogRecordType() == LogRecordType::INSERT) {
        if (payload.GetColumnCount() < 2) {
            return;
        }
        cmd.type = Command::Type::INSERT;
        cmd.key = payload.GetValue(0).GetAsString();
        cmd.value = payload.GetValue(1).GetAsString();
        ApplyInternal(cmd, nullptr, false);
        return;
    }

    if (payload.GetColumnCount() < 1) {
        return;
    }
    cmd.type = Command::Type::DELETE;
    cmd.key = payload.GetValue(0).GetAsString();
    ApplyInternal(cmd, nullptr, false);
}

void StorageEngine::CreateSnapshot(const std::string& snapshot_path) {
    std::unique_lock<std::mutex> lock(execution_latch_);
    if(log_manager_) log_manager_->Flush(); 

    try{
        std::filesystem::copy_file(db_file_name_, snapshot_path, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error& ex) {
        std::cerr << "[StorageEngine] Error creating snapshot: " << ex.what() << std::endl;
    }
}

void StorageEngine::InstallSnapshot(const std::string& snapshot_path) {
    std::unique_lock<std::mutex> lock(execution_latch_);
    Shutdown();
    b_tree_.reset();
    buffer_pool_.reset();
    pager_.reset(); 
    log_manager_.reset();

    try{
        std::filesystem::copy_file(snapshot_path, db_file_name_, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error& ex) {
        std::cerr << "[StorageEngine] Error installing snapshot: " << ex.what() << std::endl;
    }

    std::ofstream ofs(log_file_name_, std::ofstream::out | std::ofstream::trunc);
    ofs.close();

    InitializeSubsystem();
}
} // namespace aegis