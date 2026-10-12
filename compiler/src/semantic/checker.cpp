#include "checker.h"
#include <iostream>
#include <set>

using kKind = Type::kTypeKind; // AST Type

long long Checker::manifestConstInt(ConstValue* value) {
    if (value->kind_ == ConstValue::kConstKind::INT) {
        auto suffixExtraction = [&](std::string lit, std::vector<std::string> suffices) {
            int vlen = lit.length();
            for (auto suf : suffices) {
                int slen = suf.length();
                if (vlen > slen) {
                    if (lit.substr(vlen - slen, slen) == suf) {
                        return lit.substr(0, vlen - slen);
                    }
                }
            }
            return lit;
        };
        auto underlineExtraction = [&](std::string lit) {
            int vlen = lit.length();
            std::string ret;
            for (char ch : lit) {
                if (ch != '_') {
                    ret += ch;
                }
            }
            return ret;
        };
        std::string literal = value->text_;
        literal = suffixExtraction(literal, {"i32", "u32", "isize", "usize"});
        literal = underlineExtraction(literal);
        int base = 10;
        if (literal.length() > 2) {
            if (literal[1] == 'o') {
                base = 8;
                literal = literal.substr(0, literal.length() - 2);
            }
            else if (literal[1] == 'b') {
                base = 2;
                literal = literal.substr(0, literal.length() - 2);
            }
            else if (literal[1] == 'x') {
                base = 16;
                literal = literal.substr(0, literal.length() - 2);
            }
        }
        long long ret = 0;
        for (auto ch : literal) {
            ret *= base;
            if (ch >= 'A' && ch <= 'F') {
                ret += ch - 'A';
            }
            else {
                ret += ch - '0';
            }
        }
        return ret;
    }
    else if (value->kind_ == ConstValue::kConstKind::NEG) {
        return -manifestConstInt(value->inner_);
    }
    else if (value->kind_ == ConstValue::kConstKind::PATH) {
        auto path = value->text_;
        auto fir = path.find("::");
        if (fir != std::string::npos) {
            auto sec = path.find("::", fir + 1);
            if (sec != std::string::npos) {
                error("unsupported path in ConstValue");
                return 0;
            }
            
        }
    }
    else if (value->kind_ == ConstValue::kConstKind::BOOL) {

    }
    else {
        throw "manifestConstInt";
        return 0;
    }
}

semType* Checker::transformType(Type* type){
    if (type->kind_ == kKind::BUILTIN) {
        return semantic::makeBuiltin(type->name_);
    }
    if (type->kind_ == kKind::STRUCT) {
        return semantic::makeStruct(type->name_);
    }
    if (type->kind_ == kKind::BOX) {
        return semantic::makeBox(transformType(type->elem_));
    }
    if (type->kind_ == kKind::VEC) {
        return semantic::makeVec(transformType(type->elem_));
    }
    if (type->kind_ == kKind::REF) {
        return semantic::makeRef(transformType(type->elem_), type->isMut_);
    }
    if (type->kind_ == kKind::ARRAY) {
        return semantic::makeArray(transformType(type->elem_), manifestConstInt(type->arrayLength_));
    }
    if (type->kind_ == kKind::UNIT) {
        return semantic::makeUnit();
    }
    throw "transformType";
    return nullptr;
}

template<typename tp>
bool duplicatedName(const std::vector<tp>& params) {
    std::set<std::string> mem;
    for (tp now : params) {
        if (!mem.insert(now.name_).second) {
            return true;
        }
    }
    return false;
}

void Checker::collect(Crate* crate) {
    // 1st time: register
    auto all = crate->items_;
    auto registerFuncAndConst = [&](Item* item, std::map<std::string, funcSignature> &funcSig,
                                    std::map<std::string, constSignature> &constSig, bool insideImpl) {
        if (auto func = dynamic_cast<funcItem*>(item); func) {
            std::string funcName = func->name_;
            if (funcSig.count(funcName) || constSig.count(funcName)) {
                error("duplicate function name (with function or constant) : " + funcName);
            }
            else if (!insideImpl && (funcName == "get_i32" || funcName == "print_i32" || funcName == "println_i32")) {
                error("function attempted to use protected name : " + funcName);
            }
            else if (!insideImpl && func->parameter_.size() != 0 && func->parameter_[0].isSelf_) {
                error("top-level function attempted to use self : " + funcName);
            }
            else if (duplicatedName<Param>(func->parameter_)) {
                error("duplicate function parameter name : " + funcName);
            }
            else {
                funcSig[funcName];
            }
        }
        if (auto value = dynamic_cast<constItem*>(item); value) {
            std::string constName = value->name_;
            if (funcSig.count(constName) || constSig.count(constName)) {
                error("duplicate constant name (with function or constant) : " + constName);
            }
            else if (constName == "get_i32" || constName == "print_i32" || constName == "println_i32") {
                error("constant attempted to use protected name : " + constName);
            }
            else {
                constSig[constName];
                constSig[constName].value_ = value->value_;
            }
        }
    };
    for (Item* item : all) {
        registerFuncAndConst(item, table_.funcSig, table_.constSig, false);
        if (auto stru = dynamic_cast<structItem*>(item); stru) {
            std::string struName = stru->name_;
            if (table_.structSig.count(struName)) {
                error("duplicate struct name : " + struName);
            }
            else if (struName == "i32" || struName == "u32" || struName == "isize" || struName == "usize") {
                error("struct attempted to use protected name : " + struName);
            }
            else if (duplicatedName<structField>(stru->fields_)) {
                error("duplicate struct field name : " + struName);
            }
            else {
                table_.structSig[struName];
            }
        }
        if (auto impl = dynamic_cast<ImplItem*>(item); impl) {
            std::string implName = impl->type_->name_;
            if (impl->type_->kind_ != kKind::STRUCT) {
                error("attempted to implement a non-struct type, name = " + implName);
            }
            else {
                for (Item* inner : impl->items_) {
                    registerFuncAndConst(inner, table_.methods[implName], table_.associatedConsts[implName], true);
                }
            }
        }
    }
    for (const auto& [name, anything] : table_.methods) {
        if (table_.structSig.count(name) == 0) {
            error("non-existing struct " + name + " have methods");
        }
    }
    for (const auto& [name, anything] : table_.associatedConsts) {
        if (table_.structSig.count(name) == 0) {
            error("non-existing struct " + name + " have constants");
        }
    }
    return;
}

void Checker::error(std::string message) {
    std::cerr << "error occurred!\n" << message << std::endl;
    reject_ = true;
    return;
}

void Checker::checkCrate(Crate* crate) {
    collect(crate);
    return;
}