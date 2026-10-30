#include "generator.h"

#include <algorithm>
#include <stdexcept>

static std::string intLiteralFromString(std::string lit) {
    for (const std::string suf : {"usize", "isize", "u32", "i32"}) {
        if (lit.size() > suf.size() && lit.substr(lit.size() - suf.size()) == suf) {
            lit = lit.substr(0, lit.size() - suf.size());
            break;
        }
    }
    std::string literal;
    for (char ch : lit) {
        if (ch != '_') {
            literal += ch;
        }
    }
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
    long long value = std::stoll(literal, 0, base);
    return std::to_string(value);
}

static bool isUnsignedType(semType* type) {
    if (type == nullptr || type->kind_ != semantic::Type::kTypeKind::BUILTIN) {
        return false;
    }
    return type->name_ == "u32" || type->name_ == "usize";
}

static bool isBool(semType* type) {
    if (type == nullptr || type->kind_ != semantic::Type::kTypeKind::BUILTIN) {
        return false;
    }
    return type->name_ == "bool";
}

static bool isIntegerType(semType* type) {
    if (type == nullptr || type->kind_ != semantic::Type::kTypeKind::BUILTIN) {
        return false;
    }
    return type->name_ == "u32" || type->name_ == "usize" || type->name_ == "i32" || type->name_ == "isize" ;
}

static bool isUnit(semType* type) {
    return type != nullptr && type->kind_ == semType::kTypeKind::UNIT;
}

static bool isNever(semType* type) {
    return type != nullptr && type->kind_ == semType::kTypeKind::NEVER;
}

std::string IRGenerator::mangle(std::string owner, std::string name) {
    if (name == "main") {
        name = "rx_main";
    }
    return owner.empty() ? name : "__rx_m_" + owner + "_" + name;
}

std::string IRGenerator::transformType(semType* type) {
    if (type->kind_ == semType::kTypeKind::BUILTIN) {
        if (type->name_ == "i32" || type->name_ == "isize" || type->name_ == "u32" || type->name_ == "usize") {
            return "i32";
        }
        if (type->name_ == "bool") {
            return "i1";
        }
    }
    if (type->kind_ == semType::kTypeKind::UNIT) {
        return "void";
    }
    if (type->kind_ == semType::kTypeKind::REF) {
        return "ptr";
    }
    if (type->kind_ == semType::kTypeKind::BOX) {
        return "ptr";
    }
    if (type->kind_ == semType::kTypeKind::STRUCT) {
        return "%struct." + type->name_;
    }
    if (type->kind_ == semType::kTypeKind::ARRAY) {
        return "[" + std::to_string(type->arrayLength_) + " x " + transformType(type->elem_) + "]";
    }
    if (type->kind_ == semType::kTypeKind::VEC) {
        return "%Vec";
    }
    throw "not implemented";
}

IRValue IRGenerator::eliminateOneShared(IRValue value) {
    if (value.type_ == nullptr || value.type_->kind_ != semType::kTypeKind::REF) {
        return value;
    }
    std::string name = newLabel();
    putline(name + " = load " + transformType(value.type_->elem_) + ", ptr " + value.name_);
    return IRValue(name, value.type_->elem_);
}

IRValue IRGenerator::eliminateAllRefs(IRValue value) {
    while (value.type_ != nullptr && value.type_->kind_ == semType::kTypeKind::REF) {
        value = eliminateOneShared(value);
    }
    return value;
}

std::string IRGenerator::newLabel() {
    return "%t" + std::to_string(tempCounter_++);
}

void IRGenerator::putline(std::string line, bool direct) {
    if (buffering_ && !direct) {
        body_ += line + "\n";
    }
    else {
        out_ << line << "\n";
    }
}

void IRGenerator::allocate(std::string name, semType* type, bool hoist) {
    std::string line = "  " + name + " = alloca " + transformType(type);
    if (hoist) {
        entryAllocas_.push_back(line);
    }
    else {
        putline(line);
    }
}

void IRGenerator::callReturn(std::string name) {
    if (currentRetType_ == "void") {
        putline("  ret void");
    }
    else {
        putline("  ret " + currentRetType_ + " " + name);
    }
    ending_ = true;
    return;
}

void IRGenerator::enterLabel(std::string name) {
    if (name[0] == '%') {
        name = name.substr(1);
    }
    putline(name + ":");
    ending_ = false;
    return;
}

std::string IRGenerator::placeOrigin(Expr* expr) {
    if (auto path = dynamic_cast<pathExpr*>(expr); path) {
        if (path->path_.size() != 1) {
            throw "placeOrigin not implemented yet";
        }
        return *scope_.find(path->path_[0].name_);
    }
    if (auto field = dynamic_cast<fieldExpr*>(expr); field) {
        semType* type = checker_->exprType_[field->from_];
        while (type->kind_ == semType::kTypeKind::REF || type->kind_ == semType::kTypeKind::BOX) {
            type = type->elem_;
        }
        if (type->kind_ != semType::kTypeKind::STRUCT) {
            throw "field base is not a struct";
        }
        std::string base = addressOfObject(field->from_);
        int index = findIndex(type->name_, field->name_);
        std::string pos = newLabel();
        putline(pos + " = getelementptr inbounds %struct." + type->name_ + ", ptr " + base + ", i32 0, i32 " + std::to_string(index));
        return pos;
    }
    if (auto der = dynamic_cast<derefExpr*>(expr); der) {
        return generateExpr(der->operand_).name_;
    }
    if (auto ind = dynamic_cast<indexExpr*>(expr); ind) {
        semType* baseType = checker_->exprType_[ind->from_];
        while (baseType->kind_ == semType::kTypeKind::REF || baseType->kind_ == semType::kTypeKind::BOX) {
            baseType = baseType->elem_;
        }
        std::string base = addressOfObject(ind->from_);
        std::string index = generateExpr(ind->index_).name_;
        std::string ret = newLabel();
        if (baseType->kind_ == semType::kTypeKind::ARRAY) {
            putline(ret + " = getelementptr inbounds " + transformType(baseType) + ", ptr " + base + ", i32 0, i32 " + index);
            return ret;
        }
        if (baseType->kind_ == semType::kTypeKind::VEC) {
            std::string dataPtr = newLabel();
            putline(dataPtr + " = getelementptr inbounds %Vec, ptr " + base + ", i32 0, i32 0");
            std::string data = newLabel();
            putline(data + " = load ptr, ptr " + dataPtr);
            putline(ret + " = getelementptr inbounds " + transformType(baseType->elem_) + ", ptr " + data + ", i32 " + index);
            return ret;
        }
        throw "index base is not an array or vector";
    }
    throw "placeOrigin not implemented yet";
}

int IRGenerator::findIndex(std::string struName, std::string argName) {
    auto &decl = checker_->table_.structSig[struName].fields_;
    for (int j = 0; j < decl.size(); j++) {
        if (decl[j].first == argName) {
            return j;
        }
    }
    throw "findIndex not found";
}

static bool isPlaceExpr(Expr* expr) {
    return dynamic_cast<pathExpr*>(expr) != nullptr ||
           dynamic_cast<fieldExpr*>(expr) != nullptr ||
           dynamic_cast<indexExpr*>(expr) != nullptr ||
           dynamic_cast<derefExpr*>(expr) != nullptr;
}

std::string IRGenerator::addressOfObject(Expr* expr) {
    semType* type = checker_->exprType_[expr];
    if (type->kind_ == semType::kTypeKind::REF || type->kind_ == semType::kTypeKind::BOX) {
        std::string base = generateExpr(expr).name_;
        type = type->elem_;
        while (type->kind_ == semType::kTypeKind::REF || type->kind_ == semType::kTypeKind::BOX) {
            std::string loaded = newLabel();
            putline(loaded + " = load ptr, ptr " + base);
            base = loaded;
            type = type->elem_;
        }
        return base;
    }
    if (isPlaceExpr(expr)) {
        return placeOrigin(expr);
    }
    IRValue value = generateExpr(expr);
    if (ending_) {
        return "";
    }
    std::string slot = newLabel();
    allocate(slot, type);
    putline("  store " + transformType(type) + " " + value.name_ + ", ptr " + slot);
    return slot;
}

static int alignUp(int value, int align) {
    return (value + align - 1) / align * align;
}

int IRGenerator::alignOf(semType* type) {
    if (type->kind_ == semType::kTypeKind::BUILTIN) {
        if (type->name_ == "i32" || type->name_ == "u32" || type->name_ == "isize" || type->name_ == "usize") {
            return 4;
        }
        if (type->name_ == "bool") {
            return 1;
        }
    }
    if (type->kind_ == semType::kTypeKind::UNIT) {
        return 1;
    }
    if (type->kind_ == semType::kTypeKind::REF || type->kind_ == semType::kTypeKind::BOX) {
        return 4;
    }
    if (type->kind_ == semType::kTypeKind::VEC) {
        return 4;
    }
    if (type->kind_ == semType::kTypeKind::STRUCT) {
        auto &sig = checker_->table_.structSig[type->name_];
        int result = 1;
        for (auto field : sig.fields_) {
            result = std::max(result, alignOf(field.second));
        }
        return result;
    }
    if (type->kind_ == semType::kTypeKind::ARRAY) {
        return alignOf(type->elem_);
    }
    throw "alignOf not implemented";
}

int IRGenerator::sizeOf(semType* type) {
    if (type->kind_ == semType::kTypeKind::BUILTIN) {
        if (type->name_ == "i32" || type->name_ == "u32" || type->name_ == "isize" || type->name_ == "usize") {
            return 4;
        }
        if (type->name_ == "bool") {
            return 1;
        }
    }
    if (type->kind_ == semType::kTypeKind::UNIT) {
        return 0;
    }
    if (type->kind_ == semType::kTypeKind::REF || type->kind_ == semType::kTypeKind::BOX) {
        return 4;
    }
    if (type->kind_ == semType::kTypeKind::VEC) {
        return 12;
    }
    if (type->kind_ == semType::kTypeKind::STRUCT) {
        auto &sig = checker_->table_.structSig[type->name_];
        int offset = 0, result = 1;
        for (auto field : sig.fields_) {
            int align = alignOf(field.second);
            offset = alignUp(offset, align);
            offset += sizeOf(field.second);
            result = std::max(result, align);
        }
        return alignUp(offset, result);
    }
    if (type->kind_ == semType::kTypeKind::ARRAY) {
        return type->arrayLength_ * sizeOf(type->elem_);
    }
    throw "sizeOf not implemented";
}

IRValue IRGenerator::vecLen(std::string addr) {
    std::string lenPtr = newLabel();
    putline(lenPtr + " = getelementptr inbounds %Vec, ptr " + addr + ", i32 0, i32 1");
    std::string len = newLabel();
    putline(len + " = load i32, ptr " + lenPtr);
    return IRValue(len, semantic::makeBuiltin("usize"));
}

IRValue IRGenerator::vecIsEmpty(std::string addr) {
    IRValue len = vecLen(addr);
    std::string ret = newLabel();
    putline(ret + " = icmp eq i32 " + len.name_ + ", 0");
    return IRValue(ret, semantic::makeBuiltin("bool"));
}

void IRGenerator::vecPush(std::string addr, IRValue value, semType* elemType) {
    std::string dataPtr = newLabel(), lenPtr = newLabel(), capPtr = newLabel();
    putline(dataPtr + " = getelementptr inbounds %Vec, ptr " + addr + ", i32 0, i32 0");
    putline(lenPtr + " = getelementptr inbounds %Vec, ptr " + addr + ", i32 0, i32 1");
    putline(capPtr + " = getelementptr inbounds %Vec, ptr " + addr + ", i32 0, i32 2");

    std::string data = newLabel(), len = newLabel(), cap = newLabel();
    putline(data + " = load ptr, ptr " + dataPtr);
    putline(len + " = load i32, ptr " + lenPtr);
    putline(cap + " = load i32, ptr " + capPtr);

    std::string needGrow = newLabel(), growLabel = newLabel(), storeLabel = newLabel();
    putline(needGrow + " = icmp eq i32 " + len + ", " + cap);
    putline("  br i1 " + needGrow + ", label " + growLabel + ", label " + storeLabel);
    ending_ = true;

    enterLabel(growLabel);
    std::string isZero = newLabel(), twice = newLabel(), newCap = newLabel();
    putline(isZero + " = icmp eq i32 " + cap + ", 0");
    putline(twice + " = add i32 " + cap + ", " + cap);
    putline(newCap + " = select i1 " + isZero + ", i32 4, i32 " + twice);

    std::string elemSize = std::to_string(sizeOf(elemType));
    std::string sizeBytes = newLabel(), newData = newLabel();
    putline(sizeBytes + " = mul i32 " + newCap + ", " + elemSize);
    putline(newData + " = call ptr @malloc(i32 " + sizeBytes + ")");

    std::string oldBytes = newLabel();
    putline(oldBytes + " = mul i32 " + len + ", " + elemSize);
    putline("  call ptr @memcpy(ptr " + newData + ", ptr " + data + ", i32 " + oldBytes + ")");
    putline("  store ptr " + newData + ", ptr " + dataPtr);
    putline("  store i32 " + newCap + ", ptr " + capPtr);
    putline("  br label " + storeLabel);
    ending_ = true;

    enterLabel(storeLabel);
    std::string data2 = newLabel(), elemPtr = newLabel(), newLen = newLabel();
    putline(data2 + " = load ptr, ptr " + dataPtr);
    putline(elemPtr + " = getelementptr inbounds " + transformType(elemType) + ", ptr " + data2 + ", i32 " + len);
    putline("  store " + transformType(elemType) + " " + value.name_ + ", ptr " + elemPtr);
    putline(newLen + " = add i32 " + len + ", 1");
    putline("  store i32 " + newLen + ", ptr " + lenPtr);
}

IRValue IRGenerator::vecRemove(std::string addr, IRValue index, semType* elemType) {
    std::string dataPtr = newLabel(), lenPtr = newLabel();
    putline(dataPtr + " = getelementptr inbounds %Vec, ptr " + addr + ", i32 0, i32 0");
    putline(lenPtr + " = getelementptr inbounds %Vec, ptr " + addr + ", i32 0, i32 1");

    std::string data = newLabel(), len = newLabel();
    putline(data + " = load ptr, ptr " + dataPtr);
    putline(len + " = load i32, ptr " + lenPtr);

    std::string elemPtr = newLabel(), value = newLabel();
    putline(elemPtr + " = getelementptr inbounds " + transformType(elemType) + ", ptr " + data + ", i32 " + index.name_);
    putline(value + " = load " + transformType(elemType) + ", ptr " + elemPtr);

    std::string next = newLabel(), src = newLabel(), dst = newLabel();
    putline(next + " = add i32 " + index.name_ + ", 1");
    putline(src + " = getelementptr inbounds " + transformType(elemType) + ", ptr " + data + ", i32 " + next);
    putline(dst + " = getelementptr inbounds " + transformType(elemType) + ", ptr " + data + ", i32 " + index.name_);

    std::string count = newLabel(), tail = newLabel(), bytes = newLabel(), newLen = newLabel();
    putline(count + " = sub i32 " + len + ", " + index.name_);
    putline(tail + " = sub i32 " + count + ", 1");
    putline(bytes + " = mul i32 " + tail + ", " + std::to_string(sizeOf(elemType)));
    putline("  call ptr @memmove(ptr " + dst + ", ptr " + src + ", i32 " + bytes + ")");
    putline(newLen + " = sub i32 " + len + ", 1");
    putline("  store i32 " + newLen + ", ptr " + lenPtr);
    return IRValue(value, elemType);
}

std::string IRGenerator::cloneKey(semType* type) {
    if (type->kind_ == semType::kTypeKind::BUILTIN) {
        return type->name_;
    }
    if (type->kind_ == semType::kTypeKind::UNIT) {
        return "unit";
    }
    if (type->kind_ == semType::kTypeKind::REF) {
        return std::string(type->isMut_ ? "refmut_" : "ref_") + cloneKey(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::BOX) {
        return "box_" + cloneKey(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::VEC) {
        return "vec_" + cloneKey(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::ARRAY) {
        return "arr" + std::to_string(type->arrayLength_) + "_" + cloneKey(type->elem_);
    }
    if (type->kind_ == semType::kTypeKind::STRUCT) {
        return "struct_" + type->name_;
    }
    throw "cloneKey not implemented";
}

IRValue IRGenerator::callTypeClone(std::string addr, semType* type) {
    std::string key = cloneKey(type);
    if (cloneTypes_.find(key) == cloneTypes_.end()) {
        cloneTypes_[key] = type;
        cloneState_[key] = 0;
    }
    std::string ret = newLabel();
    putline(ret + " = call " + transformType(type) + " @__rx_clone_" + key + "(ptr " + addr + ")");
    return IRValue(ret, type);
}

IRValue IRGenerator::valueClone(std::string addr, semType* given) {
    if (given->kind_ == semType::kTypeKind::UNIT) {
        throw "cannot clone unit";
    }
    return callTypeClone(addr, given);
}

void IRGenerator::manifestCloneImpl(semType* type) {
    std::string key = cloneKey(type);
    if (cloneState_[key] == 1 || cloneState_[key] == 2) {
        return;
    }
    cloneState_[key] = 1;
    cloneTypes_[key] = type;

    bool savedBuffering = buffering_;
    bool savedEnding = ending_;
    std::string savedBody = body_;
    std::string savedRet = currentRetType_;
    std::string savedStruct = currentStruct_;
    std::vector<std::string> savedAllocas = entryAllocas_;

    buffering_ = true;
    ending_ = false;
    body_.clear();
    entryAllocas_.clear();

    std::string symbol = "__rx_clone_" + key;
    putline("define " + transformType(type) + " @" + symbol + "(ptr %src) {", true);

    IRValue result("", type);
    if (type->kind_ == semType::kTypeKind::BUILTIN || type->kind_ == semType::kTypeKind::REF) {
        std::string value = newLabel();
        putline(value + " = load " + transformType(type) + ", ptr %src");
        result = IRValue(value, type);
    }
    else if (type->kind_ == semType::kTypeKind::BOX) {
        std::string content = newLabel();
        putline(content + " = load ptr, ptr %src");
        IRValue inner = callTypeClone(content, type->elem_);
        std::string mem = newLabel();
        putline(mem + " = call ptr @malloc(i32 " + std::to_string(std::max(1, sizeOf(type->elem_))) + ")");
        putline("  store " + transformType(inner.type_) + " " + inner.name_ + ", ptr " + mem);
        result = IRValue(mem, type);
    }
    else if (type->kind_ == semType::kTypeKind::VEC) {
        std::string dataPtr = newLabel(), lenPtr = newLabel();
        putline(dataPtr + " = getelementptr inbounds %Vec, ptr %src, i32 0, i32 0");
        putline(lenPtr + " = getelementptr inbounds %Vec, ptr %src, i32 0, i32 1");
        std::string oldData = newLabel(), len = newLabel();
        putline(oldData + " = load ptr, ptr " + dataPtr);
        putline(len + " = load i32, ptr " + lenPtr);
        std::string bytes = newLabel(), newData = newLabel();
        putline(bytes + " = mul i32 " + len + ", " + std::to_string(sizeOf(type->elem_)));
        putline(newData + " = call ptr @malloc(i32 " + bytes + ")");

        std::string indexSlot = newLabel();
        allocate(indexSlot, semantic::makeBuiltin("i32"));
        putline("  store i32 0, ptr " + indexSlot);

        std::string loop = newLabel(), body = newLabel(), done = newLabel();
        putline("  br label " + loop);
        ending_ = true;
        enterLabel(loop);
        std::string index = newLabel(), cond = newLabel();
        putline(index + " = load i32, ptr " + indexSlot);
        putline(cond + " = icmp slt i32 " + index + ", " + len);
        putline("  br i1 " + cond + ", label " + body + ", label " + done);
        ending_ = true;
        enterLabel(body);
        std::string src = newLabel(), dst = newLabel();
        putline(src + " = getelementptr inbounds " + transformType(type->elem_) + ", ptr " + oldData + ", i32 " + index);
        IRValue item = callTypeClone(src, type->elem_);
        putline(dst + " = getelementptr inbounds " + transformType(type->elem_) + ", ptr " + newData + ", i32 " + index);
        putline("  store " + transformType(item.type_) + " " + item.name_ + ", ptr " + dst);
        std::string next = newLabel();
        putline(next + " = add i32 " + index + ", 1");
        putline("  store i32 " + next + ", ptr " + indexSlot);
        putline("  br label " + loop);
        ending_ = true;
        enterLabel(done);

        std::string newVecSlot = newLabel();
        allocate(newVecSlot, type);
        std::string newDataPtr = newLabel(), newLenPtr = newLabel(), newCapPtr = newLabel();
        putline(newDataPtr + " = getelementptr inbounds %Vec, ptr " + newVecSlot + ", i32 0, i32 0");
        putline(newLenPtr + " = getelementptr inbounds %Vec, ptr " + newVecSlot + ", i32 0, i32 1");
        putline(newCapPtr + " = getelementptr inbounds %Vec, ptr " + newVecSlot + ", i32 0, i32 2");
        putline("  store ptr " + newData + ", ptr " + newDataPtr);
        putline("  store i32 " + len + ", ptr " + newLenPtr);
        putline("  store i32 " + len + ", ptr " + newCapPtr);
        std::string value = newLabel();
        putline(value + " = load %Vec, ptr " + newVecSlot);
        result = IRValue(value, type);
    }
    else if (type->kind_ == semType::kTypeKind::STRUCT) {
        std::string slot = newLabel();
        allocate(slot, type);
        auto &fields = checker_->table_.structSig[type->name_].fields_;
        for (size_t i = 0; i < fields.size(); i++) {
            std::string srcField = newLabel(), dstField = newLabel();
            putline(srcField + " = getelementptr inbounds %struct." + type->name_ + ", ptr %src, i32 0, i32 " + std::to_string(i));
            IRValue field = callTypeClone(srcField, fields[i].second);
            putline(dstField + " = getelementptr inbounds %struct." + type->name_ + ", ptr " + slot + ", i32 0, i32 " + std::to_string(i));
            putline("  store " + transformType(field.type_) + " " + field.name_ + ", ptr " + dstField);
        }
        std::string value = newLabel();
        putline(value + " = load %struct." + type->name_ + ", ptr " + slot);
        result = IRValue(value, type);
    }
    else if (type->kind_ == semType::kTypeKind::ARRAY) {
        std::string slot = newLabel();
        allocate(slot, type);
        for (unsigned i = 0; i < type->arrayLength_; i++) {
            std::string srcElem = newLabel(), dstElem = newLabel();
            putline(srcElem + " = getelementptr inbounds " + transformType(type) + ", ptr %src, i32 0, i32 " + std::to_string(i));
            IRValue elem = callTypeClone(srcElem, type->elem_);
            putline(dstElem + " = getelementptr inbounds " + transformType(type) + ", ptr " + slot + ", i32 0, i32 " + std::to_string(i));
            putline("  store " + transformType(elem.type_) + " " + elem.name_ + ", ptr " + dstElem);
        }
        std::string value = newLabel();
        putline(value + " = load " + transformType(type) + ", ptr " + slot);
        result = IRValue(value, type);
    }
    else {
        throw "clone type not implemented";
    }

    putline("  ret " + transformType(type) + " " + result.name_);
    buffering_ = false;
    putline("entry:", true);
    for (const std::string& line : entryAllocas_) {
        putline(line, true);
    }
    out_ << body_;
    putline("}", true);

    buffering_ = savedBuffering;
    ending_ = savedEnding;
    body_ = savedBody;
    currentRetType_ = savedRet;
    currentStruct_ = savedStruct;
    entryAllocas_ = savedAllocas;
    cloneState_[key] = 2;
}

IRValue IRGenerator::callTypeEq(std::string a, std::string b, semType* type) {
    std::string key = cloneKey(type);
    if (eqTypes_.find(key) == eqTypes_.end()) {
        eqTypes_[key] = type;
        eqState_[key] = 0;
    }
    std::string ret = newLabel();
    putline(ret + " = call i1 @__rx_eq_" + key + "(ptr " + a + ", ptr " + b + ")");
    return IRValue(ret, semantic::makeBuiltin("bool"));
}

void IRGenerator::manifestEqImpl(semType* type) {
    std::string key = cloneKey(type);
    if (eqState_[key] == 1 || eqState_[key] == 2) {
        return;
    }
    eqState_[key] = 1;
    eqTypes_[key] = type;

    bool savedBuffering = buffering_;
    bool savedEnding = ending_;
    std::string savedBody = body_;
    std::string savedRet = currentRetType_;
    std::string savedStruct = currentStruct_;
    std::vector<std::string> savedAllocas = entryAllocas_;

    buffering_ = true;
    ending_ = false;
    body_.clear();
    entryAllocas_.clear();

    std::string symbol = "__rx_eq_" + key;
    putline("define i1 @" + symbol + "(ptr %a, ptr %b) {", true);

    semType* boolType = semantic::makeBuiltin("bool");
    IRValue result("true", boolType);

    if (type->kind_ == semType::kTypeKind::UNIT) {
        result = IRValue("true", boolType);
    }
    else if (type->kind_ == semType::kTypeKind::BUILTIN) {
        std::string av = newLabel(), bv = newLabel(), r = newLabel();
        putline(av + " = load " + transformType(type) + ", ptr %a");
        putline(bv + " = load " + transformType(type) + ", ptr %b");
        putline(r + " = icmp eq " + transformType(type) + " " + av + ", " + bv);
        result = IRValue(r, boolType);
    }
    else if (type->kind_ == semType::kTypeKind::REF) {
        std::string ap = newLabel(), bp = newLabel();
        putline(ap + " = load ptr, ptr %a");
        putline(bp + " = load ptr, ptr %b");
        result = callTypeEq(ap, bp, type->elem_);
    }
    else if (type->kind_ == semType::kTypeKind::BOX) {
        std::string ap = newLabel(), bp = newLabel();
        putline(ap + " = load ptr, ptr %a");
        putline(bp + " = load ptr, ptr %b");
        result = callTypeEq(ap, bp, type->elem_);
    }
    else if (type->kind_ == semType::kTypeKind::VEC) {
        std::string alenPtr = newLabel(), blenPtr = newLabel();
        putline(alenPtr + " = getelementptr inbounds %Vec, ptr %a, i32 0, i32 1");
        putline(blenPtr + " = getelementptr inbounds %Vec, ptr %b, i32 0, i32 1");
        std::string alen = newLabel(), blen = newLabel(), sameLen = newLabel();
        putline(alen + " = load i32, ptr " + alenPtr);
        putline(blen + " = load i32, ptr " + blenPtr);
        putline(sameLen + " = icmp eq i32 " + alen + ", " + blen);

        std::string resSlot = newLabel();
        allocate(resSlot, boolType);
        putline("  store i1 true, ptr " + resSlot);

        std::string loop = newLabel(), body = newLabel(), mismatch = newLabel(), done = newLabel();
        putline("  br i1 " + sameLen + ", label " + loop + ", label " + mismatch);
        ending_ = true;

        enterLabel(mismatch);
        putline("  store i1 false, ptr " + resSlot);
        putline("  br label " + done);
        ending_ = true;

        std::string indexSlot = newLabel();
        enterLabel(loop);
        allocate(indexSlot, semantic::makeBuiltin("i32"));
        putline("  store i32 0, ptr " + indexSlot);
        std::string loopHead = newLabel();
        putline("  br label " + loopHead);
        ending_ = true;

        enterLabel(loopHead);
        std::string index = newLabel(), cond = newLabel();
        putline(index + " = load i32, ptr " + indexSlot);
        putline(cond + " = icmp slt i32 " + index + ", " + alen);
        putline("  br i1 " + cond + ", label " + body + ", label " + done);
        ending_ = true;

        enterLabel(body);
        std::string adataPtr = newLabel(), bdataPtr = newLabel(), adata = newLabel(), bdata = newLabel();
        putline(adataPtr + " = getelementptr inbounds %Vec, ptr %a, i32 0, i32 0");
        putline(bdataPtr + " = getelementptr inbounds %Vec, ptr %b, i32 0, i32 0");
        putline(adata + " = load ptr, ptr " + adataPtr);
        putline(bdata + " = load ptr, ptr " + bdataPtr);
        std::string ae = newLabel(), be = newLabel();
        putline(ae + " = getelementptr inbounds " + transformType(type->elem_) + ", ptr " + adata + ", i32 " + index);
        putline(be + " = getelementptr inbounds " + transformType(type->elem_) + ", ptr " + bdata + ", i32 " + index);
        IRValue elemEq = callTypeEq(ae, be, type->elem_);
        std::string prev = newLabel(), combined = newLabel(), next = newLabel();
        putline(prev + " = load i1, ptr " + resSlot);
        putline(combined + " = and i1 " + prev + ", " + elemEq.name_);
        putline("  store i1 " + combined + ", ptr " + resSlot);
        putline(next + " = add i32 " + index + ", 1");
        putline("  store i32 " + next + ", ptr " + indexSlot);
        putline("  br label " + loopHead);
        ending_ = true;

        enterLabel(done);
        std::string final = newLabel();
        putline(final + " = load i1, ptr " + resSlot);
        result = IRValue(final, boolType);
    }
    else if (type->kind_ == semType::kTypeKind::STRUCT) {
        auto& fields = checker_->table_.structSig[type->name_].fields_;
        for (size_t i = 0; i < fields.size(); i++) {
            std::string af = newLabel(), bf = newLabel();
            putline(af + " = getelementptr inbounds %struct." + type->name_ + ", ptr %a, i32 0, i32 " + std::to_string(i));
            putline(bf + " = getelementptr inbounds %struct." + type->name_ + ", ptr %b, i32 0, i32 " + std::to_string(i));
            IRValue fieldEq = callTypeEq(af, bf, fields[i].second);
            std::string combined = newLabel();
            putline(combined + " = and i1 " + result.name_ + ", " + fieldEq.name_);
            result = IRValue(combined, boolType);
        }
    }
    else if (type->kind_ == semType::kTypeKind::ARRAY) {
        for (unsigned i = 0; i < type->arrayLength_; i++) {
            std::string ae = newLabel(), be = newLabel();
            putline(ae + " = getelementptr inbounds " + transformType(type) + ", ptr %a, i32 0, i32 " + std::to_string(i));
            putline(be + " = getelementptr inbounds " + transformType(type) + ", ptr %b, i32 0, i32 " + std::to_string(i));
            IRValue elemEq = callTypeEq(ae, be, type->elem_);
            std::string combined = newLabel();
            putline(combined + " = and i1 " + result.name_ + ", " + elemEq.name_);
            result = IRValue(combined, boolType);
        }
    }
    else {
        throw "eq type not implemented";
    }

    putline("  ret i1 " + result.name_);
    buffering_ = false;
    putline("entry:", true);
    for (const std::string& line : entryAllocas_) {
        putline(line, true);
    }
    out_ << body_;
    putline("}", true);

    buffering_ = savedBuffering;
    ending_ = savedEnding;
    body_ = savedBody;
    currentRetType_ = savedRet;
    currentStruct_ = savedStruct;
    entryAllocas_ = savedAllocas;
    eqState_[key] = 2;
}

IRValue IRGenerator::generateExpr(Expr* expr) {
    auto dt = checker_->exprType_.find(expr);
    semType* type = (dt == checker_->exprType_.end() ? nullptr : dt->second);
    if (auto lit = dynamic_cast<intLitExpr*>(expr); lit) {
        return IRValue(intLiteralFromString(lit->value_), type == nullptr ? semantic::makeBuiltin("i32") : type);
    }
    if (auto lit = dynamic_cast<boolLitExpr*>(expr); lit) {
        return IRValue(lit->value_ ? "true" : "false", type == nullptr ? semantic::makeBuiltin("bool") : type);
    }
    if (auto uni = dynamic_cast<unitExpr*>(expr); uni) {
        return IRValue("", type == nullptr ? semantic::makeUnit() : type);
    }
    if (auto path = dynamic_cast<pathExpr*>(expr); path) {
        if (isUnit(type)) {
            return IRValue("", type);
        }
        std::string goal = "";
        if (path->path_.size() == 1) {
            std::string* local = scope_.find(path->path_[0].name_);
            if (local == nullptr) {
                if (!checker_->table_.constSig.count(path->path_[0].name_)) {
                    throw "can't resolve pathExpr"; // should not happen
                }
                auto &sig = checker_->table_.constSig[path->path_[0].name_];
                Constant con = sig.real_;
                if (con.isBool_) {
                    return IRValue(con.value_ ? "true" : "false", type);
                }
                else {
                    return IRValue(std::to_string(con.value_), type);
                }
            }
            else {
                goal = *local;
            }
        }
        else {
            std::string owner = path->path_[0].name_, callee = path->path_[1].name_;
            if (owner == "Self") {
                owner = currentStruct_;
            }
            auto &sig = checker_->table_.associatedConsts[owner][callee];
            Constant con = sig.real_;
            if (con.isBool_) {
                return IRValue(con.value_ ? "true" : "false", type);
            }
            else {
                return IRValue(std::to_string(con.value_), type);
            }
        }
        std::string name = newLabel();
        putline(name + " = load " + transformType(type) + ", ptr " + goal);
        return IRValue(name, type);
    }
    if (auto bin = dynamic_cast<binaryExpr*>(expr); bin) {
        if (bin->op_ == "=") {
            IRValue rhs = generateExpr(bin->rhs_);
            if (ending_) {
                return IRValue("", semantic::makeNever());
            }
            if (isUnit(rhs.type_)) {
                return IRValue("", semantic::makeUnit());
            }
            std::string lplace = placeOrigin(bin->lhs_);
            std::string ltype = transformType(checker_->exprType_[bin->lhs_]);
            putline("store " + ltype + " " + rhs.name_ + ", ptr " + lplace);
            return IRValue("", semantic::makeUnit());
        }
        else if (bin->op_ == "+=" || bin->op_ == "-=" || bin->op_ == "*=" || 
                 bin->op_ == "/=" || bin->op_ == "%=" || bin->op_ == "&=" || 
                 bin->op_ == "|=" || bin->op_ == "^=" || bin->op_ == "<<=" || bin->op_ == ">>=") {
            IRValue rhs = generateExpr(bin->rhs_);
            if (ending_) {
                return IRValue("", semantic::makeNever());
            }
            rhs = eliminateOneShared(rhs);
            semType* ltype = checker_->exprType_[bin->lhs_];
            std::string IRltype = transformType(ltype);
            std::string lplace = placeOrigin(bin->lhs_);

            std::string mem = newLabel(), got = newLabel();
            putline(mem + " = load " + IRltype + ", ptr " + lplace);

            if (bin->op_ == "<<=" || bin->op_ == ">>=") {
                std::string amount = newLabel();
                putline(amount + " = and i32 " + rhs.name_ + ", 31");
                if (bin->op_ == "<<=") {
                    putline(got + " = shl " + IRltype + " " + mem + ", " + amount);
                }
                else if (isUnsignedType(ltype)) {
                    putline(got + " = lshr " + IRltype + " " + mem + ", " + amount);
                }
                else {
                    putline(got + " = ashr " + IRltype + " " + mem + ", " + amount);
                }
            }
            else {
                std::string op;
                if (bin->op_ == "+=") op = "add";
                else if (bin->op_ == "-=") op = "sub";
                else if (bin->op_ == "*=") op = "mul";
                else if (bin->op_ == "/=") op = isUnsignedType(ltype) ? "udiv" : "sdiv";
                else if (bin->op_ == "%=") op = isUnsignedType(ltype) ? "urem" : "srem";
                else if (bin->op_ == "&=") op = "and";
                else if (bin->op_ == "|=") op = "or";
                else if (bin->op_ == "^=") op = "xor";
                putline(got + " = " + op + " " + IRltype + " " + mem + ", " + rhs.name_);
            }
            putline("store " + IRltype + " " + got + ", ptr " + lplace);
            return IRValue("", semantic::makeUnit());
        }
        if (bin->op_ == "||" || bin->op_ == "&&") {
            std::string slot = newLabel(), second = newLabel(), end = newLabel();
            allocate(slot, semantic::makeBuiltin("bool"));
            std::string start = ((bin->op_ == "||") ? "0" : "1");
            putline("store i1 " + start + ", ptr " + slot);
            IRValue fir = generateExpr(bin->lhs_);
            if (!ending_) {
                putline("store i1 " + fir.name_ + ", ptr " + slot);
                if (bin->op_ == "||") {
                    putline("br i1 " + fir.name_ + ", label " + end + ", label " + second), ending_ = true;
                }
                else {
                    putline("br i1 " + fir.name_ + ", label " + second + ", label " + end), ending_ = true;
                }
            }
            else {
                return IRValue("", semantic::makeUnit());
            }
            enterLabel(second); {
                IRValue sec = generateExpr(bin->rhs_);
                putline("  store i1 " + sec.name_ + ", ptr " + slot);
                putline("  br label " + end), ending_ = true;
            }
            enterLabel(end);
            std::string name = newLabel();
            putline(name + " = load i1, ptr " + slot);
            return IRValue(name, semantic::makeBuiltin("bool"));
        }
        IRValue lhs = generateExpr(bin->lhs_);
        if (ending_) {
            return IRValue{"", semantic::makeNever()};
        }
        IRValue rhs = generateExpr(bin->rhs_);
        if (ending_) {
            return IRValue{"", semantic::makeNever()};
        }
        if (bin->op_ == "<" || bin->op_ == "<=" || bin->op_ == ">" ||
            bin->op_ == ">=" || bin->op_ == "==" || bin->op_ == "!=") {
            lhs = eliminateAllRefs(lhs);
            rhs = eliminateAllRefs(rhs);
        }
        else {
            lhs = eliminateOneShared(lhs);
            rhs = eliminateOneShared(rhs);
        }
        if (bin->op_ == "==" || bin->op_ == "!=") {
            if (lhs.type_->kind_ == semType::kTypeKind::UNIT) {
                return IRValue(bin->op_ == "==" ? "true" : "false", semantic::makeBuiltin("bool"));
            }
            if (lhs.type_->kind_ == semType::kTypeKind::STRUCT ||
                lhs.type_->kind_ == semType::kTypeKind::ARRAY ||
                lhs.type_->kind_ == semType::kTypeKind::VEC ||
                lhs.type_->kind_ == semType::kTypeKind::BOX) {
                auto materialize = [&](IRValue value) {
                    std::string slot = newLabel();
                    allocate(slot, value.type_);
                    putline("  store " + transformType(value.type_) + " " + value.name_ + ", ptr " + slot);
                    return slot;
                };
                IRValue eq = callTypeEq(materialize(lhs), materialize(rhs), lhs.type_);
                if (bin->op_ == "==") {
                    return eq;
                }
                std::string neg = newLabel();
                putline(neg + " = xor i1 " + eq.name_ + ", true");
                return IRValue(neg, semantic::makeBuiltin("bool"));
            }
        }
        semType* retType = (type == nullptr ? lhs.type_ : type);
        std::string retName = transformType(retType);
        std::string op;
        if (bin->op_ == "+") op = "add";
        else if (bin->op_ == "-") op = "sub";
        else if (bin->op_ == "*") op = "mul";
        else if (bin->op_ == "/") op = isUnsignedType(retType) ? "udiv" : "sdiv";
        else if (bin->op_ == "%") op = isUnsignedType(retType) ? "urem" : "srem";
        else if (bin->op_ == "&") op = "and";
        else if (bin->op_ == "|") op = "or";
        else if (bin->op_ == "^") op = "xor";
        else { // not arith
            std::string comPref = "icmp ", opPref = "";
            if (bin->op_ == "==") op = "eq";
            else if (bin->op_ == "!=") op = "ne";
            else { // not arith or != ==
                opPref = (isUnsignedType(lhs.type_) || isBool(lhs.type_)) ? "u" : "s";
                if (bin->op_ == "<") op = "lt";
                else if (bin->op_ == "<=") op = "le";
                else if (bin->op_ == ">") op = "gt";
                else if (bin->op_ == ">=") op = "ge";
                else {
                    if (bin->op_ == "<<") {
                        std::string lower = newLabel();
                        putline(lower + " = and i32 " + rhs.name_ + ", 31");
                        std::string name = newLabel();
                        putline(name + " = shl " + transformType(lhs.type_) + " " + lhs.name_ + ", " + lower);
                        return IRValue(name, retType);
                    }
                    else if (bin->op_ == ">>") {
                        std::string lower = newLabel();
                        putline(lower + " = and i32 " + rhs.name_ + ", 31");
                        std::string name = newLabel();
                        if (isUnsignedType(lhs.type_)) {
                            putline(name + " = lshr " + transformType(lhs.type_) + " " + lhs.name_ + ", " + lower);
                        }
                        else {
                            putline(name + " = ashr " + transformType(lhs.type_) + " " + lhs.name_ + ", " + lower);
                        }
                        return IRValue(name, retType);
                    }
                    else {
                        throw "binaryExpr unexpected operators";
                    }
                }
            }
            std::string name = newLabel();
            putline(name + " = " + comPref + opPref + op + " " + transformType(lhs.type_) + " " + lhs.name_ + ", " + rhs.name_);
            return IRValue(name, retType);
        }
        std::string name = newLabel();
        putline(name + " = " + op + " " + retName + " " + lhs.name_ + ", " + rhs.name_);
        return IRValue(name, retType);
    }
    if (auto call = dynamic_cast<callExpr*>(expr); call) {
        auto path = dynamic_cast<pathExpr*>(call->callee_);
        if (path == nullptr || path->path_.size() >= 3) {
            throw "callExpr path not implemented";
        }
        std::string callee, owner;
        std::string args;
        if (path->path_.size() == 1) {
            callee = path->path_[0].name_;
        }
        else {
            owner = path->path_[0].name_;
            callee = path->path_[1].name_;
            if (owner == "Self") {
                owner = currentStruct_;
            }
        }
        if (owner == "Vec") {
            if (callee == "new") {
                return IRValue("zeroinitializer", type);
            }
            if (path->path_[0].types_.empty()) {
                throw "callExpr Vec builtin function without turbofish";
            }
            semType* elemType = checker_->transformType(path->path_[0].types_[0]);
            if (callee == "len" || callee == "is_empty") {
                std::string addr = addressOfObject(call->args_[0]);
                if (ending_) {
                    return IRValue("", semantic::makeNever());
                }
                if (callee == "len") {
                    return vecLen(addr);
                }
                return vecIsEmpty(addr);
            }
            if (callee == "push" || callee == "remove") {
                std::string addr = addressOfObject(call->args_[0]);
                if (ending_) {
                    return IRValue("", semantic::makeNever());
                }
                IRValue arg = generateExpr(call->args_[1]);
                if (ending_) {
                    return IRValue("", semantic::makeNever());
                }
                if (callee == "push") {
                    vecPush(addr, arg, elemType);
                    return IRValue("", semantic::makeUnit());
                }
                return vecRemove(addr, arg, elemType);
            }
            if (path->path_[1].name_ == "clone") {
                std::string addr = generateExpr(call->args_[0]).name_;
                if (ending_) {
                    return IRValue("", semantic::makeNever());
                }
                return valueClone(addr, type);
            }
            throw "Vec method not implemented";
        }
        if (owner == "Box") {
            if (path->path_[0].types_.empty()) {
                throw "callExpr Box builtin function without turbofish";
            }
            if (path->path_[1].name_ == "new") {
                IRValue arg = generateExpr(call->args_[0]);
                if (ending_) {
                    return IRValue("", semantic::makeNever());
                }
                semType* turboType = checker_->transformType(path->path_[0].types_[0]);
                int size = std::max(1, sizeOf(turboType));
                std::string mem = newLabel();
                putline(mem + " = call ptr @malloc(i32 " + std::to_string(size) + ")");
                putline("store " + transformType(turboType) + " " + arg.name_ + ", ptr " + mem);
                return IRValue(mem, semantic::makeBox(turboType));
            }
            if (path->path_[1].name_ == "clone") {
                std::string addr = generateExpr(call->args_[0]).name_;
                if (ending_) {
                    return IRValue("", semantic::makeNever());
                }
                return valueClone(addr, type);
            }
            throw "Box method not implemented";
        }
        if (callee == "clone" && !owner.empty() &&
            checker_->table_.methods[owner].find(callee) == checker_->table_.methods[owner].end()) {
            IRValue arg = generateExpr(call->args_[0]);
            if (ending_) {
                return IRValue("", semantic::makeNever());
            }
            return callTypeClone(arg.name_, type);
        }
        auto &sig = (owner == "" ? checker_->table_.funcSig[callee] : checker_->table_.methods[owner][callee]);
        for (int i = 0; i < call->args_.size(); i++) {
            if (i != 0) {
                args += ", ";
            }
            IRValue now = generateExpr(call->args_[i]);
            if (ending_) {
                return IRValue{"", semantic::makeNever()};
            }
            args += transformType(now.type_) + " " + now.name_;
        }
        std::string retType = (type == nullptr ? "void" : transformType(type));
        std::string funcName = mangle(owner, callee);
        if (retType == "void") {
            putline("call void @" + funcName + "(" + args + ")");
            return IRValue("", type == nullptr ? semantic::makeUnit() : type);
        }
        std::string name = newLabel();
        putline(name + " = call " + retType + " @" + funcName + "(" + args + ")");
        return IRValue(name, type);
    }
    if (auto met = dynamic_cast<methodCallExpr*>(expr); met) {
        semType* recvType = checker_->exprType_[met->from_];
        semType* ownerType = recvType;
        semType* derefedType = recvType;
        while (derefedType->kind_ == semType::kTypeKind::REF) {
            derefedType = derefedType->elem_;
        }
        bool explicitly = (derefedType->kind_ == semType::kTypeKind::STRUCT && checker_->table_.methods[derefedType->name_].count(met->name_));
        if (met->name_ == "clone" && !explicitly) {
            semType* retType = checker_->exprType_[met];
            std::string addr;
            if (semantic::typeEqual(recvType, retType)) {
                if (isPlaceExpr(met->from_)) {
                    addr = placeOrigin(met->from_);
                }
                else {
                    IRValue val = generateExpr(met->from_);
                    if (ending_) {
                        return IRValue("", semantic::makeNever());
                    }
                    addr = newLabel();
                    allocate(addr, retType);
                    putline("store " + transformType(val.type_) + " " + val.name_ + ", ptr " + addr);
                }
            }
            else {
                if (recvType->kind_ != semType::kTypeKind::BOX && recvType->kind_ != semType::kTypeKind::REF) {
                    throw "methodCallExpr implicit clone failed";
                }
                IRValue val = generateExpr(met->from_);
                if (ending_) {
                    return IRValue("", semantic::makeNever());
                }
                addr = val.name_;
                while (!semantic::typeEqual(val.type_->elem_, retType)) {
                    std::string dw = newLabel();
                    putline(dw + " = load ptr, ptr " + val.name_);
                    addr = dw;
                    if (val.type_->kind_ != semType::kTypeKind::REF && val.type_->kind_ != semType::kTypeKind::BOX) {
                        throw "methodCallExpr implicit clone failed";
                    }
                    val = IRValue(dw, val.type_->elem_);
                }
            }
            return valueClone(addr, retType);
        }
        while (ownerType->kind_ == semType::kTypeKind::REF || ownerType->kind_ == semType::kTypeKind::BOX) {
            ownerType = ownerType->elem_;
        }
        if (ownerType->kind_ == semType::kTypeKind::ARRAY) {
            if (met->name_ == "len") {
                return IRValue(std::to_string(ownerType->arrayLength_), semantic::makeBuiltin("usize"));
            }
            throw "array method not implemented";
        }
        if (ownerType->kind_ == semType::kTypeKind::VEC) {
            std::string addr = addressOfObject(met->from_);
            if (ending_) {
                return IRValue("", semantic::makeNever());
            }
            if (met->name_ == "len") {
                return vecLen(addr);
            }
            if (met->name_ == "is_empty") {
                return vecIsEmpty(addr);
            }
            if (met->name_ == "push" || met->name_ == "remove") {
                IRValue arg = generateExpr(met->args_[0]);
                if (ending_) {
                    return IRValue("", semantic::makeNever());
                }
                if (met->name_ == "push") {
                    vecPush(addr, arg, ownerType->elem_);
                    return IRValue("", semantic::makeUnit());
                }
                return vecRemove(addr, arg, ownerType->elem_);
            }
            throw "Vec method not implemented";
        }
        if (ownerType->kind_ != semType::kTypeKind::STRUCT) {
            throw "methodCallExpr on non-struct not implemented";
        }
        std::string owner = ownerType->name_;
        auto dt = checker_->table_.methods.find(owner);
        if (dt == checker_->table_.methods.end() || dt->second.find(met->name_) == dt->second.end()) {
            throw "methodCallExpr cannot resolve method";
        }
        funcSignature& sig = dt->second[met->name_];

        std::string receiver;
        if (sig.selfRef_) {
            receiver = addressOfObject(met->from_);
        }
        else {
            std::string addr = addressOfObject(met->from_);
            if (ending_) {
                return IRValue("", semantic::makeNever());
            }
            receiver = newLabel();
            putline(receiver + " = load " + transformType(ownerType) + ", ptr " + addr);
        }
        if (ending_) {
            return IRValue("", semantic::makeNever());
        }

        std::string args = (sig.selfRef_ ? "ptr " : transformType(ownerType) + " ") + receiver;
        for (Expr* arg : met->args_) {
            IRValue now = generateExpr(arg);
            if (ending_) {
                return IRValue("", semantic::makeNever());
            }
            args += ", " + transformType(now.type_) + " " + now.name_;
        }

        std::string retType = (type == nullptr ? "void" : transformType(type));
        std::string funcName = mangle(owner, met->name_);
        if (retType == "void") {
            putline("call void @" + funcName + "(" + args + ")");
            return IRValue("", type == nullptr ? semantic::makeUnit() : type);
        }
        std::string name = newLabel();
        putline(name + " = call " + retType + " @" + funcName + "(" + args + ")");
        return IRValue(name, type);
    }
    if (auto ret = dynamic_cast<returnExpr*>(expr); ret) {
        IRValue tail("", semantic::makeUnit());
        if (ret->tail_) {
            tail = generateExpr(ret->tail_);
            if (ending_) {
                return IRValue("", semantic::makeNever());
            }
        }
        callReturn(tail.name_);
        return IRValue("", semantic::makeNever());
    }
    if (auto blo = dynamic_cast<blockExpr*>(expr); blo) {
        return generateBlock(blo);
    }
    if (auto fi = dynamic_cast<ifExpr*>(expr); fi) {
        std::string slot;
        bool need = type && !isUnit(type) && !isNever(type);
        if (need) {
            slot = newLabel();
            allocate(slot, type);
        }
        std::string cond = generateExpr(fi->cond_).name_;
        if (ending_) { // will this happen?
            return IRValue("", semantic::makeNever());
        }
        std::string thenLabel = newLabel(), elseLabel = newLabel(), mergeLabel = newLabel();
        putline("  br i1 " + cond + ", label " + thenLabel + ", label " + elseLabel), ending_ = true;
        bool thenEnded = false, elseEnded = false;
        enterLabel(thenLabel); {
            IRValue thenName = generateBlock(fi->then_);
            if (!(thenEnded = ending_)) {
                if (need) {
                    putline("  store " + transformType(type) + " " + thenName.name_ + ", ptr " + slot);
                }
                putline("  br label " + mergeLabel), ending_ = true;
            }
        }
        enterLabel(elseLabel); {
            if (fi->else_) {
                IRValue elseName = generateExpr(fi->else_);
                if (!(elseEnded = ending_)) {
                    if (need) {
                        putline("  store " + transformType(type) + " " + elseName.name_ + ", ptr " + slot);
                    }
                    putline("  br label " + mergeLabel), ending_ = true;
                }
            }
            else {
                putline("  br label " + mergeLabel), ending_ = true;
            }
        }
        if (thenEnded && elseEnded) {
            ending_ = true;
            return IRValue("", semantic::makeNever());
        }
        enterLabel(mergeLabel);
        if (!need) {
            return IRValue("", type);
        }
        std::string ret = newLabel();
        putline(ret + " = load " + transformType(type) + ", ptr " + slot);
        return IRValue(ret, type);
    }
    if (auto whi = dynamic_cast<whileExpr*>(expr); whi) {
        std::string condLabel = newLabel(), bodyLabel = newLabel(), exitLabel = newLabel();
        loops_.push_back(IRLoopInfo(condLabel, exitLabel, "", semantic::makeUnit()));
        putline("  br label " + condLabel), ending_ = true;
        enterLabel(condLabel); {
            IRValue cond = generateExpr(whi->cond_);
            if (!ending_) {
                putline("  br i1 " + cond.name_ + ", label " + bodyLabel + ", label " + exitLabel), ending_ = true;
            }
        }
        enterLabel(bodyLabel); {
            IRValue body = generateExpr(whi->block_);
            if (!ending_) {
                putline("  br label " + condLabel), ending_ = true;
            }
        }
        loops_.pop_back();
        enterLabel(exitLabel);
        return IRValue("", semantic::makeUnit());
    }
    if (auto loop = dynamic_cast<loopExpr*>(expr); loop) {
        std::string bodyLabel = newLabel(), exitLabel = newLabel(), slot = "";
        if (type && !isNever(type) && !isUnit(type)) {
            slot = newLabel();
            allocate(slot, type);
        }
        loops_.push_back(IRLoopInfo(bodyLabel, exitLabel, slot, type));
        putline("  br label " + bodyLabel), ending_ = true;
        enterLabel(bodyLabel); {
            IRValue body = generateExpr(loop->block_);
            if (!ending_) {
                putline("  br label " + bodyLabel), ending_ = true;
            }
        }
        loops_.pop_back(); // this should be fine here
        if (isNever(type)) {
            return IRValue("", type);
        }
        enterLabel(exitLabel); {
            if (slot != "") {
                std::string ret = newLabel();
                putline(ret + " = load " + transformType(type) + ", ptr " + slot);
                return IRValue(ret, type);
            }
            return IRValue("", type);
        }
    }
    if (auto bre = dynamic_cast<breakExpr*>(expr); bre) {
        IRLoopInfo top = loops_.back();
        if (bre->tail_) {
            IRValue v = generateExpr(bre->tail_);
            if (ending_) {
                return IRValue("", semantic::makeNever());
            }
            if (top.retDest_ != "") {
                putline("  store " + transformType(v.type_) + " " + v.name_ + ", ptr " + top.retDest_);
            }
        }
        putline("  br label " + top.breakDest_), ending_ = true;
        return IRValue("", semantic::makeNever());
    }
    if (auto con = dynamic_cast<continueExpr*>(expr); con) {
        putline("  br label " + loops_.back().continueDest_), ending_ = true;
        return IRValue("", semantic::makeNever());
    }
    if (auto un = dynamic_cast<unaryExpr*>(expr); un) {
        if (un->op_ == "-") {
            IRValue operand = generateExpr(un->operand_);
            if (ending_) {
                return IRValue("", semantic::makeNever());
            }
            operand = eliminateOneShared(operand);
            std::string name = newLabel();
            putline(name + " = sub " + transformType(operand.type_) + " 0, " + operand.name_);
            return IRValue(name, operand.type_);
        }
        if (un->op_ == "!") {
            IRValue operand = generateExpr(un->operand_);
            if (ending_) {
                return IRValue("", semantic::makeNever());
            }
            operand = eliminateOneShared(operand);
            std::string name = newLabel();
            if (isIntegerType(operand.type_)) {
                putline(name + " = xor " + transformType(operand.type_) + " -1, " + operand.name_);
            }
            else{
                putline(name + " = xor i1 true, " + operand.name_);
            }
            return IRValue(name, operand.type_);
        }
    }
    if (auto cast = dynamic_cast<castExpr*>(expr); cast) {
        IRValue from = generateExpr(cast->from_);
        if (isBool(from.type_) && isIntegerType(type)) {
            std::string name = newLabel();
            putline(name + " = zext i1 " + from.name_ + " to i32");
            return IRValue(name, type);
        }
        return IRValue(from.name_, type);
    }
    if (auto init = dynamic_cast<structInitExpr*>(expr); init) {
        std::string stru = newLabel();
        std::string typeName = dynamic_cast<pathExpr*>(init->path_)->path_[0].name_;
        if (typeName == "Self") {
            typeName = currentStruct_;
        }
        semType* structType = semantic::makeStruct(typeName);
        allocate(stru, structType);
        for (int i = 0; i < init->fields_.size(); i++) {
            std::string cur = newLabel();
            int index = findIndex(typeName, init->fields_[i].name_);
            putline(cur + " = getelementptr inbounds %struct." + typeName + ", ptr " + stru + ", i32 0, i32 " + std::to_string(index));
            IRValue ini = generateExpr(init->fields_[i].value_);
            if (ending_) { // so strange
                return IRValue("", semantic::makeNever());
            }
            putline("store " + transformType(ini.type_) + " " + ini.name_ + ", ptr " + cur);
        }
        std::string retName = newLabel();
        putline(retName + " = load %struct." + typeName + ", ptr " + stru);
        return IRValue(retName, semantic::makeStruct(typeName));
    }
    if (auto field = dynamic_cast<fieldExpr*>(expr); field) {
        std::string base = placeOrigin(field);
        std::string retName = newLabel();
        putline(retName + " = load " + transformType(type) + ", ptr " + base);
        return IRValue(retName, type);
    }
    if (auto ref = dynamic_cast<refExpr*>(expr); ref) { // minimal M4
        std::string place;
        if (isPlaceExpr(ref->operand_)) {
            place = placeOrigin(ref->operand_);
        }
        else {
            IRValue rhs = generateExpr(ref->operand_);
            if (ending_) {
                return IRValue("", semantic::makeNever());
            }
            place = newLabel();
            allocate(place, rhs.type_);
            putline("  store " + transformType(rhs.type_) + " " + rhs.name_ + ", ptr " + place);
        }
        semType* from = checker_->exprType_[ref->operand_];
        semType* to = type->elem_;
        while (!semantic::typeEqual(from, to)) {
            if (from->kind_ != semType::kTypeKind::REF && from->kind_ != semType::kTypeKind::BOX) {
                throw "refExpr coercion failed";
            }
            std::string next = newLabel();
            putline(next + " = load ptr, ptr " + place);
            place = next;
            from = from->elem_;
        }
        return IRValue(place, type);
    }
    if (auto der = dynamic_cast<derefExpr*>(expr); der) {
        IRValue down = generateExpr(der->operand_);
        if (ending_) {
            return IRValue("", semantic::makeNever());
        }
        if (down.type_->kind_ != semType::kTypeKind::REF && down.type_->kind_ != semType::kTypeKind::BOX) {
            throw "deref operand is not ref or box";
        }
        std::string name = newLabel();
        putline(name + " = load " + transformType(type) + ", ptr " + down.name_);
        return IRValue(name, type);
    }
    if (auto arr = dynamic_cast<arrayExpr*>(expr); arr) {
        std::string slot = newLabel();
        allocate(slot, type);
        for (int i = 0; i < arr->args_.size(); i++) {
            std::string pos = newLabel();
            putline(pos + " = getelementptr inbounds " + transformType(type) + ", ptr " + slot + ", i32 0, i32 " + std::to_string(i));
            IRValue init = generateExpr(arr->args_[i]);
            if (ending_) {
                return IRValue("", semantic::makeNever());
            }
            putline("store " + transformType(type->elem_) + " " + init.name_ + ", ptr " + pos);
        }
        std::string name = newLabel();
        putline(name + " = load " + transformType(type) + ", ptr " + slot);
        return IRValue(name, type);
    }
    if (auto rep = dynamic_cast<repeatedArrayExpr*>(expr); rep) {
        IRValue repValue = generateExpr(rep->value_);
        if (ending_) {
            return IRValue("", semantic::makeNever());
        }
        std::string slot = newLabel();
        allocate(slot, type);
        for (int i = 0; i < type->arrayLength_; i++) {
            std::string pos = newLabel();
            putline(pos + " = getelementptr inbounds " + transformType(type) + ", ptr " + slot + ", i32 0, i32 " + std::to_string(i));
            putline("store " + transformType(type->elem_) + " " + repValue.name_ + ", ptr " + pos);
        }
        std::string name = newLabel();
        putline(name + " = load " + transformType(type) + ", ptr " + slot);
        return IRValue(name, type);
    }
    if (auto ind = dynamic_cast<indexExpr*>(expr); ind) {
        std::string place = placeOrigin(ind);
        std::string name = newLabel();
        putline(name + " = load " + transformType(type) + ", ptr " + place);
        return IRValue(name, type);
    }
    throw "generateExpr not implemented";
}

void IRGenerator::generateStatement(Stmt* stmt) {
    if (auto let = dynamic_cast<letStmt*>(stmt); let) {
        IRValue init = generateExpr(let->init_);
        if (ending_) {
            return;
        }
        if (isUnit(init.type_)) {
            scope_.push(let->name_, "");
            return;
        }
        std::string cover = newLabel(), exprType = transformType(init.type_);
        allocate(cover, init.type_);
        putline("  store " + exprType + " " + init.name_ + ", ptr " + cover);
        scope_.push(let->name_, cover);
    }
    if (auto expr = dynamic_cast<exprStmt*>(stmt); expr) {
        generateExpr(expr->expr_);
    }
    return;
}

IRValue IRGenerator::generateBlock(blockExpr* block) {
    scope_.levelUp();
    IRValue ret("", semantic::makeUnit());
    for (auto stmt : block->stmts_) {
        if (ending_) {
            break;
        }
        generateStatement(stmt);
    }
    if (!ending_ && block->tail_) {
        ret = generateExpr(block->tail_);
    }
    scope_.levelDown();
    return ending_ ? IRValue("", semantic::makeNever()) : ret;
}

void IRGenerator::generateFunction(funcItem* func, std::string owner) {
    auto &sig = (owner == "" ? checker_->table_.funcSig[func->name_] : checker_->table_.methods[owner][func->name_]);
    std::string retType = transformType(sig.retType_);
    currentStruct_ = owner;

    std::string header = "define " + retType + " @" + mangle(owner, func->name_) + "(";
    size_t argIndex = 0;
    if (sig.isMethod_) {
        std::string selfType = sig.selfRef_ ? "ptr" : transformType(semantic::makeStruct(owner));
        header += selfType + " %arg" + std::to_string(argIndex++);
    }
    for (size_t i = 0; i < sig.params_.size(); ++i) {
        if (argIndex != 0) {
            header += ", ";
        }
        header += transformType(sig.params_[i]) + " %arg" + std::to_string(argIndex++);
    }
    putline(header + ") {", true);
    currentRetType_ = retType;
    ending_ = false;
    buffering_ = true;
    body_.clear();
    entryAllocas_.clear();
    scope_.levelUp();

    size_t paramOffset = 0;
    if (sig.isMethod_) {
        const Param& self = func->parameter_[0];
        semType* selfSem = sig.selfRef_
            ? semantic::makeRef(semantic::makeStruct(owner), sig.selfMut_)
            : semantic::makeStruct(owner);
        std::string slot = newLabel(), type = transformType(selfSem);
        allocate(slot, selfSem);
        putline("  store " + type + " %arg0, ptr " + slot);
        scope_.push(self.name_, slot);
        paramOffset = 1;
    }
    for (size_t i = paramOffset; i < func->parameter_.size(); ++i) {
        size_t sigIndex = i - paramOffset;
        std::string slot = newLabel(), type = transformType(sig.params_[sigIndex]);
        allocate(slot, sig.params_[sigIndex]);
        putline("  store " + type + " %arg" + std::to_string(i) + ", ptr " + slot);
        scope_.push(func->parameter_[i].name_, slot);
    }
    IRValue tail = generateBlock(dynamic_cast<blockExpr*>(func->body_));
    if (!ending_) {
        if (retType == "void") {
            callReturn("");
        }
        else {
            if (tail.name_.empty() || tail.type_ == nullptr || transformType(tail.type_) != retType) {
                throw "generateFunction ret tail not equal to retType";
            }
            callReturn(tail.name_);
        }
    }
    buffering_ = false;
    putline("entry:", true);
    for (const std::string& line : entryAllocas_) {
        putline(line, true);
    }
    out_ << body_;
    putline("}", true);
    scope_.levelDown();
    return;
}

void IRGenerator::generateItem(Item* item) {
    if (auto func = dynamic_cast<funcItem*>(item); func) {
        generateFunction(func, "");
    }
    if (auto impl = dynamic_cast<ImplItem*>(item); impl) {
        if (impl->type_->kind_ != Type::kTypeKind::STRUCT) { // should not happen right?
            return;
        }
        for (auto dw : impl->items_) {
            if (auto func = dynamic_cast<funcItem*>(dw); func) {
                generateFunction(func, impl->type_->name_);
            }
        }
    }
    return;
}

void IRGenerator::generateCrate(Crate* crate) {
    putline("target triple = \"riscv32-unknown-none-elf\"");
    
    putline("@.fmt_i32 = private unnamed_addr constant [3 x i8] c\"%d\\00\"");
    putline("@.fmt_i32_nl = private unnamed_addr constant [4 x i8] c\"%d\\0A\\00\"");

    putline("declare i32 @printf(ptr, ...)");
    putline("declare i32 @scanf(ptr, ...)");
    putline("declare ptr @malloc(i32)");
    putline("declare ptr @memcpy(ptr, ptr, i32)");
    putline("declare ptr @memmove(ptr, ptr, i32)");

    putline("define void @print_i32(i32 %v) {");
    putline("entry:");
    putline("  %fmt = getelementptr inbounds [3 x i8], ptr @.fmt_i32, i32 0, i32 0");
    putline("  call i32 @printf(ptr %fmt, i32 %v)");
    putline("  ret void");
    putline("}");

    putline("define void @println_i32(i32 %v) {");
    putline("entry:");
    putline("  %fmt = getelementptr inbounds [4 x i8], ptr @.fmt_i32_nl, i32 0, i32 0");
    putline("  call i32 @printf(ptr %fmt, i32 %v)");
    putline("  ret void");
    putline("}");

    putline("define i32 @get_i32() {");
    putline("entry:");
    putline("  %slot = alloca i32");
    putline("  %fmt = getelementptr inbounds [3 x i8], ptr @.fmt_i32, i32 0, i32 0");
    putline("  call i32 @scanf(ptr %fmt, ptr %slot)");
    putline("  %v = load i32, ptr %slot");
    putline("  ret i32 %v");
    putline("}");

    putline("%Vec = type { ptr, i32, i32 }");

    for (auto sig : checker_->table_.structSig) {
        putline("%struct." + sig.first + " = type {");
        for (int i = 0; i < sig.second.fields_.size(); i++) {
            auto &field = sig.second.fields_[i];
            std::string out = transformType(field.second);
            if (i != sig.second.fields_.size() - 1) {
                out += ", ";
            }
            putline(out);
        }

        putline("}");
    }

    for (Item* item : crate->items_) {
        generateItem(item);
    }

    while (true) {
        semType* next = nullptr;
        for (auto& entry : cloneTypes_) {
            if (cloneState_[entry.first] == 0) {
                next = entry.second;
                break;
            }
        }
        if (next == nullptr) {
            break;
        }
        manifestCloneImpl(next);
    }

    while (true) {
        semType* next = nullptr;
        for (auto& entry : eqTypes_) {
            if (eqState_[entry.first] == 0) {
                next = entry.second;
                break;
            }
        }
        if (next == nullptr) {
            break;
        }
        manifestEqImpl(next);
    }

    putline("define i32 @main() {");
    putline("entry:");
    putline("  call void @rx_main()");
    putline("  ret i32 0");
    putline("}");
}

void IRGenerator::generate(Checker& checker, Crate* crate, const std::string& outputPath) {

    out_ = std::ofstream(outputPath);
    if (!out_) {
        throw std::runtime_error("cannot open IR output: " + outputPath);
    }
    checker_ = &checker;
    generateCrate(crate);
    return;
}
