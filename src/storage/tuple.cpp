#include "storage/tuple.h"
#include <cstring> // For memcpy

namespace aegis {

// Calculates total physical size in bytes
uint32_t Tuple::GetLength() const {
    uint32_t total_size = 0;
    
    for (const auto& val : values_) {
        // Every value needs 4 bytes for the TypeId and 4 bytes for the Size
        total_size += sizeof(TypeId) + sizeof(uint32_t); 
        
        // Add the actual payload size
        if (val.GetTypeId() == TypeId::BOOLEAN) {
            total_size += sizeof(bool);
        } else if (val.GetTypeId() == TypeId::INTEGER) {
            total_size += sizeof(int32_t);
        } else if (val.GetTypeId() == TypeId::VARCHAR) {
            total_size += val.GetLength();
        }
    }
    return total_size;
}

// Writes the tuple to the disk page
void Tuple::SerializeTo(char* storage) const {
    uint32_t offset = 0;
    
    for (const auto& val : values_) {
        TypeId type = val.GetTypeId();
        uint32_t len = val.GetLength();

        // 1. Write the TypeId
        std::memcpy(storage + offset, &type, sizeof(TypeId));
        offset += sizeof(TypeId);

        // 2. Write the Length
        std::memcpy(storage + offset, &len, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        // 3. Write the Payload
        if (type == TypeId::BOOLEAN) {
            bool b = val.GetAsBoolean();
            std::memcpy(storage + offset, &b, sizeof(bool));
            offset += sizeof(bool);
        } 
        else if (type == TypeId::INTEGER) {
            int32_t i = val.GetAsInteger();
            std::memcpy(storage + offset, &i, sizeof(int32_t));
            offset += sizeof(int32_t);
        } 
        else if (type == TypeId::VARCHAR) {
            std::string s = val.GetAsString();
            std::memcpy(storage + offset, s.c_str(), len);
            offset += len;
        }
    }
}

// Rebuilds the tuple from the disk page
void Tuple::DeserializeFrom(const char* storage, uint32_t size) {
    values_.clear();
    uint32_t offset = 0;
    
    while (offset < size) {
        // 1. Read the TypeId
        TypeId type;
        std::memcpy(&type, storage + offset, sizeof(TypeId));
        offset += sizeof(TypeId);

        // 2. Read the Length
        uint32_t len;
        std::memcpy(&len, storage + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        // 3. Read the Payload and recreate the Value object
        if (type == TypeId::BOOLEAN) {
            bool b;
            std::memcpy(&b, storage + offset, sizeof(bool));
            offset += sizeof(bool);
            values_.push_back(Value(b));
        } 
        else if (type == TypeId::INTEGER) {
            int32_t i;
            std::memcpy(&i, storage + offset, sizeof(int32_t));
            offset += sizeof(int32_t);
            values_.push_back(Value(i));
        } 
        else if (type == TypeId::VARCHAR) {
            // Reconstruct the std::string from the raw char array
            std::string s(storage + offset, len);
            offset += len;
            values_.push_back(Value(s));
        }
    }
}

} // namespace aegis