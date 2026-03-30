#pragma once
#include<bits/stdc++.h>
using namespace std;

class StateMachine{
    public:
    //virtual constructor for derived classes
    virtual ~Stateachine()=default;

    //since raft dont care about what we are storing , we take the data as bytes
    virtual string Apply(const vector<uint8_t>& log entry)=0;

    //the snapshots of checkpoints of log when the log size gets too big
    virtual void Snapshot()=0;

    //to restore storage engine state from snapshot
    virtual void Restore()=0;

};