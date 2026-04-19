#pragma once

#include <grpcpp/grpcpp.h>
#include "raft.grpc.pb.h"
#include "storage/storage_engine.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <memory>
#include <string>

using grpc::ServerContext;
using grpc::ClientContext;
using grpc::Status;

using aegis::network::RaftConsensus;
using aegis::network::RequestVoteRequest;
using aegis::network::RequestVoteResponse;
using aegis::network::AppendEntriesRequest;
using aegis::network::AppendEntriesResponse;

using aegis::network::ClientAPI;
using aegis::network::TableRequest;
using aegis::network::RowRequest;
using aegis::network::ShowRequest;
using aegis::network::ClientResponse;
using aegis::network::SelectResponse;
using aegis::network::ShowResponse;
using aegis::network::LogEntry;

enum NodeState { FOLLOWER, CANDIDATE, LEADER };

class RaftServer final : public RaftConsensus::Service, public ClientAPI::Service {
private:
    int node_id_;
    std::atomic<NodeState> state_;
    std::atomic<int> current_term_;
    std::atomic<int> votes_received_;
    int voted_for_;
    
    std::vector<LogEntry> log_;       
    int commit_index_;
    int last_applied_;
    int current_leader_id_;           

    aegis::StorageEngine* db_engine_;

    std::mutex state_mutex_;
    std::chrono::time_point<std::chrono::steady_clock> last_heartbeat_;
    std::thread election_thread_;
    std::thread heartbeat_thread_;
    std::atomic<bool> running_;

    std::vector<std::unique_ptr<RaftConsensus::Stub>> peers_;

    void RunElectionTimer();
    void RunHeartbeat();
    void SendRequestVoteToAll();
    void SendHeartbeatsToAll();
    bool RunPreVoteRound(int prospective_term, int last_log_index, int last_log_term);
    bool ReplicateEntryToMajorityUnsafe(int target_log_index);
    void ApplyCommittedEntriesUnsafe();
    int GetLastLogIndexUnsafe() const;
    int GetLastLogTermUnsafe() const;
    bool IsCandidateLogUpToDateUnsafe(int candidate_last_log_index, int candidate_last_log_term) const;
    
    void ApplyLogToStateMachine(const LogEntry& entry);
    std::string RemoveFromIndex(const std::string& index_str, const std::string& pk);

public:
    RaftServer(int id, aegis::StorageEngine* db_engine);
    ~RaftServer();

    void AddPeer(const std::string& peer_address);

    Status PreVote(ServerContext* context, const RequestVoteRequest* request, RequestVoteResponse* response) override;
    Status RequestVote(ServerContext* context, const RequestVoteRequest* request, RequestVoteResponse* response) override;
    Status AppendEntries(ServerContext* context, const AppendEntriesRequest* request, AppendEntriesResponse* response) override;

    Status CreateTable(ServerContext* context, const TableRequest* request, ClientResponse* response) override;
    Status InsertRow(ServerContext* context, const RowRequest* request, ClientResponse* response) override;
    Status DeleteRow(ServerContext* context, const RowRequest* request, ClientResponse* response) override;
    Status DropTable(ServerContext* context, const TableRequest* request, ClientResponse* response) override;
    Status SelectTable(ServerContext* context, const TableRequest* request, SelectResponse* response) override;
    Status ShowTables(ServerContext* context, const ShowRequest* request, ShowResponse* response) override;
};