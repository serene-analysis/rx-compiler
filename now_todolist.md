# NOW：现在照着做（到"能解析、能接测试框架"为止）

> 这份文件只讲**现在要输入的指令**和**要写的文件**，其他背景一律不想。
> 你的现状：子模块、ANTLR runtime（`~/antlr4-4.13.1`）、CI 关闭都已完成。下面从零搭 `compiler/`。

**做完这一节你会得到**：一个能编译、能解析 Rx、语法错误会 `exit 1` 的 `rxcc`，并且能被课程测试框架 `make test` 调用。

---

## 第 1 步：建目录

在仓库根目录 `rx-compiler/` 下执行：

```sh
mkdir -p compiler/grammar compiler/src
```

---

## 第 2 步：写 2 个包装文法文件

新建 `compiler/grammar/RxLexer.g4`，内容（原样 2 行）：

```
lexer grammar RxLexer;
import Lexer;
```

新建 `compiler/grammar/RxParser.g4`，内容（原样 3 行）：

```
parser grammar RxParser;
options { tokenVocab=RxLexer; }
import Parser;
```

作用：让 ANTLR 生成的类叫 `RxLexer`/`RxParser`，避开和运行时同名类冲突。规则内容全部来自 `grammar/Lexer.g4`、`grammar/Parser.g4`，不用你重写。

---

## 第 3 步：写 `compiler/CMakeLists.txt`

新建 `compiler/CMakeLists.txt`，整份粘贴：

```cmake
cmake_minimum_required(VERSION 3.15)
project(rxcc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ANTLR 运行时（已装在 ~/antlr4-4.13.1）
find_package(antlr4-runtime 4.13.1 REQUIRED)
find_package(Java REQUIRED COMPONENTS Runtime)

# 生成 C++ 解析器到构建目录
set(ANTLR_JAR        /usr/local/lib/antlr-4.13.1-complete.jar)
set(WRAPPER_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/grammar)
set(ORIG_GRAMMAR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../grammar)
set(GEN_DIR          ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

set(GEN_SRCS
    ${GEN_DIR}/RxLexer.cpp
    ${GEN_DIR}/RxParser.cpp
    ${GEN_DIR}/RxParserBaseVisitor.cpp
    ${GEN_DIR}/RxParserBaseListener.cpp
    ${GEN_DIR}/RxParserListener.cpp
    ${GEN_DIR}/RxParserVisitor.cpp)

add_custom_command(
    OUTPUT ${GEN_SRCS}
    COMMAND ${Java_JAVA_EXECUTABLE} -jar ${ANTLR_JAR} -Dlanguage=Cpp -visitor
            -lib ${ORIG_GRAMMAR_DIR}
            -o ${GEN_DIR}
            ${WRAPPER_DIR}/RxLexer.g4 ${WRAPPER_DIR}/RxParser.g4
    DEPENDS ${WRAPPER_DIR}/RxLexer.g4 ${WRAPPER_DIR}/RxParser.g4
            ${ORIG_GRAMMAR_DIR}/Lexer.g4 ${ORIG_GRAMMAR_DIR}/Parser.g4
    COMMENT "Generating ANTLR C++ sources")

file(GLOB_RECURSE MY_SRCS CONFIGURE_DEPENDS src/*.cpp)

add_executable(rxcc ${MY_SRCS} ${GEN_SRCS})
target_include_directories(rxcc PRIVATE ${GEN_DIR} ${ANTLR4_INCLUDE_DIR})
target_link_libraries(rxcc PRIVATE antlr4_static)
```

---

## 第 4 步：写 `compiler/src/main.cpp`

新建 `compiler/src/main.cpp`，整份粘贴：

```cpp
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "antlr4-runtime.h"
#include "RxLexer.h"
#include "RxParser.h"

using namespace antlr4;

// 只要词法/语法报错，就把 hasError 置 true
struct ErrorListener : public BaseErrorListener {
    bool hasError = false;
    void syntaxError(Recognizer* /*recognizer*/, Token* /*offending*/,
                     size_t line, size_t column, const std::string& message,
                     std::exception_ptr /*e*/) override {
        hasError = true;
        std::cerr << line << ":" << column << ": syntax error: " << message << "\n";
    }
};

int main(int argc, char** argv) {
    // 用法：rxcc [--dump-tree] --stage semantic <源文件>
    bool dump = false;
    std::string stage;
    std::string sourcePath;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dump-tree") {
            dump = true;
        } else if (arg == "--stage" && i + 1 < argc) {
            stage = argv[++i];
        } else {
            sourcePath = arg;
        }
    }
    if (stage != "semantic" || sourcePath.empty()) {
        std::cerr << "usage: rxcc --stage semantic [--dump-tree] <source.rx>\n";
        return 1;
    }

    std::ifstream in(sourcePath, std::ios::binary);
    if (!in) {
        std::cerr << "cannot open " << sourcePath << "\n";
        return 1;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();

    try {
        ErrorListener listener;

        ANTLRInputStream input(buffer.str());
        RxLexer lexer(&input);
        lexer.removeErrorListeners();          // 关掉默认"报错后继续"的行为
        lexer.addErrorListener(&listener);

        CommonTokenStream tokens(&lexer);
        tokens.fill();

        RxParser parser(&tokens);
        parser.removeErrorListeners();
        parser.addErrorListener(&listener);

        tree::ParseTree* root = parser.crate();   // crate = 整个文件
        if (listener.hasError) {
            return 1;                              // 语法错误 → 拒绝
        }
        if (dump) {
            std::cout << root->toStringTree(&parser) << "\n";
        }

        // TODO：下一步在这里做语义检查（名称解析 + 类型检查），先直接接受
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "internal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "internal error\n";
        return 1;
    }
}
```

---

## 第 5 步：编译

在仓库根目录执行（两行）：

```sh
cmake -S compiler -B target/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/antlr4-4.13.1
cmake --build target/build -j
```

目的：生成 ANTLR 代码 + 编译出 `target/build/rxcc`。第一次大约几十秒，以后改代码只重编改动部分。

看到 `[100%] Built target rxcc` 就是成功。出问题对照文末"排错"。

---

## 第 6 步：手动试一下

```sh
printf 'fn add(a: i32, b: i32) -> i32 {\n    a + b\n}\n' > /tmp/ok.rx
./target/build/rxcc --stage semantic --dump-tree /tmp/ok.rx; echo "exit=$?"

printf 'fn f( {\n' > /tmp/bad.rx
./target/build/rxcc --stage semantic /tmp/bad.rx; echo "exit=$?"
```

目的和预期：
- 第一条：打印一大串 parse tree，`exit=0`。这就是 ANTLR 给的树，下一步把它变成自己的 AST。
- 第二条：stderr 打印语法错误，`exit=1`。

---

## 第 7 步：改 `config.mk`

打开仓库根目录 `config.mk`，把 `BUILD` 和 `SEMANTIC` 两行**替换**成下面两行（其他行不用动）：

```make
BUILD = cmake -S compiler -B target/build -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=$(HOME)/antlr4-4.13.1 && \
        cmake --build target/build -j && : > {runtime}

SEMANTIC = ./target/build/rxcc --stage semantic {source}
```

目的：告诉课程测试框架"怎么构建你的编译器"和"怎么检查一个程序"。

---

## 第 8 步：用框架跑一次

```sh
make test FILTER=official:semantic:casts-and-literals STAGE=semantic VERBOSE=true
```

目的：确认框架能构建、能调用你的 `rxcc`。

现在的预期结果：**正例（acc-*）基本 PASS，负例（err-*）基本 FAIL**（因为语义检查还没写，什么都接受）。看到这个结果说明管道全通了，可以开始写 AST 和语义检查。

---

## 排错对照

| 报错 | 原因/解决 |
|---|---|
| `antlr4-runtime.h: No such file or directory` | 第 5 步的命令漏了 `-DCMAKE_PREFIX_PATH=$HOME/antlr4-4.13.1` |
| `reference to 'Lexer' is ambiguous` | 生成时没用到包装文法；检查第 2 步两个文件是否存在、CMake 里输入是不是 `RxLexer.g4`/`RxParser.g4` |
| `Cannot find source file ... RxLexer.g4` | `compiler/grammar/` 下文件没建或文件名拼错 |
| `no supported testcases match FILTER` | 官方测试子模块没拉下来：`git submodule update --init --recursive` |
| `set semantic in config.mk before running these tests` | 第 7 步没改 `config.mk`，检查 `SEMANTIC` 那一行 |
| 编译卡很久 | `/mnt/c` 磁盘慢，属正常；别中断，后续是增量构建 |

---

## 做完后

你手里已经有了：命令行入口、ANTLR 解析、语法错误拒绝、测试框架接入。

**下一件事是 AST**：要在 `compiler/src/ast/ast.h` 定义 AST 节点，在 `compiler/src/frontend/ast_builder.{h,cpp}` 里把 parse tree 转成 AST，然后在 `main.cpp` 里接上 `--dump-ast`。具体做法去 `todolist.md` 第 5 节看，或直接来问我"下一步"。

**现在不要碰**：IR/CodeGen/RUN、REIMU、优化、`crates/rx`、删参考实现。
