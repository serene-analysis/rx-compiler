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
    bool evalArrayLength(ConstValue* value, const std::string& owner, unsigned& length);
    semType* transformType(Type* t);
    void evaluateTypes(Crate* crate);


    semType* lub(std::vector<semType*> types);
    bool canImplicitlyTurn(semType* from, semType* to);
    void checkItem(Item* item);
    void checkFunction(funcItem* func, const std::string& owner);
    semType* checkBlock(blockExpr* block, semType* wish);
    semType* checkStatement(Stmt* stmt);
    semType* checkLoop(loopExpr* loop, semType* wish);
    bool isPlaceable(Expr* expr);
    bool isMutablePlaceable(Expr* expr);
    bool isMutablyAccessible(Expr* expr);
    bool receiverMutable(Expr* expr);
    void eliminateOneShared(semType*& type, bool& valid);
    semType* lookupValue(std::string name);
    semType* infer(Expr* expr);
    semType* expect(Expr* expr, semType* wish);
    bool canCopy(semType* type);
    bool canClone(semType* type);
    bool canPartialEq(semType* type);
    bool canEq(semType* type);
    void checkLayoutLoop(std::string typeName);
    void checkLayout(Crate* crate);
    void checkDerives(Crate* crate);
    void checkEntry();

    void error(std::string message);

    globalTable table_;
    scopeStack scope_;
    enum kLayoutAndDeriveState { UNTOUCHED, CALCULATING, LAYOUTDONE, YES, NO };
    std::map<std::string, kLayoutAndDeriveState> state; // Note that this set the whole state initally aat UNTOUCHED
    std::vector<loopInfo> loops_;
    semType* currentRet_ = nullptr;
    std::string currentStruct_;
    bool reject_ = false;
};