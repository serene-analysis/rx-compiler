#include <string>

struct Type{
    enum kTypeKind { BUILTIN, STRUCT, VEC, BOX, REF, ARRAY, NEVER, UNIT }
    std::string name_ = "AAAunnamedType";
    Type* elem_ = nullptr;
    bool isMut_ = false;
    unsigned int arrayLength_ = 0;
};

Type* makeBuiltin(const std::string& name) {
    auto ret = new Type;
    
}