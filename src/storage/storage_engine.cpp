#include "storage/storage_engine.h"
#include <iostream>
#include <filesystem>

namespace aegis {
StorageEngine::StorageEngine(const std::string& db_file_name,const std::string& log_file_name)
    :db_file_name_(db_file_name),log_file_name_(log_file_name){
        InitializeSubsystem();
 }

 void StorageEngine::InitializeSubsystem(){
    pager_=std::make_unique<Pager>(db_file_name_);
    buffer_pool_=std::make_unique<BufferPool>(50,pager_.get()); 
    log_manager_=std::make_unique<LogManager>(log_file_name_);
    TupleComparator cmp(nullptr);
    b_tree_=std::make_unique<BPlusTree<Tuple,PageId,TupleComparator>>(buffer_pool_.get(),cmp);
 }

StorageEngine::~StorageEngine() {
    Shutdown();
}

void StorageEngine::Boot() {
    std::cout << "[StorageEngine] Booting up database..." << std::endl;
    
    // If the log file exists and has data, we need to recover!
    if (std::filesystem::exists(log_file_name_) && std::filesystem::file_size(log_file_name_) > 0) {
        std::cout << "[StorageEngine] Existing log file detected. Initiating ARIES Recovery..." << std::endl;
        LogRecovery recovery(log_file_name_, buffer_pool_.get());
        recovery.Redo();
        recovery.Undo();
        std::cout << "[StorageEngine] Recovery complete. Engine is online." << std::endl;
    } else {
        std::cout << "[StorageEngine] Clean boot. Engine is online." << std::endl;
    }
}

void StorageEngine::Shutdown() {
    std::cout << "[StorageEngine] Shutting down..." << std::endl;
    if (log_manager_) {
        log_manager_->Flush();         // Force any remaining logs to SSD
        log_manager_->StopFlushThread();
    }
    if (buffer_pool_) {
       // buffer_pool_->FlushAllPages(); // Force any dirty RAM pages to SSD
    }
    std::cout << "[StorageEngine] Shutdown complete." << std::endl;
}

// ==========================================================
// THE BRIDGE BETWEEN RAFT AND STORAGE
// ==========================================================
bool StorageEngine::Apply(const Command& cmd) {
    std::unique_lock<std::mutex> lock(execution_latch_);

    static txn_id_t current_txn_id = 1; 
    txn_id_t txn_id = current_txn_id++;

    // 1. CREATE THE BACKPACK FOR THE CRAB!
    Transaction txn;

    Tuple payload_tuple; 

    if (cmd.type == Command::Type::INSERT) {
        LogRecord log_record(txn_id, -1, LogRecordType::INSERT, -1, payload_tuple);
        log_manager_->AppendLogRecord(&log_record);
        
        Tuple key_tuple; 
        // 2. PASS THE TRANSACTION INSTEAD OF nullptr
        bool success = b_tree_->Insert(key_tuple, 1, &txn); 
        
        LogRecord commit_record(txn_id, log_record.GetLSN(), LogRecordType::COMMIT);
        log_manager_->AppendLogRecord(&commit_record);
        
        return success;

    } else if (cmd.type == Command::Type::DELETE) {
        LogRecord log_record(txn_id, -1, LogRecordType::DELETE, -1, payload_tuple);
        log_manager_->AppendLogRecord(&log_record);
        
        Tuple key_tuple; 
        // 3. PASS THE TRANSACTION INSTEAD OF nullptr
        bool success = b_tree_->Remove(key_tuple, &txn); 
        
        LogRecord commit_record(txn_id, log_record.GetLSN(), LogRecordType::COMMIT);
        log_manager_->AppendLogRecord(&commit_record);
        
        return success;
        
    } else if (cmd.type == Command::Type::GET) {
        Tuple key_tuple;
        std::vector<PageId> result;
        return true; 
    }

    return false;
}

void StorageEngine::CreateSnapshot(const std::string& snapshot_path){
    std::unique_lock<std::mutex> lock(execution_latch_);
    std::cout << "[StorageEngine] Creating snapshot at: " << snapshot_path << std::endl;

    //forcing all dirty RAM pages to SSD before snapshot
    if(buffer_pool_){
         
    }

    if(log_manager_){
        log_manager_->Flush(); 
    }

    try{
        std::filesystem::copy_file(db_file_name_, snapshot_path, std::filesystem::copy_options::overwrite_existing);
        std::cout << "[StorageEngine] Snapshot created successfully." << std::endl;
    } catch (const std::filesystem::filesystem_error& ex) {
        std::cerr << "[StorageEngine] Error creating snapshot: " << ex.what() << std::endl;
    }
}

void StorageEngine::InstallSnapshot(const std::string& snapshot_path){
    std::unique_lock<std::mutex> lock(execution_latch_);
    std::cout << "[StorageEngine] Installing snapshot from: " << snapshot_path << std::endl;

    //safely shutdowning all the systems
    Shutdown();
    b_tree_.reset();
    buffer_pool_.reset();
    pager_.reset(); 
    log_manager_.reset();

    try{
        std::filesystem::copy_file(snapshot_path, db_file_name_, std::filesystem::copy_options::overwrite_existing);
        std::cout << "[StorageEngine] Snapshot installed successfully." << std::endl;
    } catch (const std::filesystem::filesystem_error& ex) {
        std::cerr << "[StorageEngine] Error installing snapshot: " << ex.what() << std::endl;
    }

    //wipe all the logs ,because all the data stored in snapshot
    std::ofstream ofs(log_file_name_, std::ofstream::out | std::ofstream::trunc);
    ofs.close();

    //re-initializes all the systems
    InitializeSubsystem();
    std::cout << "[StorageEngine] Systems re-initialized after snapshot installation." << std::endl;
}
} // namespace aegis