#include <gtest/gtest.h>
#include "storage/pager.h"
#include <filesystem>
#include <string>
#include <cstring>

// A "Test Fixture" sets up a clean environment before every single test
class PagerTest : public ::testing::Test {
protected:
    const std::string test_file = "test_aegis.db";

    // SetUp runs before each TEST_F starts
    void SetUp() override {
        // Ensure we don't read garbage data from an old test run
        if (std::filesystem::exists(test_file)) {
            std::filesystem::remove(test_file);
        }
    }

    // TearDown runs after each TEST_F finishes
    void TearDown() override {
        // Clean up the disk
        if (std::filesystem::exists(test_file)) {
            std::filesystem::remove(test_file);
        }
    }
};

// Test 1: Can we allocate pages and get sequential IDs?
TEST_F(PagerTest, FileCreationAndAllocation) {
    Pager pager(test_file);
    
    // First page allocated should be ID 0
    EXPECT_EQ(pager.AllocatePage(), 0);
    // Second page allocated should be ID 1
    EXPECT_EQ(pager.AllocatePage(), 1);
    
    // The OS should have physically created the file
    EXPECT_TRUE(std::filesystem::exists(test_file));
}

// Test 2: Can we write 4KB of data and read it back perfectly?
TEST_F(PagerTest, ReadAndWritePage) {
    Pager pager(test_file);
    PageId page_id = pager.AllocatePage();
    
    // Create a 4KB buffer and fill it with our test string
    char write_buffer[PAGE_SIZE] = {0};
    std::strcpy(write_buffer, "AegisDB Storage Engine Test Data");
    
    // Write it to the OS
    pager.WritePage(page_id, write_buffer);
    
    // Create an empty buffer to read into
    char read_buffer[PAGE_SIZE] = {0};
    pager.ReadPage(page_id, read_buffer);
    
    // Assert that what we read is EXACTLY what we wrote
    EXPECT_STREQ(write_buffer, read_buffer);
}

// Test 3: The "Crash Recovery" Test (Crucial for OS Interviews)
TEST_F(PagerTest, PersistenceTest) {
    // Phase 1: Simulate the database running and writing data
    {
        Pager pager(test_file);
        PageId page_id = pager.AllocatePage();
        
        char write_buffer[PAGE_SIZE] = {0};
        std::strcpy(write_buffer, "Data that must survive a crash");
        
        pager.WritePage(page_id, write_buffer);
        pager.Sync(); // Force the OS to flush from RAM to the physical SSD
        
    } // The pager object is destroyed here, closing the OS file descriptor

    // Phase 2: Simulate the database restarting after a crash
    {
        Pager recovered_pager(test_file);
        char read_buffer[PAGE_SIZE] = {0};
        
        // Try to read Page 0 without allocating it again
        recovered_pager.ReadPage(0, read_buffer);
        
        // Assert the data survived the restart
        EXPECT_STREQ(read_buffer, "Data that must survive a crash");
    }
}