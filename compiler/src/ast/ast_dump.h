#pragma once
#include <iostream>
#include <string>
#include "ast/ast.h"

// 调试用的 AST 打印：缩进 2 空格一层。
// 纯工具代码，不需要背；实现别的阶段时可以随手改格式。

inline void dumpIndent(int depth) {
    std::cout << std::string(static_cast<size_t>(depth) * 2, ' ');
}

inline void dumpType(const Type* t, int d) {
    if (!t) return;
    dumpIndent(d);
    switch (t->kind_) {
        case Type::BUILTIN: std::cout << "Type " << t->name_ << "\n"; break;
        case Type::STRUCT:  std::cout << "Type " << t->name_ << "\n"; break;
        case Type::UNIT:    std::cout << "Type ()\n"; break;
        case Type::BOX:     std::cout << "Type Box\n"; dumpType(t->elem_, d + 1); break;
        case Type::VEC:     std::cout << "Type Vec\n"; dumpType(t->elem_, d + 1); break;
        case Type::REF:     std::cout << "Type " << (t->isMut_ ? "&mut" : "&") << "\n";
                            dumpType(t->elem_, d + 1); break;
        case Type::ARRAY:   std::cout << "Type array len="
                                      << (t->arrayLength_ ? t->arrayLength_->text_ : "?") << "\n";
                            dumpType(t->elem_, d + 1); break;
    }
}

inline void dumpConst(const ConstValue* c, int d) {
    if (!c) return;
    dumpIndent(d);
    switch (c->kind_) {
        case ConstValue::INT:  std::cout << "ConstInt " << c->text_ << "\n"; break;
        case ConstValue::BOOL: std::cout << "ConstBool " << c->text_ << "\n"; break;
        case ConstValue::PATH: std::cout << "ConstPath " << c->text_ << "\n"; break;
        case ConstValue::NEG:  std::cout << "ConstNeg\n"; dumpConst(c->inner_, d + 1); break;
    }
}

inline void dumpPathStr(const std::vector<pathSeg>& path) {
    for (size_t i = 0; i < path.size(); ++i) {
        if (i) std::cout << "::";
        std::cout << path[i].name_;
        if (!path[i].types_.empty()) {
            std::cout << "<";
            for (size_t j = 0; j < path[i].types_.size(); ++j) {
                if (j) std::cout << ", ";
                // 类型实参只打名字，够调试用
                const Type* t = path[i].types_[j];
                if (!t) { std::cout << "?"; continue; }
                if (t->kind_ == Type::BUILTIN || t->kind_ == Type::STRUCT) std::cout << t->name_;
                else if (t->kind_ == Type::UNIT) std::cout << "()";
                else if (t->kind_ == Type::BOX) std::cout << "Box";
                else if (t->kind_ == Type::VEC) std::cout << "Vec";
                else if (t->kind_ == Type::REF) std::cout << (t->isMut_ ? "&mut" : "&");
                else std::cout << "[]";
            }
            std::cout << ">";
        }
    }
}

inline void dumpExpr(const Expr* e, int d);
inline void dumpStmt(const Stmt* s, int d);
inline void dumpItem(const Item* it, int d);

inline void dumpBlock(const blockExpr* b, int d) {
    for (auto* s : b->stmts_)
        if (s) dumpStmt(s, d);
    if (b->tail_) {
        dumpIndent(d);
        std::cout << "Tail\n";
        dumpExpr(b->tail_, d + 1);
    }
}

inline void dumpExpr(const Expr* e, int d) {
    if (!e) return;
    if (auto* x = dynamic_cast<const intLitExpr*>(e)) {
        dumpIndent(d); std::cout << "IntLit " << x->value_ << "\n";
    } else if (auto* x = dynamic_cast<const boolLitExpr*>(e)) {
        dumpIndent(d); std::cout << "BoolLit " << (x->value_ ? "true" : "false") << "\n";
    } else if (auto* x = dynamic_cast<const pathExpr*>(e)) {
        dumpIndent(d); std::cout << "Path "; dumpPathStr(x->path_); std::cout << "\n";
    } else if (dynamic_cast<const unitExpr*>(e)) {
        dumpIndent(d); std::cout << "Unit\n";
    } else if (auto* x = dynamic_cast<const blockExpr*>(e)) {
        dumpIndent(d); std::cout << "Block\n"; dumpBlock(x, d + 1);
    } else if (auto* x = dynamic_cast<const binaryExpr*>(e)) {
        dumpIndent(d); std::cout << "Binary " << x->op_ << "\n";
        dumpExpr(x->lhs_, d + 1); dumpExpr(x->rhs_, d + 1);
    } else if (auto* x = dynamic_cast<const unaryExpr*>(e)) {
        dumpIndent(d); std::cout << "Unary " << x->op_ << "\n"; dumpExpr(x->operand_, d + 1);
    } else if (auto* x = dynamic_cast<const refExpr*>(e)) {
        dumpIndent(d); std::cout << "Ref " << (x->isMut_ ? "mut" : "") << " n=" << x->num_ << "\n";
        dumpExpr(x->operand_, d + 1);
    } else if (auto* x = dynamic_cast<const derefExpr*>(e)) {
        dumpIndent(d); std::cout << "Deref\n"; dumpExpr(x->operand_, d + 1);
    } else if (auto* x = dynamic_cast<const castExpr*>(e)) {
        dumpIndent(d); std::cout << "Cast\n";
        dumpExpr(x->from_, d + 1); dumpType(x->to_, d + 1);
    } else if (auto* x = dynamic_cast<const callExpr*>(e)) {
        dumpIndent(d); std::cout << "Call\n";
        dumpExpr(x->callee_, d + 1);
        for (auto* a : x->args_) dumpExpr(a, d + 1);
    } else if (auto* x = dynamic_cast<const methodCallExpr*>(e)) {
        dumpIndent(d); std::cout << "MethodCall " << x->name_ << "\n";
        dumpExpr(x->from_, d + 1);
        for (auto* a : x->args_) dumpExpr(a, d + 1);
    } else if (auto* x = dynamic_cast<const fieldExpr*>(e)) {
        dumpIndent(d); std::cout << "Field " << x->name_ << "\n"; dumpExpr(x->from_, d + 1);
    } else if (auto* x = dynamic_cast<const indexExpr*>(e)) {
        dumpIndent(d); std::cout << "Index\n";
        dumpExpr(x->from_, d + 1); dumpExpr(x->index_, d + 1);
    } else if (auto* x = dynamic_cast<const structInitExpr*>(e)) {
        dumpIndent(d); std::cout << "StructInit\n";
        dumpExpr(x->path_, d + 1);
        for (auto& f : x->fields_) {
            dumpIndent(d + 1); std::cout << "field " << f.name_ << "\n";
            dumpExpr(f.value_, d + 2);
        }
    } else if (auto* x = dynamic_cast<const arrayExpr*>(e)) {
        dumpIndent(d); std::cout << "Array\n";
        for (auto* a : x->args_) dumpExpr(a, d + 1);
    } else if (auto* x = dynamic_cast<const repeatedArrayExpr*>(e)) {
        dumpIndent(d); std::cout << "RepeatArray len="
                                 << (x->num_ ? x->num_->text_ : "?") << "\n";
        dumpExpr(x->value_, d + 1);
    } else if (auto* x = dynamic_cast<const ifExpr*>(e)) {
        dumpIndent(d); std::cout << "If\n";
        dumpExpr(x->cond_, d + 1);
        dumpExpr(x->then_, d + 1);
        if (x->else_) { dumpIndent(d + 1); std::cout << "Else\n"; dumpExpr(x->else_, d + 2); }
    } else if (auto* x = dynamic_cast<const whileExpr*>(e)) {
        dumpIndent(d); std::cout << "While\n";
        dumpExpr(x->cond_, d + 1); dumpExpr(x->block_, d + 1);
    } else if (auto* x = dynamic_cast<const loopExpr*>(e)) {
        dumpIndent(d); std::cout << "Loop\n"; dumpExpr(x->block_, d + 1);
    } else if (auto* x = dynamic_cast<const breakExpr*>(e)) {
        dumpIndent(d); std::cout << "Break\n";
        if (x->tail_) dumpExpr(x->tail_, d + 1);
    } else if (auto* x = dynamic_cast<const returnExpr*>(e)) {
        dumpIndent(d); std::cout << "Return\n";
        if (x->tail_) dumpExpr(x->tail_, d + 1);
    } else if (dynamic_cast<const continueExpr*>(e)) {
        dumpIndent(d); std::cout << "Continue\n";
    } else {
        dumpIndent(d); std::cout << "Expr?\n";
    }
}

inline void dumpStmt(const Stmt* s, int d) {
    if (!s) return;
    if (auto* x = dynamic_cast<const letStmt*>(s)) {
        dumpIndent(d);
        std::cout << "Let " << (x->isMut_ ? "mut " : "") << x->name_ << "\n";
        if (x->type_) dumpType(x->type_, d + 1);
        dumpExpr(x->init_, d + 1);
    } else if (auto* x = dynamic_cast<const exprStmt*>(s)) {
        dumpIndent(d); std::cout << "ExprStmt\n";
        dumpExpr(x->expr_, d + 1);
    } else {
        dumpIndent(d); std::cout << "Stmt?\n";
    }
}

inline void dumpItem(const Item* it, int d) {
    if (!it) return;
    if (auto* x = dynamic_cast<const funcItem*>(it)) {
        dumpIndent(d); std::cout << "Fn " << x->name_ << "(";
        for (size_t i = 0; i < x->parameter_.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << (x->parameter_[i].isMut_ ? "mut " : "") << x->parameter_[i].name_;
        }
        std::cout << ")\n";
        if (x->retType_) dumpType(x->retType_, d + 1);
        dumpExpr(x->body_, d + 1);
    } else if (auto* x = dynamic_cast<const structItem*>(it)) {
        dumpIndent(d); std::cout << "Struct " << x->name_ << "\n";
        for (auto& der : x->derives_) { dumpIndent(d + 1); std::cout << "derive " << der << "\n"; }
        for (auto& f : x->fields_) {
            dumpIndent(d + 1); std::cout << "field " << f.name_ << "\n";
            dumpType(f.type_, d + 2);
        }
    } else if (auto* x = dynamic_cast<const constItem*>(it)) {
        dumpIndent(d); std::cout << "Const " << x->name_ << "\n";
        dumpType(x->type_, d + 1);
        dumpConst(x->value_, d + 1);
    } else if (auto* x = dynamic_cast<const ImplItem*>(it)) {
        dumpIndent(d); std::cout << "Impl\n";
        dumpType(x->type_, d + 1);
        for (auto* sub : x->items_) dumpItem(sub, d + 1);
    } else {
        dumpIndent(d); std::cout << "Item?\n";
    }
}

inline void dumpCrate(const Crate* c) {
    if (!c) return;
    for (auto* it : c->items_) dumpItem(it, 0);
}
