# NOW 2（修订）：AST 用 Visitor 写，跟你 Python 解释器一样

> 上一版让你用 `getRuleIndex()` 扫描 `children`——那只是另一种更省代码的写法，不是必须的。
> 你熟悉的方式完全可行：`class AstBuilder : public RxParserBaseVisitor`，重写 `visitXxx`。本文按这个讲清楚。
> 仍然只给 **2 个代码示例**，其余你自己写。

---

## 0. 你的疑惑：generated 里到底有什么

和你 Python 解释器完全对应：

| Python 解释器项目 | 这次 |
|---|---|
| `generated/Python3Parser.h`，每个规则一个 `XxxContext` | `target/build/generated/RxParser.h`，每个规则一个 `XxxContext` |
| `generated/Python3ParserBaseVisitor.h`，`visitOr_test(...)` | `target/build/generated/RxParserBaseVisitor.h`，`visitAdditiveExpression(...)` 等 147 个方法 |
| 你写 `class EvalVisitor : public Python3ParserBaseVisitor` | 你写 `class AstBuilder : public RxParserBaseVisitor` |

自己看一眼确认：

```sh
sed -n '/visitAdditiveExpression/,+3p' target/build/generated/RxParserBaseVisitor.h
sed -n '/class  AdditiveExpressionContext/,/};/p' target/build/generated/RxParser.h
```

你会看到生成的默认实现都是 `return visitChildren(ctx);`。

---

## 1. 解释器 vs 这次：同一个 Visitor，返回值不一样

| | 你的 Python 解释器 | 这次的 AstBuilder |
|---|---|---|
| `visitXxx` 返回什么 | 运行时值（`pair<bool,int>` 等） | **AST 节点指针**（`Expr*` / `Type*` / `Stmt*` / `Item*`） |
| `visitOr_test` 里干什么 | 短路求值，直接算 | 把子结果拼成 `Binary`/`Or` 节点 |
| 有没有 AST | 没有，边走边算 | 有：先把整棵树建出来 |
| 建完树之后 | 结束 | 另写一个**普通递归函数**遍历 AST 做类型检查（不是 ANTLR visitor） |

所以流程是：`parser.crate()` 得到 parse tree → `AstBuilder::visit(tree)` 得到 AST 根 → 你自己的检查函数遍历 AST。

---

## 1.5 写代码的第一步（别一次写全）

**先建文件，顺序如下**（visitor 要返回 AST 指针，所以 AST 文件必须在前）：

1. `compiler/src/ast/ast.h`：节点定义；
2. `compiler/src/ast/ast_dump.cpp`（可选 .h）：缩进打印；
3. `compiler/src/frontend/ast_builder.h`：声明 `class AstBuilder : public RxParserBaseVisitor`，类里放 `pool_` 和模板 `make<T>`；
4. `compiler/src/frontend/ast_builder.cpp`：实现各 `visitXxx`；
5. 改 `main.cpp` 和 `CMakeLists.txt`（include 路径加 `src`）。

**第一步只做一条最小竖切**：目标是 `fn main() -> () {}` 能 dump 出 `Fn main`。

- `ast.h` 先只定义 5 个东西：`Node`（带 `enum Kind` 和虚析构）、`Crate{ std::vector<Item*> items; }`、`Item`（先只支持 FN）、`FnDef{ name / params / ret / Block* body }`、`Block{ stmts / tail }`；
- `AstBuilder` 先只 override 三个方法：`visitCrate`、`visitFunctionDefinition`、`visitBlockExpression`（其他规则走默认 `visitChildren`，此刻不用管）；
- `main.cpp` 的 `--dump-ast` 分支：`AstBuilder builder; auto* crate = std::any_cast<Crate*>(builder.visit(root));` 然后调 dump 打印；
- 编译，跑 `/tmp/mini.rx`（内容就一行 `fn main() -> () {}`），看到 `Fn main` 就算通了。

之后按第 7 节的顺序逐个加：字面量/路径 → 二元链 → 一元/后缀 → …，**每加一类编译一次**，dump 出的树一层层变完整。

> **推荐按你说的方式推进**：写一个 `visit`，就去 `ast.h` 补这个节点定义。唯一前提是几个基类和骨架要先有，否则 builder 编译不过：
> - 先定义基类：`Node`（虚析构 + `virtual dump`）、`Expr`、`Type`、`Stmt`、`Item`；
> - 再定义 `Crate`、`Item`、`FnDef`、`Block`（最小切片需要），其余节点写哪个 visit 补哪个；
> - 不必通读 Parser.g4。写某条规则时现查现写：
>   - 看规则本身：`sed -n '/^additiveExpression/,/;/p' grammar/Parser.g4`；
>   - 看生成的访问器：`sed -n '/class  AdditiveExpressionContext/,/};/p' target/build/generated/RxParser.h`。

### 1.6 缩进打印（ast_dump）怎么做

思路：每个节点自己负责打印一行"本节点信息"，然后对每个孩子递归打印、缩进加 1。

```cpp
// ast.h 里的基类
struct Node {
    virtual ~Node() = default;
    virtual void dump(int depth = 0) const = 0;
};

inline void printIndent(int depth) {
    std::cout << std::string(static_cast<size_t>(depth) * 2, ' ');
}

// 具体节点自己实现
struct BinaryExpr : Expr {
    Expr* lhs = nullptr;
    Expr* rhs = nullptr;
    std::string op;
    void dump(int depth) const override {
        printIndent(depth);
        std::cout << "Binary " << op << "\n";
        if (lhs) lhs->dump(depth + 1);
        if (rhs) rhs->dump(depth + 1);
    }
};
```

要点：

- 缩进就是 `depth * 2` 个空格，用 `std::string(n, ' ')`；
- 本节点一行打印关键信息（如 `Binary +`、`Fn main(...)`、`Let mut x: i32`），孩子换行缩进；
- `Crate::dump` 就是 `for (auto* item : items) if (item) item->dump(0);`
- 空指针先判空再递归；
- 直接 `std::cout` 最快；想攒成字符串就改成 `void dump(std::ostream& os, int depth)` 配 `std::ostringstream`；
- 这也是把节点定义拆开写的好处：加一个节点 = 加一个 `dump`，不改别人。

### 1.7 基类怎么定，粒度要多细

**基类只要 5+1 个**，按"语义值的类别"分，不按文法层次分：

| 基类 | 装什么 | 例子 |
|---|---|---|
| `Expr` | 一切产生值的表达式 | 字面量、路径、二元、调用、if、块… |
| `Type` | 类型写法 | `i32`、`Point`、`Box<T>`、`&mut T`、`[T; N]`、`()` |
| `Stmt` | 语句 | `let`、表达式语句 |
| `Item` | 项 | fn、struct、const、impl |
| `ConstVal` | 常量值（数组长度、const 初始化式） | `3`、`N`、`-1`、`true` |
| `Crate` | 整份文件的根容器 | `std::vector<Item*> items` |

`Crate` 不属于上面任何一个，就是根：`visitCrate` 返回 `Crate*`，dump 时遍历 items。它不是"项"，别塞进 `Item`。

**哪些规则 override、返回哪个基类**（按规则名判断）：

| 规则 | 返回类型 |
|---|---|
| `crate` | `Crate*` |
| `item` / `functionDefinition` / `structDefinition` / `constantItem` / `inherentImpl` / `associatedItem` | `Item*` |
| `letStatement` / `statement` | `Stmt*` |
| `typeRef` / `referenceType` / `arrayType` / `typePath` / `closedCastType` | `Type*` |
| `constValue` / `magnitude` | `ConstVal*` |
| 所有 `*Expression`、`primaryExpression`、`nonBlockPrimary`、`conditionPrimary(WithoutBareBlock)`、`literalExpression`、`arrayExpression` | `Expr*` |

**不用 override 的"脚手架规则"**，父节点直接用 `ctx->xxx()` 读：`functionParameters`、`selfParam`、`functionParam`、`identifierBinding`、`structField`、`outerAttribute`、`deriveName`、`pathExprSegment`、`pathIdentSegment`、`genericArgs`、`postfixSuffix`、`dotSuffix`、`callArguments`、`identifier`、`unaryOperator`、`additiveOperator` 等运算符规则、`structExprFields`、`structExprField`；`genericParams`/lifetime/`whereClause` 直接丢。例外：某个返回值要在多处复用（比如路径 `pathInExpression`）时，也可以 override 成 `Expr*`。

**子类粒度三问**（写每条规则时现场问）：

1. 它是"值"还是"脚手架"？脚手架 → 不建节点；
2. 是值 → 属于哪个基类（看上表）；
3. 类型检查时需要区分它吗？需要 → 单独子类；只是数据不同（运算符、名字）→ 一个类 + 字段。例：所有加减乘除位比较都用 `Binary{op}`，不要 `AddExpr/SubExpr`；`if` 用一个 `IfExpr`，不要按 else 有无再分。

写一条规则就现场做一次三问，把结论记在一个 notes 文件里；两条规则能共用同一节点就合并。不要一次规划全部 147 条。

### 1.8 名字的两种身份（声明 vs 使用）

`let mut x = 3;` 里的 `x` **不是节点**，只是 `LetStmt` 上的字符串字段：

```
LetStmt
  name  = "x"
  isMut = true
  type  = 空
  init  = IntLit 3
```

| 出现位置 | 身份 | AST 里怎么存 |
|---|---|---|
| `let mut x = 3;` 的 `x` | 声明（引入名字） | `LetStmt.name` + `isMut` |
| `fn f(x: i32)` 的 `x` | 声明 | `Param.name` + `Param.type` |
| `struct S { x: i32 }` 的 `x` | 声明（字段） | `StructField.name` + 类型 |
| `x + 1`、`foo(x)`、`x = 5` 的 `x` | 使用（引用名字） | 建 `PathExpr` 节点，里面存 `"x"` |
| `p.x` 的 `x` | 字段名 | 存在 `FieldExpr` 的字段里 |

声明处不建 `PathExpr`（否则会自己解析自己）；语义分析只对"使用"查符号表。

### 1.9 `Item` 是什么

`Item` = 程序里的"定义/声明"（Rust 术语，指函数、结构体、常量、impl、use）。Rx 顶层只允许 item，不能写 `let` 或表达式。

具体例子（每个都是一条 Item）：

```rust
fn add(a: i32, b: i32) -> i32 { a + b }   // FnDef
struct Point { x: i32, y: i32 }            // StructDef
const N: usize = 4;                        // ConstDef
impl Point { ... }                         // ImplDef
use rx::core::*;                           // 语法上是 Item，解析后丢弃
```

`impl` 里的 fn/const 也是 Item（associated item）。所以 t1.rx 的 Crate 大致是：

```
Crate
  items = [
    ConstDef N,
    StructDef Point,
    StructDef P,
    ImplDef Point [ FnDef new, FnDef sum ],
    FnDef main
  ]
```

不是 Item 的：语句（`let x = 1;`）、表达式（`a + b`）、类型（`i32`）、名字（`x`）。

为什么要有 `Item` 基类：`Crate.items` 和 impl 的关联项都是混合列表，用 `std::vector<Item*>` 存，元素实际是 `FnDef*` / `StructDef*` / `ConstDef*` / `ImplDef*`；名称解析时按 `kind` switch。

和 Python 的区别：Python 顶层 `def`/`class`/赋值都算"语句"；Rust/Rx 顶层只有 item，`let` 只能出现在函数体里，所以文法里 `item` 和 `statement` 是分开的两套。

### 1.10 六类基类的定义（写 `ast.h` 照这个）

通用约定：所有节点继承 `Node`（带 `kind`、虚析构、`dump`）；孩子存裸指针；"没有"用 `nullptr` 或空 vector 表示。

#### Crate（根容器，不属于下面任何一类）

- `std::vector<Item*> items;`

#### Item

| 节点 | 字段 | 说明 |
|---|---|---|
| `FnDef` | `name`、`std::vector<Param> params`、`Type* ret`（空 = `()`）、`Block* body` | `Param { name, Type* type, bool isMut, bool isSelf }`；`self` 用 `isSelf` 标出来 |
| `StructDef` | `name`、`std::vector<StructField> fields`、`std::vector<std::string> derives` | `StructField { name, Type* type }`；derive 存 `"Clone"` 等 |
| `ConstDef` | `name`、`Type* type`、`ConstVal* value` | |
| `ImplDef` | `Type* target`、`std::vector<Item*> items` | 里面只允许 `FnDef` / `ConstDef` |

`use` 声明不建节点。

#### Stmt

| 节点 | 字段 | 例子 |
|---|---|---|
| `LetStmt` | `name`、`bool isMut`、`Type* type`（可空）、`Expr* init` | `let mut x: i32 = 3;` |
| `ExprStmt` | `Expr* expr` | `v.push(1);`、`foo();` |

块尾表达式不属于 `Stmt`，存在 `Block.tail`。

#### Type（一个节点 + kind 就够）

| kind | 字段 | 例子 |
|---|---|---|
| `BUILTIN` | `name` = `i32/u32/isize/usize/bool` | `i32` |
| `STRUCT` | `name` = 结构体名 / `Self` | `Point`、`Self` |
| `BOX` / `VEC` | `Type* elem` | `Box<i32>`、`Vec<Point>` |
| `REF` | `Type* elem`、`bool isMut` | `&i32`、`&mut Self` |
| `ARRAY` | `Type* elem`、`ConstVal* len` | `[i32; N]` |
| `UNIT` | — | `()` |

`&&T` 是两层 `REF`。例：`&mut Vec<Box<i32>>` → `REF(mut, VEC(BOX(BUILTIN i32)))`。

#### ConstVal

| kind | 字段 | 例子 |
|---|---|---|
| `INT` / `BOOL` / `PATH` | `text` | `4`、`true`、`N`、`Config::LIMIT` |
| `NEG` | `inner` | `-1`、`-N` |

用在：const 项初始化式、数组类型长度、`[v; N]` 的长度。

#### Expr（最多的一类）

| 节点 | 字段 | 例子 |
|---|---|---|
| `IntLit` | `text`（保留原文，后缀/进制以后处理） | `42`、`0b1010u32` |
| `BoolLit` | `bool value` | `true` |
| `PathExpr` | `std::vector<PathSeg> path` | `x`、`N`、`Self::LIMIT`、构造器 `Vec::<i32>::new` |
| `UnitExpr` | — | `()` |
| `Unary` | `op`、`operand` | `-x`、`!b` |
| `Ref` | `bool isMut`、`operand` | `&s`、`&mut p.x`、`&&x`（两层 Ref） |
| `Deref` | `operand` | `*m` |
| `Binary` | `op`、`lhs`、`rhs` | `a + b * 3`、`x == y`、`a && b` |
| `Assign` | `op`（`=`/`+=`/…）、`lhs`、`rhs` | `v[0] = 1`、`i += 1` |
| `Call` | `callee`、`std::vector<Expr*> args` | `f(1)`、`Box::<i32>::new(7)` |
| `MethodCall` | `recv`、`name`、`args` | `v.push(1)`、`p.sum()` |
| `Field` | `recv`、`name` | `self.x` |
| `Index` | `recv`、`index` | `arr[0]` |
| `Array` | `std::vector<Expr*> elems` | `[1, 2, 3]` |
| `RepeatArray` | `value`、`ConstVal* count` | `[0; 4]` |
| `StructInit` | `path`、`std::vector<FieldInit> fields` | `Point { x: 1, y: 2 }`、`Self { .. }` |
| `Cast` | `operand`、`Type* target` | `x as i32` |
| `Block` | `std::vector<Stmt*> stmts`、`Expr* tail` | `{ let a = 1; a + 2 }` |
| `If` | `Expr* cond`、`Block* thenBlk`、`Expr* elseBranch`（可空；可能是 `Block` 或嵌套 `If`） | `if c { 1 } else { 2 }` |
| `While` | `Expr* cond`、`Block* body` | `while i < 10 { ... }` |
| `Loop` | `Block* body` | `loop { ... }` |
| `Break` | `Expr* value`（可空） | `break;`、`break i;` |
| `Continue` | — | `continue;` |
| `Return` | `Expr* value`（可空） | `return;`、`return 1;` |

公共辅助结构：

- `PathSeg { std::string name; std::vector<Type*> typeArgs; }`（`PathExpr` 和 `StructInit` 共用；`Vec::<i32>::new` 的 `<i32>` 在这里）
- `FieldInit { std::string name; Expr* value; }`

几个对照例子：

```
v[0] = v[1] + 2 * 3;
Assign =
  Index ( Path v, IntLit 0 )
  Binary +
    Index ( Path v, IntLit 1 )
    Binary * ( IntLit 2, IntLit 3 )
```

```
Point { x: 1, y: 2 }
StructInit Point
  field x: IntLit 1
  field y: IntLit 2
```

```
if z > 0 { 1 } else { 2 }
If
  cond: Binary > ( Path z, IntLit 0 )
  then: Block tail IntLit 1
  else: Block tail IntLit 2
```

```
&mut p.x
Ref mut
  Field x
    Path p
```

写 `ast.h` 的顺序：先 `Node` + 六类基类 + 最小切片的 `Crate/Item/FnDef/Block`，之后写一个 `visit` 补一个具体节点。

### 1.11 AstBuilder 骨架：不要抄 147 个虚函数

- 生成的 `RxParserBaseVisitor` 已经声明了全部 `visitXxx`，继承即拥有；
- 你只在 `AstBuilder` 里**声明你真正实现的那部分**，没声明的继续走默认 `visitChildren`；
- 写一个 override 时，从 `target/build/generated/RxParserBaseVisitor.h` 抄那一行签名，加 `override`；签名错了编译器会报错（`override` 的作用）；
- 辅助规则（参数表、字段声明、运算符规则等）不要 override，在父节点里直接 `ctx->xxx()` 读。

骨架示意：

```cpp
#include "RxParserBaseVisitor.h"

class AstBuilder : public RxParserBaseVisitor {
public:
    // 只声明你要实现的；名称和参数照抄生成头
    std::any visitCrate(RxParser::CrateContext* ctx) override;
    std::any visitFunctionDefinition(RxParser::FunctionDefinitionContext* ctx) override;
    std::any visitBlockExpression(RxParser::BlockExpressionContext* ctx) override;
    // 以后写一条加一条……

private:
    template <typename T, typename... Args>
    T* make(Args&&... args) {                       // 内存池
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = node.get();
        pool_.push_back(std::move(node));
        return raw;
    }

    std::vector<std::unique_ptr<Node>> pool_;
};
```

实现顺序仍是 1.5 的最小竖切：先让 `visitCrate` / `visitFunctionDefinition` / `visitBlockExpression` 三个跑通 `fn main() -> () {}`，再按第 7 节的顺序一类类加。

---

## 2. 设计约定：所有权与 std::any（讨论后确认）

### 2.1 所有权：内存池 + 裸指针

- 所有 AST 节点用 `new` 创建，由 builder 里的 `std::vector<std::unique_ptr<Node>> pool_` 统一持有；`make<T>(...)` 模板负责创建并返回裸指针。
- 节点的孩子字段存裸指针（`Expr*`、`std::vector<Expr*>`），父亲不拥有、不 delete，进程结束由 pool 统一释放。
- 示意：

```cpp
template <typename T, typename... Args>
T* make(Args&&... args) {                 // 返回裸指针给上层存
    auto node = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = node.get();
    pool_.push_back(std::move(node));     // 所有权在 pool
    return raw;
}
```

### 2.2 std::any 的真实行为（纠正三点）

1. **不是自动转换**：`return raw;` 时 `std::any` 记住的是静态类型（`Expr*`、`Type*`…）。上层要 `std::any_cast<Expr*>(v)` 取出来，类型不对会抛 `std::bad_any_cast`。
2. **父节点知道自己要什么**：`visit(ctx->expression())` 拿到的必然是 `Expr*`，`visit(ctx->typeRef())` 拿到的必然是 `Type*`。所以不用写一个"枚举五种类型再 dynamic_cast"的万能函数，按调用点写两个小 helper 就够了：

```cpp
Expr* anyExpr(const std::any& v) { return std::any_cast<Expr*>(v); }
Type* anyType(const std::any& v) { return std::any_cast<Type*>(v); }
```

3. **`dynamic_cast` 是另一回事**：它是"手上已经有一个 `Expr*`，想知道具体是 Binary 还是 Call"时才用（而且用节点的 `kind` 字段 switch 更省）。父节点存孩子只存基类指针，不需要 dynamic_cast。
4. visitor 的返回类型约定成几种：`Expr*` / `Type*` / `Stmt*` / `Item*` / `ConstVal*` / `Crate*`。每条规则返回哪种，实现时定死（示例 1、2 已这样做）。

### 2.3 AST 为什么是"简化"（不是类型变少，而是去掉文法脚手架）

数据（用 `t1.rx`，805 字节）：

| | 数量 |
|---|---|
| parse tree 节点数 | ≈ 1116 |
| AST 节点数 | ≈ 116 |
| 文法规则 / `visit` 方法 | 147 |
| AST 节点种类 | 约 40（其中表达式 ≈ 20 种） |

合并规则的真实例子——下面 8 条文法规则全部变成同一个 `Binary` 节点（运算符存在 `op` 字符串里）：

`additiveExpression` / `closedAdditiveExpression` / `conditionAdditiveExpression` / `conditionClosedAdditiveExpression` / `conditionBreakAdditiveExpression` / `conditionBreakClosedAdditiveExpression` / `statementAdditiveExpression` / `statementClosedAdditiveExpression` → `Binary`

`a + b * 3` 的对比：

- parse tree：藏在 `statementExpression → statementAssignmentExpression → … → statementAdditiveExpression → … → literalExpression` 一长串里，深度十几层；
- AST：3 层，`Binary + (Path a, Binary * (Path b, IntLit 3))`。

消失在 AST 里的东西：

- 4 套家族 + closed 变体（合并成同一节点）；
- 包装规则（`expression`、`castExpression`、`unaryExpression`、`postfixExpression`、`primaryExpression`、`nonBlockPrimary`…），它们的信息被吸收进具体节点；
- 所有标点 token（`( ) { } ; , : :: ->`、`fn`、`let`…）；
- `use` 和 lifetime 语法（直接不建节点）。

对下一步类型检查的意义：检查函数只面对"Binary / Call / Field / Let / If …"这些语义节点，不用再分支处理十来种语法专用规则。

### 2.4 节点字段的建议

- 固定结构用命名字段：`BinaryExpr { Expr* lhs; Expr* rhs; std::string op; }`、`LetStmt { std::string name; bool isMut; Type* type; Expr* init; }`；
- 变长用 vector：`CallExpr { Expr* callee; std::vector<Expr*> args; }`、`Block { std::vector<Stmt*> stmts; Expr* tail; }`；
- 基类带 `enum Kind`，后面类型检查 switch 和 dump 都方便。

---

## 3. 写每个 `visitXxx` 的固定套路

1. 从 `ctx` 上拿子 context：`ctx->multiplicativeExpression()`、`ctx->identifierBinding()`……（生成的访问器返回子节点或 vector）；
2. 递归：`visit(子ctx)` 拿回 `std::any`，用自己写的 `anyExpr(x)`（内部 `std::any_cast<Expr*>`）转成指针；
3. 组装节点；
4. `return (Expr*)node;`。

不同规则返回类型不同，约定好：类型规则返回 `Type*`，表达式 `Expr*`，语句 `Stmt*`，项 `Item*`。cast 错了 `std::any_cast` 会抛异常，正好帮你发现问题。

---

## 4. 表达式的 4 套家族（这是主要工作量）

Parser.g4 把表达式规则复制了 4 份 + closed 变体，所以你会在 visitor 里看到这些方法：

| 家族 | 方法名前缀 | 举例 |
|---|---|---|
| 普通 | `visitXxxExpression` | `visitAdditiveExpression` `visitClosedAdditiveExpression` |
| 条件 | `visitConditionXxxExpression` | `visitConditionAdditiveExpression` |
| break 操作数 | `visitConditionBreakXxxExpression` | `visitConditionBreakAdditiveExpression` |
| 语句开头 | `visitStatementXxxExpression` | `visitStatementAdditiveExpression` |

它们结构一样，处理办法：

- **写法 A（推荐先这样）**：每个方法都写，但条件/语句版本就是把普通版本的 3~5 行核心逻辑复制过来，只改 Context 类型名；
- **写法 B（写烦了再说）**：把公共逻辑抽成 helper，接收"操作数 vector + 运算符 vector"，4 个方法都调它；
- 包装规则（`expression`、`conditionExpression`、`statementExpression`…）：只有一个孩子，直接 `return visit(ctx->xxxExpression());`。

---

## 5. 示例 1：`visitLetStatement`（和类型/语句的写法）

```cpp
std::any AstBuilder::visitLetStatement(RxParser::LetStatementContext* ctx) override {
    auto* stmt = make<LetStmt>();                 // make<T> 从内存池分配并返回裸指针
    auto* bind = ctx->identifierBinding();        // 生成的访问器直接拿子节点
    stmt->isMut = bind->MUT() != nullptr;         // token 用有没有来判断
    stmt->name  = bind->identifier()->getText();  // 子规则用 getText 拿文本
    if (ctx->typeRef())    stmt->type = anyType(visit(ctx->typeRef()));
    if (ctx->expression()) stmt->init = anyExpr(visit(ctx->expression()));
    return static_cast<Stmt*>(stmt);              // std::any 里放裸指针
}
```

`make`、`anyType`、`anyExpr` 都是你自己写的 helper。注意 `ctx->typeRef()` 和 `ctx->expression()` 可能为空，先判断。

---

## 6. 示例 2：`visitAdditiveExpression`（对照你的 `visitOr_test`）

```cpp
std::any AstBuilder::visitAdditiveExpression(RxParser::AdditiveExpressionContext* ctx) override {
    auto parts = ctx->multiplicativeExpression();   // 和你当年 got = ctx->and_test() 一样
    auto ops   = ctx->additiveOperator();           // "+" / "-" 的列表
    if (parts.size() == 1) return visit(parts[0]);  // 只有一个操作数：直接往上返

    Expr* left = anyExpr(visit(parts[0]));
    for (size_t i = 0; i < ops.size(); ++i) {
        auto* node = make<BinaryExpr>();            // 左结合：((a+b)-c)
        node->op  = ops[i]->getText();
        node->lhs = left;
        node->rhs = anyExpr(visit(parts[i + 1]));
        left = node;
    }
    return static_cast<Expr*>(left);
}
```

对比一下你解释器里的 `visitOr_test`：它拿 `ctx->and_test()` 列表然后求值；这里拿同样结构的列表，然后**拼树**。所有加减乘除、位运算、比较、`&&`/`||`（普通家族先写一遍）都是这个模式。

---

## 7. 实现顺序（每步编译一次，能 dump 出东西再往下）

| # | 做什么 | 涉及规则（对照 `grammar/Parser.g4`） |
|---|---|---|
| 1 | AST 节点定义 + 内存池 + dump 函数 | — |
| 2 | 让 `fn main() -> () {}` 能 dump | `Crate` `Item` `FunctionDefinition` `BlockExpression` |
| 3 | 字面量 / 路径 / 括号 / 单元 | `LiteralExpression` `PathInExpression` `PrimaryExpression` `NonBlockPrimary`（注意套娃，先剥一层） |
| 4 | 二元链普通家族 | `Expression` `AssignmentExpression` `LogicalAnd/Or` `Comparison` `BitAnd/Or/Xor` `Shift` `Additive` `Multiplicative` + `Closed*` |
| 5 | 一元 / `as` / 后缀 | `UnaryExpression` `CastExpression` `PostfixExpression` `PostfixSuffix` `DotSuffix` `CallArguments` |
| 6 | 原子扩展 | `ArrayExpression`（`[a,b]` 和 `[v; N]`）`StructExprFields` `If` `Loop` `While` `Break` `Return` `Continue` |
| 7 | 类型 | `TypeRef` `ReferenceType` `ArrayType` `TypePath` `ClosedCastType` `GenericArgs` |
| 8 | 语句 / 块 | `Statement` `LetStatement` `StatementExpression` `BlockExpression` |
| 9 | 项 | `FunctionDefinition` `FunctionParam` `SelfParam` `StructDefinition` `StructField` `ConstantItem` `InherentImpl` `AssociatedItem` `OuterAttribute` `DeriveName` `ConstValue` |
| 10 | 条件/break/语句三套家族 | `Condition*` / `ConditionBreak*` / `Statement*` 全部（照第 4、5 步复制改类型名） |
| 11 | `use` 声明、lifetime | 解析完后直接忽略，不用处理 |

---

## 8. AST 节点清单（建议，字段自己定）

- 类型：内置（`i32/u32/isize/usize/bool`）、结构体名、`Box<T>`、`Vec<T>`、`&T`/`&mut T`、`[T; N]`、`()`
- 项：`Fn`(名字/参数/返回类型/函数体)、`Struct`(名字/字段/derive)、`Const`(名字/类型/值)、`Impl`(目标类型/关联项)；`use` 丢弃
- 语句：`Let`(名字/mut/类型标注/初始化式)、表达式语句
- 块：语句列表 + 可选尾表达式
- 表达式：整数字面量、bool 字面量、路径、单元、一元、二元、赋值/复合赋值、调用、方法调用、字段、索引、数组、`[v; N]`、结构体构造、`as`、`&`/`&mut`、解引用、块、`if`/`while`/`loop`/`break`/`continue`/`return`
- 常量值：整数字面量、`true`/`false`、常量路径、取负
- dump 函数：缩进递归打印，能看出父子关系即可

---

## 9. main.cpp 怎么接（不用贴代码，改 3 个地方）

你现在 main 里已经有 `--dump-ast` 这个开关和 `root`（parse tree）。把 `dumpAst` 分支写成：

1. `AstBuilder builder;`
2. `auto anyRoot = builder.visit(root);`，然后 `auto* crate = std::any_cast<Crate*>(anyRoot);`
3. 调你的 dump 打印 `crate`。

就这些，其他地方不动。

---

## 10. 两个测试程序（粘到 /tmp）

`/tmp/t1.rx`：

```rust
use rx::core::*;

const N: usize = 4;

struct Point {
    x: i32,
    y: i32,
}

#[derive(Clone, PartialEq)]
struct P { a: i32 }

impl Point {
    fn new(x: i32, y: i32) -> Point {
        Point { x: x, y: y }
    }
    fn sum(&self) -> i32 {
        self.x + self.y
    }
}

fn main() -> () {
    let mut p: Point = Point { x: 1, y: 2 };
    let s: i32 = p.sum();
    let mut v = Vec::<i32>::new();
    v.push(s);
    v[0] = v[1] + 2 * 3;
    let b = Box::<i32>::new(7);
    let r: &i32 = &s;
    let m: &mut i32 = &mut p.x;
    *m = *r as i32;
    let arr: [i32; N] = [1, 2, 3, 4];
    let z = arr[0];
    let c = if z > 0 { 1 } else { 2 };
    let mut i: usize = 0;
    while i < 10 {
        i += 1;
    }
    loop {
        if i == 3 { break; }
        else { continue; }
    }
    println_i32(c);
}
```

`/tmp/t2.rx`：

```rust
fn f(x: &i32, y: &mut i32) -> i32 {
    let a = &x;
    let b = &&x;
    let c = &mut *y;
    *y = -*x + !0;
    let d = [0; 4];
    let e = [[1, 2], [3, 4]];
    let g = if a == b { 1 } else if c != d { 2 } else { 3 };
    let mut i: i32 = 10;
    loop {
        i -= 1;
        if i <= 0 { break i; }
        if i == 5 { continue; }
    }
    let p = return 1;
    42
}
```

自查时能看到：`Vec<i32>::new` 里的 `<i32>`、`Point { x: 1, y: 2 }` 的字段、`&mut p.x` 的引用/解引用链、`else if` 嵌套、`break i` 带值、`[0; 4]` 与 `[[1,2],[3,4]]` 的区别。

---

## 11. 验证命令

```sh
cmake --build target/build -j
./target/build/rxcc --stage semantic --dump-ast /tmp/t1.rx
./target/build/rxcc --stage semantic --dump-ast /tmp/t2.rx
```

全量冒烟（只看崩溃，不看语义对错）：

```sh
for f in $(find tests/official/semantic -name '*.rx'); do
  ./target/build/rxcc --stage semantic --dump-ast "$f" >/dev/null 2>/tmp/e || echo "FAIL $f $(head -c 80 /tmp/e)"
done
echo done
```

---

## 12. 常见坑（Visitor 版）

1. **别依赖默认 `visitChildren`**：它只返回"最后一个非空孩子的结果"，不会帮你组装节点。凡是你关心的规则都要 override，否则 dump 里会出现莫名其妙的空洞。
2. **`std::any` 里放裸指针**，不要放 `unique_ptr`（拷贝直接编译不过）。
3. 规则返回类型要 cast 对：`any_cast<Expr*>` / `any_cast<Type*>` / `any_cast<Stmt*>`。不确定时先看 `x.type() == typeid(Expr*)`。
4. **4 套家族 + closed 变体都要覆盖**（第 4 节表里的前缀），漏一个，那个位置的树就空了。
5. `visitUnaryExpression` 里先看有没有 `unaryOperator()`：`&`、`&mut`、`&&`、`*`、`-`、`!` 都在这个子规则里（是规则不是 token）。
6. `as` 后面跟的是**类型**：`visitCastExpression` 对 `ctx->typeRef()` 或 `ctx->closedCastType()` 调 `visit` 拿 `Type*`。
7. 后缀：`ctx->postfixSuffix()` 是 vector，每个元素三种可能：`callArguments()`（调用）、`dotSuffix()`（点字段/方法）、以及 `[expression]`（索引）。
8. `break`/`return` 的操作数在条件里用的是专用规则：`conditionBreakExpression` / `conditionExpression`。
9. 路径的泛型实参 `Vec::<i32>::new()` 在 `pathExprSegment()->genericArgs()` 里，别丢，语义阶段要用。
10. 判断空：`ctx->xxx()` 返回 nullptr 或空 vector，取之前先判。

---

## 13. 完成标准

- [ ] 第 11 节命令全部成功
- [ ] t1/t2 输出能看出第 10 节列出的所有结构
- [ ] 全量 236 个 official semantic 用例无 `FAIL`
- [ ] 能讲解：为什么 visitor 的返回值和解释器不同、二元链怎么折叠、4 套表达式家族怎么处理

---

## 下一步

下一步是**语义分析**（名称解析 + 类型检查），负例从这里开始要 `exit 1`。跑通后说"下一步"，我给你 `now_todolist_3.md`（同样只给少量示例）。
