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
include_directories(include "${CMAKE_BINARY_DIR}")

# =====================================================================
# --- NETWORK LAYER (YOUR CODE) ---
# =====================================================================
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG QUIET)

if(gRPC_FOUND)
    set(GRPC_CPP_PLUGIN_EXECUTABLE "$<TARGET_FILE:gRPC::grpc_cpp_plugin>")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GRPC REQUIRED grpc++)
    find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED)

    add_library(gRPC::grpc++ INTERFACE IMPORTED)
    target_include_directories(gRPC::grpc++ INTERFACE ${GRPC_INCLUDE_DIRS})
    target_compile_options(gRPC::grpc++ INTERFACE ${GRPC_CFLAGS_OTHER})
    target_link_libraries(gRPC::grpc++ INTERFACE ${GRPC_LINK_LIBRARIES})
    target_link_options(gRPC::grpc++ INTERFACE ${GRPC_LDFLAGS_OTHER})
endif()

# Generate C++ code from the .proto file
set(PROTO_SRC "${CMAKE_BINARY_DIR}/raft.pb.cc")
set(PROTO_HDR "${CMAKE_BINARY_DIR}/raft.pb.h")
set(GRPC_SRC "${CMAKE_BINARY_DIR}/raft.grpc.pb.cc")
set(GRPC_HDR "${CMAKE_BINARY_DIR}/raft.grpc.pb.h")

add_custom_command(
    OUTPUT "${PROTO_SRC}" "${PROTO_HDR}" "${GRPC_SRC}" "${GRPC_HDR}"
    COMMAND protobuf::protoc
    ARGS --grpc_out="${CMAKE_BINARY_DIR}"
         --cpp_out="${CMAKE_BINARY_DIR}"
         -I "${CMAKE_SOURCE_DIR}/proto"
         --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
         "${CMAKE_SOURCE_DIR}/proto/raft.proto"
    DEPENDS "${CMAKE_SOURCE_DIR}/proto/raft.proto"
)

# Create the network library
add_library(aegis_network
    ${PROTO_SRC}
    ${GRPC_SRC}
)

target_link_libraries(aegis_network PUBLIC
    gRPC::grpc++
    protobuf::libprotobuf
)
# =====================================================================

# 6. Define your Storage Library 
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
    src/recovery/log_manager.cpp
    src/recovery/log_recovery.cpp
    src/storage/storage_engine.cpp
)

# 7. Define Tests 
enable_testing()

add_executable(pager_test tests/storage/pager_test.cpp)
target_link_libraries(pager_test PRIVATE aegis_storage GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(pager_test)

add_executable(buffer_pool_test tests/storage/buffer_pool_test.cpp)
target_link_libraries(buffer_pool_test PRIVATE aegis_storage GTest::gtest_main)
gtest_discover_tests(buffer_pool_test)

add_executable(log_recovery_test tests/recovery/log_recovery_test.cpp)
target_link_libraries(log_recovery_test PRIVATE aegis_storage gtest gtest_main)
gtest_discover_tests(log_recovery_test)

add_executable(storage_engine_test tests/storage/storage_engine_test.cpp)
target_link_libraries(storage_engine_test PRIVATE aegis_storage gtest gtest_main)
gtest_discover_tests(storage_engine_test)

add_executable(schema_metadata_test tests/storage/schema_metadata_test.cpp)
target_link_libraries(schema_metadata_test PRIVATE aegis_storage gtest gtest_main)
gtest_discover_tests(schema_metadata_test)

# Week 11: Benchmarks
add_executable(aegis_benchmark benchmarks/b_tree_benchmark.cpp)
target_link_libraries(aegis_benchmark PRIVATE aegis_storage)

# =====================================================================
# --- FINAL EXECUTABLES ---
# =====================================================================

# Build the Raft Server executable
add_executable(aegis_server 
    src/network/server_main.cpp 
    src/network/raft_server.cpp
)

# Link it to BOTH the network library and the storage library!
target_link_libraries(aegis_server PRIVATE aegis_network aegis_storage gRPC::grpc++ protobuf::libprotobuf)

# Build the interactive CLI Client
add_executable(aegis_client src/network/client_main.cpp)
target_link_libraries(aegis_client PRIVATE aegis_network gRPC::grpc++ protobuf::libprotobuf)