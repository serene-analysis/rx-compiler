#pragma once

#include <string>
#include <vector>
#include <map>
#include <fstream>

#include "../semantic/checker.h"

struct IRValue {
    std::string name_ = "";
    semType* type_ = nullptr;
    IRValue(std::string name = "", semType* type = nullptr) : name_(name), type_(type) {}
};

struct IRStackShadowing {
    std::string name_;
    std::string shadow_;
    int depth_ = 0;
    IRStackShadowing(std::string name, std::string shadow, int depth = 0) : name_(name), shadow_(shadow), depth_(depth) {}
};

struct IRLoopInfo {
    std::string continueDest_;
    std::string breakDest_;
    std::string retDest_;
    semType* retType_ = nullptr;
    IRLoopInfo (std::string continueDest = "", std::string breakDest = "", std::string retDest = "", semType* retType = nullptr) :
        continueDest_(continueDest), breakDest_(breakDest), retDest_(retDest), retType_(retType) {}
};

struct IRGenerator {
    void generate(Checker& checker, Crate* crate, const std::string& outputPath);
    void generateCrate(Crate* crate);
    void generateItem(Item* item);
    void generateFunction(funcItem* func, std::string owner);
    IRValue generateBlock(blockExpr* block);
    void generateStatement(Stmt* stmt);
    IRValue generateExpr(Expr* expr);

    std::string transformType(semType* type);
    IRValue eliminateOneShared(IRValue value);
    IRValue eliminateAllRefs(IRValue value);
    std::string newLabel();
    std::string placeOrigin(Expr* expr);
    std::string addressOfObject(Expr* expr);
    void putline(std::string line, bool direct = false);
    void callReturn(std::string name);
    void enterLabel(std::string name);
    void allocate(std::string name, semType* type, bool hoist = true);
    std::string mangle(std::string owner, std::string name);
    int findIndex(std::string struName, std::string argName);
    int sizeOf(semType* type);
    int alignOf(semType* type);
    IRValue vecLen(std::string addr);
    IRValue vecIsEmpty(std::string addr);
    void vecPush(std::string addr, IRValue value, semType* elemType);
    IRValue vecRemove(std::string addr, IRValue index, semType* elemType);
    std::string cloneKey(semType* type);
    IRValue callTypeClone(std::string addr, semType* type);
    IRValue valueClone(std::string addr, semType* elemType);
    void manifestCloneImpl(semType* type);
    IRValue callTypeEq(std::string a, std::string b, semType* type);
    void manifestEqImpl(semType* type);

    std::ofstream out_;
    Checker* checker_;
    scopeStack<std::string, IRStackShadowing> scope_;
    int tempCounter_ = 0;
    std::string currentRetType_;
    std::string currentStruct_;
    bool ending_ = false;
    bool buffering_ = false;
    std::string body_;
    std::vector<std::string> entryAllocas_;
    std::vector<IRLoopInfo> loops_;
    std::map<std::string, semType*> cloneTypes_;
    std::map<std::string, int> cloneState_;
    std::map<std::string, semType*> eqTypes_;
    std::map<std::string, int> eqState_;
};
