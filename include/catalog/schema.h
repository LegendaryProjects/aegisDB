#pragma once
#include "catalog/column.h"
#include <vector>

namespace aegis{

class Schema{
  private:
    std::vector<Column> columns_;

  public:
    explicit Schema(std::vector<Column> columns) : columns_(std::move(columns)) {}

    const Column& GetColumn(uint32_t column_index) const {
        return columns_[column_index];
    }

    uint32_t GetColumnCount() const {
        return columns_.size();
    }

    const std::vector<Column>& GetColumns() const { 
        return columns_; 
    }
};

} 