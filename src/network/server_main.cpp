#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <grpcpp/grpcpp.h>
#include "network/raft_server.h"
#include "storage/storage_engine.h"

using grpc::Server;
using grpc::ServerBuilder;
using namespace aegis;

namespace {

// Example cluster addresses for multi-device deployment (edit as needed).
const std::string kNode1Addr = "192.168.1.10:50051";
const std::string kNode2Addr = "192.168.1.11:50052";
const std::string kNode3Addr = "192.168.1.12:50053";

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: ./aegis_server <node_id> <my_addr_ip:port> [peer_addr1] [peer_addr2] ...\n";
        std::cerr << "Example:\n";
        std::cerr << "  Node 1: ./aegis_server 1 " << kNode1Addr << " "
                  << kNode2Addr << " " << kNode3Addr << "\n";
        std::cerr << "  Node 2: ./aegis_server 2 " << kNode2Addr << " "
                  << kNode1Addr << " " << kNode3Addr << "\n";
        std::cerr << "  Node 3: ./aegis_server 3 " << kNode3Addr << " "
                  << kNode1Addr << " " << kNode2Addr << "\n";
        return 1;
    }

    int node_id = std::stoi(argv[1]);
    std::string server_address = argv[2];

    // --- BOOT THE REAL STORAGE ENGINE ---
    // Every node gets its own physical .db and .log file
    std::string db_file = "node_" + std::to_string(node_id) + ".db";
    std::string log_file = "node_" + std::to_string(node_id) + ".log";
    
    StorageEngine db_engine(db_file, log_file);
    db_engine.Boot();

    // Pass the running database engine into the Raft server
    RaftServer service(node_id, &db_engine);

    // Connect to all peer addresses provided in the arguments
    for (int i = 3; i < argc; ++i) {
        service.AddPeer(argv[i]);
    }

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    
    // Register both the Raft cluster service AND the Client API service
    builder.RegisterService((RaftConsensus::Service*)&service);
    builder.RegisterService((ClientAPI::Service*)&service);
    
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Node " << node_id << " listening on " << server_address << std::endl;
    
    server->Wait();
    return 0;
}