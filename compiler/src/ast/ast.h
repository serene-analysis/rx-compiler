#include <vector>

struct Node{
    virtual ~Node() {

    }
};

struct Expr : Node {

};

struct Type : Node {

};

struct Stmt : Node {

};

struct Item : Node {

};

struct ConstValue : Node {

};

struct Crate : Node {
    std::vector<Item*> items_;
};