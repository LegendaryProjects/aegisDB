#include "storage/storage_engine.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <filesystem>
#include <random>

using namespace aegis;

void RunBenchmark(int num_operations, int buffer_pool_size) {
    std::string db_file = "benchmark.db";
    std::string log_file = "benchmark.log";

    if (std::filesystem::exists(db_file)) std::filesystem::remove(db_file);
    if (std::filesystem::exists(log_file)) std::filesystem::remove(log_file);

    std::cout << "========================================" << std::endl;
    std::cout << " AEGIS DB BENCHMARK" << std::endl;
    std::cout << " Operations: " << num_operations << std::endl;
    std::cout << " Buffer Pool Size: " << buffer_pool_size << " pages" << std::endl;
    std::cout << "========================================" << std::endl;

    StorageEngine engine(db_file, log_file);
    engine.Boot();

    // --- WRITE BENCHMARK (INSERT) ---
    std::cout << "\n[1] Starting WRITE Benchmark..." << std::endl;
    auto start_write = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_operations; i++) {
        Command cmd;
        cmd.type = Command::Type::INSERT;
        cmd.key = i; 
        cmd.value = "Benchmark Data Payload " + std::to_string(i);
        engine.Apply(cmd);
    }

    auto end_write = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> write_duration = end_write - start_write;
    
    double writes_per_second = num_operations / write_duration.count();
    std::cout << " -> Write Time: " << write_duration.count() << " seconds" << std::endl;
    std::cout << " -> Speed: " << (int)writes_per_second << " INSERTS / sec" << std::endl;

    // --- READ BENCHMARK (GET) ---
    std::cout << "\n[2] Starting READ Benchmark..." << std::endl;
    
    // Generate random keys to read so we don't just hit the cached pages sequentially
    std::vector<int> read_keys(num_operations);
    for (int i = 0; i < num_operations; i++) read_keys[i] = i;
    std::shuffle(read_keys.begin(), read_keys.end(), std::mt19937{std::random_device{}()});

    auto start_read = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_operations; i++) {
        Command cmd;
        cmd.type = Command::Type::GET;
        cmd.key = read_keys[i];
        engine.Apply(cmd);
    }

    auto end_read = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> read_duration = end_read - start_read;
    
    double reads_per_second = num_operations / read_duration.count();
    std::cout << " -> Read Time: " << read_duration.count() << " seconds" << std::endl;
    std::cout << " -> Speed: " << (int)reads_per_second << " GETS / sec" << std::endl;

    engine.Shutdown();
}

int main() {
    // Let's test 100,000 operations. 
    // We'll use a Buffer Pool of 1000 pages (~4MB of RAM)
    RunBenchmark(100000, 10);
    return 0;
}