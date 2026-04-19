#pragma once

#include "common/state_machine.h"
#include "storage/pager.h"
#include "storage/buffer_pool.h"
#include "storage/index/b_plus_tree.h"
#include "recovery/log_manager.h"
#include "recovery/log_recovery.h"
#include <memory>
#include <string>
#include <mutex>

namespace aegis {

class StorageEngine : public IStateMachine {
public:
    StorageEngine(const std::string& db_file_name, const std::string& log_file_name);
    ~StorageEngine() override;

    // Signature updated to handle GET commands returning a string
    bool Apply(const Command& cmd, std::string* out_value = nullptr) override;

    void Boot();
    void Shutdown();

    void CreateSnapshot(const std::string& snapshot_path) override;
    void InstallSnapshot(const std::string& snapshot_path) override;

private:
    void InitializeSubsystem();
    bool ApplyInternal(const Command& cmd, std::string* out_value, bool write_log);
    void ApplyRecoveredRecord(const LogRecord& record);

    std::string db_file_name_;
    std::string log_file_name_;
    std::mutex execution_latch_;

    std::unique_ptr<Pager> pager_;
    std::unique_ptr<BufferPool> buffer_pool_;
    std::unique_ptr<LogManager> log_manager_;
    std::unique_ptr<BPlusTree<Tuple, PageId, TupleComparator>> b_tree_;
};

} // namespace aegis