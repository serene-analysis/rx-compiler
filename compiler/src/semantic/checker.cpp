#include "checker.h"
#include <iostream>
#include <set>
#include <functional>
#include <algorithm>

using kKind = Type::kTypeKind; // AST Type

static std::string intSuffix(const std::string& text) {
    for (const std::string suf : {"usize", "isize", "u32", "i32"}) {
        if (text.size() > suf.size() && text.substr(text.size() - suf.size()) == suf) {
            return suf;
        }
    }
    return "";
}

static bool isIntegerType(semType* t) {
    if (t == nullptr || t->kind_ != semantic::Type::kTypeKind::BUILTIN) {
        return false;
    }
    return t->name_ == "i32" || t->name_ == "u32" || t->name_ == "isize" || t->name_ == "usize";
}

static bool isSignedType(semType* t) {
    return t != nullptr && t->kind_ == semantic::Type::kTypeKind::BUILTIN &&
           (t->name_ == "i32" || t->name_ == "isize");
}

static bool isBoolType(semType* t) {
    return t != nullptr && t->kind_ == semantic::Type::kTypeKind::BUILTIN && t->name_ == "bool";
}

static bool isNever(semType* type){
    return type != nullptr && type->kind_ == semType::kTypeKind::NEVER;
}

static bool matches(semType* type, semType* expected) {
    return isNever(type) || semantic::typeEqual(type, expected);
}

static semType* digOneRef(semType* type) {
    if (type != nullptr && type->kind_ == semType::kTypeKind::REF) {
        return type->elem_;
    }
    return type;
}

constSignature* Checker::findConst(const std::string& owner, const std::string& name) {
    if (owner.empty()) {
        auto it = table_.constSig.find(name);
        return it == table_.constSig.end() ? nullptr : &it->second;
    }
    auto outer = table_.associatedConsts.find(owner);
    if (outer == table_.associatedConsts.end()) {
        return nullptr;
    }
    auto it = outer->second.find(name);
    return it == outer->second.end() ? nullptr : &it->second;
}

Constant Checker::manifestConst(std::string owner, std::string name) {
    constSignature* mem = findConst(owner, name);
    if (mem == nullptr) {
        error(owner.empty() ? "unresolved constant " + name
                            : "unresolved constant " + owner + "::" + name);
        return Constant();
    }
    if (mem->state_ == constSignature::kState::DONE) {
        return mem->real_;
    }
    if (mem->state_ == constSignature::kState::CALCULATING) {
        error("a cycle appeared in constvalue evaluation");
        return Constant();
    }
    mem->state_ = constSignature::kState::CALCULATING;
    Constant ret = calculateConst(mem->value_, owner);
    mem->real_ = ret;
    mem->state_ = constSignature::kState::DONE;
    return ret;
}

Constant Checker::calculateConst(ConstValue* value, std::string owner) {
    if (value->kind_ == ConstValue::kConstKind::INT) {
        auto underlineExtraction = [&](std::string lit) {
            std::string ret;
            for (char ch : lit) {
                if (ch != '_') {
                    ret += ch;
                }
            }
            return ret;
        };
        std::string literal = value->text_;
        std::string suffix = intSuffix(literal);
        literal = literal.substr(0, literal.size() - suffix.size());
        literal = underlineExtraction(literal);
        int base = 10;
        if (literal.length() > 2) {
            if (literal[1] == 'o') {
                base = 8;
                literal = literal.substr(2);
            }
            else if (literal[1] == 'b') {
                base = 2;
                literal = literal.substr(2);
            }
            else if (literal[1] == 'x') {
                base = 16;
                literal = literal.substr(2);
            }
        }
        long long ret = std::stoll(literal, 0, base);
        return Constant(false, ret);
    }
    else if (value->kind_ == ConstValue::kConstKind::NEG) {
        if (value->inner_->kind_ == ConstValue::kConstKind::INT) {
            std::string suffix = intSuffix(value->inner_->text_);
            if (suffix == "u32" || suffix == "usize") {
                error("attempted to negate an unsigned integer literal");
                return Constant();
            }
        }
        Constant ret = calculateConst(value->inner_, owner);
        if (ret.isBool_) {
            error("attempted to negate an bool constvalue");
            return Constant();
        }
        ret.value_ *= -1;
        return ret;
    }
    else if (value->kind_ == ConstValue::kConstKind::PATH) {
        auto path = value->text_;
        auto fir = path.find("::");
        if (fir == std::string::npos) {
            return manifestConst(std::string(), path);
        }
        if (path.find("::", fir + 1) != std::string::npos) {
            error("unsupported path in ConstValue");
            return Constant();
        }
        std::string callee = path.substr(0, fir);
        std::string name = path.substr(fir + 2);
        if (callee == "Self") {
            if (owner.empty()) {
                error("attempted to use \"Self\" in top-level scope");
                return Constant();
            }
            return manifestConst(owner, name);
        }
        return manifestConst(callee, name);
    }
    else if (value->kind_ == ConstValue::kConstKind::BOOL) {
        return Constant(true, value->text_ == "TRUE" ? 1 : 0);
    }
    else {
        throw "manifestConstInt";
        return Constant();
    }
}

bool Checker::checkConstInit(const ConstValue* value, semType* expected, const std::string& owner) {
    if (expected == nullptr) {
        return false;
    }
    if (expected->kind_ == semantic::Type::kTypeKind::ERROR) {
        return true;
    }
    if (value->kind_ == ConstValue::kConstKind::BOOL) {
        if (!isBoolType(expected)) {
            error("bool constant initializer does not match the declared type");
            return false;
        }
        return true;
    }
    if (value->kind_ == ConstValue::kConstKind::INT) {
        std::string suffix = intSuffix(value->text_);
        if (!suffix.empty()) {
            if (expected->kind_ != semantic::Type::kTypeKind::BUILTIN || expected->name_ != suffix) {
                error("integer literal of type " + suffix + " does not match the declared type");
                return false;
            }
            return true;
        }
        if (!isIntegerType(expected)) {
            error("integer literal does not match the declared type");
            return false;
        }
        return true;
    }
    if (value->kind_ == ConstValue::kConstKind::NEG) {
        if (!isSignedType(expected)) {
            error("negative constant initializer requires a signed integer type");
            return false;
        }
        return checkConstInit(value->inner_, expected, owner);
    }
    if (value->kind_ == ConstValue::kConstKind::PATH) {
        auto path = value->text_;
        auto fir = path.find("::");
        std::string callee, name;
        if (fir == std::string::npos) {
            callee = "";
            name = path;
        }
        else {
            if (path.find("::", fir + 1) != std::string::npos) {
                error("unsupported path in ConstValue");
                return false;
            }
            callee = path.substr(0, fir);
            name = path.substr(fir + 2);
            if (callee == "Self") {
                if (owner.empty()) {
                    error("attempted to use \"Self\" in top-level scope");
                    return false;
                }
                callee = owner;
            }
        }
        constSignature* cs = findConst(callee, name);
        if (cs == nullptr) {
            error(callee.empty() ? "unresolved constant " + name
                                    : "unresolved constant " + callee + "::" + name);
            return false;
        }
        if (!typeEqual(cs->type_, expected)) {
            error("constant initializer type does not match the declared type");
            return false;
        }
        return true;
    }
    return true;
}

bool Checker::evalArrayLength(ConstValue* value, const std::string& owner, unsigned& length) {
    if (value->kind_ == ConstValue::kConstKind::INT) {
        std::string suffix = intSuffix(value->text_);
        if (!suffix.empty() && suffix != "usize") {
            error("attempted to explicitly use non-usize int literal as an array's length : " + value->text_);
            return false;
        }
    }
    else if (value->kind_ == ConstValue::kConstKind::PATH) {
        auto path = value->text_;
        auto fir = path.find("::");
        std::string constOwner, name;
        if (fir == std::string::npos) {
            constOwner = "";
            name = path;
        }
        else {
            if (path.find("::", fir + 1) != std::string::npos) {
                error("unsupported path in ConstValue");
                return false;
            }
            constOwner = path.substr(0, fir);
            name = path.substr(fir + 2);
            if (constOwner == "Self") {
                if (owner.empty()) {
                    error("attempted to use \"Self\" in top-level scope");
                    return false;
                }
                constOwner = owner;
            }
        }
        constSignature* cs = findConst(constOwner, name);
        if (cs == nullptr) {
            error(constOwner.empty() ? "unresolved constant " + name
                                     : "unresolved constant " + constOwner + "::" + name);
            return false;
        }
        if (!typeEqual(cs->type_, semantic::makeBuiltin("usize"))) {
            error("array length must be usize");
            return false;
        }
    }
    else {
        error("array length must be usize");
        return false;
    }
    Constant len = calculateConst(value, owner);
    length = static_cast<unsigned>(len.value_);
    return true;
}

semType* Checker::transformType(Type* type){
    if (type->kind_ == kKind::BUILTIN) {
        if (type->hasGenerigArgs) {
            error("builtin type has generic args");
            return semantic::makeError();
        }
        return semantic::makeBuiltin(type->name_);
    }
    if (type->kind_ == kKind::STRUCT) {
        if (type->hasGenerigArgs) {
            error("struct type has generic args");
            return semantic::makeError();
        }
        if (type->name_ == "Self") {
            if (currentStruct_ == "") {
                error("attempted to use Self in top-level type");
                return semantic::makeError();
            }
            return semantic::makeStruct(currentStruct_);
        }
        if (!table_.structSig.count(type->name_)) {
            error("attempted to use unknown type : " + type->name_);
            return semantic::makeError();
        }
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
        unsigned length = 0;
        if (!evalArrayLength(type->arrayLength_, currentStruct_, length)) {
            return semantic::makeError();
        }
        return semantic::makeArray(transformType(type->elem_), length);
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
            if (!insideImpl && funcName == "main" && func->hasGenericParams_) {
                error("main function should not have generic params");
            }
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
    table_.funcSig["get_i32"].retType_ = semantic::makeBuiltin("i32");
    table_.funcSig["print_i32"].retType_ = semantic::makeUnit();
    table_.funcSig["print_i32"].params_.push_back(semantic::makeBuiltin("i32"));
    table_.funcSig["println_i32"].retType_ = semantic::makeUnit();
    table_.funcSig["println_i32"].params_.push_back(semantic::makeBuiltin("i32"));
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
    for (const auto &[name, anything] : table_.methods) {
        if (table_.structSig.count(name) == 0) {
            error("non-existing struct " + name + " have methods");
        }
    }
    for (const auto &[name, anything] : table_.associatedConsts) {
        if (table_.structSig.count(name) == 0) {
            error("non-existing struct " + name + " have constants");
        }
    }
    return;
}

void Checker::evaluateConstants(Crate* crate) {
    for (Item* item : crate->items_) {
        if (auto con = dynamic_cast<constItem*>(item); con) {
            manifestConst("", con->name_);
        }
        else if (auto impl = dynamic_cast<ImplItem*>(item); impl) {
            if (impl->type_->kind_ != Type::kTypeKind::STRUCT) {
                continue;
            }
            for (Item* dw : impl->items_) {
                if (auto dcon = dynamic_cast<constItem*>(dw); dcon) {
                    manifestConst(impl->type_->name_, dcon->name_);
                }
            }
        }
    }
    return;
}

void Checker::evaluateTypes(Crate* crate) {
    for (Item* item : crate->items_) {
        if (auto con = dynamic_cast<constItem*>(item); con) {
            currentStruct_ = "";
            table_.constSig[con->name_].type_ = transformType(con->type_);
        }
        else if (auto impl = dynamic_cast<ImplItem*>(item); impl) {
            if (impl->type_->kind_ != Type::kTypeKind::STRUCT) {
                continue;
            }
            std::string mem = currentStruct_;
            currentStruct_ = impl->type_->name_;
            for (Item* dw : impl->items_) {
                if (auto con = dynamic_cast<constItem*>(dw); con) {
                    table_.associatedConsts[impl->type_->name_][con->name_].type_ = transformType(con->type_);
                }
            }
            currentStruct_ = mem;
        }
    }
    for (Item* item : crate->items_) {
        if (auto* con = dynamic_cast<constItem*>(item); con) {
            checkConstInit(con->value_, table_.constSig[con->name_].type_, "");
        }
        else if (auto* impl = dynamic_cast<ImplItem*>(item); impl) {
            if (impl->type_->kind_ != Type::kTypeKind::STRUCT) {
                continue;
            }
            std::string target = impl->type_->name_;
            for (Item* sub : impl->items_) {
                if (auto* con = dynamic_cast<constItem*>(sub); con) {
                    checkConstInit(con->value_, table_.associatedConsts[target][con->name_].type_, target);
                }
            }
        }
    }
    for (Item* item : crate->items_) {
        if (auto stru = dynamic_cast<structItem*>(item); stru) {
            auto dt = table_.structSig.find(stru->name_);
            if (dt == table_.structSig.end()) { // necessary?
                continue;
            }
            std::string mem = currentStruct_;
            currentStruct_ = stru->name_;
            dt->second.derives_ = stru->derives_;
            for (auto field : stru->fields_) {
                dt->second.fields_.push_back({field.name_, transformType(field.type_)});
            }
            currentStruct_ = mem;
        }
    }
    currentStruct_ = "";
    for (Item* item : crate->items_) {
        if (auto func = dynamic_cast<funcItem*>(item); func) {
            auto &sig = table_.funcSig[func->name_];
            sig.retType_ = transformType(func->retType_);
            for (auto para : func->parameter_) {
                sig.params_.push_back(transformType(para.type_));
            }
        }
        if (auto impl = dynamic_cast<ImplItem*>(item); impl) {
            if (impl->type_->kind_ != Type::kTypeKind::STRUCT) {
                continue;
            }
            std::string mem = currentStruct_;
            currentStruct_ = impl->type_->name_;
            for (Item* down : impl->items_) {
                if (auto func = dynamic_cast<funcItem*>(down); func) {
                    auto &sig = table_.methods[impl->type_->name_][func->name_];
                    sig.retType_ = transformType(func->retType_);
                    size_t start = 0;
                    if (!func->parameter_.empty() && func->parameter_[0].isSelf_) {
                        const Param& self = func->parameter_[0];
                        sig.isMethod_ = true;
                        if (self.type_->kind_ == kKind::REF) {
                            sig.selfRef_ = true;
                            sig.selfMut_ = self.type_->isMut_;
                        }
                        else {
                            sig.selfMut_ = self.isMut_;
                        }
                        start = 1;
                    }
                    for (size_t i = start; i < func->parameter_.size(); ++i) {
                        sig.params_.push_back(transformType(func->parameter_[i].type_));
                    }
                }
            }
            currentStruct_ = mem;
        }
    }
}

semType* Checker::lub(std::vector<semType*> types) {
    std::vector<semType*> all;
    semType* t = nullptr; // strange name in spec
    for (auto u : types) {
        if (isNever(u)) {
            continue;
        }
        if (u->kind_ == semType::kTypeKind::ERROR) {
            return u;
        }
        if (t == nullptr) {
            t = u;
            all.push_back(u);
            continue;
        }
        if (canImplicitlyTurn(u, t)) {
            all.push_back(u);
        }
        else if(canImplicitlyTurn(t, u)) {
            bool can = true;
            for (auto now : all) {
                can &= canImplicitlyTurn(now, u);
            }
            if (can) {
                t = u;
                all.push_back(u);
                continue;
            }
            else {
                error("lub failed");
                return semantic::makeError();
            }
        }
        else {
            error("lub failed");
            return semantic::makeError();
        }
    }
    return t == nullptr ? semantic::makeNever() : t;
}

bool Checker::canImplicitlyTurn(semType* from, semType* to) {
    if (isNever(from)) {
        return true;
    }
    if (semantic::typeEqual(from, to)) {
        return true;
    }
    std::function<bool(semType*, semType*, bool)> canDerefToRef = [&](semType* lhs, semType* rhs, bool mustmut) {
        if (semantic::typeEqual(lhs, rhs)) {
            return true;
        }
        if (lhs->kind_ == semType::kTypeKind::REF && (!mustmut || lhs->isMut_) && canDerefToRef(lhs->elem_, rhs, mustmut)) {
            return true;
        }
        if (lhs->kind_ == semType::kTypeKind::BOX && canDerefToRef(lhs->elem_, rhs, mustmut)) {
            return true;
        }
        return false;
    };
    if (from->kind_ == semType::kTypeKind::REF && to->kind_ == semType::kTypeKind::REF){
        if (from->isMut_ && semantic::typeEqual(from->elem_, to->elem_)) {
            return true;
        }
        if (!from->isMut_ && to->isMut_) {
            return false;
        }
        return canDerefToRef(from->elem_, to->elem_, from->isMut_ && to->isMut_);
    }
    else {
        return false;
    }
}

semType* Checker::lookupValue(std::string name) { // I think the spec said that using function as a value is an UB?
    if (scope_.find(name)) {
        return scope_.memory_[name].top().type_;
    }
    auto sig = findConst("", name);
    if (sig == nullptr) {
        error("can't resolve name in lookupValue : " + name);
        return semantic::makeError();
    }
    return sig->type_;
}

bool Checker::isPlaceable(Expr* expr) {
    if (auto path = dynamic_cast<pathExpr*>(expr); path != nullptr && path->path_.size() == 1){
        return scope_.find(path->path_[0].name_);
    }
    if (auto field = dynamic_cast<fieldExpr*>(expr); field) {
        return isPlaceable(field->from_);
    }
    if (auto index = dynamic_cast<indexExpr*>(expr); index) {
        return isPlaceable(index->from_);
    }
    if (auto deref = dynamic_cast<derefExpr*>(expr); deref) {
        return true;
    }
    return false;
}


bool Checker::isMutablePlaceable(Expr* expr) {
    if (auto path = dynamic_cast<pathExpr*>(expr); path != nullptr && path->path_.size() == 1){
        if (!scope_.find(path->path_[0].name_)) {
            return false;
        }
        return scope_.memory_[path->path_[0].name_].top().isMut_;
    }
    if (auto field = dynamic_cast<fieldExpr*>(expr); field) {
        semType* ftype = infer(field->from_);
        if (ftype->kind_ == semType::kTypeKind::REF) {
            if (!ftype->isMut_) {
                return false;
            }
            return isMutablyAccessible(field->from_);
        }
        if (ftype->kind_ == semType::kTypeKind::STRUCT) {
            return receiverMutable(field->from_);
        }
        if (ftype->kind_ == semType::kTypeKind::BOX) {
            return receiverMutable(field->from_);
        }
        return false;
    }
    if (auto index = dynamic_cast<indexExpr*>(expr); index) {
        semType* ftype = infer(index->from_);
        if (ftype->kind_ == semType::kTypeKind::REF) {
            if (!ftype->isMut_) {
                return false;
            }
            return isMutablyAccessible(index->from_);
        }
        if (ftype->kind_ == semType::kTypeKind::VEC) {
            if (!isPlaceable(index->from_)) {
                return true;
            }
            return receiverMutable(index->from_);
        }
        if (ftype->kind_ == semType::kTypeKind::ARRAY || ftype->kind_ == semType::kTypeKind::BOX) {
            return receiverMutable(index->from_);
        }
        return false;
    }
    if (auto deref = dynamic_cast<derefExpr*>(expr); deref) {
        semType* inner = infer(deref->operand_);
        if (inner->kind_ == semType::kTypeKind::BOX) {
            return isMutablePlaceable(deref->operand_);
        }
        if (inner->kind_ == semType::kTypeKind::REF && inner->isMut_) {
            return receiverMutable(deref->operand_);
        }
        return false;
    }
    return false;
}

bool Checker::isMutablyAccessible(Expr* expr) {
    if (auto path = dynamic_cast<pathExpr*>(expr); path) {
        if (path->path_.size() == 1) {
            return scope_.find(path->path_[0].name_);
        }
        else {
            return false;
        }
    }
    if (auto der = dynamic_cast<derefExpr*>(expr); der) {
        semType* inner = infer(der->operand_);
        if (inner->kind_ == semType::kTypeKind::REF && inner->isMut_) {
            if (!isPlaceable(der->operand_)) {
                return true;
            }
            else {
                return isMutablyAccessible(der->operand_);
            }
        }
        else if (inner->kind_ == semType::kTypeKind::BOX) {
            return isMutablyAccessible(der->operand_);
        }
        else {
            return false;
        }
    }
    if (auto fie = dynamic_cast<fieldExpr*>(expr); fie) {
        return isMutablyAccessible(fie->from_);
    }
    if (auto ind = dynamic_cast<indexExpr*>(expr); ind) {
        semType* ftype = infer(ind->from_);
        if (ftype->kind_ == semType::kTypeKind::REF) {
            if (!ftype->isMut_) {
                return false;
            }
            return isMutablyAccessible(ind->from_);
        }
        if (ftype->kind_ == semType::kTypeKind::VEC) {
            if (!isPlaceable(ind->from_)) {
                return true;
            }
            return isMutablePlaceable(ind->from_);
        }
        if (ftype->kind_ == semType::kTypeKind::ARRAY) {
            return isMutablyAccessible(ind->from_);
        }
        if (ftype->kind_ == semType::kTypeKind::BOX) {
            if (ftype->elem_->kind_ == semType::kTypeKind::REF && ftype->elem_->isMut_) {
                return isMutablyAccessible(ind->from_);
            }
            return isMutablePlaceable(ind->from_);
        }
        return false;
    }
    return false;
}

bool Checker::receiverMutable(Expr* expr) {
    semType* type = infer(expr);
    if (type->kind_ == semType::kTypeKind::ERROR) {
        return false;
    }
    if (type->kind_ == semType::kTypeKind::REF) {
        if (!type->isMut_) {
            return false;
        }
        if (!isPlaceable(expr)) {
            return true;
        }
        return isMutablyAccessible(expr);
    }
    if (type->kind_ == semType::kTypeKind::BOX) {
        if (type->elem_->kind_ == semType::kTypeKind::REF && type->elem_->isMut_) {
            return isMutablyAccessible(expr);
        }
        else {
            return isMutablePlaceable(expr);
        }
    }
    if (!isPlaceable(expr)) {
        return true;
    }
    return isMutablePlaceable(expr);
}

void Checker::eliminateOneShared(semType*& type, bool& valid) {
    if (type->kind_ == semType::kTypeKind::REF) {
        if (type->isMut_) {
            error("operand can't be a mutable reference");
            valid = false;
        }
        type = type->elem_;
    }
    if (type->kind_ == semType::kTypeKind::REF) {
        error("operand can't have multiple reference layers");
        valid = false;
    }
}

semType* Checker::infer(Expr* expr) {
    if (auto lit = dynamic_cast<intLitExpr*>(expr); lit) {
        std::string suf = intSuffix(lit->value_);
        return suf.empty() ? semantic::makeBuiltin("i32") : semantic::makeBuiltin(suf);
    }
    if (auto lit = dynamic_cast<boolLitExpr*>(expr); lit) {
        return semantic::makeBuiltin("bool");
    }
    if (auto unit = dynamic_cast<unitExpr*>(expr); unit) {
        return semantic::makeUnit();
    }
    if (auto block = dynamic_cast<blockExpr*>(expr); block) {
        return checkBlock(block, nullptr);
    }
    if (auto path = dynamic_cast<pathExpr*>(expr); path) {
        if (path->path_.size() == 1) {
            if (path->path_[0].types_.size() != 0) {
                error("single word path should not have template types");
                return semantic::makeError();
            }
            std::string name = path->path_[0].name_;
            return lookupValue(name);
        }
        else if (path->path_.size() == 2) {
            if (path->path_[0].types_.size() != 0 || path->path_[1].types_.size() != 0) { // pathExpr never ever allows template types
                error("single word path should not have template typess");
                return semantic::makeError();
            }
            std::string callee = path->path_[0].name_, name = path->path_[1].name_;
            if (callee == "Self") {
                if (currentStruct_ == "") {
                    error("\"Self\" outside implement");
                    return semantic::makeError();
                }
                auto sig = findConst(currentStruct_, name);
                if (sig == nullptr) {
                    error("can't resolve name in lookupValue : " + name);
                    return semantic::makeError();
                }
                return sig->type_;
            }
            else {
                auto sig = findConst(callee, name);
                if (sig == nullptr) {
                    error("can't resolve name in lookupValue : " + name);
                    return semantic::makeError();
                }
                return sig->type_;
            }
        }
        else {
            error("length of a pathExpr appears to be longer than 2:");
            return semantic::makeError();
        }
    }
    if (auto call = dynamic_cast<callExpr*>(expr); call) {
        auto path = dynamic_cast<pathExpr*>(call->callee_);
        if (path == nullptr) {
            error("callExpr callee is not a function");
            return semantic::makeError();
        }
        auto checkParams = [&](std::string callee, std::string owner) {
            if (owner != ""){
                if (table_.methods[owner].find(callee) == table_.methods[owner].end()) {
                    semType* ctype = semantic::makeStruct(owner);
                    if (callee != "clone" || !canClone(ctype)) {
                        error("can't resolve function name when calling a method of" + owner);
                        return semantic::makeError();
                    }
                    if (call->args_.size() != 1) {
                        error("callExpr callee implicitly clone args not exactly one");
                        return semantic::makeError();
                    }
                    expect(call->args_[0], semantic::makeRef(ctype, false));
                    return ctype;
                }
            }
            else {
                if (scope_.find(callee)) {
                    error("called function name is shadowed by local variables");
                    return semantic::makeError();
                }
                if (table_.funcSig.find(callee) == table_.funcSig.end()) {
                    error("callExpr attempted to use an non-existing function");
                    return semantic::makeError();
                }
            }
            auto &sig = owner == "" ? table_.funcSig[callee] : table_.methods[owner][callee];
            size_t offset = 0;
            if (sig.isMethod_) {
                if (owner.empty()) {
                    error("a method with a receiver has no owner type");
                    return semantic::makeError();
                }
                semType* rtype = semantic::makeStruct(owner);
                if (sig.selfRef_) {
                    rtype = semantic::makeRef(rtype, sig.selfMut_);
                }
                if (call->args_.size() != sig.params_.size() + 1) {
                    error("explicit method call has a wrong number of arguments");
                    return semantic::makeError();
                }
                expect(call->args_[0], rtype);
                offset = 1;
            }
            else if (call->args_.size() != sig.params_.size()) {
                error("callExpr parameters number mismatch");
                return semantic::makeError();
            }
            for (size_t i = 0; i < sig.params_.size(); i++) {
                expect(call->args_[offset + i], sig.params_[i]);
            }
            return sig.retType_;
        };
        if (path->path_.size() > 1) {
            if (path->path_.size() > 2) {
                error("pathExpr with size longer than 2");
                return semantic::makeError();
            }
            if (path->path_[1].types_.size() != 0) {
                error("turbofish appeared after function name");
                return semantic::makeError();
            }
            std::string owner = path->path_[0].name_, func = path->path_[1].name_;
            if (owner == "Self") {
                if (currentStruct_ == "") {
                    error("attempted to call \"Self\" in top-level pathExpr");
                    return semantic::makeError();
                }
                owner = currentStruct_;
            }
            if (owner == "Box" || owner == "Vec") {
                if (path->path_[0].types_.size() != 1) {
                    error(owner + " requires exactly one turbofish type argument");
                    return semantic::makeError();
                }
                semType* type = transformType(path->path_[0].types_[0]);
                if (owner == "Box") {
                    if (func == "new") {
                        if (call->args_.size() != 1) {
                            error("Box::new requires exactly one value argument");
                            return semantic::makeError();
                        }
                        expect(call->args_[0], type);
                        return semantic::makeBox(type);
                    }
                    if (func == "clone") {
                        if (call->args_.size() != 1) {
                            error("Box::clone requires exactly one reference argument");
                            return semantic::makeError();
                        }
                        if (!canClone(type)) {
                            error("Box::<T>::clone called while T can not clone");
                            return semantic::makeError();
                        }
                        expect(call->args_[0], semantic::makeRef(semantic::makeBox(type), false));
                        return semantic::makeBox(type);
                    }
                    error("unsupported associated function : " + owner + "::" + func);
                    return semantic::makeError();
                }
                // Vec
                if (func == "new") {
                    if (!call->args_.empty()) {
                        error("Vec::new requires no value arguments");
                        return semantic::makeError();
                    }
                    return semantic::makeVec(type);
                }
                if (func == "len" || func == "is_empty" || func == "clone") {
                    if (call->args_.size() != 1) {
                        error("Vec::" + func + " requires exactly one reference argument");
                        return semantic::makeError();
                    }
                    expect(call->args_[0], semantic::makeRef(semantic::makeVec(type), false));
                    if (func == "len") {
                        return semantic::makeBuiltin("usize");
                    }
                    if (func == "is_empty") {
                        return semantic::makeBuiltin("bool");
                    }
                    if (!canClone(type)) {
                        error("Vec::<T>::clone called while T can not clone");
                        return semantic::makeError();
                    }
                    return semantic::makeVec(type);
                }
                if (func == "push" || func == "remove") {
                    if (call->args_.size() != 2) {
                        error("Vec::" + func + " requires a mutable reference and one value argument");
                        return semantic::makeError();
                    }
                    expect(call->args_[0], semantic::makeRef(semantic::makeVec(type), true));
                    if (func == "push") {
                        expect(call->args_[1], type);
                        return semantic::makeUnit();
                    }
                    expect(call->args_[1], semantic::makeBuiltin("usize"));
                    return type;
                }
                error("unsupported associated function : " + owner + "::" + func);
                return semantic::makeError();
            }
            if (path->path_[0].types_.size() != 0) {
                error("using turbofish in non-Box and non-Vec function calling");
                return semantic::makeError();
            }
            return checkParams(func, owner);
        }
        if (!path->path_[0].types_.empty()) {
            error("callExpr single word path have types");
            return semantic::makeError();
        }
        std::string callee = path->path_[0].name_;
        return checkParams(callee, "");
    }
    if (auto met = dynamic_cast<methodCallExpr*>(expr); met) {
        if (met->hasGenericArgs_) {
            error("methodCallExpr should not has generic args");
            return semantic::makeError();
        }
        semType* lhs = infer(met->from_);
        if (lhs->kind_ == semType::kTypeKind::ERROR) {
            return semantic::makeError();
        }
        semType* callee = lhs;
        if (met->name_ == "clone") {
            if (met->args_.size() != 0) {
                error("methodCallExpr called clone with no args");
                return semantic::makeError();
            }
            // 候选链先从 lhs 逐层解引用；先在当前可达类型上找 clone，
            // 找不到再沿 Box/引用继续往内层找显式 clone。
            while (callee->kind_ == semType::kTypeKind::REF) {
                callee = callee->elem_;
            }
            bool explicitCloneable = (callee->kind_ == semType::kTypeKind::STRUCT && table_.methods[callee->name_].count("clone"));
            if (!explicitCloneable) {
                if (canClone(callee)) {
                    return callee;
                }
                semType* probe = lhs;
                while (probe->kind_ == semType::kTypeKind::REF || probe->kind_ == semType::kTypeKind::BOX) {
                    probe = probe->elem_;
                    if (probe->kind_ == semType::kTypeKind::STRUCT &&
                        table_.methods[probe->name_].count("clone")) {
                        explicitCloneable = true;
                        break;
                    }
                    if (canClone(probe)) {
                        return probe;
                    }
                }
                if (!explicitCloneable) {
                    if (lhs->kind_ == semType::kTypeKind::REF && !lhs->isMut_) {
                        return lhs;
                    }
                    error("methodCallExpr called clone implicitly but failed");
                    return semantic::makeError();
                }
            }
        }
        while (callee->kind_ == semType::kTypeKind::REF || callee->kind_ == semType::kTypeKind::BOX) {
            callee = callee->elem_;
        }
        if (callee->kind_ == semType::kTypeKind::VEC || callee->kind_ == semType::kTypeKind::ARRAY) {
            if (callee->kind_ == semType::kTypeKind::VEC) {
                if (met->name_ == "len" || met->name_ == "is_empty" || met->name_ == "clone") {
                    if (met->args_.size() != 0) {
                        error("vec." + met->name_ + " called with non-empty args");
                        return semantic::makeError();
                    }
                    if (met->name_ == "len") {
                        return semantic::makeBuiltin("usize");
                    }
                    else if(met->name_ == "is_empty") {
                        return semantic::makeBuiltin("bool");
                    }
                    else {
                        if (!canClone(callee->elem_)) {
                            error("Vec.clone called while the element type can not clone");
                            return semantic::makeError();
                        }
                        return callee;
                    }
                }
                else if (met->name_ == "push" || met->name_ == "remove") {
                    if (met->args_.size() != 1) {
                        error("vec." + met->name_ + " called with not exactly one arg");
                        return semantic::makeError();
                    }
                    if (!receiverMutable(met->from_)) {
                        error("vec." + met->name_ + " called by a non mutable expr");
                        return semantic::makeError();
                    }
                    if (met->name_ == "push") {
                        expect(met->args_[0], callee->elem_);
                        return semantic::makeUnit();
                    }
                    expect(met->args_[0], semantic::makeBuiltin("usize"));
                    return callee->elem_;
                }
                else {
                    error("vec." + met->name_ + "is not supported");
                    return semantic::makeError();
                }
            }
            // array
            if (met->name_ != "len" && met->name_ != "clone") {
                error("array. called a non-len and non-clone method");
                return semantic::makeError();
            }
            if (met->args_.size() != 0) {
                error("array." + met->name_ + " called with non-empty args");
                return semantic::makeError();
            }
            if (met->name_ == "clone") {
                if (!canClone(callee->elem_)) {
                    error("Array.clone called while the element type can not clone");
                    return semantic::makeError();
                }
                return callee;
            }
            return semantic::makeBuiltin("usize");
        }
        callee = lhs;
        while (callee->kind_ == semType::kTypeKind::REF || callee->kind_ == semType::kTypeKind::BOX) {
            callee = callee->elem_;
        }
        if (callee->kind_ != semType::kTypeKind::STRUCT) {
            error("methodCallExpr can't find appropriate receiver");
            return semantic::makeError();
        }
        std::string owner = callee->name_;
        auto dt = table_.methods.find(owner);
        if (dt == table_.methods.end() ||
            dt->second.find(met->name_) == dt->second.end()) {
            error("can't resolve function name when calling a method of " + owner);
            return semantic::makeError();
        }
        auto &sig = dt->second[met->name_];
        bool can = false;
        if (!sig.isMethod_) {
            can = false;
        }
        else if (!sig.selfRef_) {
            can = (lhs->kind_ != semType::kTypeKind::REF);
        }
        else if (!sig.selfMut_) {
            can = true;
        }
        else {
            can = receiverMutable(met->from_);
        }
        if (!can) {
            error("methodCallExpr receiver does not match the method");
            return semantic::makeError();
        }
        if (met->args_.size() != sig.params_.size()) {
            error("methodCallExpr arguments number mismatch");
            return semantic::makeError();
        }
        for (size_t i = 0; i < sig.params_.size(); i++) {
            expect(met->args_[i], sig.params_[i]);
        }
        return sig.retType_;
    }
    if (auto una = dynamic_cast<unaryExpr*>(expr); una) {
        semType* type = infer(una->operand_);
        if (type->kind_ == semType::kTypeKind::ERROR) {
            return semantic::makeError();
        }
        bool valid = true;
        eliminateOneShared(type, valid);
        if (!valid) {
            return semantic::makeError();
        }
        if (una->op_ == "-") { // I remember unaryExpr only have -x and !x?
            if (!isSignedType(type)) {
                error("unary minus attempted to negate an unsigned value");
                return semantic::makeError();
            }
            return type;
        }
        if (una->op_ == "!") {
            if (!isIntegerType(type) && !isBoolType(type)) {
                error("unary negate attempted to negate a non-integer and non-boolean value");
                return semantic::makeError();
            }
            return type;
        }
        error("unaryExpr with an operator neither - nor !");
        return semantic::makeError();
    }
    if (auto fi = dynamic_cast<ifExpr*>(expr); fi) {
        expect(fi->cond_, semantic::makeBuiltin("bool"));
        if (fi->else_ != nullptr) {
            return lub({infer(fi->then_), infer(fi->else_)});
        }
        else {
            if (!matches(infer(fi->then_), semantic::makeUnit())) {
                error("ifExpr then is not unit");
                return semantic::makeError();
            }
            return semantic::makeUnit();
        }
    }
    if (auto whi = dynamic_cast<whileExpr*>(expr); whi) {
        loops_.push_back(loopInfo{nullptr, {}, true});
        expect(whi->cond_, semantic::makeBuiltin("bool"));
        loops_.pop_back();
        loops_.push_back(loopInfo{semantic::makeUnit(), {}});
        checkBlock(whi->block_, semantic::makeUnit());
        loops_.pop_back();
        return semantic::makeUnit();
    }
    if (auto loop = dynamic_cast<loopExpr*>(expr); loop) {
        return checkLoop(loop, nullptr);
    }
    if (auto bre = dynamic_cast<breakExpr*>(expr); bre) {
        if (loops_.empty()) {
            error("break outside loop");
            return semantic::makeError();
        }
        if (loops_.back().barrier_) {
            error("attempted to break an outer loop in a while's condition");
            return semantic::makeError();
        }
        loopInfo &top = loops_.back();
        if (!bre->tail_ && top.expected_ && !matches(semantic::makeUnit(), top.expected_)) {
            error("break without a value in a loop which expects one");
        }
        semType* type;
        if (bre->tail_) {
            semType* actual = infer(bre->tail_);
            if (top.expected_ != nullptr) {
                expect(bre->tail_, top.expected_);
                type = isNever(actual) ? actual : top.expected_;
            }
            else {
                type = actual;
            }
        }
        else {
            type = semantic::makeUnit();
        }
        top.breaks_.push_back(type);
        return semantic::makeNever();
    }
    if (auto con = dynamic_cast<continueExpr*>(expr); con) {
        if (loops_.empty()) {
            error("continue outside a loop");
            return semantic::makeError();
        }
        if (loops_.back().barrier_) {
            error("attempted to continue an outer loop in a while's condition");
            return semantic::makeError();
        }
        return semantic::makeNever();
    }
    if (auto ret = dynamic_cast<returnExpr*>(expr); ret) {
        if (ret->tail_) {
            expect(ret->tail_, currentRet_);
        }
        else if (currentRet_ != nullptr && !matches(currentRet_, semantic::makeUnit())) {
            error("returned an unit in a function with a return type");
        }
        return semantic::makeNever();
    }
    if (auto cast = dynamic_cast<castExpr*>(expr); cast) {
        semType* ltype = infer(cast->from_);
        semType* rtype = transformType(cast->to_);
        if (ltype->kind_ == semType::kTypeKind::ERROR) {
            return rtype;
        }
        if ((!isIntegerType(ltype) && !isBoolType(ltype)) || 
             !isIntegerType(rtype)){
            error("castExpr lhs is neither int nor bool");
            return semantic::makeError();
        }
        return rtype;
    }
    if (auto der = dynamic_cast<derefExpr*>(expr); der) {
        semType* t = infer(der->operand_);
        if (t->kind_ == semType::kTypeKind::ERROR) {
            return semantic::makeError();
        }
        if (t->kind_ == semType::kTypeKind::BOX) {
            return t->elem_;
        }
        if (t->kind_ == semType::kTypeKind::REF) {
            return t->elem_;
        }
        else {
            error("attempted to deref a non-reference type");
            return semantic::makeError();
        }
    }
    if (auto ref = dynamic_cast<refExpr*>(expr); ref) {
        if (ref->isMut_ && isPlaceable(ref->operand_) && !isMutablePlaceable(ref->operand_)) {
            error("attempted to mutably borrow an immutable place");
        }
        return semantic::makeRef(infer(ref->operand_), ref->isMut_);
    }
    if (auto bin = dynamic_cast<binaryExpr*>(expr); bin) {
        auto lhs = bin->lhs_, rhs = bin->rhs_;
        std::string op = bin->op_;
        if (op == "=") {
            if (!isMutablePlaceable(lhs)) {
                error("assignment lhs is not a mutable lvalue");
                return semantic::makeError();
            }
            expect(rhs, infer(lhs));
            return semantic::makeUnit();
        }
        auto inside = [&](std::string str, std::vector<std::string> all) {
            for (auto now : all) {
                if (now == str){
                    return true;
                }
            }
            return false;
        };
        if (inside(op, {"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>", "<", "<=", ">", ">=", "==", "!=", "&&", "||"})) {
            semType* ltype = infer(lhs);
            if (ltype->kind_ == semType::kTypeKind::ERROR) {
                return semantic::makeError();
            }
            if (inside(op, {"+", "-", "*", "/", "%"})) {
                bool valid = true;
                eliminateOneShared(ltype, valid);
                semType* rtype = infer(rhs);
                eliminateOneShared(rtype, valid);
                if (!isIntegerType(ltype)) {
                    error("arith ltype is not an integer type");
                    valid = false;
                }
                if (!semantic::typeEqual(ltype, rtype)) {
                    error("arith lhs and rhs have different types");
                    return semantic::makeError();
                }
                return valid ? ltype : semantic::makeError();
            }
            if (inside(op, {"&", "|", "^"})) {
                bool valid = true;
                semType* rtype = infer(rhs);
                eliminateOneShared(ltype, valid);
                eliminateOneShared(rtype, valid);
                if (!isIntegerType(ltype) && !isBoolType(ltype)) {
                    error("bitwise lhs is neither an integer type nor a boolean type");
                    return semantic::makeError();
                }
                if (!semantic::typeEqual(ltype, rtype)) {
                    error("bitwise two sides have different types");
                    return semantic::makeError();
                }
                return ltype;
            }
            if (inside(op, {"<<", ">>"})) {
                bool valid = true;
                semType* rtype = infer(rhs);
                eliminateOneShared(ltype, valid);
                eliminateOneShared(rtype, valid);
                if (!isIntegerType(ltype)) {
                    error("shift lhs is not an integer type");
                    return semantic::makeError();
                }
                if (!isIntegerType(rtype)) {
                    error("shift rhs is not an integer type");
                    return semantic::makeError();
                }
                return ltype;
            }
            if (inside(op, {"<", "<=", ">", ">="})) {
                semType* rtype = infer(rhs);
                if (ltype->kind_ == semType::kTypeKind::ERROR || rtype->kind_ == semType::kTypeKind::ERROR) {
                    return semantic::makeError();
                }
                bool compatible = semantic::typeEqual(ltype, rtype) ||
                    (ltype->kind_ == semType::kTypeKind::REF &&
                     rtype->kind_ == semType::kTypeKind::REF &&
                     !ltype->isMut_ && rtype->isMut_ &&
                     semantic::typeEqual(ltype->elem_, rtype->elem_));
                if (!compatible) {
                    error("ordering operands have incompatible reference layers");
                    return semantic::makeError();
                }
                while (ltype->kind_ == semType::kTypeKind::REF) {
                    ltype = ltype->elem_;
                }
                while (rtype->kind_ == semType::kTypeKind::REF) {
                    rtype = rtype->elem_;
                }
                if (!isIntegerType(ltype) && !isBoolType(ltype)) {
                    error("arithequal lhs is neither an integer nor a boolean type");
                    return semantic::makeError();
                }
                if (!isIntegerType(rtype) && !isBoolType(rtype)) {
                    error("arithequal rhs is neither an integer nor a boolean type");
                    return semantic::makeError();
                }
                return semantic::makeBuiltin("bool");
            }
            if (inside(op, {"==", "!="})) {
                if (!semantic::typeEqual(ltype, infer(rhs))) {
                    error("(not)equal binaryExpr lhs and rhs have different types");
                    return semantic::makeError();
                }
                if (!canPartialEq(ltype)) {
                    error("(not)equal binaryExpr type can't partialEq");
                    return semantic::makeError();
                }
                return semantic::makeBuiltin("bool");
            }
            if (inside(op, {"&&", "||"})) {
                if (!semantic::typeEqual(ltype, infer(rhs))) {
                    error("and/or lhs and rhs have different types");
                    return semantic::makeError();
                }
                if (!isBoolType(ltype)) {
                    error("and/or lhs is not a boolean type");
                    return semantic::makeError();
                }
                return semantic::makeBuiltin("bool");
            }
        }
        if (inside(op, {"+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=", "^="})) {
            if (!isMutablePlaceable(lhs)) {
                error("compound lhs is not a mutable lvalue");
                return semantic::makeError();
            }
            semType* ltype = infer(lhs);
            semType* rtype = infer(rhs);
            bool valid = true;
            if (inside(op, {"+=", "-=", "*=", "/=", "%="})) {
                if (!isIntegerType(ltype)) {
                    error("arithequal lhs is not an integer type");
                    return semantic::makeError();
                }
                eliminateOneShared(rtype, valid);
                if (!semantic::typeEqual(ltype, rtype)) {
                    error("arithequal lhs and rhs have different types");
                    return semantic::makeError();
                }
                return semantic::makeUnit();
            }
            if (inside(op, {"&=", "|=", "^="})) {
                if (!isIntegerType(ltype) && !isBoolType(ltype)) {
                    error("bitwiseequal lhs is neither an integer nor a boolean type");
                    return semantic::makeError();
                }
                eliminateOneShared(rtype, valid);
                if (!semantic::typeEqual(ltype, rtype)) {
                    error("bitwiseequal lhs and rhs have different types");
                    return semantic::makeError();
                }
                return semantic::makeUnit();
            }
            if (inside(op, {"<<=", ">>="})) {
                if (!isIntegerType(ltype)) {
                    error("shiftequal lhs is not an integer type");
                    return semantic::makeError();
                }
                eliminateOneShared(rtype, valid);
                if (!isIntegerType(rtype)) {
                    error("shiftequal lhs and rhs have different types");
                    return semantic::makeError();
                }
                return semantic::makeUnit();
            }
        }
    }
    if (auto fie = dynamic_cast<fieldExpr*>(expr); fie) {
        semType* ftype = infer(fie->from_);
        while (ftype->kind_ == semType::kTypeKind::REF || ftype->kind_ == semType::kTypeKind::BOX) {
            ftype = ftype->elem_;
        }
        if (ftype->kind_ == semType::kTypeKind::ERROR) {
            return semantic::makeError();
        }
        if (ftype->kind_ != semType::kTypeKind::STRUCT) {
            error("attempted to use non-struct as a field");
            return semantic::makeError();
        }
        if (table_.structSig.find(ftype->name_) == table_.structSig.end()) {
            error("fieldExpr base name does not name a struct");
            return semantic::makeError();
        }
        auto fields = table_.structSig[ftype->name_];
        for (auto sig : fields.fields_) {
            if (sig.first == fie->name_) {
                return sig.second;
            }
        }
        error("target field does not have correspond variable name");
        return semantic::makeError();
    }
    if (auto init = dynamic_cast<structInitExpr*>(expr); init) {
        if (auto path = dynamic_cast<pathExpr*>(init->path_); path) {
            if (path->path_.size() != 1) {
                error("structInitExpr path is not a type name");
                return semantic::makeError();
            }
            if (path->path_[0].types_.size() != 0) {
                error("structInitExpr path should not have template types");
                return semantic::makeError();
            }
            std::string name = path->path_[0].name_;
            if (name == "Self") {
                if (currentStruct_.empty()) {
                    error("attempted to use \"Self\" in top-level structInitExpr");
                    return semantic::makeError();
                }
                name = currentStruct_;
            }
            if (table_.structSig.find(name) == table_.structSig.end()) {
                error("structInitExpr base name does not name a struct");
                return semantic::makeError();
            }
            auto sig = table_.structSig[name].fields_;
            auto isig = init->fields_; // AST, not allowed to reference it
            if (sig.size() != isig.size()) {
                error("structInitExpr structSig and structField have different size");
                return semantic::makeError();
            }
            std::sort(sig.begin(), sig.end(), [&](std::pair<std::string, semType*> now, std::pair<std::string, semType*> oth) {
                return now.first < oth.first;
            });
            std::sort(isig.begin(), isig.end(), [&](fieldInit now, fieldInit oth) {
                return now.name_ < oth.name_;
            });
            int size = sig.size();
            for (int i = 0; i < size; i++) {
                if(sig[i].first != isig[i].name_) {
                    error("structInitExpr structSig and structField name mismatch");
                    return semantic::makeError();
                }
                expect(isig[i].value_, sig[i].second);
            }
            return semantic::makeStruct(name);
        }
        else {
            error("structInitExpr path is not pathExpr");
            return semantic::makeError();
        }
    }
    if (auto arr = dynamic_cast<arrayExpr*>(expr); arr) {
        std::vector<semType*> types;
        if (arr->args_.size() == 0){
            error("arrayExpr args.size() = 0");
            return semantic::makeError();
        }
        for (auto now : arr->args_) {
            types.push_back(infer(now));
        }
        semType* ret = lub(types);
        if (ret->kind_ == semType::kTypeKind::ERROR) {
            return ret;
        }
        return semantic::makeArray(ret, arr->args_.size());
    }
    if (auto rarr = dynamic_cast<repeatedArrayExpr*>(expr); rarr) {
        semType* type = infer(rarr->value_);
        unsigned length = 0;
        if (!evalArrayLength(rarr->num_, currentStruct_, length)) {
            return semantic::makeError();
        }
        if (length > 1 && !canCopy(type)) {
            error("constructing a repeatedArray with length greater than one and a non-copyable element");
            return semantic::makeError();
        }
        return semantic::makeArray(type, length);
    }
    if (auto index = dynamic_cast<indexExpr*>(expr); index) {
        semType* type = infer(index->from_);
        while (type->kind_ == semType::kTypeKind::REF || type->kind_ == semType::kTypeKind::BOX) {
            type = type->elem_;
        }
        if (type->kind_ == semType::kTypeKind::ERROR) {
            return semantic::makeError();
        }
        if (type->kind_ != semType::kTypeKind::ARRAY && type->kind_ != semType::kTypeKind::VEC) {
            error("attempted to index a non-array type");
            return semantic::makeError();
        }
        expect(index->index_, semantic::makeBuiltin("usize"));
        return type->elem_;
    }
    return semantic::makeError();
}

semType* Checker::expect(Expr* expr, semType* wish) {
    if (wish == nullptr) {
        return infer(expr);
    }
    if (auto lit = dynamic_cast<intLitExpr*>(expr); lit) {
        std::string suf = intSuffix(lit->value_);
        if (suf.empty()) {
            if (isIntegerType(wish)) {
                return wish;
            }
            else {
                error("wish an unsuffixed integer to be a non-integer type");
                return semantic::makeError();
            }
        }
        else {
            semType* cur = semantic::makeBuiltin(suf);
            if (!semantic::typeEqual(cur, wish)) {
                error("wish an suffixed integer to be another type");
                return semantic::makeError();
            }
            return wish;
        }
    }
    if (auto block = dynamic_cast<blockExpr*>(expr); block) {
        return checkBlock(block, wish);
    }
    if (auto loop = dynamic_cast<loopExpr*>(expr); loop) {
        return checkLoop(loop, wish);
    }
    if (auto fi = dynamic_cast<ifExpr*>(expr); fi) {
        expect(fi->cond_, semantic::makeBuiltin("bool"));
        if (fi->else_ != nullptr){
            expect(fi->then_, wish);
            expect(fi->else_, wish);
            return wish;
        }
        else {
            expect(fi->then_, semantic::makeUnit());
            if(!matches(wish, semantic::makeUnit())) {
                error("if without else has a non-unit type");
                return semantic::makeError();
            }
            return semantic::makeUnit();
        }
    }
    if (auto arr = dynamic_cast<arrayExpr*>(expr); arr) {
        if (wish->kind_ == semType::kTypeKind::ARRAY) {
            if (arr->args_.size() != wish->arrayLength_) {
                error("array literal length mismatch");
                return semantic::makeError();
            }
            for (auto arg : arr->args_) {
                expect(arg, wish->elem_);
            }
            return wish;
        }
    }
    if (auto rarr = dynamic_cast<repeatedArrayExpr*>(expr); rarr) {
        if (wish->kind_ == semType::kTypeKind::ARRAY) {
            unsigned length = 0;
            if (!evalArrayLength(rarr->num_, currentStruct_, length)) {
                return semantic::makeError();
            }
            if (length != wish->arrayLength_) {
                error("array constexpr length mismatch");
                return semantic::makeError();
            }
            expect(rarr->value_, wish->elem_);
            if (length > 1 && !canCopy(wish->elem_)) { // Shall I do this ?
                error("constructing a repeatedArray with length greater than one and a non-copyable element");
                return semantic::makeError();
            }
            return wish;
        }
    }
    if (auto ind = dynamic_cast<indexExpr*>(expr); ind) {
        semType* btype = infer(ind->from_);
        if (btype->kind_ == semType::kTypeKind::VEC && !canCopy(btype->elem_) &&
            semantic::typeEqual(btype->elem_, wish) && !isMutablePlaceable(ind)) {
                error("attempted to move a non-copy value out of an immutable vector index");
                return semantic::makeError();
        }
        // Note that we do not solve this case, it goes to infer();
    }
    semType* type = infer(expr);
    if (!matches(type, wish) && !canImplicitlyTurn(type, wish)) {
        error("expect type mismatch");
        return semantic::makeError();
    }
    return wish;
}

void Checker::checkItem(Item* item){
    if (auto func= dynamic_cast<funcItem*>(item); func) {
        checkFunction(func, "");
    }
    else if (auto impl = dynamic_cast<ImplItem*>(item); impl) {
        if (impl->type_->kind_ != Type::kTypeKind::STRUCT) {
            return;
        }
        for (Item* down : impl->items_) {
            if (auto func = dynamic_cast<funcItem*>(down); func){
                checkFunction(func, impl->type_->name_);
            }
        }
    }
}

void Checker::checkFunction(funcItem* func, const std::string& owner) {
    std::string mem = currentStruct_;
    currentStruct_ = owner;
    funcSignature* sig = owner.empty() ? &table_.funcSig[func->name_] : &table_.methods[owner][func->name_];
    currentRet_ = sig->retType_; // Finally use this currentRet_ ! I can't remember the times where I type current and enter and I get currentRet instead of currentStruct_ .
    scope_.levelUp();
    for (auto para : func->parameter_) {
        scope_.push(para.name_, localShadowing(transformType(para.type_), para.isMut_));
    }
    checkBlock(static_cast<blockExpr*>(func->body_), currentRet_);
    scope_.levelDown();
    currentStruct_ = mem; // +1, what can I say
    return;
}

semType* Checker::checkBlock(blockExpr* block, semType* wish) {
    scope_.levelUp();
    bool reachable = true;
    semType* last = nullptr;
    for (auto stmt : block->stmts_) {
        last = checkStatement(stmt);
        if (isNever(last)) {
            reachable = false;
        }
    }
    semType* ret;
    if (block->tail_) {
        semType* tailType = expect(block->tail_, wish);
        ret = reachable ? tailType : semantic::makeNever();
    }
    else {
        ret = reachable ? semantic::makeUnit() : semantic::makeNever();
    }
    if (wish != nullptr && !matches(ret, wish)) {
        error("blockExpr's tail_ has a different semType with our wish");
        scope_.levelDown();
        return semantic::makeError();
    }
    scope_.levelDown();
    return ret;
}

semType* Checker::checkLoop(loopExpr* loop, semType* wish) {
    loops_.push_back(loopInfo{wish, {}});
    checkBlock(loop->block_, semantic::makeUnit());
    loopInfo info = loops_.back();
    loops_.pop_back();
    if (info.breaks_.empty()) {
        return semantic::makeNever();
    }
    bool allNever = true;
    for (auto bre : info.breaks_) {
        allNever &= (bre->kind_ == semType::kTypeKind::NEVER);
    }
    if (allNever) {
        return semantic::makeNever();
    }
    if (wish != nullptr) {
        return wish;
    }
    return lub(info.breaks_);
}

semType* Checker::checkStatement(Stmt* stmt) {
    if (auto let = dynamic_cast<letStmt*>(stmt); let) {
        semType* curType = let->type_ ? transformType(let->type_) : nullptr;
        semType* init = expect(let->init_, curType);
        scope_.push(let->name_, localShadowing(init, let->isMut_));
        return isNever(init) ? init : semantic::makeUnit();
    }
    if (auto expr = dynamic_cast<exprStmt*>(stmt); expr) {
        semType* type = infer(expr->expr_);
        if (!expr->hasSemi_ && !matches(type, semantic::makeUnit())) {
            error("non-tail block statement without semicolon attempted to be non-unit");
            return semantic::makeError();
        }
        return type;
    }
    return semantic::makeUnit();
}

bool Checker::canCopy(semType* type) {
    if (type->kind_ == semType::kTypeKind::BUILTIN || type->kind_ == semType::kTypeKind::UNIT) {
        return true;
    }
    if (type->kind_ == semType::kTypeKind::REF) {
        return !type->isMut_;
    }
    if (type->kind_ == semType::kTypeKind::ARRAY) {
        return canCopy(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::BOX) {
        return false;
    }
    if (type->kind_ == semType::kTypeKind::VEC) {
        return false;
    }
    if (type->kind_ == semType::kTypeKind::STRUCT) {
        std::string name = type->name_ + "Copy";
        if (state[name] == kLayoutAndDeriveState::UNTOUCHED) {
            state[name] = kLayoutAndDeriveState::CALCULATING;
        }
        else if (state[name] == kLayoutAndDeriveState::CALCULATING) {
            return false; // Only copy return false here
        }
        else if (state[name] == kLayoutAndDeriveState::YES || state[name] == kLayoutAndDeriveState::NO) {
            return state[name] == kLayoutAndDeriveState::YES;
        }
        else { // Won't happen in my imagination
            error("some strange things happened in derive traits checking");
            return false;
        }
        auto &sig = table_.structSig[type->name_];
        auto derives = sig.derives_;
        bool copyable = false, cloneable = false;
        for (std::string derive : derives) {
            if (derive == "Copy") {
                copyable = true;
            }
            if (derive == "Clone") {
                cloneable = true;
            }
        }
        if (!copyable || !cloneable) {
            state[name] = kLayoutAndDeriveState::NO;
            return false;
        }
        for (auto inner : sig.fields_) {
            if (!canCopy(inner.second)) {
                state[name] = kLayoutAndDeriveState::NO;
                return false;
            }
        }
        state[name] = kLayoutAndDeriveState::YES;
        return true;
    }
    return false;
}
bool Checker::canClone(semType* type) {
    if (type->kind_ == semType::kTypeKind::BUILTIN || type->kind_ == semType::kTypeKind::UNIT) {
        return true;
    }
    if (type->kind_ == semType::kTypeKind::REF) {
        return !type->isMut_;
    }
    if (type->kind_ == semType::kTypeKind::ARRAY) {
        return canClone(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::BOX) {
        return canClone(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::VEC) {
        return canClone(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::STRUCT) {
        std::string name = type->name_ + "Clone";
        if (state[name] == kLayoutAndDeriveState::UNTOUCHED) {
            state[name] = kLayoutAndDeriveState::CALCULATING;
        }
        else if (state[name] == kLayoutAndDeriveState::CALCULATING) {
            return true;
        }
        else if (state[name] == kLayoutAndDeriveState::YES || state[name] == kLayoutAndDeriveState::NO) {
            return state[name] == kLayoutAndDeriveState::YES;
        }
        else { // Won't happen in my imagination
            error("some strange things happened in derive traits checking");
            return false;
        }
        auto &sig = table_.structSig[type->name_];
        auto derives = sig.derives_;
        bool cloneable = false;
        for (std::string derive : derives) {
            if (derive == "Clone") {
                cloneable = true;
            }
        }
        if (!cloneable) {
            state[name] = kLayoutAndDeriveState::NO;
            return false;
        }
        for (auto inner : sig.fields_) {
            if (!canClone(inner.second)) {
                state[name] = kLayoutAndDeriveState::NO;
                return false;
            }
        }
        state[name] = kLayoutAndDeriveState::YES;
        return true;
    }
    return false;
}
bool Checker::canPartialEq(semType* type) {
    if (type->kind_ == semType::kTypeKind::BUILTIN || type->kind_ == semType::kTypeKind::UNIT) {
        return true;
    }
    if (type->kind_ == semType::kTypeKind::REF) {
        return canPartialEq(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::ARRAY) {
        return canPartialEq(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::BOX) {
        return canPartialEq(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::VEC) {
        return canPartialEq(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::STRUCT) {
        std::string name = type->name_ + "PartialEq";
        if (state[name] == kLayoutAndDeriveState::UNTOUCHED) {
            state[name] = kLayoutAndDeriveState::CALCULATING;
        }
        else if (state[name] == kLayoutAndDeriveState::CALCULATING) {
            return true;
        }
        else if (state[name] == kLayoutAndDeriveState::YES || state[name] == kLayoutAndDeriveState::NO) {
            return state[name] == kLayoutAndDeriveState::YES;
        }
        else { // Won't happen in my imagination
            error("some strange things happened in derive traits checking");
            return false;
        }
        auto &sig = table_.structSig[type->name_];
        auto derives = sig.derives_;
        bool partialeqable = false;
        for (std::string derive : derives) {
            if (derive == "PartialEq") {
                partialeqable = true;
            }
        }
        if (!partialeqable) {
            state[name] = kLayoutAndDeriveState::NO;
            return false;
        }
        for (auto inner : sig.fields_) {
            if (!canPartialEq(inner.second)) {
                state[name] = kLayoutAndDeriveState::NO;
                return false;
            }
        }
        state[name] = kLayoutAndDeriveState::YES;
        return true;
    }
    return false;
}
bool Checker::canEq(semType* type) {
    if (type->kind_ == semType::kTypeKind::BUILTIN || type->kind_ == semType::kTypeKind::UNIT) {
        return true;
    }
    if (type->kind_ == semType::kTypeKind::REF) {
        return canEq(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::ARRAY) {
        return canEq(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::BOX) {
        return canEq(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::VEC) {
        return canEq(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::STRUCT) {
        std::string name = type->name_ + "Eq";
        if (state[name] == kLayoutAndDeriveState::UNTOUCHED) {
            state[name] = kLayoutAndDeriveState::CALCULATING;
        }
        else if (state[name] == kLayoutAndDeriveState::CALCULATING) {
            return true;
        }
        else if (state[name] == kLayoutAndDeriveState::YES || state[name] == kLayoutAndDeriveState::NO) {
            return state[name] == kLayoutAndDeriveState::YES;
        }
        else { // Won't happen in my imagination
            error("some strange things happened in derive traits checking");
            return false;
        }
        auto &sig = table_.structSig[type->name_];
        auto derives = sig.derives_;
        bool eqable = false, partialeqable = false;
        for (std::string derive : derives) {
            if (derive == "Eq") {
                eqable = true;
            }
            if (derive == "PartialEq") {
                partialeqable = true;
            }
        }
        if (!eqable || !partialeqable) {
            state[name] = kLayoutAndDeriveState::NO;
            return false;
        }
        for (auto inner : sig.fields_) {
            if (!canEq(inner.second)) {
                state[name] = kLayoutAndDeriveState::NO;
                return false;
            }
        }
        state[name] = kLayoutAndDeriveState::YES;
        return true;
    }
    return false;
}

void Checker::checkLayoutLoop(std::string typeName) {
    if (reject_) {
        return;
    }
    if (table_.structSig.find(typeName) == table_.structSig.end()) {
        return;
    }
    auto &sig = table_.structSig[typeName];
    std::string name = typeName + "Layout"; // note that adding any suffix here is acceptable
        // as long as we use five different suffices for layout, copy, clone, partialeq and eq
    if (state[name] == kLayoutAndDeriveState::UNTOUCHED) {
        state[name] = kLayoutAndDeriveState::CALCULATING;
    }
    else if (state[name] == kLayoutAndDeriveState::CALCULATING) {
        error("layout forms a loop");
        return;
    }
    else if (state[name] == kLayoutAndDeriveState::LAYOUTDONE) {
        return;
    }
    for (auto down : sig.fields_) {
        while (down.second->kind_ == semType::kTypeKind::ARRAY) {
            down.second = down.second->elem_;
        }
        if (down.second->kind_ == semType::kTypeKind::STRUCT) {
            checkLayoutLoop(down.second->name_);
        }
    }
    state[name] = kLayoutAndDeriveState::LAYOUTDONE;
    return;
}

void Checker::checkLayout(Crate* crate) {
    for (Item* item : crate->items_) {
        if (auto stru = dynamic_cast<structItem*>(item); stru) {
            checkLayoutLoop(stru->name_);
        }
    }
    return;
}

void Checker::checkDerives(Crate* crate) {
    for (Item* item : crate->items_) {
        if (auto stru = dynamic_cast<structItem*>(item); stru) {
            std::set<std::string> apr;
            for (auto der : stru->derives_) {
                if (apr.find(der) != apr.end()) {
                    error("duplicate derive name : " + der);
                    return;
                }
                apr.insert(der);
            }
            bool hasCopy = apr.count("Copy"), hasClone = apr.count("Clone"), 
                 hasEq = apr.count("Eq"), hasPartialEq = apr.count("PartialEq");
            // we'll check copy without clone and eq without partialeq above
            semType* type = semantic::makeStruct(stru->name_);
            if (hasCopy && !canCopy(type)) {
                error("struct " + stru->name_ + " does not support Copy");
                return;
            }
            if (hasClone && !canClone(type)) {
                error("struct " + stru->name_ + " does not support Clone");
                return;
            }
            if (hasEq && !canEq(type)) {
                error("struct " + stru->name_ + " does not support Eq");
                return;
            }
            if (hasPartialEq && !canPartialEq(type)) {
                error("struct " + stru->name_ + " does not support PartialEq");
                return;
            }
        }
    }
}

void Checker::checkEntry() {
    auto dt = table_.funcSig.find("main");
    if (dt == table_.funcSig.end()) {
        error("main not found");
        return;
    }
    if (!dt->second.params_.empty()) {
        error("main should not have value parameters");
    }
    if (dt->second.retType_ == nullptr ||
        dt->second.retType_->kind_ != semType::kTypeKind::UNIT) {
        error("main should return unit");
    }
}

void Checker::error(std::string message) {
    std::cerr << "error occurred!\n" << message << std::endl;
    reject_ = true;
    return;
}

void Checker::checkCrate(Crate* crate) {
    collect(crate);
    if (reject_) {
        return;
    }
    evaluateConstants(crate);
    if (reject_) {
        return;  
    }
    evaluateTypes(crate);
    if (reject_) {
        return;
    } // stage 0 ended: register all functions, structs, constvalues, implements ; evaluated constvalues ; evaluated all types mentioned in former items

    checkEntry();
    if (reject_) {
        return;
    }
    checkLayout(crate); // This is stage final. Time really flies.
    if (reject_) {
        return;
    }
    checkDerives(crate);
    if (reject_) {
        return;
    }

    for (Item* item : crate->items_) {
        checkItem(item);
    }  
    return;
}