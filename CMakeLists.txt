# 1. Minimum version of CMake required
cmake_minimum_required(VERSION 3.14)

# FIX: Silences the Mac/CMake timestamp warning
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# 2. Name of the project and language (C++)
project(AegisDB VERSION 1.0 LANGUAGES CXX)

# 3. Require modern C++ (C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. Enable Google Test (GTest)
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# 5. Define Includes (so #include "storage/pager.h" works)
include_directories(include)

# 6. Define your Storage Library (FIX: Pointing to actual pager.cpp)
add_library(aegis_storage
    src/storage/pager.cpp
    src/storage/buffer_pool.cpp
    src/storage/table_heap.cpp
    src/storage/index/b_plus_tree.cpp
    src/storage/index/b_plus_tree_leaf_page.cpp
    src/storage/index/b_plus_tree_internal_page.cpp
    src/storage/tuple.cpp
    src/storage/table_page.cpp
    src/type/value.cpp
    src/storage/index/index_iterator.cpp
    src/storage/tuple.cpp
    src/recovery/log_manager.cpp
    src/recovery/log_recovery.cpp
    src/storage/storage_engine.cpp
)

# 7. Define Tests (FIX: Pointing to actual pager_test.cpp)
enable_testing()

add_executable(pager_test tests/storage/pager_test.cpp)

# Link the test file with your storage engine and Google Test
target_link_libraries(pager_test
    PRIVATE
    aegis_storage
    GTest::gtest_main
)

# Register the test so 'ctest' command runs it
include(GoogleTest)
gtest_discover_tests(pager_test)


add_executable(buffer_pool_test tests/storage/buffer_pool_test.cpp)

target_link_libraries(buffer_pool_test
    PRIVATE
    aegis_storage
    GTest::gtest_main
)
gtest_discover_tests(buffer_pool_test)

add_executable(log_recovery_test tests/recovery/log_recovery_test.cpp)

target_link_libraries(log_recovery_test PRIVATE aegis_storage gtest gtest_main)
gtest_discover_tests(log_recovery_test)

add_executable(storage_engine_test tests/storage/storage_engine_test.cpp)
target_link_libraries(storage_engine_test PRIVATE aegis_storage gtest gtest_main)
gtest_discover_tests(storage_engine_test)

# Week 11: Benchmarks
add_executable(aegis_benchmark benchmarks/b_tree_benchmark.cpp)
target_link_libraries(aegis_benchmark PRIVATE aegis_storage)