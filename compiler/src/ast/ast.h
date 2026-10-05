#pragma once
#include <any>
#include <string>
#include <vector>

struct Node{
    virtual ~Node() {

    }
};

struct Type;
struct Stmt;

struct ConstValue : Node {
    enum kConstKind { INT, BOOL, PATH, NEG } ;
    kConstKind kind_;
    std::string text_;
    ConstValue* inner_;
};

struct Expr : Node {

};

struct pathSeg {
    std::string name_;
    std::vector<Type*> types_;
};

struct pathExpr : Expr {
    std::vector<pathSeg> path_;
};

struct blockExpr : Expr {
    std::vector<Stmt*> stmts_;
    Expr* tail_;
};

struct whileExpr : Expr {
    Expr* cond_;
    blockExpr* block_;
};

struct loopExpr : Expr {
    blockExpr* block_;
};

struct ifExpr : Expr {
    Expr* cond_;
    blockExpr* then_;
    Expr* else_;
};

struct binaryExpr : Expr {
    std::string op_;
    Expr* lhs_;
    Expr* rhs_;
};

struct castExpr : Expr {
    Expr* from_;
    Type* to_;
};

struct unaryExpr : Expr {
    std::string op_;
    Expr* operand_;
};

struct refExpr : Expr {
    bool isMut_;
    int num_;
    Expr* operand_;
};

struct derefExpr : Expr {
    Expr* operand_;
};

struct callExpr : Expr {
    Expr* callee_;
    std::vector<Expr*> args_;
};

struct methodCallExpr : Expr {
    Expr* from_;
    std::string name_;
    std::vector<Expr*> args_;
};

struct fieldExpr : Expr {
    Expr* from_;
    std::string name_;
};

struct indexExpr : Expr {
    Expr* from_;
    Expr* index_;
};

struct fieldInit {
    std::string name_;
    Expr* value_;
};

struct structInitExpr : Expr {
    Expr* path_;
    std::vector<fieldInit> fields_;
};

struct unitExpr : Expr {

};

struct breakExpr : Expr {
    Expr* tail_;
};

struct returnExpr : Expr {
    Expr* tail_;
};

struct continueExpr : Expr {

};

struct intLitExpr : Expr {
    std::string value_;
};

struct boolLitExpr : Expr {
    bool value_;
};

struct arrayExpr : Expr {
    std::vector<Expr*> args_;
};

struct repeatedArrayExpr : Expr {
    Expr* value_;
    ConstValue* num_;
};

struct Type : Node {
    enum kTypeKind { BUILTIN, STRUCT, BOX, VEC, REF, ARRAY, UNIT } ;
    kTypeKind kind_;
    std::string name_;
    Type* elem_;
    bool isMut_;
    ConstValue* arrayLength_;
};

struct Stmt : Node {

};

struct letStmt : Stmt {
    std::string name_;
    bool isMut_;
    Type* type_;
    Expr* init_;
};

struct exprStmt : Stmt {
    Expr* expr_;
};

struct Item : Node {

};
/*
item
    : useDeclaration (discarded)
    | functionDefinition
    | structDefinition
    | constantItem
    | inherentImpl
    ;
*/

struct Param {
    std::string name_;
    Type* type_;
    bool isMut_, isSelf_;
};

struct funcItem : Item {
    std::string name_;
    std::vector<Param> parameter_;
    Type* retType_;
    Expr* body_;
};
/*
functionDefinition
    : FN identifier genericParams? LPAREN functionParameters? RPAREN
      (ARROW typeRef)? whereClause? blockExpression
    ;
*/

struct structField {
    std::string name_;
    Type* type_;
};
/*
structField
    : identifier COLON typeRef
    ;
*/

struct structItem : Item {
    std::vector<std::string> derives_;
    std::string name_;
    std::vector<structField> fields_;
};
/*
structDefinition
    : outerAttribute* STRUCT identifier genericParams? whereClause?
      LBRACE (structField (COMMA structField)* COMMA?)? RBRACE
    ;
*/

struct constItem : Item {
    std::string name_;
    Type* type_;
    ConstValue* value_;
};
/*
constantItem
    : CONST identifier COLON typeRef equalsSign constValue SEMI
    ;
*/

struct ImplItem : Item {
    Type* type_;
    std::vector<Item*> items_;
};
/*
inherentImpl
    : IMPL genericParams? typeRef whereClause? LBRACE associatedItem* RBRACE
    ;
*/

struct Crate : Node {
    std::vector<Item*> items_;
};
/*
crate
    : item* EOF
    ;
*/

inline Expr* anyExpr(const std::any& val) { return std::any_cast<Expr*>(val); }
inline Type* anyType(const std::any& val) { return std::any_cast<Type*>(val); }
inline Stmt* anyStmt(const std::any& val) { return std::any_cast<Stmt*>(val); }
inline Item* anyItem(const std::any& val) { return std::any_cast<Item*>(val); }
inline ConstValue* anyConstValue(const std::any& val) { return std::any_cast<ConstValue*>(val); }
inline std::string anyString(const std::any& val) { return std::any_cast<std::string>(val); }