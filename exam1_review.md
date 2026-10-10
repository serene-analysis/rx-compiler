# 第一次检查考试复习：g4 → AST 与画树

> 考试范围（老师原话）：利用 g4 构建 AST 的过程；画简单 AST 树；基础内容。
> 本材料按"理解管线 → 认识节点 → 大量画树例题 → 自测"组织。

---

## 0. 60 秒总览

```
源码.rx
  │  ① ANTLR 词法分析（grammar/Lexer.g4）
  ▼
token 流
  │  ② ANTLR 语法分析（grammar/Parser.g4，入口规则 crate）
  ▼
parse tree（语法树：每个文法规则一个 Context + 标点 token）
  │  ③ 我们写的 ASTVisitor（继承 RxParserBaseVisitor）
  ▼
AST（自己的节点：只保留语义）
  │  ④ 语义检查（下一阶段）
  ▼
exit 0 / exit 1
```

记牢一句话：**ANTLR 负责词法/语法（parse tree），我们写 visitor 把 parse tree 变成 AST。**

---

## 1. 从 g4 到 parse tree（要能讲）

### 1.1 规则与 Context

- 每条 `规则名 : ... ;` 在生成代码里对应一个 `规则名Context` 类；
- 规则里的 token（大写名字，如 `FN`、`LET`）是叶子节点；
- 每条文法规则**发生一次**就在 parse tree 里留下一个节点（包括为消歧义存在的包装规则）。

例如 `a + b` 的 parse tree 长这样（层层包装）：

```
expression
└─ assignmentExpression
   └─ logicalOrExpression
      └─ ...（十几层）
         └─ additiveExpression
            ├─ multiplicativeExpression → ... → pathInExpression(a)
            ├─ additiveOperator(+)
            └─ multiplicativeExpression → ... → pathInExpression(b)
```

**对比 AST：只有两个 Path 和一个 Binary。** 这就是"AST 去掉了文法脚手架"。

### 1.2 优先级和结合性由文法决定

- **优先级**：规则分层。`additiveExpression` 的孩子是 `multiplicativeExpression`，所以 `1 + 2 * 3` 中 `*` 自然更深：`Binary(+, 1, Binary(*, 2, 3))`。
- **左结合**：`A (op A)*` 形式（如 `a - b - c` = `(a-b)-c`）。
- **右结合**：`A (op 右递归)?` 形式（如赋值 `a = b = c` = `a = (b = c)`）。
- **一元**：`unaryOperator unaryExpression` 右递归，天然可叠（`!!x`、`-*p`、`&mut *p`）。
- **后缀**：`primary postfixSuffix*` 从左往右套（`f(x)[0].y`）。
- **cast**：`unary (AS type)*` 左折叠（`x as u32 as i32` = `(x as u32) as i32`）。

### 1.3 上下文标点（简答准备）

`>` 后面是什么会改变 token 切法：lexer 用 mode 把 `>`、`>>`、`>=`、`>>=` 拆成 `GT` / `GT_SECOND` / `GE_EQ` / `SHR_EQ` 等 token，让：
- 泛型收尾 `Vec<Vec<i32>>` 的 `>>` 能当两个 `>`（`genericClose`）；
- `a >= b` 能当 `>` + `=`；
- `a >> b` 是移位（`shiftRight : GT GT_SECOND`）。

`&&` 是 `ANDAND` 一个 token，但在类型/前缀借用里表示**两个 `&`**。

### 1.4 4 套表达式家族 + closed（为什么要知道）

Parser.g4 把表达式规则复制了 4 套（`xxxExpression`、`conditionXxx`、`conditionBreakXxx`、`statementXxx`）和一批 `closedXxx`，都是为了消歧义（`<` 是泛型还是比较、块开头的边界等）。**它们在 AST 里全部合并成同一批节点**（Binary/Call/…），这是"AST 简化"的最大来源。

### 1.5 visitor 的机制（可能考）

- `RxParserBaseVisitor` 为每条规则生成 `virtual std::any visitXxx(XxxContext*)`，默认 `return visitChildren(ctx);`；
- 我们继承它，按后序遍历：先 `visit` 孩子拿子树，再 `make` 父节点、把子树存进字段、`return` 给上层；
- `std::any` 按**静态类型精确匹配**：具体节点返回前要 `static_cast<Expr*>` 等基类指针；父节点用 `anyExpr/anyType/...` 取；
- 参数、字段这种"一串数据"不建 visit，父节点直接用 `ctx->xxx()` 读（如 `functionParam()`、`structField()`）；
- `use` 声明和 lifetime 语法解析后直接丢弃（规范允许）。

---

## 2. AST 节点一览

### 2.1 六大类

`Crate`（根，`items`）、`Item`、`Stmt`、`Expr`、`Type`、`ConstValue`。

| 类 | 节点 | 关键字段 |
|---|---|---|
| Item | `Fn` | 名字、参数表、返回类型、函数体 Block |
| Item | `Struct` | 名字、字段表、derive 列表 |
| Item | `Const` | 名字、声明类型、ConstValue |
| Item | `Impl` | 目标类型、关联项（Fn/Const） |
| Stmt | `Let` | 名字、mut、类型标注（可空）、初始化式 |
| Stmt | `ExprStmt` | 表达式（值被丢弃） |
| Type | 内置/Struct/Box/Vec/Ref/Array/Unit | 名字、elem、isMut、数组长度 |
| ConstValue | Int/Bool/Path/Neg | 文本、inner |

### 2.2 Expr 节点（画树主角）

| 节点 | 写法 | 孩子/字段 |
|---|---|---|
| `IntLit` / `BoolLit` | `42` / `true` | 值 |
| `Path` | `x`、`Self::LIMIT`、`Vec::<i32>::new` | 段表（段可带类型实参） |
| `Unit` | `()` | — |
| `Unary` | `-x`、`!b` | op + operand |
| `Ref` | `&x`、`&mut x`、`&&x` | isMut + operand（`&&` 两层） |
| `Deref` | `*p` | operand |
| `Binary` | `a + b`、`==`、`&&` | op + lhs + rhs |
| `Assign` | `a = b`、`i += 1` | op + lhs + rhs |
| `Call` | `f(1)`、`Box::<i32>::new(7)` | callee + 实参表 |
| `MethodCall` | `v.push(1)`、`p.sum()` | recv + 方法名 + 实参表 |
| `Field` | `p.x`、`self.x` | recv + 字段名 |
| `Index` | `arr[0]`、`v[i]` | recv + 下标 |
| `Array` | `[1, 2, 3]` | 元素表 |
| `RepeatArray` | `[0; 4]` | value + 长度 ConstValue |
| `StructInit` | `Point { x: 1 }`、`Self { ... }` | Path + 字段名/值列表 |
| `Cast` | `x as i32` | operand + 目标 Type |
| `Block` | `{ ... }` | stmts 列表 + tail（可空） |
| `If` | `if c { a } else { b }` | cond + then + else（else 可空，可为嵌套 If） |
| `While` | `while c { ... }` | cond + body |
| `Loop` | `loop { ... }` | body |
| `Break` / `Return` | `break i;` / `return;` | 值（可空） |
| `Continue` | `continue;` | — |

### 2.3 会"消失"的东西

- `use` 声明、lifetime（`<'a>`、`&'a`、where 子句）→ 不建节点；
- 标点 token、括号分组 `(x)`（透传 x）、包装规则 → 不建节点；
- 参数、字段、路径段、`PathSeg`、内置方法签名 → 普通 struct/字符串，不是 Expr 节点。

---

## 3. 画树规范（考试答题建议）

1. 每个节点写 `名字(关键信息)`：`Binary(+)`、`MethodCall(push)`、`Field(x)`、`Cast(i32)`、`Ref(&mut)`、`Path(Vec<i32>::new)`、`IntLit(3)`、`Let(mut x)`、`Fn(add)`、`Struct(Point)`。
2. 孩子用缩进或连线画，保持"父在上、子在下一层"。
3. 可选字段没有就画空/省略（如 `If` 没有 else、`Return` 无返回值）。
4. `Block` 明确区分 `stmts`（语句）和 `tail`（尾表达式，可空）。
5. `Path` 的**类型实参**要写出来：`Path(Vec<i32>::new)`——`<i32>` 挂在 `Vec` 段上，别丢。
6. `Item`/`Param`/字段这些不是 Expr，不要和表达式节点混在一层。

---

## 4. 例题（先自己画，再对答案）

### 例 1：优先级
```rust
let x = 1 + 2 * 3;
```
```
Let(x)
└─ Binary(+)
   ├─ IntLit(1)
   └─ Binary(*)
      ├─ IntLit(2)
      └─ IntLit(3)
```

### 例 2：二元左结合
```rust
a - b - c;
```
```
ExprStmt
└─ Binary(-)
   ├─ Binary(-)
   │  ├─ Path(a)
   │  └─ Path(b)
   └─ Path(c)
```

### 例 3：赋值右结合
```rust
a = b = c;
```
```
ExprStmt
└─ Assign(=)
   ├─ Path(a)
   └─ Assign(=)
      ├─ Path(b)
      └─ Path(c)
```

### 例 4：一元
```rust
-y + !b;
```
```
ExprStmt
└─ Binary(+)
   ├─ Unary(-)
   │  └─ Path(y)
   └─ Unary(!)
      └─ Path(b)
```

### 例 5：cast 链
```rust
x as u32 as i32;
```
```
ExprStmt
└─ Cast(i32)
   └─ Cast(u32)
      └─ Path(x)
```

### 例 6：引用/解引用
```rust
let r = &x;
let m = &mut p.x;
let b = &&x;
*m = *r as i32;
```
```
Let(r)
└─ Ref(&)
   └─ Path(x)

Let(m)
└─ Ref(&mut)
   └─ Field(x)
      └─ Path(p)

Let(b)
└─ Ref(&)
   └─ Ref(&)
      └─ Path(x)

ExprStmt
└─ Assign(=)
   ├─ Deref
   │  └─ Path(m)
   └─ Cast(i32)
      └─ Deref
         └─ Path(r)
```

### 例 7：后缀链
```rust
f(x)[0].y;
```
```
ExprStmt
└─ Field(y)
   └─ Index
      ├─ Call
      │  ├─ Path(f)
      │  └─ Path(x)
      └─ IntLit(0)
```

### 例 8：方法调用
```rust
v.push(1 + 2);
```
```
ExprStmt
└─ MethodCall(push)
   ├─ Path(v)
   └─ Binary(+)
      ├─ IntLit(1)
      └─ IntLit(2)
```

### 例 9：结构体构造
```rust
let p = Point { x: 1, y: g(2) };
```
```
Let(p)
└─ StructInit
   ├─ Path(Point)
   ├─ field x
   │  └─ IntLit(1)
   └─ field y
      └─ Call
         ├─ Path(g)
         └─ IntLit(2)
```

### 例 10：数组
```rust
let a = [1, 2, 3];
let b = [0; N];
```
```
Let(a)
└─ Array
   ├─ IntLit(1)
   ├─ IntLit(2)
   └─ IntLit(3)

Let(b)
└─ RepeatArray
   ├─ value: IntLit(0)
   └─ count: Path(N)          ← 长度是 ConstValue 路径
```

### 例 11：if / else if
```rust
fn f(c: bool, d: bool) -> i32 {
    if c { 1 } else if d { 2 } else { 3 }
}
```
```
Fn(f)  params: c: bool, d: bool   ret: i32
└─ Block
   └─ tail: If
      ├─ cond: Path(c)
      ├─ then: Block(tail IntLit(1))
      └─ else: If
         ├─ cond: Path(d)
         ├─ then: Block(tail IntLit(2))
         └─ else: Block(tail IntLit(3))
```

### 例 12：while
```rust
while i < 10 { i += 1; }
```
```
ExprStmt
└─ While
   ├─ cond: Binary(<)
   │  ├─ Path(i)
   │  └─ IntLit(10)
   └─ body: Block
      └─ stmts: ExprStmt
         └─ Assign(+=)
            ├─ Path(i)
            └─ IntLit(1)
```

### 例 13：loop 作为块尾
```rust
fn f() -> () {
    let mut i: usize = 0;
    loop { if i == 0 { break; } }
}
```
```
Fn(f)  ret: ()
└─ Block
   ├─ stmts: Let(mut i)
   │         ├─ type: usize
   │         └─ IntLit(0)
   └─ tail: Loop                      ← 无分号的 expressionWithBlock 在末尾 = 块尾
      └─ body: Block
         └─ tail: If                  ← 同理，if 也是块尾
            ├─ cond: Binary(==)
            │  ├─ Path(i)
            │  └─ IntLit(0)
            └─ then: Block
               └─ stmts: ExprStmt
                  └─ Break            ← break 无值
```

### 例 14：块 = 语句 + 尾表达式
```rust
{ let d = 1; d + 2 }
```
```
Block
├─ stmts: Let(d) → IntLit(1)
└─ tail: Binary(+)
   ├─ Path(d)
   └─ IntLit(2)
```

### 例 15：函数定义
```rust
fn add(a: i32, b: i32) -> i32 { a + b }
```
```
Fn(add)
├─ params: a: i32, b: i32
├─ ret: i32
└─ body: Block
   └─ tail: Binary(+)
      ├─ Path(a)
      └─ Path(b)
```

### 例 16：struct + derive + impl + Self
```rust
#[derive(Clone, PartialEq)]
struct Point { x: i32, y: i32 }

impl Point {
    fn get(&self) -> i32 { self.x }
    fn make(a: i32) -> Point { Self { x: a, y: 0 } }
}
```
```
Struct(Point)  derives: Clone, PartialEq
├─ field x: i32
└─ field y: i32

Impl(Point)
├─ Fn(get)
│  ├─ params: self: &Self
│  ├─ ret: i32
│  └─ body: Block
│     └─ tail: Field(x)
│        └─ Path(self)
└─ Fn(make)
   ├─ params: a: i32
   ├─ ret: Point
   └─ body: Block
      └─ tail: StructInit
         ├─ Path(Self)
         ├─ field x: Path(a)
         └─ field y: IntLit(0)
```

### 例 17：const 与常量路径
```rust
const N: usize = 4;
struct Cfg;
impl Cfg { const LIMIT: usize = 2; }
fn f() -> () {
    let a: [i32; N] = [1, 2, 3, 4];
    let b = [0; Cfg::LIMIT];
}
```
```
Const(N)  type: usize  value: ConstInt(4)
Impl(Cfg)
└─ Const(LIMIT)  type: usize  value: ConstInt(2)

Fn(f)
└─ Block
   ├─ stmts: Let(a)
   │         ├─ type: Array(elem i32, len Path(N))
   │         └─ Array(IntLit 1, IntLit 2, IntLit 3, IntLit 4)
   └─ stmts: Let(b)
             └─ RepeatArray
                ├─ value: IntLit(0)
                └─ count: Path(Cfg::LIMIT)
```

### 例 18：use 与 lifetime 丢弃
```rust
use rx::core::*;
fn f<'a>(x: &'a i32) -> i32 { *x }
```
```
（use 不产生节点）

Fn(f)
├─ params: x: Ref(&) → Type(i32)     ← lifetime 'a 丢掉，只留 &
├─ ret: i32
└─ body: Block
   └─ tail: Deref
      └─ Path(x)
```

### 例 19：常见语句
```rust
println_i32(c);
return 1;
break i;
continue;
```
```
ExprStmt → Call
           ├─ Path(println_i32)
           └─ Path(c)

ExprStmt → Return → IntLit(1)     // return; 则 Return 无孩子
ExprStmt → Break  → Path(i)      // break; 则 Break 无孩子
ExprStmt → Continue
```

### 例 20：带类型实参的路径
```rust
let mut v = Vec::<i32>::new();
let b = Box::<i32>::new(7);
```
```
Let(mut v)
└─ Call
   └─ Path(Vec<i32>::new)     ← 段 Vec 带类型实参 <i32>，再跟段 new

Let(b)
└─ Call
   ├─ Path(Box<i32>::new)
   └─ IntLit(7)
```

---

## 5. 易错点清单

1. **`if` / `block` / `loop` / `while` 是表达式**，不是"语句节点"；当语句用时包在 `ExprStmt` 里，值被丢弃。
2. **无分号的 `expressionWithBlock` 在块尾 = 块的值**（规范原话），要画在 `Block.tail` 下，不是 `stmts`。
3. 有分号的表达式是语句（`ExprStmt`）；`let` 是 `Let`。
4. 结合性别搞反：二元/cast 左折叠；赋值右结合；一元右递归。
5. `&&x` 画**两层 Ref**；`&mut` 的 mut 标在 Ref 上。
6. `()` 是 `Unit`；`{}` 是 `Block`（空块无 tail）。
7. `p.x` 是 `Field`，`p.m()` 是 `MethodCall`，`p.m` 是 `Field`。
8. `Path` 要画类型实参（`Vec<i32>::new`）；`StructInit` 里也有一个 `Path` 孩子。
9. `use`/lifetime 不画；参数和字段不是 Expr。
10. `[a,b]` 是 `Array`，`[v; N]` 是 `RepeatArray`（N 是 ConstValue，不是表达式）。
11. `if` 的 else 可以是嵌套 `If`（else-if），画成孩子。
12. 没有括号节点：`(x)` 直接透传 `Path(x)`。

---

## 6. 自测 8 题（先画，答案在末尾）

1. `let x = 1u32 + 2u32;`
2. `let c = if z > 0 { 1 } else { 2 };`
3. `v[0] = v[1];`
4. `let a = &mut p.x; let b = *a;`
5. `a - b - c * d;`
6. `loop { break 1 + 2; }`（作为函数体最后一个表达式）
7. `fn f(x: &Vec<i32>) -> usize { x.len() }`
8. `const K: usize = 3; let a: [i32; K] = [7, 8, 9];`

<details>
<summary>答案</summary>

1.
```
Let(x)
└─ Binary(+)
   ├─ IntLit(1u32)
   └─ IntLit(2u32)
```
（无 expected、有后缀，两个都是 u32；运算结果 u32。）

2.
```
Let(c)
└─ If
   ├─ cond: Binary(>)
   │  ├─ Path(z)
   │  └─ IntLit(0)
   ├─ then: Block(tail IntLit(1))
   └─ else: Block(tail IntLit(2))
```

3.
```
ExprStmt
└─ Assign(=)
   ├─ Index
   │  ├─ Path(v)
   │  └─ IntLit(0)
   └─ Index
      ├─ Path(v)
      └─ IntLit(1)
```

4.
```
Let(a)
└─ Ref(&mut)
   └─ Field(x)
      └─ Path(p)

Let(b)
└─ Deref
   └─ Path(a)
```

5.
```
Binary(-)
├─ Binary(-)
│  ├─ Path(a)
│  └─ Path(b)
└─ Binary(*)
   ├─ Path(c)
   └─ Path(d)
```
（`*` 优先级更高，先组成右孩子；`-` 左结合。）

6.
```
Block
└─ tail: Loop
   └─ body: Block
      └─ stmts: ExprStmt
         └─ Break
            └─ Binary(+)
               ├─ IntLit(1)
               └─ IntLit(2)
```
（`break 1 + 2;` 带分号，是循环体里的一条语句；`loop {...}` 作为函数体最后的无分号块状表达式，是外层 `Block` 的 `tail`。如果写成 `loop { break 1 + 2 }` 没分号，`Break` 才会成为循环体的 tail。）

7.
```
Fn(f)
├─ params: x: Ref(&) → Type(Vec) → Type(i32)
├─ ret: usize
└─ body: Block
   └─ tail: MethodCall(len)
      └─ Path(x)
```

8.
```
Const(K)  type: usize  value: ConstInt(3)

Let(a)
├─ type: Array(elem i32, len Path(K))
└─ Array
   ├─ IntLit(7)
   ├─ IntLit(8)
   └─ IntLit(9)
```

</details>

---

## 7. 概念问答准备（可能的简答/口试）

1. **parse tree 和 AST 的区别？**
   parse tree 是文法规则的原样记录：每个规则一次节点、每个 token 一个叶子，包含标点、包装规则、4 套消歧义家族；AST 只保留语义，把 8 条文法规则合并成一个 `Binary` 之类的节点，丢掉标点/use/lifetime。深度从十几层降到两三层的例子：`a + b * 3`。
2. **为什么用 visitor 建 AST？**
   ANTLR 生成 `ParserBaseVisitor`，每个 Context 有 `accept`，后序遍历拿到子树的 `std::any` 结果，父节点把结果存进自己的字段，天然自底向上建树；只需重写关心的规则，其余走默认 `visitChildren`。
3. **为什么具体节点要 `static_cast<基类*>` 再 return？**
   `std::any_cast` 按精确类型匹配；如果返回 `blockExpr*`，父节点 `any_cast<Expr*>` 会抛 `bad_any_cast`。所以统一返回 `Expr*/Type*/Stmt*/Item*/ConstValue*`。
4. **`use` 和 lifetime 为什么可以丢？**
   规范保证：测试里的 `use` 只为兼容参考 rustc，不会靠它引入新名字；lifetime 都合法且不影响类型/运行语义。解析仍要正确（坏语法要报），只是不进 AST/语义。
5. **4 套表达式家族为什么存在？**
   消歧义：条件里不允许无括号结构体构造、语句开头的块边界、`<`/`>>` 与泛型的冲突。它们结构相同，AST 合并处理。
6. **`>`/`>>` 为什么要特殊处理？**
   泛型收尾、比较、移位、复合赋值都用 `>`，lexer 用 mode 切成 `GT`/`GT_SECOND`/`GE_EQ`/`SHR_EQ`，由文法上下文决定含义。
7. **块尾规则？**
   "无分号的 `expressionWithBlock` 在块的最后位置是块的值"；否则值丢弃、类型 `()`。
8. **哪些东西不建节点？**
   括号分组、标点、use、lifetime/where、参数/字段/路径段这些"数据"，以及 `#[derive]`（derive 名字存进 Struct 节点）。

---

## 8. 考前 5 分钟速记

- 管线：g4 → parse tree → visitor → AST。
- 优先级看规则分层；结合性：二元左、赋值右、一元右、后缀左、cast 左。
- `if/block/loop/while` 是表达式；末尾无分号的块状表达式是块尾。
- `&&` 两层 Ref；`&mut` 标 mut；`()` 是 Unit。
- Path 带类型实参；`[v; N]` 是 RepeatArray；`Point{...}` 是 StructInit。
- 六大类、Item 四节点、Stmt 两节点、Use/lifetime 不画。
