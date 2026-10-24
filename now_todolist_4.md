# NOW 4：IR 阶段（LLVM IR）

> 前置：Semantic 已收尾。
>
> - 官方 semantic：`239/239`
> - `tests/unofficial/semantic-edge-cases`：`13/13`
>
> 目标：`rxcc --stage ir` 能从合法 Rx 程序生成可被 Clang/LLVM 22 接受的 RV32IM/ILP32 LLVM IR（`.ll`），并通过 `make test STAGE=ir` 的 IR → Clang → REIMU 流程。
>
> 本文是路线图，不提供完整实现。IR 的数据结构、SSA 构造、指令选择和 helper 拆分自己写。

---

## 0. 先明确两件事

### 0.1 IR 不是“直接生成汇编”

IR 阶段只需要产出 LLVM IR 文本：

```sh
./target/build/rxcc --stage ir input.rx -o output.ll
```

测试运行器随后调用：

```sh
clang-22 -S -x ir output.ll \
  -o program.s \
  --target=riscv32-unknown-none-elf \
  -march=rv32im -mabi=ilp32 -O0
```

再用 REIMU 执行 `program.s`。所以 IR 阶段**不需要自己的 RISC-V backend**；但 CodeGen 阶段需要在同一份 IR 上做自己的 backend，因此不要把 IR 只做成“拼字符串”，最好保留结构化的 Function/BasicBlock/Instruction，再提供一个 LLVM 文本打印器。

### 0.2 IR 需要 semantic 信息

IR lowering 需要知道：

- 每个表达式的 semType（尤其 i32/u32/isize/usize 的 signedness、引用 referent、数组元素、Box/Vec 元素）；
- 每个 call/method 最终调用的是哪个函数/方法/内建；
- 每个 field 属于哪个 struct、字段序号；
- 每个 path 是局部变量、常量还是关联常量；
- 每个 let/参数的局部槽位（可由 IR 自己维护作用域，但类型必须来自 semantic）。

现在 `Checker` 的 `infer/expect` 不保存表达式类型，`main.cpp` 也会丢弃 `Checker`。**这是 IR 的第一个前置任务**，不要跳过。

---

## 1. Semantic 信息导出（先做）

推荐做一个独立的 `SemanticInfo` / `IRContext`，由 `main.cpp` 在 semantic 成功后传给 IR：

```text
struct SemanticInfo {
    globalTable table;                       // 函数/struct/常量签名
    unordered_map<const Expr*, semType*> exprType;
    unordered_map<const Expr*, ResolvedCall> resolvedCall;   // call/method/构造
    unordered_map<const fieldExpr*, FieldInfo> resolvedField; // owner + field index
    unordered_map<const pathExpr*, ConstRef> resolvedConst;   // 常量值/owner
};
```

最小可行方案：

1. 在 `Checker` 里加 `std::unordered_map<const Expr*, semType*> exprTypes_`；
2. 在 `infer` 每个 `return`、`expect` 每个成功返回点登记类型；
3. 在 `callExpr/methodCallExpr` 解析成功后登记 `ResolvedCall`；
4. 在 `fieldExpr` 登记 `ownerStruct + fieldIndex`；
5. 在 `pathExpr` 解析到常量时登记 `ConstRef`；
6. 暴露 `const globalTable&` 和这些 map；
7. `main.cpp` 保留 `Checker`（或构造 `SemanticInfo`）直到 IR 生成结束。

注意：

- `expect` 成功时通常返回 **wish**；但 IR 需要的实际类型语义要一致。对于 `&mut → &` coercion，IR 的表达式结果仍是原地址，类型按 target 处理即可。
- `break` 的 never 值、unreachable 表达式、ERROR 不进入合法 IR。
- `Self`、`Type::CONST`、`Self::CONST` 的解析信息最好在 semantic 阶段确定，不要到 IR 再查一遍。
- 如果不想改 AST，可以把所有信息放在 `SemanticInfo`，不要给 AST 加 semantic 依赖。

### 1.1 局部槽位

IR lowering 可以自己维护 `std::map<string, Alloca>` 和 block scope：

- 参数进入函数时分配 alloca 并 store；
- `let` 初始化先求值，再分配/store，再 push；
- block 结束 pop；
- `pathExpr` 先查局部 map，再查常量表；
- 同名 shadowing 用 IR 自己的栈即可，不需要 semantic 的 localId。

这样 semantic 只需要提供**类型和解析结果**，不需要提供槽位编号。

---

## 2. IR 目标与类型表示

目标：

```text
triple = "riscv32-unknown-none-elf"
data layout = 32-bit pointers, little endian, i32 32-bit
```

LLVM 22 使用 opaque pointer：引用、Box 内容地址统一用 `ptr`。

| semType | LLVM 类型 |
|---|---|
| `i32` / `isize` | `i32`（运算按 signed 选指令） |
| `u32` / `usize` | `i32`（运算按 unsigned 选指令） |
| `bool` | SSA 值用 `i1`；内存字段/数组元素用 `i8` |
| `&T` / `&mut T` | `ptr` |
| `Box<T>` | `ptr`（指向 T 的存储） |
| `Vec<T>` | `%Vec = type { ptr, i32, i32 }`（data/len/cap） |
| `[T; N]` | `[N x T]` |
| struct | named type `%struct.Name` |
| `()` | `void`（返回值/无值表达式）；不要作为普通 SSA 值 |
| `!` | `unreachable` |

### 2.1 Struct 布局

按声明顺序计算：

- 每个字段从满足自身 alignment 的第一个偏移开始；
- struct alignment = 最大字段 alignment；
- struct size 向上取整到 alignment；
- `bool` 占 1 字节，`&T`/`Box<T>`/`Vec` 占 4 字节；
- `[T; N]` size = size(T) * N，alignment = alignment(T)。

LLVM 里用 named struct，字段顺序与源码声明顺序一致。同时需要一个 C++ 侧的 `sizeOf(semType*)` / `alignOf(semType*)`，用于 `__rx_alloc`、数组 stride、Vec 元素移动；必须与 LLVM 布局一致。

递归 struct 已经在 semantic 布局检查里确认有限大小；IR 计算 size/align 时仍要用 `struct name -> size/align` 的缓存，遇到 `Box/Vec/REF` 不展开。

### 2.2 Vec

建议统一：

```llvm
%Vec = type { ptr, i32, i32 }   ; data, len, cap
```

- `Vec::<T>::new()`：`{ null, 0, 0 }`
- `len`：load len
- `is_empty`：`len == 0`
- `push`：必要时增长；元素存储地址 = data + len * size(T)，然后 len++
- `remove(i)`：把元素 load 到临时，`memmove` 后面的元素，len--，返回临时
- `v[i]`：GEP data + i*size(T)，再 load/store
- `Vec<T>` 本身没有自动 Deref，不能把 Vec 当数组或元素引用

增长策略自定（例如 cap 从 4 开始、翻倍）；测试不检查容量。`__rx_alloc`/`malloc` 返回的存储必须至少 size 字节且对齐。

### 2.3 Box

`Box<T>` 在 IR 里是 `ptr`：

- `Box::<T>::new(v)`：`__rx_alloc(size(T), align(T))`，把 v store 进去，返回指针；
- `*b`：从 Box 的指针 load T；如果 b 本身是局部变量，先 load 出 ptr；
- `Box<T>` 的移动就是复制指针；
- `Box<T>::clone`：分配新 T 存储，递归 clone T，返回新指针；
- `Box<T>` 的 `==`：比较 `*lhs` 和 `*rhs` 的结构值。

### 2.4 bool 存储

建议内存统一用 `i8`，SSA 用 `i1`：

- load：`load i8` 后 `trunc i8 to i1`；
- store：`zext i1 to i8`；
- struct/array 里 bool 字段是 i8；
- `==/!=` bool 用 i1；
- bool → int cast 用 `zext i1 to i32`。

### 2.5 Unit / Never

- `()` 不作为普通值；函数返回 `void`；unit 表达式不产生 SSA；
- `if`/`block`/`loop` 结果为 unit 时不生成 phi，只生成控制流；
- never 分支生成 `unreachable`。

---

## 3. 代码生成骨架

推荐目录：

```text
compiler/src/ir/
├── ir.h/.cpp          // Module/Function/BasicBlock/Value/Instruction
├── builder.h/.cpp     // 创建 alloca/load/store/arithmetic/call/...
├── lower.h/.cpp       // AST -> IR（表达式、语句、item）
├── layout.h/.cpp      // size/align/field offset
└── emit.h/.cpp        // IR -> LLVM textual .ll
```

`main.cpp`：

```text
--stage semantic : 维持现状
--stage ir       : parse → AST → semantic → lower → write {output}
```

IR 生成失败（semantic reject）时 exit 1，不写半成品 `.ll`。

### 3.1 栈槽基线

先做 correctness：

- 每个局部变量、参数、临时值都 `alloca` 在函数入口；
- 表达式求值结果用 SSA 临时；
- 赋值统一 load/store；
- 不要求 mem2reg；
- 不要求优化。

后续 backend 可以直接消费同一份 IR；如果以后做 SSA 优化再改。

### 3.2 两个核心函数

```text
Value emitValue(Expr*)       // 读值；place 则 load
Value emitPlace(Expr*)       // 返回地址 ptr；不 load
```

- `pathExpr`：局部 alloca 或常量；
- `*p`：emitValue(p) 得到 ptr，作为 place；
- `fieldExpr`：emitPlace(from) + GEP 字段；
- `indexExpr`：数组或 Vec 的 data + GEP；
- `refExpr`：`&place` → emitPlace；对临时值 materialize alloca；
- `structInitExpr`：按字段类型构造 aggregate；
- `arrayExpr`：构造 `[N x T]`；
- `repeatedArrayExpr`：value 只求值一次，重复 store/insertvalue（N>1 必须 Copy，semantic 已检查）。

### 3.3 临时值

需要 place 但操作数是 value 时（`make().field`、`make_array()[i]`、`&mut 3`、方法 receiver）：

1. 先求值一次；
2. 分配 alloca；
3. store；
4. 把 alloca 当地址使用。

临时生命周期可以先简单地 alloca 到函数入口、保留到函数结束；测试不检查析构，1 MiB stack 够用。以后需要再缩短。

---

## 4. 表达式 lowering

### 4.1 字面量

- 整数字面量：按 semType 生成 i32；suffix 决定 signedness，但 LLVM 存储同是 i32；
- bool 字面量：i1；
- unit：不产生值；
- 常量路径：直接使用 semantic 求出的 Constant 值；
- `ConstValue` 里的负号、radix、下划线已在 semantic 求值，IR 不再解析源码字面量。

### 4.2 运算符

- `+ - *`：`add/sub/mul`，不要加 nsw/nuw（wrapping）；
- `/ %`：signed 类型用 `sdiv/srem`，unsigned 用 `udiv/urem`；
- `& | ^`：`and/or/xor`；
- `<< >>`：先对右操作数 `and 31`，再 `shl` / `ashr`（signed）或 `lshr`（unsigned）；
- unary `-`：`sub 0, x`；`!`：整数用 `xor x, -1`，bool 用 `xor i1 x, true`；
- 比较：signed 用 `icmp slt/sle/sgt/sge`，unsigned 用 `icmp ult/ule/ugt/uge`；bool 用 `icmp eq/ne`；
- `&& ||`：短路基本块；
- `== !=`：按 semType 生成递归相等；
  - 标量：`icmp eq/ne`；
  - `&T` / `&mut T`：比较 referent 的值，不是地址；
  - struct：逐字段比较，忽略 padding；
  - array：逐元素；
  - Box：比较 `*p`；
  - Vec：先比较 len，再逐元素；
- cast：
  - int→int：LLVM 都是 i32，通常无需指令；类型只影响后续 signed 选择；
  - bool→int：`zext i1 to i32`；
- 不允许的 cast 已在 semantic 拒绝。

### 4.3 赋值和复合赋值

- `lhs = rhs`：先求 rhs，再求 lhs 地址，store；
- 复合赋值：按 spec 先求 rhs（含引用操作数），再求 dest 地址，load 旧值，执行运算，store；
- `&mut` 复合赋值语义上先解引用得到标量，不要直接把引用类型当 dest；
- 赋值结果 unit。

### 4.4 引用

- `&place` / `&mut place`：emitPlace，得到 ptr；
- 引用 coercion（`&mut T → &T`、`&Box<T> → &T`、`&mut Box<T> → &mut T`）在 IR 里通常只是同一个地址，类型按 target 处理；
- `&mut` reborrow 也没有额外运行时操作；
- 引用相等/排序按 referent 处理；
- `&T` Clone：复制指针；
- `&mut T` 没有 Clone。

### 4.5 块、if、while、loop

- block：顺序 lower；tail 作为值；无 tail 且不可达 → unreachable；
- if：cond br；有 else 两分支 phi（非 unit）；无 else 的 unit 结果；
- while：
  - cond block 求 bool；
  - body block；false 跳 exit；
  - break 跳 exit；continue 跳 cond；
- loop：
  - body block；
  - break 的值写入 merge slot 或 phi；
  - continue 跳 body 起点；
  - 无 break 的 loop 后面 `unreachable`；
- `return`：store 返回值（如有）并 br 到统一 return block；
- `!` 表达式：`unreachable`；
- `break/continue` 的嵌套目标由 lower 的 loop 栈决定，semantic 已保证合法。

### 4.6 调用

- 自由函数：直接 `call @mangled`；
- `Type::assoc(...)` / `Self::assoc(...)`：按 semantic 解析结果调用；
- 方法：receiver 作为显式第一个参数：
  - by-value `self`：传入 aggregate/指针的值（按内部约定）；
  - `&self` / `&mut self`：传入发射好的地址；
  - autoref/deref 的语义已由 semantic 选择，IR 只需按 `ResolvedCall` 执行；
- default/never 返回值：`call void`；
- 参数/返回值都是 Rx 类型；aggregate 可以按 LLVM aggregate by value 传，或统一改成 pointer + copy，必须先定一种内部 ABI；
- 递归、互递归直接 call。

### 4.7 struct / array / aggregate

- struct init：按字段类型构造 aggregate，可以用 `insertvalue` 或先 alloca 再逐字段 store；
- struct/array 按值赋值：load/store aggregate（LLVM 会复制）；
- 函数 by-value 参数/返回：内部 ABI 要保证 caller 的修改不影响 callee 的副本；
- 字段访问：GEP；数组索引：GEP；
- 整个 struct/array 的 `PartialEq`：逐字段/逐元素；
- struct 字段值读取直接 load；`Box`/引用自动解引用在 semantic 已决定，IR 按表达式类型 emitPlace/emitValue。

---

## 5. Box / Vec / builtin 运行库

### 5.1 运行时声明

如果直接使用 REIMU libc（推荐，BUILD 的 `{runtime}` 可以留空）：

```llvm
declare i32 @get_i32()
declare void @print_i32(i32)
declare void @println_i32(i32)
declare ptr @__rx_alloc(i32, i32)
declare ptr @malloc(i32)
declare ptr @memcpy(ptr, ptr, i32)
declare ptr @memmove(ptr, ptr, i32)
```

两种实现方式二选一：

1. 在生成的 IR 里定义 `get_i32/print_i32/println_i32`，内部调用 `scanf/printf`；用全局格式字符串；
2. 在 `config.mk` 的 `BUILD` 里把参考 C runtime 编译成 `{runtime}` 汇编。

不要在 IR 阶段假设 `get_i32` 等已经存在；如果没有 runtime.s，就必须自己定义或直接调用 libc。

### 5.2 `__rx_alloc`

可以：

- 直接调用 `malloc`（REIMU 的对齐满足 1/4；Box/Vec 只需要 1/4）；
- 或者生成一个 `__rx_alloc` wrapper，内部 `malloc`，忽略 align 参数；
- 不要在编译器里自己维护堆分配器，除非你想在 backend 阶段一起处理。

### 5.3 `Vec` 操作

- `new`：零值结构；
- `len`/`is_empty`：load len；
- `push`：
  - 若 len == cap：newCap = max(4, cap*2)；
  - `__rx_alloc(newCap * size(T), align(T))`；
  - `memcpy` 旧 len 个元素；
  - 更新 data/cap；
  - 把 value store 到 data + len*size；
  - len++；
- `remove`：
  - load data[i] 到临时；
  - `memmove` data+i+1 到 data+i，长度 `(len-i-1)*size`；
  - len--；
  - 返回临时；
- `v[i]`：GEP；越界不检查；
- `Vec<T>` 的 `clone`：alloc len*size（或 cap），递归 clone 每个元素，返回 `{newData, len, len}`；
- `Vec<T>` 的 `==`：len 相等且逐元素。

### 5.4 `Box` 操作

- `new`：alloc size(T)，store T，返回 ptr；
- `*b`：load ptr；T 为 aggregate 时 load aggregate；
- `clone`：alloc T，递归 clone，返回新 ptr；
- `==`：比较 `*lhs` 与 `*rhs`。

### 5.5 derived Clone / PartialEq / Eq

不要在每个使用点无限展开递归结构。建议：

- 为需要的 `(type, Clone)` / `(type, PartialEq)` 生成 mangled helper；
- helper 内部递归调用自身处理 struct/Vec/Box；
- `Copy` 不产生方法，复制直接 load/store；
- `&T` clone 只复制指针；
- `&mut T` 不能 clone；
- struct clone 逐字段；
- struct equality 逐字段；
- array 逐元素；
- Vec/Box 按容器规则；
- 递归类型（`Node { children: Vec<Node> }`）的 helper 会递归调用自身，运行时由数据终止。

这部分也可以先在 lowering 里内联展开；如果代码膨胀明显，再抽 helper。

---

## 6. main / ABI / 符号

- 源 `main` 生成内部 mangled 函数（如 `_rx_main`），返回 void；
- 导出：

```llvm
define i32 @main() {
  call void @_rx_main()
  ret i32 0
}
```

- 源函数之间的调用使用内部 mangled 符号；
- I/O 和 `__rx_alloc` 使用 C ABI；
- 外部边界遵守 psABI：
  - 标量参数 a0-a7，返回值 a0；
  - 栈 16 字节对齐；
  - callee-saved s0-s11；
- 所有 struct/array 的 by-value 约定必须先固定，IR 和 backend 一致；
- 不要让源函数 `main` 直接占用 machine `main` 符号，否则递归调用和入口返回语义都会错。

---

## 7. 建议的里程碑

每完成一个 milestone 跑对应 suite，IR 阶段使用：

```sh
make test STAGE=ir FILTER=official:codegen:<suite>
```

### M0：骨架 + I/O + 字面量/let

- `--stage ir -o`
- module/triple/data layout
- declare/define、main wrapper
- 整数/bool/unit 字面量，局部 alloca/load/store
- `print_i32` / `println_i32`
- 测试：`builtin-io`, `casts-and-literals`, `names-and-shadowing`

### M1：运算符 + 控制流

- 算术/位运算/移位/比较/逻辑
- cast
- if/while/loop/break/continue/return
- 测试：`integer-arithmetic`, `shifts`, `boolean-and-short-circuit`, `compound-assignment`, `blocks-if-and-never`, `loops-and-jumps`, `loop-state-merges`

### M2：函数、调用、递归、方法

- 函数签名/参数/返回
- 自由函数调用、递归
- 两段关联调用、方法调用、receiver 调整
- 测试：`calls-recursion-and-abi`, `methods-and-self`, `source-main-calls`, `live-state-calls`

### M3：数组、struct、字段、索引

- struct layout/init/field
- array literal/repeat/index
- aggregate by-value 参数/返回/赋值
- 测试：`arrays`, `structs-and-fields`, `aggregate-arguments-and-reference-fields`, `nested-containers`

### M4：引用、place、临时值、coercion

- address/place lowering
- `&`/`&mut`/`*`
- 临时 materialize
- assignment/compound 的右先左后
- 测试：`references-and-mutability`, `reference-coercions`, `reference-lub`, `evaluation-order-and-temporaries`, `vec-index-mutability`

### M5：Box/Vec 与容器方法

- Box/Vec 表示、alloc、push/remove/len/is_empty/index
- Box/Vec clone/equality
- 测试：`box-and-moves`, `vec-operations`, `nested-containers`, `runtime-value-merges`

### M6：derive 能力与综合

- Clone/PartialEq/Eq helper
- Copy 的值语义
- 测试：`copy-clone-and-equality`, `recursive-traits`, `trait-dispatch-and-reference-equality`, `constants-and-paths`, `comprehensive-*`

---

## 8. 调试与验收

### 8.1 替换 IR 命令

当前 `config.mk` 的 `IR` 还是参考 rustc。等自己的 IR 能跑通第一个 suite 后，再改成：

```make
IR = ./target/build/rxcc --stage ir {source} -o {output}
```

在 IR 没稳定前不要换，否则所有 IR 测试都会红。

### 8.2 单独验证

```sh
./target/build/rxcc --stage ir test.rx -o test.ll
clang-22 -S -x ir test.ll \
  -o test.s --target=riscv32-unknown-none-elf \
  -march=rv32im -mabi=ilp32 -O0
xmake run -P vendor/REIMU reimu --memory=256M --stack=1M \
  -f test.s,runtime.s -o test.out
```

先只要求 Clang 能接受 IR，再要求 REIMU 输出正确。

### 8.3 常见错误

- bool 用 i32/i8 混用导致 `icmp` 类型错；
- signed 类型用了 `udiv/lshr`，或 unsigned 用了 `sdiv/ashr`；
- shift 没 mask 低 5 位；
- struct/array by-value 参数/返回只传了指针，callee 修改污染 caller；
- `Vec` 索引忘了 `i * size(T)`，或 `push` 增长后旧引用问题（测试保证不出现）；
- 临时值重复求值（`make_array()[0]` 调了两次）；
- `&mut` coercion 多做了 load，结果变成值而不是地址；
- `main` wrapper 没导出 machine `main`，或源 `main` 和 machine `main` 重名；
- 递归 Clone/PartialEq helper 在 `Vec<Self>` 上无限生成；
- struct padding 参与 equality；
- 忘记声明 `__rx_alloc`/`memcpy`/`memmove`；
- LLVM aggregate 太大导致栈/寄存器问题。

### 8.4 完成标准

```sh
make test FILTER=official:semantic STAGE=semantic   # 239/239
make test STAGE=ir                                  # IR 全部通过
make test FILTER=unofficial STAGE=semantic          # 13/13
```

IR 全绿后，再进 CodeGen：用自己的 backend 从同一份结构化 IR 生成 RV32IM 汇编，而不是继续依赖 Clang。
