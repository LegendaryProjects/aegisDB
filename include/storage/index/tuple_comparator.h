#pragma once

#include "storage/tuple.h"
#include "catalog/schema.h"

namespace aegis {

class TupleComparator {
public:
    // The comparator needs to know the rules (the Schema) to compare tuples
    explicit TupleComparator(const Schema* schema) : schema_(schema) {}

    // The B+ Tree will call this function.
    // Returns -1 if (a < b)
    // Returns  1 if (a > b)
    // Returns  0 if (a == b)
    int operator()(const Tuple& a, const Tuple& b) const {
        if (schema_ == nullptr) return 0;
        uint32_t column_count = schema_->GetColumnCount();

        for (uint32_t i = 0; i < column_count; i++) {
            Value val_a = a.GetValue(i);
            Value val_b = b.GetValue(i);

            // Compare this specific column
            int cmp = val_a.CompareLessThan(val_b);
            
            if (cmp == -1) {
                return -1; // Tuple A is strictly less
            } 
            if (val_b.CompareLessThan(val_a) == -1) {
                return 1;  // Tuple A is strictly greater
            }
            
            // If we get here, column 'i' is perfectly equal. 
            // The loop continues to the next column to break the tie!
        }

        // If the loop finishes and every column was equal, the tuples are identical
        return 0; 
    }

private:
    const Schema* schema_;
};

} // namespace aegis