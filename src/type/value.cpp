#include "type/value.h"
#include <stdexcept>
#include <cstring>

namespace aegis{

template<typename T>
inline int Evaluate(const T& a,const T& b){
    if(a<b)return -1;
    if(a>b)return 1;
    return 0;
}

int Value::CompareEqual(const Value& other) const{
    if(this->type_id_ != other.type_id_){
        throw std::invalid_argument("Connot compare different datatypes");
    }

    switch (type_id_) {
        case TypeId::BOOLEAN:
            return value_.boolean_ == other.value_.boolean_ ? 0 : 1;
        case TypeId::INTEGER:
            return value_.integer_ == other.value_.integer_ ? 0 : 1;
        case TypeId::VARCHAR:
            return std::strncmp(value_.varchar_, other.value_.varchar_, 32) == 0 ? 0 : 1;
        default:
            throw std::logic_error("Unknown type in CompareEquals.");
    }
}

int Value::CompareLessThan(const Value& other) const {
    if (this->type_id_ != other.type_id_) {
        throw std::invalid_argument("Cannot compare Values of different types.");
    }

    switch (type_id_) {
        case TypeId::BOOLEAN:
            return Evaluate(value_.boolean_, other.value_.boolean_);
        case TypeId::INTEGER:
            return Evaluate(value_.integer_, other.value_.integer_);
        case TypeId::VARCHAR: {
            int cmp = std::strncmp(value_.varchar_, other.value_.varchar_, 32);
            return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
        }
        default:
            throw std::logic_error("Unknown type in CompareLessThan.");
    }
}

int Value::CompareGreaterThan(const Value& other) const {
    return other.CompareLessThan(*this);
}


}