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

**worked example：`functionDefinition` 的每个孩子**

```
functionDefinition
  : FN identifier genericParams? LPAREN functionParameters? RPAREN
    (ARROW typeRef)? whereClause? blockExpression ;
```

| 孩子 | 属于哪类 | 怎么处理 |
|---|---|---|
| `identifier` | 名字 | `ctx->identifier()->getText()` 存字符串 |
| `genericParams` | 无 | 忽略（Rx 的泛型参数只有 lifetime，规范允许丢） |
| `functionParameters` | 一串值（脚手架） | **不 override**；在 `visitFunctionDefinition` 里遍历 `fp->selfParam()` / `fp->functionParam()`，拼成 `std::vector<Param>` 存进 `FnDef` |
| `typeRef` | `Type` | `anyType(visit(ctx->typeRef()))`；注意这里是**返回类型**，参数的 `typeRef` 嵌在 `functionParam` 里，取不到 |
| `whereClause` | 无 | 忽略 |
| `blockExpression` | `Expr`（实际是 `Block`） | `anyExpr(visit(ctx->blockExpression()))`；需要 `Block*` 时 `static_cast<Block*>`（语法保证它一定是块） |

`selfParam` 也在这个表里一并处理（不 override）：名字固定 `"self"`，`AMP()` 判断是否引用，`MUT()` 判断 mut，`SELF_VALUE()` 忽略；类型按 `Self` / `&Self` / `&mut Self` 构造，`lifetime` 丢弃。

**worked example：`structDefinition` 的每个孩子**

```
structDefinition
  : outerAttribute* STRUCT identifier genericParams? whereClause?
    LBRACE (structField (COMMA structField)* COMMA?)? RBRACE ;
```

| 孩子 | 属于哪类 | 怎么处理 |
|---|---|---|
| `outerAttribute` | 语义信息，**不能丢** | 不 override；`for (auto* attr : ctx->outerAttribute()) for (auto* d : attr->deriveName()) derives.push_back(d->getText())`，存 `"Copy"`/`"Clone"`/`"PartialEq"`/`"Eq"` |
| `identifier` | 名字 | `getText()` |
| `genericParams` / `whereClause` | 无 | 忽略（只有 lifetime） |
| `structField` | 一串值（脚手架） | 不 override；每个字段 `f->identifier()->getText()` + `anyType(visit(f->typeRef()))`，拼成 `std::vector<StructField>` |

> 区分：`use`、`lifetime`、`genericParams`、`whereClause` 可以丢；**attribute 里装的是 derive 能力，必须保留名字**。

判断口诀：**有语义的值 → 按六类映射；一串值 → 父节点里读；纯语法脚手架 → 忽略。**

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

**`statement` 里的 `expressionWithBlock` / `statementExpression` 是什么**：它们是"表达式语句"——Rust/Rx 里一个表达式后面加分号、丢弃值，就是一个语句。所以 `Stmt` 只有两种：`Let` 和 `Expr`（包一个表达式）。具体分工：

- `expressionWithBlock`：块状表达式（`{...}`、`if`、`loop`、`while`），分号可省；
- `statementExpression`：普通表达式链在"语句开头"位置的副本（消歧义用），必须有分号；
- 单独一个 `;` 是空语句，AST 直接跳过。

**块尾的特殊规则（容易写错）**：规范原文：*"Even though the formal grammar permits parsing an un-semicoloned ExpressionWithBlock under statement sequences, in the final position of a block it acts as the block's tail expression and determines the block's value."* 实测：

```rust
let x: i32 = { if true { 1 } else { 2 } };  // x = 1
let y: i32 = { loop { break 5; } };          // y = 5
```

所以 `visitBlockExpression` 不能只看 `statementExpression?` 那个孩子：最后一个 `statement` 如果是 **无分号的 `expressionWithBlock`**（`st->SEMI() == nullptr`）且后面没有 `statementExpression`，它其实是块的 `tail_`，不是语句。算法：

```
for (i, st) in ctx->statement():
    if st 是 letStatement          -> Stmt(LET)
    elif st 是 statementExpression -> Stmt(EXPR)（这种必有分号）
    elif st 是 expressionWithBlock:
        if st->SEMI() == nullptr 且 i 是最后一个 且 ctx->statementExpression() 为空
            -> block->tail_ = visit(expressionWithBlock)
        else -> Stmt(EXPR, visit(expressionWithBlock))
    else（只有分号）               -> 跳过
if ctx->statementExpression() 非空 -> block->tail_ = visit(它)
```

`visitExpressionWithBlock` 和 `visitStatementExpression` 都返回 `Expr*`（前者分发给 block/if/loop/while，后者是包装规则直接转发）。

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

`&&T` / `&&mut T`：`ANDAND` 产生两层 `REF`，`MUT` 属于**内层**——`&&mut T` = `&(&mut T)`，`&&T` = `&(&T)`；`lifetime`（`&'a T`）丢弃。表达式里的前缀 `&&x` 同理。

**`typePath` 的文法怎么读**（`typePath : typePathSegment (PATHSEP typePathSegment)*`，`typePathSegment : pathIdentSegment (PATHSEP? genericArgs)?`）：

- 一段就是一个名字（`pathIdentSegment`，`::` 连接多段，像 Python 的 `a.b.c` 换成 `::`）；
- 名字后面可以跟尖括号类型参数 `genericArgs`（像 C++ 的 `vector<int>`），`PATHSEP?` 表示 `Vec::<i32>` 和 `Vec<i32>` 两种写法都合法；
- `genericArg : lifetime | typeRef`，所以 `Vec<&'a i32>` 里混的 `'a` 要丢掉。

例子与 `Type` 节点对应：

| 写法 | 解析结果 |
|---|---|
| `i32`、`bool` | `BUILTIN(name)` |
| `Point`、`Self` | `STRUCT(name)` |
| `Box<i32>` | `BOX(elem=i32)` |
| `Vec::<Point>` | `VEC(elem=Point)`（`::` 有无都一样） |
| `Vec<Box<i32>>` | `VEC(elem=BOX(i32))`（递归） |
| `std::boxed::Box<i32>` | 多段路径，取最后一段 `Box` + 它的实参（Rx 无模块/导入解析，测试不用多段路径引名） |

所以 `visitTypePath` 只要"取最后一段的名字 + 该段的类型实参（跳过 lifetime）"，归一化成 `BUILTIN/STRUCT/BOX/VEC`，不需要为路径建 vector 字段。

**`genericClose` 是干什么的**：泛型的右尖括号收尾。`Vec<Box<i32>>` 末尾的 `>>` 会被 lexer 拆成 `GT` + `GT_SECOND` 两个 token，`genericClose : GT | GT_SECOND` 就是吃这个收尾的语法包装（同时也让 `Vec<i32>=...` 里的 `>=` 拆成 `>` + `=` 各归各家）。纯标点，AST 里直接忽略，`visitGenericClose` 不用写。

**`closedCastType` 是什么**：`as` 转换目标的一种"受限类型写法"，用来消解 `x as T < y` 的歧义（`<` 到底是泛型实参开头还是比较运算）。文法里只有这几种"收口"写法能安全地后跟 `<` 或 `<<`：

```
closedCastType
  : LPAREN typeRef? RPAREN                         // (T) / ()
  | arrayType                                       // [T; N]
  | (AMP | ANDAND) lifetime? MUT? closedCastType    // &T / &&mut T
  | (typePathSegment PATHSEP)* pathIdentSegment PATHSEP? genericArgs   // 必须以泛型实参收尾，如 Vec<i32>
  ;
```

- 注意最后一条 `genericArgs` 是**必须的**：`x as Vec<i32> < y` 解析为 `(x as Vec<i32>) < y`；而裸名字 `x as T < y` 会把 `<` 当泛型实参开头（负例就是考这个：链式比较 `a < b < c` 不允许，必须加括号）。
- 它只出现在 cast 系列规则里（`castExpression ... AS closedCastType`、`closedCastExpression`、condition/statement/conditionBreak 的对应变体）。
- AST 里当普通 `Type*` 处理即可：`LPAREN typeRef? RPAREN` → 有 `typeRef` 就递归、否则 `UNIT`；`arrayType` → 递归；`&/&&` → 包 `REF`（`&&` 两层、`MUT` 属内层）；最后一条 → 和 `typePath` 同样的"名字 + 泛型实参"归一化。

**路径前缀段为什么不重要**：类型路径的前缀是**模块限定**（`std::boxed::Box` 里 `std::boxed` 是模块），而 Rx 没有模块、没有导入解析（`use` 丢弃、测试也不靠它引入名字），类型的身份就是最后一段的名字。所以 `typePath` / `closedCastType` 都只看最后一段。

表达式路径不一样：`Self::LIMIT`、`Box::<i32>::new`、`Config::COUNT` 里的前缀是语义的一部分（限定到某个类型/命名空间），所以 `PathExpr` 必须保存完整段表 `vector<PathSeg>`。

**写 `visitPathInExpression` 和 `visitClosedCastType` 的要点**：

- `visitPathInExpression` **不能照 typePath 归一化**：表达式路径保留全部段，`vector<PathSeg>`，每段 `name = seg->pathIdentSegment()->getText()`，`typeArgs` 从 `seg->genericArgs()` 取（`genericArg()->typeRef()`，lifetime 跳过）；返回 `static_cast<Expr*>`。
- `visitClosedCastType` 是 `visitTypeRef` + `visitReferenceType` 的合体：
  - `ctx->typeRef()` 非空 → 直接 `visit`（括号里的类型）；否则若 `ctx->LPAREN()` 存在 → `UNIT`；
  - `ctx->arrayType()` 非空 → 直接 `visit`；
  - `ctx->closedCastType()` 非空 → 递归后包 `REF`（`ANDAND` 两层，`MUT` 属内层）；
  - 其余是 path 分支：取 `ctx->pathIdentSegment()` + 直接孩子的 `ctx->genericArgs()`，按 typePath 的方式归一化（前缀 `typePathSegment` 忽略）。
- 把"名字 + 类型实参 → `BUILTIN/STRUCT/BOX/VEC`"抽成私有 helper（例如 `Type* namedType(const std::string&, std::vector<Type*>)`），`visitTypePath` 和 `visitClosedCastType` 共用。

#### ConstVal

| kind | 字段 | 例子 |
|---|---|---|
| `INT` / `BOOL` / `PATH` | `text` | `4`、`true`、`N`、`Config::LIMIT` |
| `NEG` | `inner` | `-1`、`-N` |

用在：const 项初始化式、数组类型长度、`[v; N]` 的长度。

`constValue` 里的 `pathInExpression` 是**常量路径**（`N`、`Config::LIMIT`、`Self::LIMIT`），不是普通表达式：**不要 `visitPathInExpression`**，直接 `ctx->pathInExpression()->getText()` 存进 `ConstVal` 的 PATH（文本；sema 再做限定/解析）。`MINUS magnitude` → `NEG(inner)`；`( constValue )` 剥掉括号直接递归；整数字面量存原文（含后缀/进制/下划线），数值留到 sema 求。`magnitude` 同理（整数字面量 / 常量路径 / 括号）。

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

**结合性怎么处理**：

- 二元链规则（`logicalOr`…`multiplicative` 及各家族）：`操作数 (op 操作数)*`，**左结合**，用左折叠：`left = visit(parts[0])`，然后 `for i: left = Binary(op=ops[i], lhs=left, rhs=visit(parts[i+1]))`。`a - b - c` 得到 `Binary(-, Binary(-, a, b), c)`：**左儿子是"已经折好的左半段"，右儿子是单个操作数**。
- `assignmentExpression`：`logicalOr (assignmentOperator expression)?`，**右结合**，最多一个 op；有 op 就建 `Assign(lhs, op, rhs=visit(expression))`，没有就转发 `logicalOr`。
- `comparisonExpression`：最多一个比较。`LT()` 非空时 lhs 是 `closedBitOrExpression()`、rhs 是 `bitOrExpression(0)`；否则 lhs = `bitOrExpression(0)`，有 `comparisonExceptLt()` 时 op = 它的 `getText()`、rhs = `bitOrExpression(1)`，没有比较符就直接转发。
- `castExpression`：`unary (AS type)*` **左折叠**，每层包 `Cast`（`x as u32 as i32` = `Cast(Cast(x, u32), i32)`）；`closedCastExpression` 是 `unaryExpression | castExpression AS closedCastType`，最多在末尾再包一层 `Cast`。`as` 比乘除结合更紧：`a * b as u32` = `a * (b as u32)`。
- `unaryExpression`：`unaryOperator unaryExpression | postfixExpression`，**前缀一元、右递归**，天然可叠（`!!x`、`-*p`、`&mut *p`）。`*` → `Deref`；`-`/`!` → `Unary`；`&`/`&mut` → `Ref`；`&&` → 两层 `Ref`（`MUT` 属内层）；没有前缀就转发 `postfixExpression`。后缀（调用/索引/字段）比一元绑得更紧，所以基准是 `postfixExpression`。
- `postfixExpression`：`primary postfixSuffix*`，**从左往右叠后缀**：`callArguments` → `Call`（当前表达式当 callee）、`[e]` → `Index`、`dotSuffix` 带 `callArguments` → `MethodCall`、`dotSuffix` 只有 `identifier` → `Field`。`f(x)[0].y` 依次包成 `Field(Index(Call(f, x), 0), y)`。condition/postfix、statement/postfix 变体形状相同。
- `nonBlockPrimary` 的 `pathInExpression (LBRACE structExprFields? RBRACE)?`：带 `{...}` 是**结构体构造**（`Point { x: 1, y: 2 }`、`Self { ... }`），字段值取 `structExprField()->expression()`（Rx 没有字段简写，必须写 `name: expr`）；不带就是普通路径表达式，交给 `visitPathInExpression`。提取路径段的代码抽成 helper，与 `visitPathInExpression` 共用。条件位置不允许不带括号的结构体构造（`conditionPrimaryWithoutBareBlock` 里没有这个分支），文法已经处理，你不用管。
- `nonBlockPrimary` 其余分支：`literalExpression` → `IntLit`/`BoolLit`（存原文，含后缀/进制/下划线，数值留 sema）；`(e)` 透明透传、`()` → `Unit`；`arrayExpression` 靠 `SEMI()` 区分 `[a, b]`（Array）和 `[v; N]`（RepeatArray，N 是 `constValue`，用 `anyConstValue` 取）；`BREAK`/`RETURN` 操作数可空（`break;`/`return`），`CONTINUE` 无操作数。`conditionPrimaryWithoutBareBlock` 是条件版：操作数换成 `conditionBreakExpression` / `conditionExpression`，多出 if/loop/while 分支、没有结构体构造。
- 条件/语句/conditionBreak 家族形状相同，照抄换 Context 类型。

公共辅助结构：

- `PathSeg { std::string name; std::vector<Type*> typeArgs; }`（`PathExpr` 和 `StructInit` 共用；`Vec::<i32>::new` 的 `<i32>` 在这里）
- `FieldInit { std::string name; Expr* value; }`

`Param` / `StructField` / `PathSeg` / `FieldInit` 都是**普通 struct，不是 Node 子类**：它们不会被 `visit`，也不放进 `std::any`，只是挂在某个节点上的数据字段。只有六类语义值才需要继承 `Node`。

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

**三个容易踩的坑（写第一个具体节点时就要定下来）**：

1. 单孩子包装规则直接转发，不要套壳。`item`、`associatedItem` 这类规则只有一个非空孩子，直接 `return visit(那个孩子);`，不需要 `make<Item>()` 再记 type——具体子类（`FnDef` 等）自己就是 `Item` 的子类。判断哪个非空用生成的访问器：`ctx->functionDefinition()` 等。
2. `std::any` 按**静态类型精确匹配**：具体规则的 return 必须把指针转成基类，例如 `return static_cast<Item*>(now);`（或 `Expr*`/`Stmt*`/`Type*`/`ConstValue*`/`Crate*`）。如果返回 `FnDef*`，父节点 `anyItem(...)` 会抛 `std::bad_any_cast`——即使 `FnDef` 继承自 `Item`，`any_cast` 也不做派生类到基类的转换。
3. 被丢弃的规则（`useDeclaration`）返回 `static_cast<Item*>(nullptr)`，不要返回空 `std::any()`，否则父节点 cast 抛异常。父节点（如 `visitCrate`）拿到空指针后要判空或跳过。

### 1.12 `use` 和 lifetime（含 `whereClause`）为什么可以丢

规范原文依据：

- Parser conventions 的 "Syntax that may be discarded after parsing"：`use` 声明整条丢弃；**lifetime 语法整段丢弃**，包括 lifetime 参数声明、引用上的 lifetime 标注、显式 lifetime 实参、outlives 约束、**where 子句里的 lifetime 约束**；不需要名称解析、推断、elision 检查或借用检查。
- Language scope：`Type parameters and general generics` 被排除在外；`Lifetime syntax is supported, but its validity is guaranteed rather than checked.`
- Lifetime validity 一节：编译器可以在解析后丢弃 lifetime 语法，不必带进语义分析和 IR；测试保证程序里的 lifetime 都合法。

所以 `genericParams` 和 `whereClause` 可以整个忽略：Rx 里 `whereClauseItem` 只可能是 `lifetime : lifetimeBounds` 或 `typeRef : ...`，而 `TypeParamBounds` 最终也只由 lifetime 组成——没有 trait、没有真正的类型约束。

**哪些 visitor 方法因此不用实现**（留着桩也行，纯死代码；想干净就把头文件那行和 `.cpp` 桩一起删）：

`visitGenericParams`、`visitLifetimeParam`、`visitLifetime`、`visitLifetimeBounds`、`visitTypeParamBounds`、`visitWhereClause`、`visitWhereClauseItem`、`visitGenericClose`。

**注意区分 `genericArgs`/`genericArg`**：`Vec::<i32>` 里的 `i32` 是有用的类型实参，不是 lifetime。处理方式同参数表：父节点（`typePathSegment` / `pathExprSegment` 的读取处）直接遍历 `genericArgs()->genericArg()`，遇到 `typeRef` 就 `visit` 成 `Type*`；不要给 `visitGenericArgs` 设计返回类型。

规范（Language scope / use compatibility）对 `use` 的保证：

- 测试里的 `use` 只是为了让同一份源码也能被参考 rustc 编译（如 `use rx::core::*;`），**不会靠它引入 Rx 里本来没有的名字**；
- 程序用到的 `get_i32` / `print_i32` / `println_i32`、`Box` / `Vec` 在 Rx 里本来就是内建，不导入也能用；
- 别名（`use ... as x;`）允许出现，但测试不会使用别名；
- 违反这些保证的写法属于 UB，不会出现在正例、负例或性能测试里。

所以丢弃 `use` 不会把合法测试程序误判成 CE。两个注意点：

1. **语法错误仍要报**：`use` 的语法必须由 parser 正常解析，解析失败（malformed use syntax）就是语法错误、`exit 1`；我们只是"解析成功后"不把它放进 AST。
2. **不要顺手放开未定义名字**：负例靠普通名称/类型错误来 `exit 1`，如果因为"也许来自 use"就接受未定义名字，负例会误通过。

---

### 1.13 AST 的"儿子"在哪里：边是字段，不是 children 数组

**parse tree 和 AST 都是树，区别在"边怎么表示"：**

- parse tree：每个节点有一个通用的 `children` 数组，儿子就是数组元素（连标点 token 也在里面）；
- AST：节点用**命名字段**存子节点（或子节点 vector），这些字段就是边。例如：

```
LetStmt : name_ + init_            → init_ 指向表达式子树
Binary  : op_ + lhs_ + rhs_        → lhs_/rhs_ 指向左右子树
Call    : callee_ + args_(vector)  → callee_ 和每个实参是儿子
FnDef   : parameter_(vector<Param>) + body_(Block*)
Block   : stmts_(vector<Stmt*>) + tail_(Expr*)
```

**树是"存字段"那一刻建起来的**，不是 visitor 的调用顺序建起来的：

1. visitor 是后序遍历（自底向上）：先 `visit(子)` 拿到子树根；
2. 父节点 `make<自己>()`，把子树根存进自己的字段（`node->lhs_ = anyExpr(visit(...))`）；
3. 父节点把自己 return 给爷爷节点，爷爷再存进自己的字段；
4. 最后 `visitCrate` 返回根，从 `Crate*` 顺着字段能走到所有节点。

**`pool_` 与树的关系**：`pool_` 只是所有节点的平铺内存（负责释放），不是树；树是那些**指针字段**构成的结构。每个节点只会被一个父字段（或一个 vector）引用，所以从 `Crate*` 可达的图就是一棵树，不会成环。

**为什么现在"找不到儿子"**：因为大部分方法还是 `return visitChildren(ctx);` 的桩——字段没填，边就不存在。等 `visitBinaryExpression` 之类实现后，字段填上，树自然成形。

**想看见儿子**：写 dump（打印自己 → 递归 dump 字段里的每个子节点），就是第 1.6 节的写法；或者调试时从 `Crate*` 一层层展开字段。

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
