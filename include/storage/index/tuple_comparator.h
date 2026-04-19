#pragma once

#include "storage/tuple.h"
#include "catalog/schema.h"

namespace aegis {

class TupleComparator {
public:
    explicit TupleComparator(const Schema* schema) : schema_(schema) {}

    // The B+ Tree will call this function to sort keys
    int operator()(const Tuple& a, const Tuple& b) const {
        
        // FIX: Directly extract and compare the strings for our NoSQL MVP!
        Value val_a = a.GetValue(0);
        Value val_b = b.GetValue(0);
        
        // CompareLessThan naturally returns -1 (less), 1 (greater), or 0 (equal)
        return val_a.CompareLessThan(val_b); 
    }

private:
    const Schema* schema_;
};

} // namespace aegis