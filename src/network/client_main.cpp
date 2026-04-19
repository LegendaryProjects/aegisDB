#include <iostream>
#include <string>
#include <sstream>
#include <memory>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <unordered_set>
#include <vector>
#include <grpcpp/grpcpp.h>
#include "raft.grpc.pb.h"

using namespace grpc;
using namespace aegis::network;

namespace {

// Cluster node endpoints for client-side routing.
// Update these addresses when running nodes across different devices.
const std::vector<std::string> kClusterNodeAddresses = {
    "127.0.0.1:50051",
    "127.0.0.1:50052",
    "127.0.0.1:50053",
};

constexpr int kDefaultStartupNodeId = 1;

bool IsNotLeaderMessage(const std::string& message) {
    return message.rfind("Not leader", 0) == 0;
}

struct RpcResult {
    bool success{false};
    bool retryable{false};
    int leader_id{-1};
    std::string message;
};

}  // namespace

class AegisClient {
private:
    std::unique_ptr<ClientAPI::Stub> stub_;
public:
    void Connect(const std::string& address) {
        stub_ = ClientAPI::NewStub(CreateChannel(address, InsecureChannelCredentials()));
    }

    RpcResult Execute(const std::string& type, TableRequest& t_req, RowRequest& r_req) {
        RpcResult result;
        ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
        ClientResponse res;

        Status status;
        if (type == "CREATE") {
            status = stub_->CreateTable(&ctx, t_req, &res);
        } else if (type == "INSERT") {
            status = stub_->InsertRow(&ctx, r_req, &res);
        } else if (type == "DELETE") {
            status = stub_->DeleteRow(&ctx, r_req, &res);
        } else if (type == "DROP") {
            status = stub_->DropTable(&ctx, t_req, &res);
        } else {
            result.message = "Unknown command type.";
            return result;
        }

        if (status.ok()) {
            result.success = res.success();
            result.leader_id = res.current_leader_id();
            result.message = res.message();
            result.retryable = !result.success && IsNotLeaderMessage(result.message);
            return result;
        }

        result.retryable = true;
        result.message = "RPC failed: " + status.error_message();
        return result;
    }

    RpcResult ExecuteSelect(TableRequest& req) {
        RpcResult result;
        ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
        SelectResponse res;
        Status status = stub_->SelectTable(&ctx, req, &res);
        if (status.ok()) {
            result.success = res.success();
            result.leader_id = res.current_leader_id();
            result.message = res.data();
            result.retryable = !result.success && IsNotLeaderMessage(result.message);
            return result;
        }
        result.retryable = true;
        result.message = "RPC failed: " + status.error_message();
        return result;
    }

    RpcResult ExecuteShow() {
        RpcResult result;
        ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
        ShowRequest req;
        ShowResponse res;
        Status status = stub_->ShowTables(&ctx, req, &res);
        if (status.ok()) {
            result.success = res.success();
            result.leader_id = res.current_leader_id();
            result.message = res.data();
            result.retryable = !result.success && IsNotLeaderMessage(result.message);
            return result;
        }
        result.retryable = true;
        result.message = "RPC failed: " + status.error_message();
        return result;
    }
};

// Helper to remove commas, parentheses, and quotes to keep entry extremely clean
std::string CleanToken(std::string s) {
    std::string res;
    for (char c : s) {
        if (c != '(' && c != ')' && c != ',' && c != '"' && c != '\'' && c != ';') {
            res += c;
        }
    }
    return res;
}

std::string Upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

int main(int argc, char** argv) {
    AegisClient client;

    // Optional runtime overrides for multi-device clusters:
    // ./aegis_client <n1_ip> <n1_port> <n2_ip> <n2_port> <n3_ip> <n3_port>
    std::vector<std::string> cluster_node_addresses = kClusterNodeAddresses;
    if (argc == 7) {
        cluster_node_addresses = {
            std::string(argv[1]) + ":" + std::string(argv[2]),
            std::string(argv[3]) + ":" + std::string(argv[4]),
            std::string(argv[5]) + ":" + std::string(argv[6]),
        };
    } else if (argc != 1) {
        std::cerr << "Usage: ./aegis_client <n1_ip> <n1_port> <n2_ip> <n2_port> <n3_ip> <n3_port>\n";
        std::cerr << "Example: ./aegis_client 192.168.1.10 50051 192.168.1.11 50052 192.168.1.12 50053\n";
        std::cerr << "Or run ./aegis_client with no args to use defaults in kClusterNodeAddresses.\n";
        return 1;
    }

    const int kClusterNodeCount = static_cast<int>(cluster_node_addresses.size());
    if (kClusterNodeCount == 0) {
        std::cerr << "No cluster endpoints configured in kClusterNodeAddresses.\n";
        return 1;
    }

    int current_node_id = std::clamp(kDefaultStartupNodeId, 1, kClusterNodeCount);
    client.Connect(cluster_node_addresses[current_node_id - 1]);

    std::cout << "\n=== AegisDB Relational Terminal ===\n";
    std::cout << " CREATE TABLE <name> COLUMNS <col1> <col2*> ... (Use * for Primary Key)\n";
    std::cout << " INSERT INTO <name> VALUES (<val1>, <val2>, ...)\n";
    std::cout << " DELETE <pk_val> FROM <name>\n";
    std::cout << " DROP TABLE <name>\n";
    std::cout << " SELECT FROM <name>\n";
    std::cout << " SHOW TABLES\n\n";

    std::string line;
    while (true) {
        std::cout << "aegis> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd; iss >> cmd;
        cmd = Upper(CleanToken(cmd));
        if (cmd == "EXIT" || cmd == "QUIT") break;

        int leader_id = -1; bool success = false; std::string msg = "";
        TableRequest t_req; RowRequest r_req; std::string type = "";

        if (cmd == "CREATE") {
            std::string table_kw, tbl, columns_kw;
            iss >> table_kw >> tbl >> columns_kw;
            table_kw = Upper(CleanToken(table_kw));
            columns_kw = Upper(CleanToken(columns_kw));
            tbl = CleanToken(tbl);

            if (table_kw != "TABLE" || columns_kw != "COLUMNS" || tbl.empty()) {
                std::cout << "Syntax: CREATE TABLE <name> COLUMNS <col1> <col2*> ...\n";
                continue;
            }

            std::string cols = "", word;
            int star_count = 0;
            while (iss >> word) { 
                word = CleanToken(word); 
                if (word.empty()) {
                    continue;
                }
                if (word.find('*') != std::string::npos) star_count++;
                if (!cols.empty()) cols += ","; 
                cols += word; 
            }
            if (cols.empty()) {
                std::cout << "Error: No columns provided.\n";
                continue;
            }
            if (star_count != 1) { std::cout << "Error: Must specify exactly ONE primary key using '*'. Example: col1 col2* col3\n"; continue; }
            t_req.set_table_name(tbl); t_req.set_columns(cols); type = "CREATE";
        }
        else if (cmd == "INSERT") {
            std::string into_kw, tbl, values_kw;
            iss >> into_kw >> tbl >> values_kw;
            into_kw = Upper(CleanToken(into_kw));
            values_kw = Upper(CleanToken(values_kw));
            tbl = CleanToken(tbl);

            if (into_kw != "INTO" || values_kw != "VALUES" || tbl.empty()) {
                std::cout << "Syntax: INSERT INTO <name> VALUES (<val1>, <val2>, ...)\n";
                continue;
            }

            std::string vals = "", word;
            while (iss >> word) { 
                word = CleanToken(word); 
                if (word.empty()) {
                    continue;
                }
                if (!vals.empty()) vals += ","; 
                vals += word; 
            }
            if (vals.empty()) {
                std::cout << "Error: No values provided.\n";
                continue;
            }
            r_req.set_table_name(tbl); r_req.set_values(vals); type = "INSERT";
        }
        else if (cmd == "DELETE") {
            std::string pk, from_kw, tbl;
            iss >> pk >> from_kw >> tbl;
            from_kw = Upper(CleanToken(from_kw));
            pk = CleanToken(pk);
            tbl = CleanToken(tbl);
            if (from_kw != "FROM" || pk.empty() || tbl.empty()) {
                std::cout << "Syntax: DELETE <pk_val> FROM <name>\n";
                continue;
            }
            r_req.set_table_name(tbl); r_req.set_pk_value(CleanToken(pk)); type = "DELETE";
        }
        else if (cmd == "DROP") {
            std::string table_kw, tbl;
            iss >> table_kw >> tbl;
            table_kw = Upper(CleanToken(table_kw));
            tbl = CleanToken(tbl);
            if (table_kw != "TABLE" || tbl.empty()) {
                std::cout << "Syntax: DROP TABLE <name>\n";
                continue;
            }
            t_req.set_table_name(tbl); type = "DROP";
        }
        else if (cmd == "SELECT") {
            std::string from_kw, tbl;
            iss >> from_kw >> tbl;
            from_kw = Upper(CleanToken(from_kw));
            tbl = CleanToken(tbl);
            if (from_kw != "FROM" || tbl.empty()) {
                std::cout << "Syntax: SELECT FROM <name>\n";
                continue;
            }
            t_req.set_table_name(tbl); type = "SELECT";
        } 
        else if (cmd == "SHOW") {
            std::string tables_kw;
            iss >> tables_kw;
            tables_kw = Upper(CleanToken(tables_kw));
            if (tables_kw != "TABLES") {
                std::cout << "Syntax: SHOW TABLES\n";
                continue;
            }
            type = "SHOW";
        } else {
            std::cout << "Unknown command.\n"; continue;
        }

        // Execute with failover: current node -> leader hint -> remaining nodes.
        auto execute_on_node = [&](int node_id) {
            current_node_id = node_id;
            client.Connect(cluster_node_addresses[current_node_id - 1]);

            if (type == "SELECT") {
                return client.ExecuteSelect(t_req);
            }
            if (type == "SHOW") {
                return client.ExecuteShow();
            }
            return client.Execute(type, t_req, r_req);
        };

        std::unordered_set<int> attempted_nodes;
        int next_node = current_node_id;
        for (int attempt = 0; attempt < kClusterNodeCount; ++attempt) {
            if (next_node < 1 || next_node > kClusterNodeCount || attempted_nodes.count(next_node) > 0) {
                next_node = -1;
                for (int candidate = 1; candidate <= kClusterNodeCount; ++candidate) {
                    if (attempted_nodes.count(candidate) == 0) {
                        next_node = candidate;
                        break;
                    }
                }
                if (next_node == -1) {
                    break;
                }
            }

            const RpcResult result = execute_on_node(next_node);
            attempted_nodes.insert(next_node);

            success = result.success;
            leader_id = result.leader_id;
            msg = result.message;

            if (success) {
                if (leader_id > 0 && leader_id <= kClusterNodeCount) {
                    current_node_id = leader_id;
                }
                break;
            }

            // Keep business errors from a valid leader (duplicate PK, table missing, etc.)
            // instead of masking them with a later follower redirection message.
            if (!result.retryable) {
                break;
            }

            if (leader_id > 0 && leader_id <= kClusterNodeCount && attempted_nodes.count(leader_id) == 0) {
                next_node = leader_id;
            } else {
                next_node = -1;
            }
        }

        if (!success && msg.empty()) {
            msg = "Request failed: unable to reach a healthy cluster node.";
        }

        if (!msg.empty()) {
            std::cout << msg;
            if (msg.back() != '\n') {
                std::cout << "\n";
            }
        }
    }
    return 0;
}