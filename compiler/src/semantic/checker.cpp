#include "checker.h"
#include <iostream>
#include <set>

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

semType* Checker::transformType(Type* type){
    if (type->kind_ == kKind::BUILTIN) {
        return semantic::makeBuiltin(type->name_);
    }
    if (type->kind_ == kKind::STRUCT) {
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
        ConstValue* c = type->arrayLength_;
        if (c->kind_ == ConstValue::kConstKind::INT) {
            std::string suffix = intSuffix(c->text_);
            if (!suffix.empty() && suffix != "usize") {
                error("attempted to explicitly use non-usize int literal as an array's length : " + c->text_);
                return semantic::makeError();
            }
        }
        else if (c->kind_ == ConstValue::kConstKind::PATH) {
            auto path = c->text_;
            auto fir = path.find("::");
            std::string owner, name;
            if (fir == std::string::npos) {
                owner = "";
                name = path;
            }
            else {
                if (path.find("::", fir + 1) != std::string::npos) {
                    error("unsupported path in ConstValue");
                    return semantic::makeError();
                }
                owner = path.substr(0, fir);
                name = path.substr(fir + 2);
                if (owner == "Self") {
                    if (currentStruct_.empty()) {
                        error("attempted to use \"Self\" in top-level scope");
                        return semantic::makeError();
                    }
                    owner = currentStruct_;
                }
            }
            constSignature* cs = findConst(owner, name);
            if (cs == nullptr) {
                error(owner.empty() ? "unresolved constant " + name
                                    : "unresolved constant " + owner + "::" + name);
                return semantic::makeError();
            }
            if (!typeEqual(cs->type_, semantic::makeBuiltin("usize"))) {
                error("array length must be usize");
                return semantic::makeError();
            }
        }
        else {
            error("array length must be usize");
            return semantic::makeError();
        }
        Constant len = calculateConst(type->arrayLength_, currentStruct_);
        return semantic::makeArray(transformType(type->elem_), len.value_);
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
        return isMutablePlaceable(field->from_);
    }
    if (auto index = dynamic_cast<indexExpr*>(expr); index) {
        return isMutablePlaceable(index->from_);
    }
    if (auto deref = dynamic_cast<derefExpr*>(expr); deref) {
        semType* inner = infer(deref->operand_);
        return inner->kind_ == semType::kTypeKind::REF && inner->isMut_;
    }
    return false;
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
            std::string name = path->path_[0].name_;
            return lookupValue(name);
        }
        else if (path->path_.size() == 2) {
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
        if (path->path_.size() > 1) {
            // This won't be dealt until stage 5
            return semantic::makeError();
        }
        if (!path->path_[0].types_.empty()) {
            error("callExpr single word path have types");
            return semantic::makeError();
        }
        std::string callee = path->path_[0].name_;
        if (scope_.find(callee)) {
            error("called function name is shadowed by local variables");
            return semantic::makeError();
        }
        if (table_.funcSig.find(callee) == table_.funcSig.end()) {
            error("callExpr attempted to use an non-existing function");
            return semantic::makeError();
        }
        auto &sig = table_.funcSig[callee];
        if (sig.params_.size() != call->args_.size()) {
            error("callExpr parameters number mismatch");
            return semantic::makeError();
        }
        for (int i = 0; i < sig.params_.size(); i++) {
            expect(call->args_[i], sig.params_[i]);
        }
        return sig.retType_;
    }
    if (auto una = dynamic_cast<unaryExpr*>(expr); una) {
        semType* type = infer(una->operand_);
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
            semType* rhs = infer(fi->else_);
            if (!semantic::typeEqual(infer(fi->then_), rhs)) {
                error("ifExpr then and else have different type");
                return semantic::makeError();
            }
            return rhs;
        }
        else {
            if (!semantic::typeEqual(infer(fi->then_), semantic::makeUnit())) {
                error("ifExpr then is not unit");
                return semantic::makeError();
            }
            return semantic::makeUnit();
        }
    }
    if (auto whi = dynamic_cast<whileExpr*>(expr); whi) {
        expect(whi->cond_, semantic::makeBuiltin("bool"));
        if (!semantic::typeEqual(infer(whi->block_), semantic::makeUnit())) {
            error("whileExpr block is not unit");
            return semantic::makeError();
        }
        return semantic::makeUnit();
    }
    if (auto loop = dynamic_cast<loopExpr*>(expr); loop) {
        if (!semantic::typeEqual(infer(loop->block_), semantic::makeUnit())) {
            error("loopExpr block is not unit");
            return semantic::makeError();
        }
        return semantic::makeUnit();
    }
    if (auto bre = dynamic_cast<breakExpr*>(expr); bre) {
        if (bre->tail_) {
            infer(bre->tail_);
        }
        return semantic::makeNever();
    }
    if (auto con = dynamic_cast<continueExpr*>(expr); con) {
        return semantic::makeNever();
    }
    if (auto ret = dynamic_cast<returnExpr*>(expr); ret) {
        if (ret->tail_) {
            expect(ret->tail_, currentRet_);
        }
        else if (currentRet_ != nullptr && !semantic::typeEqual(currentRet_, semantic::makeUnit())) {
            error("returned an unit in a function with a return type");
        }
        return semantic::makeNever();
    }
    if (auto cast = dynamic_cast<castExpr*>(expr); cast) {
        semType* ltype = infer(cast->from_);
        semType* rtype = transformType(cast->to_);
        if ((!isIntegerType(ltype) && !isBoolType(ltype)) || 
             !isIntegerType(rtype)){
            error("castExpr lhs is neither int nor bool");
            return semantic::makeError();
        }
        return rtype;
    }
    if (auto der = dynamic_cast<derefExpr*>(expr); der) {
        semType* t = infer(der->operand_);
        if (t->kind_ == semType::kTypeKind::REF) {
            return t->elem_;
        }
        else {
            error("attempted to deref a non-reference type");
            return semantic::makeError();
        }
    }
    if (auto ref = dynamic_cast<refExpr*>(expr); ref) {
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
            if (inside(op, {"+", "-", "*", "/", "%"})) {
                if (!isIntegerType(ltype)) {
                    error("arith lhs is not an integer type");
                    return semantic::makeError();
                }
                if (!semantic::typeEqual(ltype, digOneRef(infer(rhs)))) {
                    error("arith lhs and rhs have different types");
                    return semantic::makeError();
                }
                return ltype;
            }
            if (inside(op, {"&", "|", "^"})) {
                if (!isIntegerType(ltype) && !isBoolType(ltype)) {
                    error("bitwise lhs is neither an integer nor a boolean type");
                    return semantic::makeError();
                }
                if (!semantic::typeEqual(ltype, digOneRef(infer(rhs)))) {
                    error("bitwise lhs and rhs have different types");
                    return semantic::makeError();
                }
                return ltype;
            }
            if (inside(op, {"<<", ">>"})) {
                if (!isIntegerType(ltype)) {
                    error("shift lhs is neither an integer nor a boolean type");
                    return semantic::makeError();
                }
                if (!isIntegerType(digOneRef(infer(rhs)))) {
                    error("shift lhs and rhs have different types");
                    return semantic::makeError();
                }
                return ltype;
            }
            if (inside(op, {"<", "<=", ">", ">="})) {
                if (!semantic::typeEqual(ltype, digOneRef(infer(rhs)))) {
                    error("comparison lhs and rhs have different types");
                    return semantic::makeError();
                }
                if (!isIntegerType(ltype) && !isBoolType(ltype)) {
                    error("arithequal lhs is neither an integer nor a boolean type");
                    return semantic::makeError();
                }
                return semantic::makeBuiltin("bool");
            }
            if (inside(op, {"==", "!="})) {
                if (!semantic::typeEqual(ltype, infer(rhs))) {
                    error("(not)equal lhs and rhs have different types");
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
            if (inside(op, {"+=", "-=", "*=", "/=", "%="})) {
                if (!isIntegerType(ltype)) {
                    error("arithequal lhs is not an integer type");
                    return semantic::makeError();
                }
                if (!semantic::typeEqual(ltype, digOneRef(infer(rhs)))) {
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
                if (!semantic::typeEqual(ltype, digOneRef(infer(rhs)))) {
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
                if (!isIntegerType(digOneRef(infer(rhs)))) {
                    error("shiftequal lhs and rhs have different types");
                    return semantic::makeError();
                }
                return semantic::makeUnit();
            }
        }
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
    semType* type = infer(expr);
    if (!semantic::typeEqual(type, wish)) {
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
    for (auto stmt : block->stmts_) {
        checkStatement(stmt);
    }
    semType* ret = (block->tail_ ? expect(block->tail_, wish) : semantic::makeUnit()); // as mentioned in spec, our wish can inflitrate into block's tail
    if (wish != nullptr && !semantic::typeEqual(ret, wish)) {
        error("blockExpr's tail_ has a different semType with our wish");
        scope_.levelDown();
        return semantic::makeError();
    }
    scope_.levelDown();
    return ret;
}

void Checker::checkStatement(Stmt* stmt) {
    if (auto let = dynamic_cast<letStmt*>(stmt); let) {
        semType* curType = let->type_ ? transformType(let->type_) : nullptr;
        semType* init = expect(let->init_, curType);
        scope_.push(let->name_, localShadowing(init, let->isMut_));
    }
    if (auto expr = dynamic_cast<exprStmt*>(stmt); expr) {
        infer(expr->expr_);
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

    for (Item* item : crate->items_) {
        checkItem(item);
    }  
    return;
}