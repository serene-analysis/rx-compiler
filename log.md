# 调试日志（log.md）

> 记录本项目已遇到并修复的报错，方便复习和 Code Review。
> 格式：现象 → 原因 → 修复 → 教训。

## 一、环境与构建

### 1. `git status` 显示整个仓库都被修改
- 现象：17 个 tracked 文件全部 modified，diff 每行既加又删。
- 原因：Windows 侧 Git 把 LF 签出成了 CRLF。
- 修复：`git config core.autocrlf input`；建议加 `.gitattributes`（`* text=auto eol=lf`）。
- 教训：WSL + `/mnt/c` 下先检查换行符，再谈其他改动。

### 2. VSCode IntelliSense 报 `antlr4-runtime.h: No such file or directory`
- 现象：能编译，但编辑器标红。
- 原因：IntelliSense 不看 CMake 的 `-I`，只看 `.vscode/c_cpp_properties.json`。
- 修复：includePath 加 `${env:HOME}/antlr4-4.13.1/include/antlr4-runtime`。
- 教训：编辑器报错和构建报错是两套系统。

### 3. VSCode CMake 插件 configure 报 `Could not find antlr4-runtime`
- 现象：`make test` 能构建，插件不行。
- 原因：插件自己 configure，不带 `config.mk` 里的 `-DCMAKE_PREFIX_PATH`；且 `build/CMakeCache.txt` 残留 `antlr4-runtime_DIR=NOTFOUND`。
- 修复：CMakeLists 里 `find_package(... HINTS "$ENV{HOME}/antlr4-4.13.1")`；删 `build/` 重新 configure；`.gitignore` 加 `/build/`。
- 教训：缓存里的 NOTFOUND 会一直被复用，必须删缓存。

### 4. 生成的类 `Lexer`/`Parser` 和 ANTLR 运行时同名，编译不过
- 现象：`reference to 'Lexer' is ambiguous`；`-package rx` 对 C++ 支持不完整（头文件包了命名空间、`.cpp` 没包）。
- 原因：文法名就是类名。
- 修复：加两个 2 行包装文法 `compiler/grammar/RxLexer.g4`（`import Lexer;`）和 `RxParser.g4`（`import Parser;`），生成 `RxLexer`/`RxParser`。
- 验证：包装文法和"复制原文法改文法名"生成的代码逐字节一致（只差注释路径），确认不影响解析行为。

### 5. ANTLR `-o` 目录多套一层路径
- 现象：`-o /tmp/gen grammar/Lexer.g4` 生成到 `/tmp/gen/grammar/`。
- 原因：相对路径输入会被 ANTLR 镜像目录结构；绝对路径输入直接输出到 `-o`。
- 修复：CMake 里统一用绝对路径。

### 6. `antlr4_static` 不传 include 目录
- 现象：链接没问题，编译报找不到头文件。
- 原因：该 imported target 只带 `Threads`，不设 `INTERFACE_INCLUDE_DIRECTORIES`。
- 修复：`target_include_directories(rxcc PRIVATE ${GEN_DIR} ${ANTLR4_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)`。

### 7. CMakeLists 里 `file(GLOB ...)` 写了两行，后一行覆盖前一行
- 现象：重配置后 `main.cpp` 不参与编译，`undefined reference to main`。
- 原因：第二行 `GLOB_RECURSE MY_SRCS src/ast/*.cpp` 把变量覆盖成只含 ast 目录。
- 修复：只留一行 `file(GLOB_RECURSE MY_SRCS CONFIGURE_DEPENDS src/*.cpp)`（GLOB_RECURSE 本身就会递归）。

## 二、ast.h 编译期

### 8. 缺前向声明
- 现象：`‘Type’ was not declared`、`‘Stmt’ was not declared`（`Type*`/`Stmt*` 先于定义被使用）。
- 修复：文件开头加 `struct Type; struct Stmt;`。

### 9. 头文件卫生
- 现象：`std::any` 不识别（缺 `<any>`）、`std::string` 不识别（缺 `<string>`）、`anyString` 报"无法重载仅按返回类型区分的函数"（IntelliSense 因未知类型给出的迷惑诊断）、多文件包含可能重复定义。
- 修复：`#pragma once`；补 `<any>/<string>/<memory>`；五个 `anyXxx` 加 `inline`。
- 教训：看到"仅按返回类型区分"这种怪报错，先检查是不是某个类型根本没声明。

## 三、AST 建树运行期（bad_any_cast / throw / 空指针）

### 10. `visitFunctionDefinition` 空指针
- 现象：`fn f() { }`（无参数）或 `fn f() { }`（无返回类型）直接崩。
- 原因：`visit(ctx->functionParameters())` / `visit(ctx->typeRef())` 在 children 为空时 `visit(nullptr)`。
- 修复：判空；无返回类型建 `UNIT` 的 `Type` 节点。

### 11. `visitAssociatedItem` 写成了 `throw`
- 现象：解析 impl 块 `internal error`（`throw "visitAssociatedItem"` 是 `const char*`，落到 `catch (...)`）。
- 原因：占位 stub 不是"永远不会被调到"——`visitInherentImpl` 就会调它。
- 修复：改成转发 `functionDefinition()` / `constantItem()`。
- 教训：`throw` 占位很危险；不用就删声明+定义，别留在可达路径上。

### 12. `visitBlockExpression` 返回具体指针（最典型）
- 现象：`internal error: bad any_cast`，连 `fn main() {}` 都崩。
- 原因：`return now;` 里 `now` 是 `blockExpr*`，`std::any` 存的是 `blockExpr*`；父节点 `anyExpr` 做 `any_cast<Expr*>`，**精确类型不匹配**。
- 修复：`return static_cast<Expr*>(now);`。
- 教训：**每个返回节点的 visit 都要 `static_cast<基类*>`**，`std::any_cast` 不会自动向上转型。

### 13. `visitIfExpression` 的 `then_` 忘了调 `visit`
- 现象：`bad_any_cast`（gdb `catch throw` 定位到 `visitIfExpression`）。
- 原因：`anyExpr(ctx->blockExpression(0))` 把 Context 指针塞进 any，而不是子树的 `Expr*`。
- 修复：`anyExpr(visit(ctx->blockExpression(0)))`。
- 定位技巧：`gdb -batch -ex 'catch throw' -ex run -ex bt --args ./rxcc ...` 直接给调用栈。

### 14. `visitStructDefinition` 忘了 `push_back`
- 现象：Struct 打印出来没有字段（编译运行都不报错，纯漏数据）。
- 原因：循环里构造了 `structField cur` 却没塞进 `now->fields_`。
- 教训：冒烟只查崩溃不够，dump 出来要肉眼看。

### 15. `&mut` 一元运算丢了 mut（4 处）
- 现象：`&mut p.x` dump 成 `Ref`（`isMut_ == false`）；`visitUnaryExpression` 在 n==1 分支写死 `refMake(false, ...)`。
- 影响：condition / conditionBreak / statement 三个家族复制同一段代码，4 处都有。
- 修复：改为 `refMake(isMut, operand)`。

### 16. `while`/`loop` 节点被自己的循环体"吞掉"
- 现象：dump 里 `while i < 10 { ... }` 只剩 `Block`，条件、`While` 节点全没了；`loop` 同理（只剩块）。
- 原因：`visitExpressionWithBlock` 先判 `ctx->blockExpression()`，而 `while`/`loop` 的循环体正是 `ExpressionWithBlockContext` 的直接孩子，所以先被当成裸块返回；`conditionPrimaryWithoutBareBlock` 同病。
- 修复：先判 `LOOP()`/`WHILE()`，最后才判 `blockExpression()`；顺手把两处 `std::any_cast<blockExpr*>(visit(...))` 改成 `static_cast<blockExpr*>(anyExpr(visit(...)))`（前者会因 `visitBlockExpression` 存的是 `Expr*` 而抛异常）。
- 教训：**子规则的种类要先于"形状相似的孩子"判断**；dump 冒烟过了不代表树对，要肉眼核对。

## 四、通用规律

1. `std::any` 按**静态类型精确匹配**：具体节点 return 时必须 `static_cast<Expr*/Type*/Stmt*/Item*/ConstValue*>`；参数之类的普通 struct 按原类型返回（父节点 `any_cast<T>` 取）。
2. 任何 `ctx->xxx()` 都可能为 nullptr/空：`genericParams`、`functionParameters`、`typeRef`、`whereClause`…取之前先判。
3. 冒烟命令（只看崩溃）：
   ```sh
   for f in $(find tests/official/semantic -name '*.rx'); do
     ./target/build/rxcc --stage semantic --dump-ast "$f" >/dev/null 2>/tmp/e || echo "FAIL $f $(cat /tmp/e)"
   done
   ```
4. 运行时错误定位：`gdb -batch -ex 'catch throw' -ex run -ex bt --args ./rxcc ...`；或让 `catch (const std::exception& e)` 打印 `e.what()`。
5. 编辑器/构建/运行时是三套系统，报错要分清。
