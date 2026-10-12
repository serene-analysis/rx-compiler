#pragma once
#include "symbol.h"
#include "type.h"
#include "../ast/ast.h"

struct Checker {
    void checkCrate(Crate* rt);

    long long manifestConstInt(ConstValue* value);
    void collect(Crate* crate);
    semType* transformType(Type* t);

    void checkItem(Item* item);
    semType* infer(Expr* expr);
    semType* expect(Expr* expr, semType* wish);

    void error(std::string message);

    globalTable table_;
    scopeStack scope_;
    semType* currentRet_ = nullptr;
    std::string currentStruct_;
    bool reject_ = false;
};