#pragma once
#include <cstdint>
#include <string>  
#include <cstring>
#include <stdexcept>
#include "type_id.h"

namespace aegis{

    class Value{
    public:
        Value():type_id_(TypeId::INVALID),size_(0){}

        explicit Value(bool value):type_id_(TypeId::BOOLEAN),size_(sizeof(bool)){
            value_.boolean_=value;
        }

        explicit Value(int32_t value):type_id_(TypeId::INTEGER),size_(sizeof(int32_t)){
            value_.integer_=value;
        }

        explicit Value(const std::string& val) : type_id_(TypeId::VARCHAR) {
        size_ = val.length();
        uint32_t copy_len = size_ < 32 ? size_ : 31; 
        std::memcpy(value_.varchar_, val.c_str(), copy_len);
        value_.varchar_[copy_len] = '\0';
       }

       TypeId GetTypeId() const { return type_id_; }
       uint32_t GetLength() const { return size_; }
    
       int32_t GetAsInteger() const { return value_.integer_; }
       bool GetAsBoolean() const { return value_.boolean_; }
       std::string GetAsString() const { 
           if (type_id_ == TypeId::VARCHAR) {
               return std::string(value_.varchar_, size_);
           }
          return "";
        }

        int CompareEqual(const Value& other) const;
        int CompareLessThan(const Value& other) const;
        int CompareGreaterThan(const Value& other) const;

    private:
        TypeId type_id_;
        uint32_t size_;

        union{
            bool boolean_;
            int32_t integer_;
            char varchar_[32];  
        } value_;

    };
}

