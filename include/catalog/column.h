#pragma once
#include "type/type_id.h"
#include <string>

namespace aegis{

class Column{
  private:
    std::string column_name_;
    TypeId column_type_;
    uint32_t column_length_;

public:
    Column()=default;

    Column(std::string column_name, TypeId column_type, uint32_t column_length)
        : column_name_(std::move(column_name)), column_type_(column_type), column_length_(column_length) {
            if(column_type==TypeId::INTEGER)column_length_=4;
            else if(column_type==TypeId::BOOLEAN)column_length_=1;
            
        }

    const std::string& GetName() const { return column_name_; }
    TypeId GetType() const { return column_type_; }
    uint32_t GetSize() const { return column_length_; }
};
}