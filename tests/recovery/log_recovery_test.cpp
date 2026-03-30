#include <gtest/gtest.h>
#include "recovery/log_manager.h"
#include "recovery/log_recovery.h"
#include "storage/tuple.h"
#include <filesystem>

using namespace aegis;

TEST(RecoveryTest, AriesRedoAndUndoTest) {
    std::string log_file = "test_aegis.log";
    if (std::filesystem::exists(log_file)) std::filesystem::remove(log_file);

    // =======================================================
    // PHASE 1: THE SERVER IS RUNNING
    // =======================================================
    {
        LogManager log_manager(log_file);
        Tuple dummy_tuple; 
        
        // Transaction 1: Inserts and Commits (WINNER)
        LogRecord begin1(1, -1, LogRecordType::BEGIN);
        lsn_t lsn1 = log_manager.AppendLogRecord(&begin1);
        
        LogRecord insert1(1, lsn1, LogRecordType::INSERT, 5, dummy_tuple);
        lsn_t lsn2 = log_manager.AppendLogRecord(&insert1);
        
        LogRecord commit1(1, lsn2, LogRecordType::COMMIT);
        log_manager.AppendLogRecord(&commit1);

        // Transaction 2: Inserts, but the server crashes before it can commit! (LOSER)
        LogRecord begin2(2, -1, LogRecordType::BEGIN);
        lsn_t lsn3 = log_manager.AppendLogRecord(&begin2);
        
        LogRecord insert2(2, lsn3, LogRecordType::INSERT, 8, dummy_tuple);
        log_manager.AppendLogRecord(&insert2); // Note: We don't commit Txn 2!

        log_manager.Flush();
    } // CRASH!

    // =======================================================
    // PHASE 2: REBOOT AND RECOVER
    // =======================================================
    {
        LogRecovery recovery_manager(log_file, nullptr);
        
        recovery_manager.Redo(); // Replay everything (Txn 1 & 2)
        recovery_manager.Undo(); // Notice Txn 2 never committed, so it rolls it back!
    }
}