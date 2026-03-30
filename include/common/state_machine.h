#pragma once

#include <vector>
#include <string>

namespace aegis {

// A generic command coming from the Raft layer
struct Command {
    enum class Type { INSERT, DELETE, GET } type;
    int32_t key;
    std::string value; // Simplified payload for now
};

// The generic interface that Raft will use to talk to Storage
class IStateMachine {
public:
    virtual ~IStateMachine() = default;

    // Raft calls this when a log entry is committed and needs to be executed
    virtual bool Apply(const Command& cmd) = 0;

    // Used for Week 10 (Snapshots)
     virtual void CreateSnapshot(const std::string& snapshot_path) = 0;
     virtual void InstallSnapshot(const std::string& snapshot_path) = 0;
};

} // namespace aegis