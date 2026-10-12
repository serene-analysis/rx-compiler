#pragma once
#include <string>

namespace semantic{

struct Type{
    enum kTypeKind { BUILTIN, STRUCT, BOX, VEC, REF, ARRAY, UNIT, NEVER, ERROR } ;
    kTypeKind kind_ = kTypeKind::ERROR;
    std::string name_ = "AAAunnamedType";
    Type* elem_ = nullptr;
    bool isMut_ = false;
    unsigned int arrayLength_ = 0;
};

inline Type* makeBuiltin(const std::string& name) {
    auto ret = new Type;
    ret->name_ = name;
    ret->kind_ = Type::kTypeKind::BUILTIN;
    return ret;
}

inline Type* makeStruct(const std::string& name) {
    auto ret = new Type;
    ret->name_ = name;
    ret->kind_ = Type::kTypeKind::STRUCT;
    return ret;
}

inline Type* makeBox(Type* elem){
    auto ret = new Type;
    ret->elem_ = elem;
    ret->kind_ = Type::kTypeKind::BOX;
    return ret;
}

inline Type* makeVec(Type* elem){
    auto ret = new Type;
    ret->elem_ = elem;
    ret->kind_ = Type::kTypeKind::VEC;
    return ret;
}

inline Type* makeRef(Type* inner, bool isMut){
    auto ret = new Type;
    ret->elem_ = inner;
    ret->isMut_ = isMut;
    ret->kind_ = Type::kTypeKind::REF;
    return ret;
}

inline Type* makeArray(Type* some, unsigned int len) {
    auto ret = new Type;
    ret->elem_ = some;
    ret->arrayLength_ = len;
    ret->kind_ = Type::kTypeKind::ARRAY;
    return ret;
}

inline Type* makeUnit() {
    auto ret = new Type;
    ret->kind_ = Type::kTypeKind::UNIT;
    return ret;
}

inline Type* makeNever() {
    auto ret = new Type;
    ret->kind_ = Type::kTypeKind::NEVER;
    return ret;
}

inline Type* makeError() {
    auto ret = new Type;
    ret->kind_ = Type::kTypeKind::ERROR;
    return ret;
}

inline bool typeEqual(Type* now, Type* oth) {
    if (now == oth) {
        return true;
    }
    if (now == nullptr || oth == nullptr) {
        return false;
    }
    if (now->kind_ == Type::kTypeKind::ERROR || oth->kind_ == Type::kTypeKind::ERROR) {
        return true;
    }
    if (now->kind_ != oth->kind_) {
        return false;
    }
    if (now->kind_ == Type::kTypeKind::BUILTIN) {
        return now->name_ == oth->name_;
    }
    if (now->kind_ == Type::kTypeKind::STRUCT) {
        return now->name_ == oth->name_;
    }
    if (now->kind_ == Type::kTypeKind::BOX) {
        return typeEqual(now->elem_, oth->elem_);
    }
    if (now->kind_ == Type::kTypeKind::VEC) {
        return typeEqual(now->elem_, oth->elem_);
    }
    if (now->kind_ == Type::kTypeKind::REF) {
        return now->isMut_ == oth->isMut_ && typeEqual(now->elem_, oth->elem_);
    }
    if (now->kind_ == Type::kTypeKind::ARRAY) {
        return now->arrayLength_ == oth->arrayLength_ && typeEqual(now->elem_, oth->elem_);
    }
    if (now->kind_ == Type::kTypeKind::UNIT) {
        // Really?
        return true;
    }
    if (now->kind_ == Type::kTypeKind::NEVER) {
        // Are you sure?
        return true;
    }
    throw "typeEqual(no type met)";
    return true;
}
/*
enum kTypeKind { BUILTIN, STRUCT, VEC, BOX, REF, ARRAY, NEVER, UNIT, ERROR } ;
kTypeKind kind_ = kTypeKind::ERROR;
std::string name_ = "AAAunnamedType";
Type* elem_ = nullptr;
bool isMut_ = false;
unsigned int arrayLength_ = 0;
*/

}