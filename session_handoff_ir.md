# Rx 编译器 IR 阶段交接文档

> 用途：开新 session 时，让新 assistant 读这个文件即可接上 IR 进度。
> 当前阶段：Semantic 已完成，IR 刚开始。下一步是函数返回值、return、if/while/loop。
>
> **新 session 第一句话建议直接输入：**
>
> ```
> 读 session_handoff_ir.md、now_todolist_4.md、llvm_man.md 和
> compiler/src/ir/generator.h、compiler/src/ir/generator.cpp，
> 继续写 IR。我写代码为主，你负责 review/解释/按需小改；
> 遵守 rule_explanation.md，不要连续生成超过 300 行代码。
> 先看当前状态，然后从函数返回值和 return/if/while/loop 开始。
> ```

---

## 1. 项目与协作规则

- 仓库：`/mnt/c/Users/Lenovo/Desktop/大二上/Compiler/rx-compiler`
- 目标：Rx（Rust 子集）编译器。
- 阶段：AST 已完成 → Semantic 已完成 → **IR（当前）** → CodeGen → Optimize。
- 用户自己写代码为主；assistant 负责 review、解释、指出 bug，只有明确说“帮我改/修”时才改文件。
- `rule_explanation.md`：连续超过 300 行（不含空行）的 AI 生成代码违规。**任何修改都要小段、可解释**；不要一次性重写大文件。
- CI/CD 从下次 Review 起必须有记录；当前 CI 配置在 `.github/workflows/test.yml`。

## 2. 当前测试状态

Semantic 全绿：

```sh
make test FILTER=official:semantic STAGE=semantic
# 239 passed
```

自定义边界用例：

```sh
make test FILTER=unofficial STAGE=semantic
# 13 passed
```

`tests/unofficial/semantic-edge-cases/` 覆盖：
- `&mut T` / `&mut Box<T>` / `&mut S` 的 `.clone()`
- `Box<S>` / `&mut S` 通过解引用调用显式 `clone`
- `Box<&mut S>` 的字段/方法可变访问
- `Box<&mut Vec<T>>` 的 push/index
- 所有 break 都是 never 的 loop
- 方法 lifetime 参数、struct lifetime 参数
- 比较不通过 Box 自动解引用、嵌套 mut 不匹配
- 复合赋值拒绝 `&mut` 右操作数
- struct/常量路径/方法段的类型实参
- 非 Clone `&mut T` 的 clone
- `main<'a>()`

这些用例已经提交；`tests/official` 已更新到 `1abfeae`。

## 3. 当前构建（OJ 已通过）

```sh
cmake --build target/build -j1
```

- `config.mk` 的 BUILD 已改成 `cmake --build target/build -j1`，避免 OJ 并行 OOM。
- `compiler/CMakeLists.txt`：
  - 默认使用仓库内 `compiler/generated/` 的 ANTLR 源码，**不依赖 Java**；
  - 只有 `-DRX_REGENERATE_ANTLR=ON` 时才找 Java 并重新生成；
  - generated 源文件用 `-O0` 编译，降低内存。
- `compiler/generated/` 已经提交，不要删除。
- OJ 上曾因为 `Param::isMut_` 未初始化、entry 检查位置错误、`scopeStack` 模板、Java/OOM 等问题失败，均已修复。

## 4. 工具环境（本机已装好）

- **Clang 22.1.8**：用户级安装
  - wrapper：`~/.local/bin/clang-22`
  - 实际前缀：`~/.local/llvm22/root`
  - wrapper 会自动设置 `LD_LIBRARY_PATH`，并已补 `libz3`。
- **xmake 3.1.2**：`~/.local/bin/xmake`
  - shell integration 在 `~/.bashrc`；
  - 新终端如果找不到：`source ~/.xmake/profile`
- **REIMU**：已编译在 `target/reimu`
  - 可执行文件：`target/reimu/linux/x86_64/release/reimu`
- Rust target `riscv32im-unknown-none-elf` 已安装，`target/reference/.../librx.rlib` 存在。

### 4.1 当前参考 IR 命令不可用（已知）

`config.mk` 的 `IR` 仍是模板的 reference rustc 命令，`make test STAGE=ir` 会报：

```text
error: failed to get bitcode from object file for LTO (Can't find section .llvmbc)
```

这是 rustc 1.97 + reference rlib 的 LTO 兼容问题，**不是 Clang/REIMU 没装好**。自己的 IR 测试目前走手动命令；等自己的 IR 命令稳定后再改 `config.mk` 的 `IR`。

## 5. Semantic 交接要点

完整 semantic 细节见：

- `session_handoff_semantic.md`
- `now_todolist_3.md`

当前实现文件：

```text
compiler/src/semantic/type.h      semType + typeEqual + 工厂
compiler/src/semantic/symbol.h    funcSignature/structSignature/constSignature
                                  localShadowing/stackShadowing/scopeStack（已模板化）
compiler/src/semantic/checker.h   Checker 接口 + IR 用 exprType_/table() getter
compiler/src/semantic/checker.cpp collect/常量/类型/check 全部
```

近期重要修复：

- `Param::type_/isMut_/isSelf_` 都有默认初始化；`visitSelfParam` 显式设置 `isMut_`。
- `scopeStack` 改成 `template<typename L, typename T>`；Checker 使用 `scopeStack<localShadowing, stackShadowing>`。
- entry 检查在 `evaluateTypes` 之后（`checkEntry`），不能在 collect 里读尚未填写的 `retType_`。
- 不可达 tail：block 在到达 tail 前已不可达时，整块类型是 never。
- loop：所有 break 操作数都是 never 时，即使有 expected type 也返回 never。
- 比较：引用层必须匹配；额外只允许左边 `&T`、右边 `&mut T` 且 referent 相同；不通过 Box 自动解引用。
- 复合赋值 RHS 只允许 scalar 或一层 shared `&`；`&mut` 拒绝。
- clone 候选：`&mut T`、`Box<&mut T>`、`Box<显式 clone>` 等都能处理。
- `Checker` 有：
  ```cpp
  const globalTable& table() const;
  const std::map<Expr*, semType*>& exprTypes() const;
  ```
  `infer/expect` wrapper 会把每个表达式类型写入 `exprType_`。

## 6. IR 当前进度（重点）

### 6.1 文件

```text
compiler/src/ir/generator.h
compiler/src/ir/generator.cpp
llvm_man.md              LLVM IR 语法手册（用户可查）
now_todolist_4.md        IR 阶段完整路线图
```

`generator.cpp` 目前约 260 行，**只是刚开始**：

- `IRValue { std::string name_; semType* type_; }`
- `IRStackShadowing`
- `IRGenerator`：
  - `generateCrate/Item/Function/Block/Statement/Expr`
  - `transformType`
  - `newLable/newTemp`
  - `putline/allocate`
  - `scope_` 使用 `scopeStack<std::string, IRStackShadowing>`
  - `tempCounter_`

### 6.2 已经能做什么

- `main.cpp` 支持：
  ```sh
  ./target/build/rxcc --stage semantic <source>
  ./target/build/rxcc --stage ir <source> -o out.ll
  ```
- 语义失败时 IR 阶段 exit 1；语义成功才生成 IR。
- 最小 IR：
  - `target triple`
  - I/O 定义（`get_i32/print_i32/println_i32`，内部用 `printf/scanf`）
  - 顶层函数 `define`
  - `main` wrapper（源 main 改名为 `rx_main`）
- `generateStatement`：
  - `let`：`generateExpr` → `alloca` → `store` → 入作用域
  - `exprStmt`
- `generateExpr` 已支持：
  - `intLit`（含 suffix/进制/下划线，`intLiteralFromString`）
  - `boolLit`
  - `unit`
  - 单段 `pathExpr` 局部 load
  - `binary`：`+ - * / % & | ^`（`+` 已能生成 `add i32 ...`）
  - 单段 `callExpr`（`println_i32`、用户函数；callee 名已修正）
- `generateFunction`：
  - 仅顶层函数（`owner==""`）
  - 参数 alloca/store
  - 参数 scope + body block scope
  - 末尾固定 `ret void`
- `generateBlock`：
  - 自己 `levelUp/levelDown`
  - 函数参数在函数层，函数体顶层局部在 body 层

### 6.3 已验证的 IR smoke

```sh
printf 'use rx::core::*;\nfn main() { let x = 1 + 2; println_i32(x); }\n' > /tmp/t.rx
./target/build/rxcc --stage ir /tmp/t.rx -o /tmp/t.ll
clang-22 -S -x ir /tmp/t.ll \
  --target=riscv32-unknown-none-elf -march=rv32im -mabi=ilp32 -O0 \
  -o /tmp/t.s
xmake run -P vendor/REIMU reimu --memory=256M --stack=1M \
  -f /tmp/t.s,/tmp/empty.s -o /tmp/t.out
cat /tmp/t.out
# 输出: 3
```

I/O 定义现在已直接写进 `.ll`；**不要再给 `get_i32/print_i32/println_i32` 加 `declare` 再 `define`**，Clang 22 会报 `invalid redefinition`。只保留 `printf/scanf` 的 `declare`。

### 6.4 当前 IR 的明确缺口

按优先级：

1. **函数返回值**
   - `generateFunction` 目前固定 `ret void`，非 void 函数非法。
   - 需要用 block tail 的 `IRValue` 生成 `ret <type> <value>`。
   - `returnExpr` 未实现；需要支持 `return e` / `return`。
   - never 路径需要 `unreachable`。

2. **控制流**
   - `ifExpr`、`whileExpr`、`loopExpr`、`breakExpr`、`continueExpr` 未实现。
   - 需要 block/label/phi 或 alloca 合并。
   - loop 的 break 值、continue 目标、while 的 cond/body/exit 都未做。

3. **更多表达式**
   - `unaryExpr`（`-`、`!`）
   - `castExpr`
   - 比较 `< <= > >= == !=`
   - `&& ||` 短路
   - `blockExpr` 作为表达式
   - `fieldExpr/indexExpr/structInitExpr/arrayExpr/repeatedArrayExpr`
   - `methodCallExpr`
   - 常量路径、`Self::`、`Type::CONST`
   - 二段 path call / 方法 / 内建 Box/Vec

4. **赋值**
   - `generateStatement` 目前没有 `x = ...`、`x += ...`。
   - 需要 `generatePlace(Expr*)`：path/field/index/deref。
   - 普通赋值：RHS 先求值，再算目的地址，store。
   - 复合赋值：RHS、目的地址、load、运算、store。

5. **类型系统**
   - `transformType` 目前只支持：
     - 整数 → `i32`
     - `bool` → `i1`
     - `()` → `void`
   - 还缺 `ptr`（引用/Box）、`[N x T]`、struct、Vec、layout、size/align。
   - bool 存内存时按手册建议用 `i8`，SSA 用 `i1`；当前简化版 alloca `i1`，后续要统一。

6. **函数/方法/ABI**
   - 只有顶层普通函数；没有 impl 方法、receiver、方法查找。
   - 源 main mangled 成 `rx_main`；wrapper 已正确。
   - 参数按 alloca/store；非 void 返回值未做。
   - 用户函数前向调用：Clang textual IR 允许先调用后定义（已验证），暂时不必强制 declare；但不要给已定义函数再写 `declare`（会重定义）。

7. **Box/Vec/derive**
   - 完全没有：Box/Vec 表示、alloc/malloc、push/remove/grow、clone/equality、derive helper、数组/struct/字段布局。

## 7. 下一步建议（新 session 从这里开始）

目标：先让 IR 支持 `builtin-io` 和基本算术/分支/循环，再扩大。

建议顺序：

1. **函数返回 + return**
   - 修改 `generateFunction`：用 `sig.retType_` 和 body tail 返回值生成 `ret`。
   - 在 `generateExpr` 加 `returnExpr`。
   - 必要时给非 void 函数加统一 return block 或简单 tail return。

2. **`blockExpr` 作为表达式**
   - `generateExpr(blockExpr*)` 调 `generateBlock`，返回 tail 值。

3. **if/while/loop/break/continue**
   - 生成 label/br；有值 if/loop 用 alloca 或 phi。
   - `break` 跳到 loop exit；`continue` 跳到 cond/body。
   - while 条件 bool。

4. **unary/cast/比较/逻辑**
   - `-`：`sub 0, x`
   - `!`：bool `xor i1, true`；整数 `xor -1`
   - cast：int-int 通常无指令，bool→int `zext`
   - 比较：signed/unsigned 选择；结果 i1
   - `&&/||`：短路 block

5. **赋值/place**
   - 先做局部 path 的 `=`，再做 field/index/deref。
   - 复合赋值需要类型 signedness。

6. **之后**
   - 用户函数调用/前向调用
   - 数组/struct/字段/索引
   - Box/Vec
   - 方法/impl
   - derive clone/equality

不要一次做 Box/Vec；先把 M1/M2 的小目标跑通。

## 8. 当前 git 状态（交接时）

```text
M compiler/src/main.cpp
M compiler/src/semantic/checker.cpp
M compiler/src/semantic/checker.h
M compiler/src/semantic/symbol.h
?? compiler/src/ir/
?? llvm_man.md
?? session-ses_eabd.md
```

- `now_todolist_4.md`、`tests/unofficial/`、`compiler/generated/` 已经在 git 中。
- `compiler/src/ir/` 和 `llvm_man.md` 还未提交；新 session 不要假设它们已提交。
- `session-ses_eabd.md` 是当前会话导出，忽略。

## 9. 常用命令

```sh
# 编译
cmake --build target/build -j1

# semantic 回归
make test FILTER=official:semantic STAGE=semantic
make test FILTER=unofficial STAGE=semantic

# 手动 IR smoke
./target/build/rxcc --stage ir /tmp/t.rx -o /tmp/t.ll
clang-22 -S -x ir /tmp/t.ll \
  --target=riscv32-unknown-none-elf -march=rv32im -mabi=ilp32 -O0 \
  -o /tmp/t.s
: > /tmp/empty.s
xmake run -P vendor/REIMU reimu --memory=256M --stack=1M \
  -f /tmp/t.s,/tmp/empty.s -o /tmp/t.out

# 查看 Clang 错误
clang-22 -S -x ir /tmp/t.ll --target=riscv32-unknown-none-elf \
  -march=rv32im -mabi=ilp32 -O0 -o /tmp/t.s
```

## 10. 已知坑

- `declare` + 同名 `define` 在 Clang 22 会报 `invalid redefinition`；I/O 只保留定义。
- `putline` 内部才应该 `out_ <<`；业务代码用 `putline/allocate`。
- `generateFunction` 固定 `ret void`，非 void 函数暂时非法。
- `generateExpr` 缺 unary/cast/比较/短路/控制流/字段/索引/方法。
- `generateStatement` 缺赋值/复合赋值。
- `transformType` 只支持 scalar/unit；bool SSA `i1`、内存 `i8` 还没统一。
- 临时值/地址：现在只支持局部 path；没有 `generatePlace`。
- `exprType_` 里记录的是 `expect` 后的目标类型；IR 大多数情况需要这个，少部分需要源类型时再考虑加 map。
- `make test STAGE=ir` 现在仍走 reference rustc，不能用来测自己的 IR；先手动跑。
- 改动前先看 `git diff` 和 `git status`，不要覆盖未提交的 semantic 修复。
