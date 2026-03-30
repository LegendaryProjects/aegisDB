#include <gtest/gtest.h>
#include "storage/buffer_pool.h"
#include "storage/pager.h"
#include <filesystem>
#include <string>
#include <cstring>
#include "storage/page.h"

using namespace aegis; // <--- This fixes the "Unknown Type Name" errors!

class BufferPoolTest : public ::testing::Test {
protected:
    const std::string test_file = "test_aegis_pool.db";

    void SetUp() override {
        if (std::filesystem::exists(test_file)) {
            std::filesystem::remove(test_file);
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(test_file)) {
            std::filesystem::remove(test_file);
        }
    }
};

// Test 1: Can we fetch a page, write to it, and read it from RAM?
TEST_F(BufferPoolTest, BasicPinAndUnpin) {
    Pager pager(test_file);
    BufferPool bpm(5, &pager); // Pool size of 5 frames

    PageId page_id = pager.AllocatePage();
    
    // NEW WAY: Fetch the Page object, then get the data
    Page* frame_page = bpm.FetchPage(page_id);
    ASSERT_NE(frame_page, nullptr); // Ensure we got a valid frame
    char* frame_data = frame_page->GetData();
    
    // Write data into the RAM frame
    std::strcpy(frame_data, "Hello from the Buffer Pool!");
    
    // Unpin it (mark as dirty because we modified it)
    bpm.UnpinPage(page_id, true);

    // Fetch it again - this should be a Cache Hit (no disk read)
    Page* page = bpm.FetchPage(page_id);
    ASSERT_NE(page, nullptr);
    char* cached_data = page->GetData();
    EXPECT_STREQ(cached_data, "Hello from the Buffer Pool!");
    bpm.UnpinPage(page_id, false);
}

// Test 2: Does the LRU algorithm evict the correct page?
TEST_F(BufferPoolTest, LRUEviction) {
    Pager pager(test_file);
    BufferPool bpm(3, &pager); // Tiny pool size of 3

    PageId p0 = pager.AllocatePage();
    PageId p1 = pager.AllocatePage();
    PageId p2 = pager.AllocatePage();
    PageId p3 = pager.AllocatePage();

    // Fill the pool (Frames 0, 1, and 2 are full and pinned)
    bpm.FetchPage(p0);
    bpm.FetchPage(p1);
    bpm.FetchPage(p2);

    // Unpin p0 first, then p1. 
    // p0 is the "Least Recently Used" now.
    bpm.UnpinPage(p0, false);
    bpm.UnpinPage(p1, false);

    // Fetch p3. The pool is full, so it MUST evict p0 to make room.
    Page* page = bpm.FetchPage(p3);
    ASSERT_NE(page, nullptr);
    char* p3_data = page->GetData();

    bpm.FetchPage(p1);
    
    // Try to fetch a 4th page while p1, p2, and p3 are ALL pinned.
    // The pool should return nullptr because it has no unpinned frames to evict.
    PageId p4 = pager.AllocatePage();
    Page* p4_page = bpm.FetchPage(p4);
    
    // We expect the PAGE POINTER to be null (no crash by calling GetData here!)
    EXPECT_EQ(p4_page, nullptr);
}

// Test 3: The "Write-Back" Test. Does evicted dirty data actually hit the SSD?
TEST_F(BufferPoolTest, DirtyPageWriteBack) {
    Pager pager(test_file);
    PageId p0 = pager.AllocatePage();
    PageId p1 = pager.AllocatePage();

    {
        BufferPool bpm(1, &pager); // Pool size of ONE. 
        
        // Fetch p0 and modify it
        Page* page = bpm.FetchPage(p0);
        ASSERT_NE(page, nullptr);
        char* p0_data = page->GetData();
        
        std::strcpy(p0_data, "Dirty Data that must be saved");
        bpm.UnpinPage(p0, true); // True = Dirty

        // Fetch p1. Because the pool size is 1, this forces the BPM 
        // to evict p0. Since p0 is dirty, the BPM should silently 
        // call Pager::WritePage to save it to disk before loading p1.
        bpm.FetchPage(p1);
        bpm.UnpinPage(p1, false);
    } // BPM is destroyed here, flushing any remaining dirty pages.

    // Now, bypass the Buffer Pool and use raw Pager to check the physical disk
    char disk_buffer[PAGE_SIZE] = {0};
    pager.ReadPage(p0, disk_buffer);
    
    // Assert the data survived the eviction!
    EXPECT_STREQ(disk_buffer, "Dirty Data that must be saved");
}