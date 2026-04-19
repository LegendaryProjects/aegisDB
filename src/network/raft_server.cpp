#include "network/raft_server.h"
#include <iostream>
#include <random>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace {

constexpr int kHeartbeatIntervalMs = 80;
constexpr int kRpcTimeoutMs = 250;
constexpr int kElectionTimeoutMinMs = 2000;
constexpr int kElectionTimeoutMaxMs = 3200;

std::string Trim(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        start++;
    }

    if (start == input.size()) {
        return "";
    }

    size_t end = input.size() - 1;
    while (end > start && std::isspace(static_cast<unsigned char>(input[end]))) {
        end--;
    }
    return input.substr(start, end - start + 1);
}

bool ContainsToken(const std::string& csv, const std::string& token) {
    const std::string normalized = Trim(token);
    if (normalized.empty()) {
        return false;
    }

    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (Trim(item) == normalized) {
            return true;
        }
    }
    return false;
}

std::string AppendUniqueToken(const std::string& csv, const std::string& token) {
    const std::string normalized = Trim(token);
    if (normalized.empty() || ContainsToken(csv, normalized)) {
        return csv;
    }

    if (csv.empty()) {
        return normalized;
    }
    return csv + "," + normalized;
}

}  // namespace

using namespace aegis;
using namespace aegis::network;

RaftServer::RaftServer(int id, aegis::StorageEngine* db_engine) 
    : node_id_(id),
      state_(FOLLOWER),
      current_term_(0),
      votes_received_(0),
      voted_for_(-1),
    commit_index_(-1),
    last_applied_(-1),
      current_leader_id_(-1),
      db_engine_(db_engine),
      running_(true) {
    last_heartbeat_ = std::chrono::steady_clock::now();
    election_thread_ = std::thread(&RaftServer::RunElectionTimer, this);
    heartbeat_thread_ = std::thread(&RaftServer::RunHeartbeat, this);
}

RaftServer::~RaftServer() {
    running_ = false;
    if (election_thread_.joinable()) election_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
}

void RaftServer::AddPeer(const std::string& peer_address) {
    peers_.push_back(RaftConsensus::NewStub(grpc::CreateChannel(peer_address, grpc::InsecureChannelCredentials())));
}

std::string RaftServer::RemoveFromIndex(const std::string& index_str, const std::string& pk) {
    std::stringstream ss(index_str);
    const std::string target = Trim(pk);
    std::string item;
    std::string res;
    while (std::getline(ss, item, ',')) {
        const std::string normalized = Trim(item);
        if (normalized != target && !normalized.empty()) {
            if (!res.empty()) {
                res += ",";
            }
            res += normalized;
        }
    }
    return res;
}

int RaftServer::GetLastLogIndexUnsafe() const {
    return static_cast<int>(log_.size()) - 1;
}

int RaftServer::GetLastLogTermUnsafe() const {
    if (log_.empty()) {
        return 0;
    }
    return log_.back().term();
}

bool RaftServer::IsCandidateLogUpToDateUnsafe(int candidate_last_log_index, int candidate_last_log_term) const {
    const int local_last_term = GetLastLogTermUnsafe();
    if (candidate_last_log_term != local_last_term) {
        return candidate_last_log_term > local_last_term;
    }
    return candidate_last_log_index >= GetLastLogIndexUnsafe();
}

void RaftServer::ApplyCommittedEntriesUnsafe() {
    const int last_log_index = GetLastLogIndexUnsafe();
    if (commit_index_ > last_log_index) {
        commit_index_ = last_log_index;
    }

    while (last_applied_ < commit_index_) {
        last_applied_++;
        if (last_applied_ >= 0 && last_applied_ < static_cast<int>(log_.size())) {
            ApplyLogToStateMachine(log_[last_applied_]);
        }
    }
}

bool RaftServer::ReplicateEntryToMajorityUnsafe(int target_log_index) {
    const int majority = static_cast<int>(peers_.size() + 1) / 2 + 1;
    int success_count = 1;  // leader itself

    for (auto& peer_stub : peers_) {
        ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kRpcTimeoutMs));

        AppendEntriesRequest req;
        req.set_term(current_term_);
        req.set_leader_id(node_id_);
        req.set_prev_log_index(-1);
        req.set_prev_log_term(0);
        req.set_leader_commit(commit_index_);

        for (const auto& entry : log_) {
            LogEntry* new_entry = req.add_entries();
            *new_entry = entry;
        }

        AppendEntriesResponse res;
        if (peer_stub->AppendEntries(&context, req, &res).ok()) {
            if (res.term() > current_term_) {
                current_term_ = res.term();
                state_ = FOLLOWER;
                voted_for_ = -1;
                current_leader_id_ = -1;
                return false;
            }
            if (res.success()) {
                success_count++;
            }
        }

        if (success_count >= majority) {
            break;
        }
    }

    if (success_count >= majority) {
        if (target_log_index > commit_index_) {
            commit_index_ = target_log_index;
        }
        ApplyCommittedEntriesUnsafe();
        return true;
    }

    return false;
}

bool RaftServer::RunPreVoteRound(int prospective_term, int last_log_index, int last_log_term) {
    const int majority = static_cast<int>(peers_.size() + 1) / 2 + 1;
    int votes = 1;  // self pre-vote

    for (auto& peer_stub : peers_) {
        ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kRpcTimeoutMs));

        RequestVoteRequest req;
        req.set_term(prospective_term);
        req.set_candidate_id(node_id_);
        req.set_last_log_index(last_log_index);
        req.set_last_log_term(last_log_term);

        RequestVoteResponse res;
        if (peer_stub->PreVote(&context, req, &res).ok()) {
            if (res.vote_granted()) {
                votes++;
                if (votes >= majority) {
                    return true;
                }
            }

            if (res.term() > current_term_) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                if (res.term() > current_term_) {
                    current_term_ = res.term();
                    state_ = FOLLOWER;
                    voted_for_ = -1;
                    current_leader_id_ = -1;
                    last_heartbeat_ = std::chrono::steady_clock::now();
                }
            }
        }
    }

    return votes >= majority;
}

// --- RELATIONAL TO B+ TREE DISK MAPPER ---
void RaftServer::ApplyLogToStateMachine(const LogEntry& entry) {
    Command cmd;
    std::string tbl = entry.table_name();

    if (entry.op() == aegis::network::Operation::CREATE_TABLE) {
        cmd.type = Command::Type::INSERT;
        cmd.key = "SCHEMA_" + tbl; cmd.value = entry.payload(); db_engine_->Apply(cmd);
        cmd.key = "INDEX_" + tbl; cmd.value = ""; db_engine_->Apply(cmd); 
        
        // Track Table Metadata
        cmd.type = Command::Type::GET; cmd.key = "METADATA_TABLES"; std::string tables;
        db_engine_->Apply(cmd, &tables);
        tables = AppendUniqueToken(tables, tbl);
        cmd.type = Command::Type::INSERT; cmd.value = tables; db_engine_->Apply(cmd);
    } 
    else if (entry.op() == aegis::network::Operation::INSERT_ROW) {
        cmd.type = Command::Type::INSERT;
        cmd.key = "ROW_" + tbl + "_" + entry.pk_value(); cmd.value = entry.payload(); db_engine_->Apply(cmd);
        
        cmd.type = Command::Type::GET; cmd.key = "INDEX_" + tbl; std::string idx;
        if (db_engine_->Apply(cmd, &idx)) {
            idx = AppendUniqueToken(idx, entry.pk_value());
            cmd.type = Command::Type::INSERT; cmd.value = idx; db_engine_->Apply(cmd);
        }
    } 
    else if (entry.op() == aegis::network::Operation::DELETE_ROW) {
        cmd.type = Command::Type::DELETE;
        cmd.key = "ROW_" + tbl + "_" + entry.pk_value(); db_engine_->Apply(cmd);
        
        cmd.type = Command::Type::GET; cmd.key = "INDEX_" + tbl; std::string idx;
        if (db_engine_->Apply(cmd, &idx)) {
            cmd.type = Command::Type::INSERT; cmd.value = RemoveFromIndex(idx, entry.pk_value()); db_engine_->Apply(cmd);
        }
    } 
    else if (entry.op() == aegis::network::Operation::DROP_TABLE) {
        cmd.type = Command::Type::DELETE;
        cmd.key = "SCHEMA_" + tbl; db_engine_->Apply(cmd);
        cmd.key = "INDEX_" + tbl; db_engine_->Apply(cmd);

        // Untrack Table Metadata
        cmd.type = Command::Type::GET; cmd.key = "METADATA_TABLES"; std::string tables;
        if (db_engine_->Apply(cmd, &tables)) {
            cmd.type = Command::Type::INSERT; cmd.value = RemoveFromIndex(tables, tbl); db_engine_->Apply(cmd);
        }
    }
}

// ====================================================================
// --- RELATIONAL CLIENT API ---
// ====================================================================

Status RaftServer::CreateTable(ServerContext* context, const TableRequest* request, ClientResponse* response) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != LEADER) {
        response->set_success(false);
        response->set_message("Not leader. Please retry against the current leader.");
        response->set_current_leader_id(current_leader_id_);
        return Status::OK;
    }
    
    Command cmd; cmd.type = Command::Type::GET; cmd.key = "SCHEMA_" + request->table_name(); std::string dummy;
    if (db_engine_->Apply(cmd, &dummy)) {
        response->set_success(false);
        response->set_message("Error: Table already exists.");
        response->set_current_leader_id(node_id_);
        return Status::OK;
    }

    LogEntry new_entry; new_entry.set_term(current_term_); 
    new_entry.set_op(aegis::network::Operation::CREATE_TABLE); 
    new_entry.set_table_name(request->table_name()); new_entry.set_payload(request->columns());

    log_.push_back(new_entry);
    const int new_log_index = GetLastLogIndexUnsafe();
    const bool committed = ReplicateEntryToMajorityUnsafe(new_log_index);

    if (!committed) {
        response->set_success(false);
        if (state_ == LEADER) {
            response->set_message("Error: Failed to commit CREATE TABLE to majority.");
            response->set_current_leader_id(node_id_);
        } else {
            response->set_message("Not leader. Please retry against the current leader.");
            response->set_current_leader_id(current_leader_id_);
        }
        return Status::OK;
    }

    response->set_success(true);
    response->set_message("Table '" + request->table_name() + "' created successfully.");
    response->set_current_leader_id(node_id_);
    return Status::OK;
}

Status RaftServer::InsertRow(ServerContext* context, const RowRequest* request, ClientResponse* response) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != LEADER) {
        response->set_success(false);
        response->set_message("Not leader. Please retry against the current leader.");
        response->set_current_leader_id(current_leader_id_);
        return Status::OK;
    }
    
    Command cmd; cmd.type = Command::Type::GET; cmd.key = "SCHEMA_" + request->table_name(); std::string schema;
    if (!db_engine_->Apply(cmd, &schema)) {
        response->set_success(false);
        response->set_message("Error: Table does not exist.");
        response->set_current_leader_id(node_id_);
        return Status::OK;
    }

    int pk_index = -1; std::stringstream ss_schema(schema); std::string col; int curr_idx = 0;
    while (std::getline(ss_schema, col, ',')) {
        if (col.find('*') != std::string::npos) { pk_index = curr_idx; break; }
        curr_idx++;
    }

    if (pk_index == -1) {
        response->set_success(false);
        response->set_message("Error: Corrupted Schema (No PK found).");
        response->set_current_leader_id(node_id_);
        return Status::OK;
    }

    std::stringstream ss_vals(request->values()); std::string val; std::string pk_value = ""; curr_idx = 0;
    while (std::getline(ss_vals, val, ',')) {
        if (curr_idx == pk_index) { pk_value = val; break; }
        curr_idx++;
    }

    if (pk_value.empty()) {
        response->set_success(false);
        response->set_message("Error: Missing values. Could not resolve Primary Key.");
        response->set_current_leader_id(node_id_);
        return Status::OK;
    }

    cmd.key = "ROW_" + request->table_name() + "_" + pk_value; std::string dummy;
    if (db_engine_->Apply(cmd, &dummy)) {
        response->set_success(false);
        response->set_message("Error: Duplicate entry '" + pk_value + "' for primary key.");
        response->set_current_leader_id(node_id_);
        return Status::OK;
    }

    LogEntry new_entry; new_entry.set_term(current_term_); 
    new_entry.set_op(aegis::network::Operation::INSERT_ROW); 
    new_entry.set_table_name(request->table_name()); new_entry.set_pk_value(pk_value); new_entry.set_payload(request->values());

    log_.push_back(new_entry);
    const int new_log_index = GetLastLogIndexUnsafe();
    const bool committed = ReplicateEntryToMajorityUnsafe(new_log_index);

    if (!committed) {
        response->set_success(false);
        if (state_ == LEADER) {
            response->set_message("Error: Failed to commit INSERT to majority.");
            response->set_current_leader_id(node_id_);
        } else {
            response->set_message("Not leader. Please retry against the current leader.");
            response->set_current_leader_id(current_leader_id_);
        }
        return Status::OK;
    }

    response->set_success(true);
    response->set_message("Query OK, 1 row affected.");
    response->set_current_leader_id(node_id_);
    return Status::OK;
}

Status RaftServer::DeleteRow(ServerContext* context, const RowRequest* request, ClientResponse* response) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != LEADER) {
        response->set_success(false);
        response->set_message("Not leader. Please retry against the current leader.");
        response->set_current_leader_id(current_leader_id_);
        return Status::OK;
    }

    Command cmd; cmd.type = Command::Type::GET; cmd.key = "ROW_" + request->table_name() + "_" + request->pk_value(); std::string dummy;
    if (!db_engine_->Apply(cmd, &dummy)) {
        response->set_success(false);
        response->set_message("Error: Primary key not found.");
        response->set_current_leader_id(node_id_);
        return Status::OK;
    }

    LogEntry new_entry; new_entry.set_term(current_term_); 
    new_entry.set_op(aegis::network::Operation::DELETE_ROW); 
    new_entry.set_table_name(request->table_name()); new_entry.set_pk_value(request->pk_value());

    log_.push_back(new_entry);
    const int new_log_index = GetLastLogIndexUnsafe();
    const bool committed = ReplicateEntryToMajorityUnsafe(new_log_index);

    if (!committed) {
        response->set_success(false);
        if (state_ == LEADER) {
            response->set_message("Error: Failed to commit DELETE to majority.");
            response->set_current_leader_id(node_id_);
        } else {
            response->set_message("Not leader. Please retry against the current leader.");
            response->set_current_leader_id(current_leader_id_);
        }
        return Status::OK;
    }

    response->set_success(true);
    response->set_message("Query OK, 1 row deleted.");
    response->set_current_leader_id(node_id_);
    return Status::OK;
}

Status RaftServer::DropTable(ServerContext* context, const TableRequest* request, ClientResponse* response) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != LEADER) {
        response->set_success(false);
        response->set_message("Not leader. Please retry against the current leader.");
        response->set_current_leader_id(current_leader_id_);
        return Status::OK;
    }

    Command cmd;
    cmd.type = Command::Type::GET;
    cmd.key = "SCHEMA_" + request->table_name();
    std::string schema;
    if (!db_engine_->Apply(cmd, &schema)) {
        response->set_success(false);
        response->set_message("Error: Table does not exist.");
        response->set_current_leader_id(node_id_);
        return Status::OK;
    }

    LogEntry new_entry; new_entry.set_term(current_term_); 
    new_entry.set_op(aegis::network::Operation::DROP_TABLE); 
    new_entry.set_table_name(request->table_name());

    log_.push_back(new_entry);
    const int new_log_index = GetLastLogIndexUnsafe();
    const bool committed = ReplicateEntryToMajorityUnsafe(new_log_index);

    if (!committed) {
        response->set_success(false);
        if (state_ == LEADER) {
            response->set_message("Error: Failed to commit DROP TABLE to majority.");
            response->set_current_leader_id(node_id_);
        } else {
            response->set_message("Not leader. Please retry against the current leader.");
            response->set_current_leader_id(current_leader_id_);
        }
        return Status::OK;
    }

    response->set_success(true);
    response->set_message("Query OK, Table dropped.");
    response->set_current_leader_id(node_id_);
    return Status::OK;
}

Status RaftServer::ShowTables(ServerContext* context, const ShowRequest* request, ShowResponse* response) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != LEADER) {
        response->set_success(false);
        response->set_data("Not leader. Please retry against the current leader.");
        response->set_current_leader_id(current_leader_id_);
        return Status::OK;
    }

    Command cmd; cmd.type = Command::Type::GET; cmd.key = "METADATA_TABLES"; std::string tables;
    db_engine_->Apply(cmd, &tables);

    if (tables.empty()) {
        response->set_success(true); response->set_data("Empty set"); response->set_current_leader_id(node_id_);
        return Status::OK;
    }

    std::vector<std::string> t_list;
    std::stringstream ss(tables); std::string t;
    size_t max_width = 16; // "Tables_in_aegis"
    while (std::getline(ss, t, ',')) {
        t = Trim(t);
        if (!t.empty()) {
            t_list.push_back(t);
            max_width = std::max(max_width, t.length());
        }
    }

    if (t_list.empty()) {
        response->set_success(true);
        response->set_data("Empty set");
        response->set_current_leader_id(node_id_);
        return Status::OK;
    }

    std::string sep = "+" + std::string(max_width + 2, '-') + "+\n";
    std::string output = sep + "| Tables_in_aegis" + std::string(max_width - 15, ' ') + " |\n" + sep;
    for (const auto& table : t_list) {
        output += "| " + table + std::string(max_width - table.length(), ' ') + " |\n";
    }
    output += sep;

    response->set_success(true); response->set_data(output); response->set_current_leader_id(node_id_);
    return Status::OK;
}

Status RaftServer::SelectTable(ServerContext* context, const TableRequest* request, SelectResponse* response) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != LEADER) {
        response->set_success(false);
        response->set_data("Not leader. Please retry against the current leader.");
        response->set_current_leader_id(current_leader_id_);
        return Status::OK;
    }

    Command cmd; cmd.type = Command::Type::GET; cmd.key = "SCHEMA_" + request->table_name(); std::string schema;
    if (!db_engine_->Apply(cmd, &schema)) {
        response->set_success(false);
        response->set_data("Error: Table '" + request->table_name() + "' doesn't exist");
        response->set_current_leader_id(node_id_);
        return Status::OK;
    }

    std::vector<std::string> cols;
    std::stringstream ss_schema(schema); std::string col;
    while(std::getline(ss_schema, col, ',')) cols.push_back(col);

    cmd.key = "INDEX_" + request->table_name(); std::string index;
    db_engine_->Apply(cmd, &index);

    std::vector<std::vector<std::string>> rows;
    if (!index.empty()) {
        std::stringstream ss_idx(index); std::string pk;
        while(std::getline(ss_idx, pk, ',')) {
            pk = Trim(pk);
            if (pk.empty()) {
                continue;
            }
            cmd.key = "ROW_" + request->table_name() + "_" + pk; std::string row_data;
            if (db_engine_->Apply(cmd, &row_data)) {
                std::vector<std::string> r_vals; std::stringstream ss_row(row_data); std::string val;
                while(std::getline(ss_row, val, ',')) r_vals.push_back(val);
                rows.push_back(r_vals);
            }
        }
    }

    std::vector<size_t> widths(cols.size(), 0);
    for (size_t i = 0; i < cols.size(); i++) widths[i] = cols[i].length();
    for (const auto& r : rows) {
        for (size_t i = 0; i < std::min(r.size(), widths.size()); i++) {
            widths[i] = std::max(widths[i], r[i].length());
        }
    }

    auto print_separator = [&]() {
        std::string sep = "+";
        for (size_t w : widths) sep += std::string(w + 2, '-') + "+";
        return sep + "\n";
    };

    std::string output = print_separator() + "|";
    for (size_t i = 0; i < cols.size(); i++) output += " " + cols[i] + std::string(widths[i] - cols[i].length(), ' ') + " |";
    output += "\n" + print_separator();

    if (rows.empty()) {
        output += "Empty set\n";
    } else {
        for (const auto& r : rows) {
            output += "|";
            for (size_t i = 0; i < cols.size(); i++) {
                std::string val = (i < r.size()) ? r[i] : "";
                output += " " + val + std::string(widths[i] - val.length(), ' ') + " |";
            }
            output += "\n";
        }
        output += print_separator();
    }
    
    response->set_success(true); response->set_data(output); response->set_current_leader_id(node_id_);
    return Status::OK;
}

// ====================================================================
// --- NODE-TO-NODE RPCs & BACKGROUND THREADS ---
// ====================================================================
Status RaftServer::PreVote(ServerContext* context, const RequestVoteRequest* request, RequestVoteResponse* response) {
    std::lock_guard<std::mutex> lock(state_mutex_);

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - last_heartbeat_).count();
    const bool recently_heard_leader =
        (state_ == LEADER) || (current_leader_id_ != -1 && elapsed_ms < kElectionTimeoutMinMs);
    const bool is_up_to_date = IsCandidateLogUpToDateUnsafe(request->last_log_index(), request->last_log_term());

    bool grant_vote = false;
    if (request->term() >= current_term_ && is_up_to_date && !recently_heard_leader) {
        grant_vote = true;
    }

    response->set_term(current_term_);
    response->set_vote_granted(grant_vote);
    return Status::OK;
}

Status RaftServer::RequestVote(ServerContext* context, const RequestVoteRequest* request, RequestVoteResponse* response) {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (request->term() > current_term_) {
        current_term_ = request->term();
        state_ = FOLLOWER;
        voted_for_ = -1;
        current_leader_id_ = -1;
        last_heartbeat_ = std::chrono::steady_clock::now();
    }

    bool grant_vote = false;
    const bool is_up_to_date = IsCandidateLogUpToDateUnsafe(request->last_log_index(), request->last_log_term());
    if (request->term() == current_term_ && is_up_to_date &&
        (voted_for_ == -1 || voted_for_ == request->candidate_id())) {
        grant_vote = true;
        voted_for_ = request->candidate_id();
        state_ = FOLLOWER;
        last_heartbeat_ = std::chrono::steady_clock::now();
    }

    response->set_vote_granted(grant_vote);
    response->set_term(current_term_);

    if (grant_vote) {
        std::cout << "[Node " << node_id_ << "] Voted YES for Candidate "
                  << request->candidate_id() << " in Term " << current_term_ << std::endl;
    }
    return Status::OK;
}

Status RaftServer::AppendEntries(ServerContext* context, const AppendEntriesRequest* request, AppendEntriesResponse* response) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (request->term() < current_term_) {
        response->set_success(false);
        response->set_term(current_term_);
        return Status::OK;
    }

    const int previous_term = current_term_;
    const int previous_leader_id = current_leader_id_;
    const bool was_leader = (state_ == LEADER);

    if (request->term() > current_term_) {
        voted_for_ = -1;
    }

    current_term_ = request->term();
    state_ = FOLLOWER;
    current_leader_id_ = request->leader_id();
    last_heartbeat_ = std::chrono::steady_clock::now();

    if (was_leader || previous_leader_id != current_leader_id_ || previous_term != current_term_) {
        std::cout << "[Node " << node_id_ << "] Recognized Node " << current_leader_id_
                  << " as Leader for Term " << current_term_ << std::endl;
    }

    const int prev_index = request->prev_log_index();
    const int prev_term = request->prev_log_term();
    if (prev_index >= 0) {
        if (prev_index > GetLastLogIndexUnsafe()) {
            response->set_success(false);
            response->set_term(current_term_);
            return Status::OK;
        }
        if (log_[prev_index].term() != prev_term) {
            log_.resize(prev_index);
            if (commit_index_ > GetLastLogIndexUnsafe()) {
                commit_index_ = GetLastLogIndexUnsafe();
            }
            if (last_applied_ > commit_index_) {
                last_applied_ = commit_index_;
            }
            response->set_success(false);
            response->set_term(current_term_);
            return Status::OK;
        }
    }

    int insert_index = prev_index + 1;
    int applied_count = 0;
    for (int i = 0; i < request->entries_size(); i++) {
        const LogEntry& incoming = request->entries(i);

        if (insert_index <= GetLastLogIndexUnsafe()) {
            if (log_[insert_index].term() != incoming.term()) {
                log_.resize(insert_index);
                if (commit_index_ > GetLastLogIndexUnsafe()) {
                    commit_index_ = GetLastLogIndexUnsafe();
                }
                if (last_applied_ > commit_index_) {
                    last_applied_ = commit_index_;
                }
            }
        }

        if (insert_index > GetLastLogIndexUnsafe()) {
            log_.push_back(incoming);
            applied_count++;
        }
        insert_index++;
    }

    if (insert_index <= GetLastLogIndexUnsafe()) {
        log_.resize(insert_index);
        if (commit_index_ > GetLastLogIndexUnsafe()) {
            commit_index_ = GetLastLogIndexUnsafe();
        }
        if (last_applied_ > commit_index_) {
            last_applied_ = commit_index_;
        }
    }

    const int leader_commit = request->leader_commit();
    const int last_log_index = GetLastLogIndexUnsafe();
    if (leader_commit > commit_index_) {
        commit_index_ = std::min(leader_commit, last_log_index);
    }
    ApplyCommittedEntriesUnsafe();

    if (applied_count > 0) {
        std::cout << "[Node " << node_id_ << "] Synced " << applied_count
                  << " new entries from Leader." << std::endl;
    }

    response->set_success(true);

    response->set_term(current_term_);
    return Status::OK;
}

void RaftServer::SendRequestVoteToAll() {
    int term_snapshot = 0;
    int last_log_index = -1;
    int last_log_term = 0;
    int majority = 0;
    bool became_leader = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (state_ != CANDIDATE) {
            return;
        }
        term_snapshot = current_term_;
        last_log_index = GetLastLogIndexUnsafe();
        last_log_term = GetLastLogTermUnsafe();
        majority = static_cast<int>(peers_.size() + 1) / 2 + 1;

        if (votes_received_ >= majority) {
            std::cout << "\n>>> [Node " << node_id_ << "] WON ELECTION FOR TERM "
                      << current_term_ << "! BECOMING LEADER <<<" << std::endl;
            state_ = LEADER;
            current_leader_id_ = node_id_;
            last_heartbeat_ = std::chrono::steady_clock::now();
            became_leader = true;
        }
    }

    if (became_leader) {
        SendHeartbeatsToAll();
        return;
    }

    for (auto& peer_stub : peers_) {
        ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kRpcTimeoutMs));

        RequestVoteRequest req;
        req.set_term(term_snapshot);
        req.set_candidate_id(node_id_);
        req.set_last_log_index(last_log_index);
        req.set_last_log_term(last_log_term);

        RequestVoteResponse res;
        if (peer_stub->RequestVote(&context, req, &res).ok()) {
            std::lock_guard<std::mutex> lock(state_mutex_);

            if (res.term() > current_term_) {
                current_term_ = res.term();
                state_ = FOLLOWER;
                voted_for_ = -1;
                current_leader_id_ = -1;
                return;
            }

            if (state_ == CANDIDATE && current_term_ == term_snapshot && res.vote_granted()) {
                votes_received_++;
                if (votes_received_ >= majority) {
                    std::cout << "\n>>> [Node " << node_id_ << "] WON ELECTION FOR TERM "
                              << current_term_ << "! BECOMING LEADER <<<" << std::endl;
                    state_ = LEADER;
                    current_leader_id_ = node_id_;
                    last_heartbeat_ = std::chrono::steady_clock::now();
                    became_leader = true;
                    break;
                }
            }
        }
    }

    if (became_leader) {
        SendHeartbeatsToAll();
    }
}

void RaftServer::SendHeartbeatsToAll() {
    int term_snapshot = 0;
    int commit_snapshot = -1;
    std::vector<LogEntry> log_snapshot;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (state_ != LEADER) {
            return;
        }
        term_snapshot = current_term_;
        commit_snapshot = commit_index_;
        log_snapshot = log_;
    }

    for (auto& peer_stub : peers_) {
        ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kRpcTimeoutMs));

        AppendEntriesRequest req;
        req.set_term(term_snapshot);
        req.set_leader_id(node_id_);
        req.set_prev_log_index(-1);
        req.set_prev_log_term(0);
        req.set_leader_commit(commit_snapshot);
        for (const auto& entry : log_snapshot) {
            LogEntry* new_entry = req.add_entries();
            *new_entry = entry;
        }

        AppendEntriesResponse res;
        if (peer_stub->AppendEntries(&context, req, &res).ok()) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (res.term() > current_term_) {
                current_term_ = res.term();
                state_ = FOLLOWER;
                voted_for_ = -1;
                current_leader_id_ = -1;
                return;
            }
        }
    }
}

void RaftServer::RunElectionTimer() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(kElectionTimeoutMinMs, kElectionTimeoutMaxMs);
    int timeout_ms = dist(gen);

    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kHeartbeatIntervalMs));

        bool run_pre_vote = false;
        int prospective_term = 0;
        int last_log_index = -1;
        int last_log_term = 0;
        int term_to_start = 0;

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (state_ == LEADER) {
                timeout_ms = dist(gen);
                continue;
            }

            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - last_heartbeat_).count();

            if (elapsed_ms > timeout_ms) {
                prospective_term = current_term_ + 1;
                last_log_index = GetLastLogIndexUnsafe();
                last_log_term = GetLastLogTermUnsafe();
                run_pre_vote = true;
            }
        }

        if (!run_pre_vote) {
            continue;
        }

        if (RunPreVoteRound(prospective_term, last_log_index, last_log_term)) {
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - last_heartbeat_).count();

                if (state_ != LEADER && elapsed_ms > timeout_ms) {
                    state_ = CANDIDATE;
                    current_term_++;
                    voted_for_ = node_id_;
                    votes_received_ = 1;
                    current_leader_id_ = -1;
                    last_heartbeat_ = std::chrono::steady_clock::now();
                    term_to_start = current_term_;
                }
            }

            if (term_to_start > 0) {
                std::cout << "[Node " << node_id_ << "] Starting Election for Term " << term_to_start << "..." << std::endl;
                SendRequestVoteToAll();
            }
        } else {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (state_ != LEADER) {
                // Failed pre-vote: wait another randomized timeout before retrying.
                last_heartbeat_ = std::chrono::steady_clock::now();
            }
        }

        timeout_ms = dist(gen);
    }
}

void RaftServer::RunHeartbeat() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kHeartbeatIntervalMs));

        bool is_leader = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            is_leader = (state_ == LEADER);
        }

        if (is_leader) {
            SendHeartbeatsToAll();
        }
    }
}