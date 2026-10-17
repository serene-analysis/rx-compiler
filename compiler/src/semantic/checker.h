#pragma once
#include "symbol.h"
#include "type.h"
#include "../ast/ast.h"

struct Checker {
    void checkCrate(Crate* rt);

    Constant calculateConst(ConstValue* value, std::string owner);
    Constant manifestConst(std::string owner, std::string name);
    constSignature* findConst(const std::string& owner, const std::string& name);
    bool checkConstInit(const ConstValue* value, semType* expected, const std::string& owner);
    void collect(Crate* crate);
    void evaluateConstants(Crate* crate);
    semType* transformType(Type* t);
    void evaluateTypes(Crate* crate);


    void checkItem(Item* item);
    void checkFunction(funcItem* func, const std::string& owner);
    semType* checkBlock(blockExpr* block, semType* wish);
    void checkStatement(Stmt* stmt);
    bool isPlaceable(Expr* expr);
    bool isMutablePlaceable(Expr* expr);
    semType* lookupValue(std::string name);
    semType* infer(Expr* expr);
    semType* expect(Expr* expr, semType* wish);

    void error(std::string message);

    globalTable table_;
    scopeStack scope_;
    semType* currentRet_ = nullptr;
    std::string currentStruct_;
    bool reject_ = false;
};