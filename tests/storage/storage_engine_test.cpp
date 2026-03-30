#include <gtest/gtest.h>
#include "storage/storage_engine.h"
#include <filesystem>
#include <iostream>
#include "concurrency/transaction.h"

using namespace aegis;


   
TEST(StorageEngineTest, EndToEndApplyTest) {
    std::string db_file = "test_e2e.db";
    std::string log_file = "test_e2e.log";

    // Clean up old files before the test
    if (std::filesystem::exists(db_file)) std::filesystem::remove(db_file);
    if (std::filesystem::exists(log_file)) std::filesystem::remove(log_file);

    // =======================================================
    // PHASE 1: RAFT SENDS COMMANDS
    // =======================================================
    std::cout << "\n--- PHASE 1: ENGINE ONLINE ---" << std::endl;
    {
        StorageEngine engine(db_file, log_file);
        engine.Boot(); // Should trigger a clean boot

        // 1. Raft says: "Consensus reached! Insert key 42."
        Command cmd_insert;
        cmd_insert.type = Command::Type::INSERT;
        cmd_insert.key = 42;
        cmd_insert.value = "Hello Distributed World!";
        
        bool insert_success = engine.Apply(cmd_insert);
        EXPECT_TRUE(insert_success);
        std::cout << "Raft INSERT applied successfully." << std::endl;

        // 2. Raft says: "Read key 42."
        Command cmd_get;
        cmd_get.type = Command::Type::GET;
        cmd_get.key = 42;
        
        bool get_success = engine.Apply(cmd_get);
        EXPECT_TRUE(get_success);
        std::cout << "Raft GET applied successfully." << std::endl;

        // 3. Raft says: "Consensus reached! Delete key 42."
        Command cmd_delete;
        cmd_delete.type = Command::Type::DELETE;
        cmd_delete.key = 42;
        
        bool delete_success = engine.Apply(cmd_delete);
        EXPECT_TRUE(delete_success);
        std::cout << "Raft DELETE applied successfully." << std::endl;

        engine.Shutdown();
    }

    // =======================================================
    // PHASE 2: CRASH AND RECOVER
    // =======================================================
    std::cout << "\n--- PHASE 2: SIMULATING NODE REBOOT ---" << std::endl;
    {
        StorageEngine engine2(db_file, log_file);
        
        // This should detect the log file and automatically trigger ARIES Recovery!
        engine2.Boot(); 
        
        engine2.Shutdown();
    }
    std::cout << "--- END TO END TEST COMPLETE ---\n" << std::endl;
}