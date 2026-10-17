#pragma once
#include <vector>
#include <utility>
#include <map>
#include <stack>
#include "type.h"

using semType = semantic::Type;

struct funcSignature {
    std::vector<semType*> params_;
    semType* retType_ = nullptr;
    bool isMethod_ = false;
    bool selfRef_ = false;
    bool selfMut_ = false;
};

struct structSignature {
    std::vector<std::pair<std::string, semType*>> fields_;
    std::vector<std::string> derives_;
};

struct ConstValue;

struct Constant {
    bool isBool_ = false;
    long long value_ = 0;
    Constant(bool isBool = false, long long value = 0) : isBool_(isBool), value_(value) {}
};

struct constSignature {
    semType* type_ = nullptr;
    ConstValue* value_ = nullptr;
    enum kState { UNTOUCHED, CALCULATING, DONE };
    kState state_ = UNTOUCHED;
    Constant real_;
};

struct globalTable {
    std::map<std::string, funcSignature> funcSig;
    std::map<std::string, structSignature> structSig;
    std::map<std::string, constSignature> constSig;
    std::map<std::string, std::map<std::string, funcSignature>> methods;
    std::map<std::string, std::map<std::string, constSignature>> associatedConsts;
};

struct localShadowing {
    semType* type_;
    bool isMut_;
    localShadowing(semType* type = nullptr, bool isMut = false) : type_(type), isMut_(isMut) {}
};

struct stackShadowing {
    std::string name_;
    localShadowing shadow_;
    int depth_;
    stackShadowing(std::string name, localShadowing shadow, int depth = 0) : name_(name), shadow_(shadow), depth_(depth) {}
};

struct scopeStack {
    std::map<std::string, std::stack<localShadowing>> memory_;
    std::stack<stackShadowing> stk_;
    int currentDepth = 0;
    void levelUp() {
        currentDepth++;
        return;
    }
    void push(std::string name, localShadowing shadow) {
        memory_[name].push(shadow);
        stk_.push(stackShadowing(name, shadow, currentDepth));
        return;
    }
    void levelDown() {
        while (!stk_.empty() && stk_.top().depth_ == currentDepth) {
            memory_[stk_.top().name_].pop();
            stk_.pop();
        }
        currentDepth--;
        return;
    }
    localShadowing* find(std::string name) {
        auto dt = memory_.find(name);
        if (dt == memory_.end() || dt->second.empty()) {
            return nullptr;
        }
        return &dt->second.top();
    }
};