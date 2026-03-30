#pragma once

#include "type/value.h"
#include <vector>
#include <string>
#include <stdexcept>

namespace aegis {

class Tuple {
public:
    // Create an empty tuple (used when we are about to Deserialize into it)
    Tuple() = default;

    // Create a tuple given a vector of Values (used when inserting new data)
    explicit Tuple(std::vector<Value> values) : values_(std::move(values)) {}

    // --- Accessors ---
    Value GetValue(uint32_t column_index) const {
        if (column_index < values_.size()) {
            return values_[column_index];
        }
        throw std::out_of_range("Tuple column index out of range.");
    }

    uint32_t GetColumnCount() const { 
        return values_.size(); 
    }

    // --- NEW: Serialization Methods ---
    
    // Calculates the exact number of bytes this Tuple will take up on the disk page
    uint32_t GetLength() const;

    // Squishes the Tuple into raw bytes and writes them to the provided memory address
    void SerializeTo(char* storage) const;

    // Reads raw bytes from a memory address and rebuilds the Tuple object
    void DeserializeFrom(const char* storage, uint32_t size);

private:
    std::vector<Value> values_;
};

} // namespace aegis