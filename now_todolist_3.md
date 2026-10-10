# NOW 3：语义分析（详细版）

> 前置：AST 建树已完成（`--dump-ast` 正常、236 个 semantic 用例 0 崩溃）。
> 目标：`make test FILTER=official:semantic STAGE=semantic` 全绿——正例 `exit 0`，负例 `exit 1`，不许崩溃/超时。
> 本文解释"语义分析到底在干什么、怎么一步步做"，不提供完整实现。核心代码自己写、自己讲（AI 规范 + Code Review）。

---

## 0. 语义分析到底在检查什么

Parser 只保证"语法像 Rx"；语义分析保证"意思说得通"。对照：

```rust
fn main() -> () {
    let x: i32 = true;          // 语法对，类型错
    y = 1;                       // 语法对，y 没定义
    let z = 1; z = 2;            // 语法对，z 不是 mut
    let r = &z; *r = 3;          // 语法对，不能通过共享引用写
    let a = 1 + true;            // 语法对，运算符类型不匹配
}
```

这些程序都能被 Parser 解析成树，但都应该 `exit 1`。反过来说，合法程序必须 `exit 0`。

语义分析有 **四件大事**：

1. **名称解析**：每个"使用的名字"到底指哪个声明？（局部？参数？函数？常量？结构体？方法？）
2. **类型检查/推断**：每个表达式的类型是什么？赋值、调用、运算符用得对不对？
3. **place 与可变性检查**：赋值/借用/可变方法调用的目标是不是"可写的位置"？
4. **常量求值**：const 项、数组长度、`[v; N]` 的 N 能算出值吗？有没有循环依赖？

外加一批小检查：`main` 入口、derive 能力、递归布局、impl 合法性、保护名。

和你的 Python 解释器的根本区别：解释器**运行到那一步才知道**类型/名字错；编译器要在**不运行**的情况下全部判断出来。

评分只看退出码：**有任何一个语义错误 → `return 1`；全部通过 → `return 0`**。诊断信息打印到 stderr 即可（AST 还没存行列号，先打印"错误种类 + 涉及的名字"）。

---

## 1. 整体架构：两遍 + 符号表 + 错误收集

### 1.1 为什么要两遍

顶层声明与使用顺序无关：

```rust
fn main() -> () { helper(); }   // helper 还没声明
fn helper() -> () {}
```

所以先扫一遍收集所有顶层信息，再检查函数体。

- **第一遍 collect**：登记所有函数签名、结构体（字段、derive）、常量、impl 的方法/关联常量。
- **第二遍 check**：逐个检查函数体、常量值。

### 1.2 符号表长什么样

- 全局：`functions: name -> 签名`、`structs: name -> 字段表/derive`、`consts: name -> {类型, 值}`；每个 struct 一张 `assoc: name -> 方法/关联常量`。
- 局部：`std::vector<Scope>` 当栈，每个 Scope 是 `name -> Symbol`；进块 push、出块 pop。
- **查名从内到外，找到最近的绑定就停，不回退**（被局部遮蔽的函数不能再当函数调用）。
- **两个命名空间**：类型空间和值空间分开——`struct S` 和 `fn S` 可以同名；`S { ... }` 里 `S` 查类型空间，表达式 `S(...)` 查值空间。

### 1.3 错误收集

- 每个检查函数返回 `Type*`（出错返回 `nullptr` 或一个 `Error` 类型），把错误写进 `errors_`，**继续检查**（多报几个）。
- 最后统一：`errors_ 非空 → return 1`。
- 不要一遇到错就 `throw`/`exit`：崩溃会被判"负例失败"，而且只能看到一个错。

---

## 2. 名称解析（第一件大事）

### 2.1 声明与使用

| 出现位置 | 身份 | AST 里怎么存（你已完成） |
|---|---|---|
| `let x = 1;` 的 `x` | 声明 | `letStmt.name_` |
| `fn f(x: i32)` 的 `x` | 声明 | `Param.name_` |
| `x + 1` 的 `x` | 使用 | `pathExpr`，语义阶段查符号表 |
| `Self::LIMIT` 的 `Self` | 类型名 | path 的一段 |

### 2.2 作用域的规则（用例子记）

```rust
fn f(a: i32) -> i32 {         // 参数在当前函数作用域
    let x = 1;                 // 外层局部
    {
        let x = true;          // 内层遮蔽：出了块 x 又是 i32
        0
    }
    x                          // 这里 x 是 i32
}

fn g() -> i32 { 42 }
fn main() -> () {
    let g = 1;                 // 遮蔽函数 g
    g();                       // 编译错误：g 现在是 i32，不能调用（不回退找函数）
}
```

### 2.3 重复定义与保护名

- 顶层函数/常量/结构体在各自命名空间内不能重名；结构体字段不能重名；函数参数不能重名。
- 一个结构体的所有 `impl` 共享一个关联项空间：**跨 impl 重复方法/关联常量也算错误**。
- 保护名（不能重声明）：
  - 类型空间：`i32 u32 isize usize bool Box Vec Copy Clone PartialEq Eq`
  - 值空间：`get_i32 print_i32 println_i32`
  - `impl` 只能给用户结构体，不能给 `i32`/`Box` 这类内建。
- `use` 和 lifetime 在 AST 阶段已丢弃，不参与解析。

### 2.4 常用查名函数

```
Symbol* lookupValue(name);   // 从局部栈往外 -> 常量 -> 函数
TypeInfo* lookupType(name);  // 结构体 -> 内建类型
FnSig* findMethod(Type* recv, name);  // 后面方法查找用
```

---

## 3. 类型检查（第二件大事）

### 3.1 内部类型表示

建议定义独立的 sema 类型（不是 AST 的 `Type*`）：

```
Type { Kind kind; string name; Type* elem; bool isMut; unsigned arrayLen;
       // Kind: BUILTIN/STRUCT/REF/ARRAY/BOX/VEC/UNIT/NEVER
}
```

- 结构体按**名字**比较；`[T; N]` 按长度值比较（`[i32; 4]` = `[i32; (4usize)]`）；`&T` 比较 referent 类型和 mut；lifetime 不影响类型同一性。
- 常用类型做单例/缓存；`typeEqual(a, b)` 结构比较。
- **给表达式记类型**：`std::unordered_map<const Expr*, Type*>` 或给 `Expr` 加 `Type* ty_`（下一阶段 IR 要用）。

### 3.2 `infer`：自底向上算每个表达式的类型

检查器两个入口：

```
Type* infer(Expr* e);              // 没有期望类型
Type* check(Expr* e, Type* expected);  // 有期望类型（见 3.3）
```

每种表达式的规则（Rx 布尔/整数规则）：

| 表达式 | 类型规则 | 例子 |
|---|---|---|
| 整数字面量 | 按 3.3 确定 | `1` → i32 |
| bool 字面量 | `bool` | `true` |
| 路径 `x` | 查符号表，返回声明类型 | 局部/常量 |
| 一元 `-x` | x 是有符号整数 → 同类型 | `-1i32` |
| 一元 `!x` | x 整数或 bool → 同类型 | `!true` |
| 二元 `/ * % + -`、位运算 | 两边是**同一个**整数类型（或一层 `&T`）→ 该类型 | `1i32 + 2i32` |
| 移位 `<< >>` | 左边整数 L（或 `&L`），右边任意整数 R → L | `1u32 << 2` |
| 比较 `< <= > >=` | 两边同为整数/bool（各层 mut 匹配的引用也行）→ bool | `a < b` |
| `== !=` | **同一类型**且能力允许 → bool（见 7） | `1 == 1i32` |
| `&& ||` | 两边必须 bool → bool | |
| `x as T` | int→int、bool→int；目标不给 x 期望类型 | `1 as u32` |
| `&x` / `&mut x` | `&T` / `&mut T`，要求 x 是可借用的 place | |
| `*p` | p 是 `&T`/`&mut T`/`Box<T>` → T（Vec 不能） | |
| 调用 `f(...)` | 查签名，实参逐个 check 成参数类型 → 返回类型 | |
| 方法调用 `x.m(...)` | 方法查找（见 5）→ 返回类型 | |
| 字段 `x.f` | 按接收者类型（引用/Box 自动解引用）找字段 → 字段类型 | `p.x` |
| 索引 `x[i]` | x 是数组或 Vec；**i 的 expected 是 usize** → 元素类型 | `arr[0]` |
| 数组 `[a,b,c]` | 元素 LUB（见 3.4）；长度 = 个数 | `[1,2]` → `[i32;2]` |
| 重复数组 `[v; N]` | v 的类型；N 常量求值 | `[0; 4]` → `[i32;4]` |
| 结构体构造 `S { ... }` | 字段齐全且类型匹配 → S | |
| 块 | 有 tail → tail 类型；没有 → `()` | `{ 1 }` → i32 |
| `if` | 无 else → `()`；有 else → 分支 LUB/expected | |
| `while` | 条件必须 bool，整个 while → `()` | |
| `loop` | 体是...；`break` 的值按 LUB → 该类型；无 break 值 → `()` | |
| `break/return/continue` | `!`（never） | |

### 3.3 期望类型（expected type）——整数字面量的类型怎么定

整数字面量类型按顺序：**① 后缀 → ② expected → ③ 默认 i32**。

```rust
let a = 1;              // 无后缀无期望 → i32
let b: u32 = 1;         // 期望 u32 → 1 是 u32
let c = 1u32;           // 后缀 → u32
print_i32(1);           // 形参 i32 → 1 是 i32
v.push(1);              // T=i32 → 1 是 i32
v[0];                   // 索引期望 usize → 0 是 usize
```

**expected 从哪里来（check 的调用点）**：

- 带类型标注的 `let` 初始化器；
- 赋值右侧（`x = e` 按 x 的类型 check e）；
- 函数/方法/构造调用的实参（按形参类型 check）；
- `return e` / 函数体块尾（按返回类型）；
- 结构体字段初始化（按字段类型）；
- 有声明类型的 const 初始化器；
- 会**穿透**：括号、块尾、`if` 两分支、同一 loop 的所有 break 值。

**不穿透**（这种写法规范列为 UB，测试不出现，不用专门处理）：

```rust
let x: u32 = 1 + 2;     // 期望不会穿进二元运算 → UB，不会考
```

实现上就是两个函数：

```
infer(e)               // 自己算
check(e, expected)     // 在允许的位点把 expected 传下去；否则退回 infer + typeEqual
```

### 3.4 LUB：两个分支类型不一致时取"公共类型"

用在：`if` 两分支、数组元素、同一 loop 的所有 `break` 值。

规则（按源码顺序看，忽略 never）：

1. 第一个非 never 的类型为 T；
2. 后面的类型 U：若 U 能隐式调整到 T → 保持 T；否则若 T 能调整到 U 且先前结果也能 → 改用 U；
3. 都不行 → UB（测试不出现，不用实现）。

```rust
let a = [1, 2, 3];                 // 都是 i32 → [i32; 3]
let b = if c { 1 } else { 2 };     // i32
let d = loop { break 1; };         // i32
```

### 3.5 never 与不可达

- `return e`、`break e`、`continue` 的类型是 `!`；`while` 体、无 else 的 `if`、无 break 值的 loop 是 `()`。
- `!` 可以和任意类型合并（LUB 里被忽略）。
- **不可达代码里的名称/类型错误仍要报**（只有 place mutability 违规在不可达代码里是 UB、不考）：

```rust
fn f() -> i32 {
    return 1;
    let y = z;      // z 未定义 → 仍然 exit 1
}
```

---

## 4. place 与可变性（第三件大事）

**place** = 可以放东西/可以取地址的位置：变量、字段、数组/Vec 索引、`*p`。

规则：

```rust
fn main() -> () {
    let x = 1;
    x = 2;              // 错误：x 不是 mut
    let mut y = 1;
    y = 2;              // ok

    let r = &y;         // 共享引用
    *r = 3;             // 错误：共享引用不可写
    let m = &mut y;     // 可变引用；要求 y 是 mut place
    *m = 4;             // ok
    y = 5;              // ok（测试保证 m 之后没再用）

    let mut v = Vec::<i32>::new();
    v.push(1);          // push(&mut self)：v 必须是可变 place
    v[0] = 5;           // ok
    let e = &v[0];      // 共享借用
}
```

判定方法：写一个 `isPlace(Expr*)` 和 `isMutablePlace(Expr*)` 递归：

- 局部变量：看声明有没有 `mut`；
- 字段/索引：看接收者的 place 是否可变（但**穿过共享引用后不可恢复**）；
- `*p`：p 是 `&mut T` → 可变；`&T` → 只读；`Box<T>` → 看 Box 自己的 owner；
- `&mut place` 要求 place 可变；
- Vec 索引的隐式借用：读只需共享；写/可变借用要求该索引步骤可变（细节多，对照 `vec-index-mutability` suite 补）。

---

## 5. 方法查找与内建（第四件大事）

### 5.1 方法查找算法

对 `recv.name(args)`：

1. 先知道 recv 的类型 T；
2. 候选顺序：T 本身；如果 T 是 `&U`、`&mut U` 或 `Box<U>`，逐层解引用到 U（**Vec 不解引用**）；每到一个类型，把 `&T` / `&mut T` 作为接收者的方法也加入候选；
3. 找唯一匹配的方法；同一候选类型上多个不同方法匹配是 UB（不考）；没有 → 编译错误；
4. 选中后再检查：接收者够不够可变（`&mut self` 要求可变 place）、实参类型对不对。

```rust
struct S { x: i32 }
impl S {
    fn get(&self) -> i32 { self.x }
    fn set(&mut self, v: i32) -> () { self.x = v; }
}
fn main() -> () {
    let mut s = S { x: 0 };
    s.set(3);          // 接收者 &mut S，s 可变 → ok
    let r = s.get();   // &S，ok
    let t = &s;
    t.get();           // 自动解引用 &S -> S，再取 &self → ok
    t.set(1);          // 错误：t 是共享引用，拿不到 &mut S
}
```

### 5.2 内建方法/函数表（要特判）

| 类型 | 用法 |
|---|---|
| `Box<T>` | `Box::<T>::new(value)`（关联函数）；`.clone()`（T: Clone 时） |
| `Vec<T>` | `Vec::<T>::new()`；`.len()`、`.is_empty()`、`.push(v)`、`.remove(i)`；`.clone()`（T: Clone） |
| `[T; N]` | 只有 `.len()` |
| 自由函数 | `get_i32() -> i32`、`print_i32(i32)`、`println_i32(i32)` |

- 容器构造必须显式写类型实参（省略是 UB，不考）；`Box::<i32>::new` 的类型实参在 path 段里（你 AST 已保留）。
- 其他整数类型传给 `print_i32` 必须显式 `as`。

---

## 6. 常量求值

常量上下文只有三处：**const 项初始化式、数组长度 `[T; N]`、`[v; N]` 的 N**。

ConstValue 只允许：整数字面量、`true/false`、常量路径（`N`、`Type::NAME`、`Self::NAME`）、`-值`、括号。

```rust
const N: usize = 4;
const M: usize = N;              // 引用别的常量
const K: usize = N + 1;          // 语法错误类？不是——ConstValue 不允许加法 → 编译错误
struct Cfg;
impl Cfg { const LIMIT: usize = 2; }

fn main() -> () {
    let a: [i32; N] = [1, 2, 3, 4];      // N 求值为 4
    let b = [0; Cfg::LIMIT];              // 关联常量
}
```

实现：

- collect 时给常量建"名字 → 求值状态（未算/正在算/已算+值）"；
- 求值常量路径时递归；发现"正在算"就是**循环依赖 → 编译错误**；
- 数组长度统一转成 `usize`；
- 常量名解析：非限定 `N`、`Type::NAME`、`Self::NAME`；指向局部/函数 → 编译错误。

---

## 7. derive 与能力表

`#[derive(...)]` 给结构体标记能力，要求递归检查：

| derive | 要求 |
|---|---|
| `Copy` | 每个字段都是 Copy，且**同一结构体必须显式 derive Clone** |
| `Clone` | 每个字段都是 Clone（`&mut T` 不是） |
| `PartialEq` | 每个字段都是 PartialEq |
| `Eq` | 每个字段都是 Eq，且必须已 derive PartialEq |

基础能力表（背下来）：

| 类型 | Copy | Clone | PartialEq/Eq |
|---|---|---|---|
| 整数、bool | 是 | 是 | 是 |
| `&T` | 是 | 是 | 看 T |
| `&mut T` | 否 | 否 | 看 T |
| `[T; N]` | 跟着 T | 跟着 T | 跟着 T |
| `Box<T>` / `Vec<T>` | 否 | T: Clone 时 | 按元素比较 |
| 结构体 | 看 derive | 看 derive | 看 derive |

```rust
#[derive(Copy)]               // 错误：Copy 必须同时 derive Clone
struct A { x: i32 }

#[derive(Clone)]              // 错误：&mut i32 不是 Clone
struct B { r: &mut i32 }

#[derive(Eq)]                 // 错误：Eq 要求先 derive PartialEq
struct C { x: i32 }

#[derive(Clone, PartialEq, Eq)]  // ok
struct D { v: Vec<i32> }
```

递归类型（`struct Node { children: Vec<Node> }`）检查要能终止（用"正在检查"集合）。

---

## 8. 其他检查

- **入口**：必须有 `fn main() -> ()`（返回可省略、无参数、无泛型）。
- **递归布局**：按值的环无有限大小 → 编译错误；Box/Vec/引用打断环：
  ```rust
  struct Bad { next: Bad }              // 错误
  struct Ok1 { next: Box<Ok1> }         // ok
  struct Ok2 { next: Vec<Ok2> }         // ok
  struct Bad2 { arr: [Bad2; 2] }        // 错误（数组也按值）
  ```
- **impl 合法性**：只能给用户结构体；重复方法/关联常量（跨 impl）报错。
- **不可达代码**：见 3.5。

---

## 9. 完整走查一个例子

```rust
struct Point { x: i32, y: i32 }

fn dist2(p: &Point) -> i32 {
    p.x * p.x + p.y * p.y
}

fn main() -> () {
    let s = dist2(&Point { x: 3, y: 4 });
    print_i32(s);
}
```

检查过程：

1. **collect**：登记 `Point{x:i32,y:i32}`、`dist2: (&Point) -> i32`、`main: () -> ()`。
2. **check `dist2`**：进入作用域，`p: &Point`。`p.x`：字段访问，接收者 `&Point` 解引用到 `Point`，字段 `x: i32`；`*`（乘法）两边 i32 → i32；`+` 两边 i32 → i32。函数尾类型 i32 = 返回类型 ✔。
3. **check `main`**：
   - `Point { x: 3, y: 4 }`：查类型空间得 Point，两个字段都有且类型 i32 ✔ → 类型 Point；
   - `&Point{...}`：Ref → `&Point`；
   - `dist2(实参)`：形参 `&Point`，check 实参成 `&Point` ✔ → 返回 i32；
   - `let s = ...`：s 推断为 i32；
   - `print_i32(s)`：形参 i32，s 是 i32 ✔；返回 `()`，作为语句丢弃。
4. 无错误 → `return 0`。

把这段手工流程对应到代码里，就是你写的 `checkExpr/checkStmt`。

---

## 10. 代码结构建议

```
compiler/src/sema/
├── type.h        // sema::Type + typeEqual + 常用类型
├── symbol.h      // Symbol / Scope / GlobalTable
├── checker.h     // class Checker
└── checker.cpp   // collect + check + 各种规则
```

`checker.h` 大致接口：

```cpp
class Checker {
public:
    bool checkCrate(const Crate* crate);      // main 调这个
private:
    // 两遍
    void collect(const Crate*);
    void checkItem(const Item*);
    void checkBlock(const blockExpr*, Type* expected);
    void checkStmt(const Stmt*);

    // 表达式
    Type* infer(const Expr*);
    Type* checkExpr(const Expr*, Type* expected);

    // 查名 / place
    Symbol* lookupValue(const std::string&);
    TypeInfo* lookupType(const std::string&);
    bool isPlace(const Expr*);
    bool isMutablePlace(const Expr*);

    // 错误
    void error(const std::string& msg);
    bool ok_ = true;
};
```

- 表达式类型可以 `std::unordered_map<const Expr*, Type*>` 存，或直接给 `Expr` 加 `Type* ty_`。
- 错误信息先打印名字和种类即可；AST 没有行列号，暂时做不到精确定位。
- 每写一个规则，先在 `tests/custom/` 加正/负两条小用例，比跑整个 suite 快。

### 10.5 第一遍 collect 的具体做法（当前要写的）

**不需要写 ANTLR visitor**。`ASTVisitor` 是给 parse tree 转 AST 用的；现在你面对的是自己的 AST，写普通递归函数即可。按节点类型分发有两种做法：

- `dynamic_cast` 链（和你的 `ast_dump.h` 一模一样）——不用改 `ast.h`；
- 或给 `Expr`/`Stmt`/`Item` 基类加 `enum Kind` 再 `switch`——更清晰但要改文件。

先用 dynamic_cast，够用。

**符号表就是普通的哈希表**，"name -> 签名"就是 `unordered_map<string, 结构体>`：

```cpp
struct FnSig {
    std::vector<Type*> params;   // 不含 self；已解析成 sema 类型
    Type* ret;
    bool isMethod = false;       // 是否在 impl 里
    bool selfByRef = false;      // &self / &mut self
    bool selfMut   = false;      // &mut self / mut self
};

struct StructInfo {
    std::vector<std::pair<std::string, Type*>> fields;  // vector 保持声明序
    std::vector<std::string> derives;
};

struct ConstInfo {
    Type* type = nullptr;
    const ConstValue* value = nullptr;     // AST 节点
    enum State { UNRESOLVED, RESOLVING, DONE } state = UNRESOLVED;
    long long ivalue = 0;                  // 求值结果
    bool bvalue = false;
};

class GlobalTable {
public:
    std::unordered_map<std::string, FnSig> functions;
    std::unordered_map<std::string, StructInfo> structs;
    std::unordered_map<std::string, ConstInfo> consts;
    // struct 名 -> 方法名 -> 签名；跨 impl 共用同一张（重复即错误）
    std::unordered_map<std::string, std::unordered_map<std::string, FnSig>> methods;
    std::unordered_map<std::string, std::unordered_map<std::string, ConstInfo>> assocConsts;
};
```

**签名 = 参数类型列表 + 返回类型**（都是**解析后的 sema 类型**，不是 AST 的 `Type*`）：

```rust
fn add(a: i32, b: i32) -> i32 { .. }   // params={i32,i32}, ret=i32
fn get(&self) -> i32 { .. }            // params={}, ret=i32, isMethod, selfByRef
fn set(&mut self, v: i32) -> () { .. } // params={i32}, ret=(), isMethod, selfMut
```

`self` 不放进 `params`，用三个 bool 表示——后面方法调用检查接收者可变性时直接看它们。返回类型省略时 `ret = ()`。

**分两小遍，解决"结构体后用"**：

```rust
struct A { b: B }      // 引用后面才声明的 B
struct B { x: i32 }
```

1. **登记名字**：所有 struct 名（fields 先空）、函数名、常量名、每个 impl 的方法名/关联常量名（值占位）；遇到重名立即报错。
2. **解析类型**：再走一遍，用 `resolveType(astType)` 填字段类型、参数/返回类型、常量声明类型。`resolveType` 只查 `structs` 的名字表，所以第 1 步完成后就没问题。

（`Self`：在 impl 里解析成 impl 的目标结构体名，collect 时记录 `currentImpl`。数组长度的常量求值可以延迟到常量阶段，`resolveType` 先留 AST 长度节点或跳过。）

**collect 阶段能抓的负例**：

- 同名函数 / 同名结构体 / 同名常量；
- 结构体字段重名；
- **同一结构体的全部 impl 共享一张关联项表**，跨 impl 重名也算错误；
- `impl` 目标必须是用户结构体（不能给 `i32`/`Box`/`Vec`…）。

**做完后自测**：跑 `names-and-shadowing`、`namespace-errors`，看负例是否开始干净地 `exit 1`。

### 10.6 collect 走查一个完整例子

```rust
const N: usize = 4;

struct Point { x: i32, y: i32 }

impl Point {
    fn get(&self) -> i32 { self.x }
    fn set(&mut self, v: i32) -> () { self.x = v; }
}

fn add(a: i32, b: i32) -> i32 { a + b }

fn main() -> () {
    let p = Point { x: 1, y: 2 };
    println_i32(add(p.x, p.y));
}
```

**第 1 小遍：登记名字（值先空着，重名立即报错）**

```
functions:  add  -> {}(占位)      main -> {}(占位)
structs:    Point -> { fields 空 }
consts:     N    -> {}(占位)
methods:    Point -> { get -> {}(占位), set -> {}(占位) }
```

遇到下面这种就在这一步报错（不用等类型解析）：

```rust
fn g() -> i32 { 1 }
fn g() -> i32 { 2 }      // functions 里已有 g → duplicate function

struct P { x: i32 }
impl P { fn f(&self) -> i32 { 1 } }
impl P { fn f(&self) -> i32 { 2 } }   // methods["P"] 里已有 f → duplicate method（跨 impl 也算）
```

**第 2 小遍：解析类型，把占位填满**（`resolveType` 只查 structs 的名字表，所以顺序无关）

- `Point.x`、`Point.y`：`resolveType(i32)` → 内置 i32；
- `get`：`selfByRef = true`、`params = {}`、`ret = i32`；
- `set`：`selfMut = true`、`params = {i32}`、`ret = ()`；
- `add`：`params = {i32, i32}`、`ret = i32`；
- `main`：`params = {}`、`ret = ()`（没写 `->` 就填 `()`）；
- `N`：`type = usize`，`value = ConstInt(4)`（求值状态先 UNRESOLVED，等常量阶段）。

填完：

```
functions:
  add  -> { params=[i32, i32], ret=i32 }
  main -> { params=[],        ret=() }

structs:
  Point -> { fields=[("x", i32), ("y", i32)], derives=[] }

methods:
  Point -> get -> { params=[],     ret=i32, isMethod, selfByRef }
           set -> { params=[i32],  ret=(),  isMethod, selfMut   }

consts:
  N -> { type=usize, value=ConstInt(4), state=UNRESOLVED }
```

**`resolveType` 的几步 trace**（AST 的 Type 节点 → sema 类型）：

| AST 写法 | sema 结果 |
|---|---|
| `i32` | `BUILTIN("i32")`（单例） |
| `Point` | `STRUCT("Point")`，顺便校验 structs 里有这个名字 |
| `Self`（在 `impl Point` 内） | `STRUCT("Point")` |
| `Box<Vec<i32>>` | `BOX(elem=VEC(elem=BUILTIN i32))` |
| `&mut i32` | `REF(elem=i32, isMut=true)` |
| `[i32; N]` | `ARRAY(elem=i32, len=?)`，长度等常量求值得到 4 |

**collect 骨架示意**（不是完整代码）：

```cpp
void Checker::collect(const Crate* crate) {
    // 1) 登记名字
    for (auto* item : crate->items_) {
        if (auto* s = dynamic_cast<const structItem*>(item)) {
            if (tbl_.structs.count(s->name_)) error("duplicate struct " + s->name_);
            tbl_.structs[s->name_] = {};
        } else if (auto* f = dynamic_cast<const funcItem*>(item)) {
            if (tbl_.functions.count(f->name_)) error("duplicate fn " + f->name_);
            tbl_.functions[f->name_] = {};
        } else if (auto* c = dynamic_cast<const constItem*>(item)) {
            if (tbl_.consts.count(c->name_)) error("duplicate const " + c->name_);
            tbl_.consts[c->name_] = {};
        } else if (auto* im = dynamic_cast<const ImplItem*>(item)) {
            std::string tname = typeNameOf(im->type_);   // 从 AST 类型取结构体名
            for (auto* sub : im->items_) {
                if (auto* f = dynamic_cast<const funcItem*>(sub)) {
                    if (tbl_.methods[tname].count(f->name_)) error("duplicate method " + f->name_);
                    tbl_.methods[tname][f->name_] = {};
                } else if (auto* c = dynamic_cast<const constItem*>(sub)) {
                    if (tbl_.assocConsts[tname].count(c->name_)) error("duplicate assoc const");
                    tbl_.assocConsts[tname][c->name_] = {};
                }
            }
        }
    }
    // 2) 解析类型：填 structs 字段、functions/methods 签名、const 声明类型
    //    遇到 Self 时用当前 impl 的目标结构体名
}
```

**做完 collect 就能过的负例**：重名函数/结构体/常量/字段、跨 impl 重复方法、`impl i32 { .. }`（第 2 小遍里检查目标必须是用户结构体）。`names-and-shadowing` 里的作用域/遮蔽要等 check 阶段的局部符号表。

### 10.7 `type.h` / `symbol.h` 里到底放什么

一句话：`type.h` 是检查器的"词典"（类型长什么样、怎么比较），`symbol.h` 是检查器的"记忆"（现在有哪些名字、各自什么类型），`checker.cpp` 是"大脑"（走树、查表、报错）。分开只是为了 checker.cpp 别变成两千行。

**`type.h`**（sema 的规范类型 + 工具）：

```cpp
namespace sema {
struct Type {
    enum Kind { BUILTIN, STRUCT, REF, ARRAY, BOX, VEC, UNIT, NEVER } kind;
    std::string name;          // BUILTIN / STRUCT 的名字
    Type* elem = nullptr;      // REF / ARRAY / BOX / VEC
    bool isMut = false;        // REF
    unsigned arrayLen = 0;     // ARRAY
};

Type* builtin(const std::string& name);      // i32/u32/isize/usize/bool，返回单例
Type* makeRef(Type* elem, bool isMut);
Type* makeArray(Type* elem, unsigned len);
Type* makeBox(Type* elem);
Type* makeVec(Type* elem);
Type* UnitType();
Type* NeverType();

bool typeEqual(const Type* a, const Type* b);   // 结构比较：struct 按名字、array 按长度…
std::string typeToString(const Type* t);        // 报错信息用
}
```

内存管理：sema 类型 `new` 出来不释放也没事（编译器进程短命），内置类型存单例。它和 AST 的 `Type*` 是两回事：AST 的 Type 是"写法"（可能写着 `Self`、`Box<T>`），sema 的 Type 是"解析后的规范类型"。

**`symbol.h`**（表 + 表项）：

```cpp
namespace sema {
struct FnSig { std::vector<Type*> params; Type* ret;
               bool isMethod = false, selfByRef = false, selfMut = false; };
struct StructInfo { std::vector<std::pair<std::string, Type*>> fields;
                    std::vector<std::string> derives; };
struct ConstInfo { Type* type = nullptr; const ConstValue* value = nullptr;
                   enum State { UNRESOLVED, RESOLVING, DONE } state = UNRESOLVED;
                   long long ivalue = 0; bool bvalue = false; };

struct LocalSym { Type* type; bool isMut; };

struct GlobalTable {
    std::unordered_map<std::string, FnSig> functions;
    std::unordered_map<std::string, StructInfo> structs;
    std::unordered_map<std::string, ConstInfo> consts;
    std::unordered_map<std::string, std::unordered_map<std::string, FnSig>> methods;
    std::unordered_map<std::string, std::unordered_map<std::string, ConstInfo>> assocConsts;
};

class ScopeStack {
public:
    void push();  void pop();
    void declare(const std::string& name, LocalSym s);   // 同层可遮蔽，直接覆盖
    LocalSym* find(const std::string& name);             // 从内到外，找到就停
private:
    std::vector<std::unordered_map<std::string, LocalSym>> scopes_;
};
}
```

**它们在哪被用**：

| 检查器里的动作 | 用到什么 |
|---|---|
| `infer(pathExpr x)` | `ScopeStack::find(x)` → `LocalSym::type` |
| 赋值/`&mut` 检查 | `LocalSym::isMut` + place 递归 |
| 调用 `f(args)` | `GlobalTable::functions` 里比对 `FnSig` |
| `x.m()` | `GlobalTable::methods` + 方法查找算法 |
| `resolveType(Point)` | `GlobalTable::structs` 里查名字 |
| const/数组长度 | `GlobalTable::consts` + `ConstInfo` 求值状态 |

写代码的顺序：先 `type.h`（纯数据+构造函数，最容易），再 `symbol.h`（纯数据），最后 `checker.cpp`（逻辑）。每写完一个文件都能单独编译过，不会有"一半文件互相依赖"的烦恼。

> **可选简化：不写 `type.h`，直接复用 AST 的 `Type`。** 因为 Rx 类型语法简单，两份结构确实几乎一样。区别只在"角色"：AST 的 Type 是**源码写法**（可能写着 `Self`、数组长度是没求值的 `ConstValue*`、结构体名还没校验），sema 的 Type 是**解析后的规范类型**（`Self` 已换成目标结构体、长度是数字、名字已验证、有 `NEVER/ERROR`、内置类型单例可指针比较）。
>
> 复用方案：collect 第 2 小遍**就地解析** AST 的 Type——
> - `struct Type` 加 `Kind` 里补 `NEVER`/`ERROR`，加 `unsigned arrayLen_ = 0; bool lenResolved_ = false;`；
> - 遇到 `STRUCT name=="Self"` 且当前在 `impl X` 里 → 直接改名 `X`；
> - 数组长度求值后写进 `arrayLen_`；
> - 结构体名不在 `structs` 表里 → 改成 `ERROR` 并报错；
> - `typeEqual` 比较解析后的字段。
>
> 省半天到一天的工作量，适合时间紧的情况；缺点是把"写法"和"语义"混在一个 struct 里，Code Review 观感稍差。**二选一，别两套混用。**

---

## 11. 分阶段计划（每阶段跑对应 suite，绿了再往下）

### Phase 1：名称解析
作用域栈、遮蔽、两个命名空间、重复定义（含跨 impl）、保护名、关联项查名。
测试：`names-and-shadowing`、`namespace-errors`、`protected-names`、`lifetimes-and-use`。

### Phase 2：基础表达式与语句
字面量类型确定、路径、运算符规则、`let`、基本赋值/复合赋值的 mut 检查、普通调用、`return`/块尾、`if/while/loop` 基础。
测试：`casts-and-literals`、`integer-arithmetic`、`shifts`、`compound-assignment`、`boolean-and-short-circuit`。

### Phase 3：期望类型、分支合并、never
check/infer 双入口、expected 位点、LUB、never、不可达校验。
测试：`expected-types`、`blocks-if-and-never`、`loops-and-jumps`、`loop-state-merges`、`runtime-value-merges`。

### Phase 4：引用与可变性
REF 类型、coercion（`&mut→&`、解引用路径、never）、place/可变性、coercion 位点。
测试：`references-and-mutability`、`reference-coercions`、`reference-lub`、`scalar-reference-operators`、`vec-index-mutability`。

### Phase 5：聚合、方法、内建
结构体构造/字段、数组与长度、方法查找、Box/Vec 方法、内建 I/O。
测试：`arrays`、`structs-and-fields`、`nested-containers`、`recursive-layout`、`box-and-moves`、`vec-operations`、`methods-and-self`、`calls-recursion-and-abi`、`aggregate-arguments-and-reference-fields`、`evaluation-order-and-temporaries`、`live-state-calls`。

### Phase 6：常量、derive、入口、综合
常量求值与循环依赖、derive 能力、main、impl 合法性。
测试：`constants-and-paths`、`constant-errors`、`copy-clone-and-equality`、`recursive-traits`、`invalid-impls-and-generics`、`entry`、`builtin-io`、`source-main-calls`、5 个 `comprehensive-*`。

---

## 12. 测试与调试

```sh
make test FILTER=official:semantic:names-and-shadowing STAGE=semantic VERBOSE=true
make test FILTER=official:semantic STAGE=semantic
```

- 失败日志在 `target/tests/run-*/`（`.command/.stdout/.stderr`）。
- 负例必须"干净地 `exit 1`"；崩溃/超时都算失败。
- 每个 Phase 结束跑一次全量，记录剩余失败的 suite。

---

## 13. 明确不用做的（UB，测试不会出现）

- 省略 `Box::new`/`Vec::new` 类型实参、函数作值、保护名当结构体名、局部与常量同名、不可达里的 mut 违规、跨类型相等、LUB 无公共目标、点调用多个匹配、零大小数据（除 `()` 作结果）、无效 lifetime、`use` 违规。
- 不要为它们写检查，会浪费大量时间，还可能误伤正例。

---

## 14. 今天的动作

1. 建 `sema/type.h`、`sema/symbol.h`、`sema/checker.h/.cpp` 空壳；`main.cpp` 接上 `checkCrate`（先直接 `return true`），跑一次 `make test ...` 确认链路没坏。
2. 实现 collect 阶段 + 作用域栈 + 查名（Phase 1），跑 `names-and-shadowing`，逐个把失败用例变成规则。
3. 绿了就进 Phase 2，每完成一个 Phase 跑全量回归。
