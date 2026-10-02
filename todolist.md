# Rx 编译器 · 第一阶段详细 Todolist（做到 AST 验收）

> 本文假设你写过 ANTLR + C++ 的 Python 解释器（踩过 Visitor、`std::any`、CMake、`ctx`），但没写过真正的编译器和类型检查。
> 配套资料：`starting_letter.txt`（任务书）、`README-EN.md`（模板用法）、[Rx 语言规范](https://acmclasscourse-2025.github.io/rx-compiler-specification/)（语言权威定义）、`tests/official`（官方测试）。
> 本阶段验收时间：**第 4 周周日 23:59**。本阶段占总评 15%，Code Review 失败等于本阶段没完成。

---

## 0. 全局图景：编译器要做什么

### 0.1 最终目标

把 Rx 语言（Rust 的一个子集）的源代码编译成 RV32IM 的 RISC-V 汇编，并且要做优化。整个课程分四个阶段，本阶段只做最前面的部分：

```
阶段一（本文，第4周）      阶段二（第8周）   阶段三（第12周）    阶段四（第16周）
源码.rx
  │
  ▼
[词法分析 Lexer]           ← ANTLR 帮你生成
  │  得到 token 流
  ▼
[语法分析 Parser]          ← ANTLR 帮你生成
  │  得到 parse tree（语法树）
  ▼
[构造 AST]                 ← 你写
  │  得到干净的抽象语法树
  ▼
[语义分析]                 ← 你写（名称解析 + 类型检查）
  │  通过 → exit 0；有错 → exit 1
  ▼
 (本阶段到此为止，验收)
  │
  ▼
[生成 LLVM IR]            ← 阶段二
  │
  ▼
[生成 RISC-V 汇编]        ← 阶段三
  │
  ▼
[各种优化]                 ← 阶段四
```

### 0.2 和你写过的 Python 解释器有什么不同

| | 你的 Python 解释器 | 本课程（阶段一） |
|---|---|---|
| 输入 | 简化 Python | Rx（Rust 子集） |
| ANTLR 用哪个目标 | C++ | C++（一样） |
| 拿到 parse tree 后 | Visitor 直接求值（边遍历边算） | 先转成自己的 AST，**只做检查，不执行** |
| 对错误的处理 | 运行时抛异常/报错 | 编译期静态检查：不运行程序就要判断"这个程序合法吗" |
| 输出 | 程序运行结果 | 阶段一只输出"接受(0)/拒绝(1)" |
| Visitor 返回值 | 基本是运行时值 | AST 指针/类型信息，用 `std::any` 或干脆手写递归 |

最关键的区别：**解释器是"执行"，编译器前端是"判断"**。比如看到 `let x: i32 = true;`，解释器可以等真的执行到这行再报错；编译器必须在没运行的情况下就判定它不合法，然后 `exit 1`。

### 0.3 课程怎么评判你的编译器

课程提供了一套测试框架：测试目录里每个案例有一个 `manifest.json`，里面写着"这个 `.rx` 文件应该被接受还是拒绝"。

- `SEMANTIC` 测试：框架运行你的编译器，检查退出码——期望接受则必须是 `0`，期望拒绝则必须是 `1`。
- **崩溃、超时、其他退出码一律算失败**（包括"期望拒绝但你的程序段错误了"）。
- 每阶段测试跑法都一样，见第 7 节。

---

## 1. 课程规则与验收标准（把任务书翻译成人话）

### 1.1 时间与成绩

- 第 4 周周日 23:59 **AST 验收**；第 8 周 IR；第 12 周 CodeGen；第 16 周 Optimize。
- AST 占 15%。每个阶段都含 Code Review 分，**Code Review 失败 = 该阶段未完成**。
- 一学期至少要参加 4 次 Code Review：2–3 次常规 + 1–2 次抽查。次数和时长跟第 4/8/12/16 周的考试成绩、平时提问情况有关。
- 第 4 周考试内容就是编译器设计，自己写的就保证能及格；考试成绩会通过 Code Review 影响分数。
- AI 使用规范：**不允许整段使用 AI 生成**，否则那部分没有分。请把关键设计、关键算法自己写、自己讲得清楚。

### 1.2 本阶段验收标准（先把话说死）

- [ ] `config.mk` 里的 `SEMANTIC` 命令能正确判断 Rx 程序：合法输出 `exit 0`，不合法输出 `exit 1`。
- [ ] `make test FILTER=official:semantic` 里 45 个 suite、约 236 个 `.rx` 对应的所有正/负案例全部通过。
- [ ] 代码里有一套自己设计的 AST 和语义检查，能通过 Code Review（讲得清结构、算法、错误处理）。
- [ ] 词法/语法错误也要被拒绝（不是只有类型错误才拒绝）。

### 1.3 退出码契约（非常重要）

框架只看退出码，不看你的错误信息写得好不好（错误信息是给你自己调试和 Code Review 看的）：

| 情况 | 你的编译器应该 | 框架判定 |
|---|---|---|
| 程序合法 | `exit 0` | 正例通过 |
| 程序有语法/语义错误 | `exit 1`（自己正常返回，别 `abort()`） | 负例通过 |
| 崩溃/段错误 | 被信号杀死，退出码 >128 | 失败 |
| 超时（默认 30 秒） | — | 失败 |
| 用了其他退出码（比如 2） | — | 失败 |

所以：**所有错误路径都用 `exit 1` 或 `return 1`**，不要让异常逃出去。C++ 里建议 `main` 内层层 `try/catch`，catch 到任何东西都返回 1。

### 1.4 本阶段明确不用做的事

- 不做借用检查、生命周期检查、所有权检查（规范保证测试里这些永远是对的）。
- 不做溢出检查、数组越界检查、panic 处理。
- 不做运行（没有 `RUN`），不生成汇编。
- move/copy 语义（"这个值被移走了"）属于运行行为，本阶段类型判对即可，等 IR/CodeGen 再管。

---

## 2. 环境准备（按顺序做，每步都说明原因）

### 2.1 先修 CRLF 换行符问题（Day 1，5 分钟）

现在用 `git status` 看，仓库里 17 个文件全都显示"已修改"，但 `git diff --stat` 显示每行既加又删。原因：这些文件原本是 Linux 换行（LF），被 Windows 侧的 Git 签出成了 CRLF。

```sh
git config core.autocrlf input
git diff --ignore-cr-at-eol --stat    # 应该发现"真实改动"是 0
git checkout -- .                     # 用 LF 重新签出工作区
```

- [ ] 执行上面三条命令
- [ ] 在仓库根目录新建 `.gitattributes`，内容一行：`* text=auto eol=lf`（防止以后再乱）
- [ ] 想一下：如果之后要提交作业，不要把这个 CRLF 噪声提交上去

> 另外：仓库在 `/mnt/c`（Windows 盘），WSL 读写它会比较慢。本阶段只有编译器编译这一个重活，问题不大；如果后面编译 RISC-V 测试觉得慢，可以把仓库移到 WSL 的 `~/` 下。

### 2.2 初始化子模块（官方测试就藏在这里）

```sh
git submodule update --init --recursive
```

这会把两样东西拉下来：

- `tests/official/` —— 官方测试集（**你的需求文档**，里面有 semantic/codegen/optimization 等目录）
- `vendor/REIMU/` —— RISC-V 模拟器（阶段三才用）

> **注意**：模板的 `.gitmodules` 里还列了一个 `spec`（规范源码），但这个仓库里没登记它的 gitlink（`git ls-files -s spec` 是空的），所以 `git submodule update` 不会拉它——这不是你操作错了。规范直接看在线版就行。

- [ ] 执行命令
- [ ] `git submodule status` 应显示两行、且行首没有 `-`（有 `-` 表示没拉下来）
- [ ] 打开 `tests/official/semantic/` 随便看几个目录的 `manifest.json` 和 `.rx` 文件，感受"测试长什么样"

> `manifest.json` 大致长这样（每个条目就是一个测试）：
> ```json
> [
>   { "source": "acc-basic.rx", "stage": "semantic", "compilation_success": true,  "description": "应该接受" },
>   { "source": "err-type.rx",  "stage": "semantic", "compilation_success": false, "description": "应该拒绝" }
> ]
> ```

### 2.3 ANTLR：工具已有，运行时需要自己编译

**工具**：系统里已有 `/usr/local/lib/antlr-4.13.1-complete.jar`。我已经实测过：用它直接生成现有 `grammar/Lexer.g4` + `grammar/Parser.g4` 的 C++ 代码，可以成功（虽然文法注释写的是 4.13.2，4.13.1 工具完全兼容）。

**运行时（runtime）**：ANTLR 生成的 C++ 代码要链接 `antlr4-runtime` 库。系统 apt 里只有 4.10 版本，和 4.13.1 工具不匹配，**不要用**。自己编译（一次性，约几分钟）：

```sh
git clone --branch 4.13.1 --depth 1 https://github.com/antlr/antlr4.git /tmp/antlr4
cmake -S /tmp/antlr4/runtime/Cpp -B /tmp/antlr4/runtime/Cpp/build \
  -DCMAKE_BUILD_TYPE=Release -DANTLR_BUILD_CPP_TESTS=OFF -DWITH_DEMO=OFF \
  -DANTLR_BUILD_STATIC=ON -DANTLR_BUILD_SHARED=OFF \
  -DANTLR4_INSTALL=ON -DCMAKE_INSTALL_PREFIX="$HOME/antlr4-4.13.1"
cmake --build /tmp/antlr4/runtime/Cpp/build -j"$(nproc)"
cmake --install /tmp/antlr4/runtime/Cpp/build
```

- [ ] 编译安装 runtime，装到 `~/antlr4-4.13.1`
- [ ] 用这个命令验证生成没问题（只生成到临时目录，不会污染仓库）：

```sh
mkdir -p /tmp/gen-test
java -jar /usr/local/lib/antlr-4.13.1-complete.jar \
  -Dlanguage=Cpp -visitor -o /tmp/gen-test "$PWD/grammar/Lexer.g4" "$PWD/grammar/Parser.g4"
ls /tmp/gen-test
```

你会看到 `Lexer.cpp/.h`、`Parser.cpp/.h`、`ParserBaseVisitor.*` 等一批文件，这就和你 Python 解释器里 `generated/` 目录一样的东西。

> 注意：这只是"验证工具能跑"。这样生成的类名是 `Lexer`/`Parser`，写编译器时会和 ANTLR 运行时的同名类冲突；正式工程要按 4.1 用包装文法生成 `RxLexer`/`RxParser`。

> 小坑：如果输入写成相对路径 `grammar/Lexer.g4`，ANTLR 会在 `-o` 目录下**再建一层 `grammar/` 子目录**；写成绝对路径（或先 `cd grammar` 再用文件裸名）就直接输出到 `-o` 里。后面 CMake 里我们统一用绝对路径。

### 2.3.1 课程到底提供了什么（已查证，别再纠结）

**结论：课程只提供两个 `.g4` 文法文件，没有另一份"现成的 lexer/parser 代码"。用 ANTLR 生成出来的就是你自己的 lexer/parser。**

- README：`G4 grammar for Rx under grammar/. You may use it to generate the lexer and parser for your compiler.`——任务书里说的"现成的 Parser/Lexer"就是指这份 g4。
- spec 导言：`we provide an ANTLR v4 grammar for Rx that automates parsing, allowing you to begin directly with an AST.`——也说明只提供文法。
- 语法定义的权威是 spec（导言声明 spec 与 rustc 是 source of truth；两边有分歧就发邮件问 TA）；g4 是这套约定的 ANTLR 实现。Parser.g4 里多出来的 `closed*`、`condition*`、`statement*` 规则只是给 ANTLR 消歧义用的，不改变语言范围。
- `grammar/Parser.g4` 注释写着 "Read README.md for token conventions, precedence, and block tails"，但仓库（和上游模板的 grammar 目录）里**没有这个 README**，只有两个 g4 文件。它指的内容实际在 spec 的 **Parser conventions** 章节（grammar.html）：运算符优先级、`>>`/`>=`/`>>=`/`&&` 的上下文解释、块尾（block tails）。

我实测过的两条结论：

1. 官方 `tests/official/lexer`（30 accept + 23 reject）和 `tests/official/parser`（365 accept + 77 reject）共 495 例，拿 g4 生成的解析器按官方约定判定，**495/495 与官方预期完全一致**。runner 跳过 `lex`/`parse` 这两个 stage（因为前端是学校给的、不考自己写词法语法），但你可以拿来当 parser 自测。要点：
   - 每条 parser 用例的 `metadata.entry` 给出解析入口：`crate` / `expression` / `typeRef` / `item` / `letStatement`，对应调用 `parser.crate()` 等；
   - 解析完必须检查**已到 EOF**，否则 `a < b < c` 这种负例会误判为通过；
   - lexer 用例看有没有 `ERROR_CHAR`、`INVALID_NUMBER`、`INVALID_LIFETIME`、`INVALID_CHARACTER_LITERAL`、`UNTERMINATED_BLOCK_COMMENT` 这类坏 token（`RESERVED_KEYWORD` 是合法 token，不算坏）。
2. 4.1 推荐的包装文法（`import`）与"把原文法复制一份、只改文法名"生成的代码**逐字节相同**（只差注释里的源文件路径），`.tokens` 也完全相同——所以包装只是换个类名避开 C++ 冲突，不会改变任何解析行为。

### 2.4 阶段二以后才需要的（本阶段可以不装）

- `xmake`（编译 REIMU 模拟器用，当前没装）
- `clang-22`（IR 测试把 `.ll` 转汇编用，当前没装）
- `rustup target add riscv32im-unknown-none-elf`（跑参考实现 rustc 用）

这些到第 5–8 周再折腾也行，别现在分心。

### 2.5 关掉 CI（开发期建议）

`README` 建议开发期间把 GitHub Actions 关掉，避免每次 push 都跑测试：

```sh
mv .github/workflows/test.yml .github/workflows/test.yml.disabled
```

- [ ] 执行

---

## 3. 工程骨架、命令行和 config.mk

### 3.1 目录结构建议

原来的 `crates/rx/` 是"参考实现 rustc 的辅助代码"，你实现编译器后就不需要它了（但先别删，留着对照 `config.mk` 里参考命令怎么跑）。建议新建 `compiler/` 目录放你的 C++ 代码：

```
rx-compiler/
├── grammar/                    # 学校给的文法，别改
│   ├── Lexer.g4
│   └── Parser.g4
├── compiler/                   # 你的编译器（新建）
│   ├── CMakeLists.txt
│   ├── grammar/                # 2 行包装文法（见 3.3/4.1，解决类名冲突）
│   │   ├── RxLexer.g4
│   │   └── RxParser.g4
│   └── src/
│       ├── main.cpp            # 命令行入口
│       ├── common/             # 位置信息、报错工具
│       ├── ast/                # AST 节点定义
│       ├── frontend/           # parse tree → AST、错误监听
│       └── sema/               # 符号表、类型、检查
├── target/                     # 构建产物（.gitignore 已忽略）
│   └── build/rxcc              # 编译出的编译器可执行文件
├── tests/
│   ├── official/               # 子模块，别改
│   └── custom/                 # 你自己的测试放这里
├── config.mk                   # 告诉测试框架怎么调用你的编译器
└── Makefile                    # 不用改
```

### 3.2 命令行接口（CLI）

测试框架通过 `config.mk` 里的命令来调用你，所以命令行形式是规定的：

```sh
./target/build/rxcc --stage semantic <源码路径>
```

- 合法：进程返回 0
- 不合法：进程返回 1，错误信息打到 `stderr`（内容随意，格式建议 `文件:行:列: 错误描述`）
- 本阶段只需要 `semantic` 一个 stage；阶段二再加 `--stage ir {source} -o {output}` 等

`main.cpp` 骨架（伪代码，思路即可）：

```cpp
int main(int argc, char** argv) {
    try {
        // 1. 解析参数：--stage semantic，源码路径
        // 2. 读文件（读不到 → 返回 1）
        // 3. 词法+语法分析，任何语法错误 → 返回 1
        // 4. 构造 AST
        // 5. 语义分析（名称解析 + 类型检查），任何错误 → 返回 1
        return 0;                       // 全部通过
    } catch (...) {
        std::cerr << "内部错误\n";
        return 1;                       // 绝对不能让异常逃出去
    }
}
```

### 3.3 CMakeLists.txt 完整示例（已实测可编译运行）

> 这个版本修掉了两个我实际踩到的坑，别照原来那份旧版抄：
> 1. `antlr4_static` 这个 CMake target **不会自动带头文件目录**，必须手动加 `${ANTLR4_INCLUDE_DIR}`，否则报 `antlr4-runtime.h: No such file or directory`。
> 2. 生成的类名就叫 `Lexer`/`Parser`，和 ANTLR 运行时里的 `antlr4::Lexer`/`antlr4::Parser` 冲突，直接编译报 `reference to 'Lexer' is ambiguous`。ANTLR 的 `-package` 选项对 C++ 支持不完整（头文件包了命名空间、`.cpp` 没包），也用不了。解决办法：在 `compiler/grammar/` 放两个 2 行包装文法（见 4.1），生成 `RxLexer`/`RxParser` 类。

```cmake
cmake_minimum_required(VERSION 3.15)
project(rxcc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ANTLR 运行时（用你刚装到 ~/antlr4-4.13.1 的）
find_package(antlr4-runtime 4.13.1 REQUIRED)
find_package(Java REQUIRED COMPONENTS Runtime)

# ---- 用 ANTLR 工具生成 C++ 代码（生成到构建目录，不提交进 git）----
set(ANTLR_JAR        /usr/local/lib/antlr-4.13.1-complete.jar)
set(WRAPPER_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/grammar)      # 2 行包装文法
set(ORIG_GRAMMAR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../grammar)   # 学校原文法（供 import 查找）
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

# ---- 你的编译器 ----
file(GLOB_RECURSE MY_SRCS CONFIGURE_DEPENDS src/*.cpp)

add_executable(rxcc ${MY_SRCS} ${GEN_SRCS})
target_include_directories(rxcc PRIVATE ${GEN_DIR} ${ANTLR4_INCLUDE_DIR})
target_link_libraries(rxcc PRIVATE antlr4_static)
```

说明：
- `find_package` 需要能找到 runtime，所以构建时传 `-DCMAKE_PREFIX_PATH=$HOME/antlr4-4.13.1`。
- 生成的文件只在构建目录，不污染源码；改 `.g4` 会自动重新生成。
- C++17；Linux 下不需要 `ANTLR4CPP_STATIC` 之类的宏。

### 3.4 改 config.mk（就改两行）

```make
# 可选但建议：编译你的编译器。{runtime} 是框架要求的共享运行时代码文件，
# 本阶段没有运行时，所以用 : > 写一个空文件（框架会检查这个文件存在）。
BUILD = cmake -S compiler -B target/build -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=$(HOME)/antlr4-4.13.1 && \
        cmake --build target/build -j && : > {runtime}

# 本阶段的验收命令：接受 exit 0，拒绝 exit 1
SEMANTIC = ./target/build/rxcc --stage semantic {source}
```

- `BUILD` 每次 `make test` 前跑一次（有 300 秒上限），CMake 增量构建很快，不用担心。
- 第一份 `config.mk` 里的参考实现命令留着当对照，确认稳定后再删。

### 3.5 先跑一次看看

```sh
make test FILTER=official:semantic STAGE=semantic VERBOSE=true
```

现在你的编译器还没写完，会看到一堆失败，这是正常的。重点先确认：**BUILD 成功、你的 rxcc 被调用了、失败原因是"退出码不对"而不是"找不到文件/命令"**。

---

## 4. 第一步：让 ANTLR 跑起来，看懂 parse tree

### 4.1 生成代码（用包装文法绕开 C++ 类名冲突）

先建两个只有 2 行的包装文法（放 `compiler/grammar/`），它们不复制任何规则，只做改名：

`compiler/grammar/RxLexer.g4`：

```
lexer grammar RxLexer;
import Lexer;
```

`compiler/grammar/RxParser.g4`：

```
parser grammar RxParser;
options { tokenVocab=RxLexer; }
import Parser;
```

生成命令（已实测可用）：

```sh
java -jar /usr/local/lib/antlr-4.13.1-complete.jar \
  -Dlanguage=Cpp -visitor \
  -lib "$PWD/grammar" \
  -o "$PWD/compiler/generated" \
  "$PWD/compiler/grammar/RxLexer.g4" "$PWD/compiler/grammar/RxParser.g4"
```

- `-Dlanguage=Cpp`：生成 C++（和你 Python 解释器一样）
- `-visitor`：额外生成 `ParserBaseVisitor`（不用 Listener 可以加 `-no-listener`）
- `import Lexer;` / `import Parser;` 是 ANTLR 的文法复用机制：编译时把学校原文法的规则原样并进来，**规则内容一字不改**；`-lib "$PWD/grammar"` 告诉 ANTLR 去哪里找原文法
- 会有一个警告 `warning(109): options ignored in imported grammar Parser`，**是正常的**：原文法里的 `tokenVocab=Lexer` 被忽略，改用包装文法的 `tokenVocab=RxLexer`
- **为什么不能直接用 `grammar/Lexer.g4`、`grammar/Parser.g4` 生成**：生成的类就叫 `Lexer`/`Parser`，和运行时里的 `antlr4::Lexer`/`antlr4::Parser` 重名，编译时报 `reference to 'Lexer' is ambiguous`。ANTLR 的 `-package` 试图解决但对 C++ 支持不完整（头文件包了命名空间、`.cpp` 没包），同样编不过（都实测过）。包装文法生成 `RxLexer`/`RxParser`，token 常量和规则名和原来完全一样（例如 `RxParser::FN`、`RxParser::CrateContext`）
- **包装会影响解析行为吗？不会**：我对比过"import 包装"和"复制原文法、只改文法名"两种方式的全部生成产物，`.cpp/.h` **逐字节相同**（只差注释里的源文件路径），`.tokens` 完全相同。包装只改类名。
- 详细背景（课程只给 g4、官方 lexer/parser 用例怎么用、`Parser.g4` 里引用的 README 在哪）见 2.3.1

生成的文件：

| 文件 | 内容 |
|---|---|
| `RxLexer.h/.cpp` | 词法分析器类 `RxLexer`，产出一条条 token |
| `RxParser.h/.cpp` | 语法分析器类 `RxParser`，每个文法规则对应一个 `XxxContext` 类和 `xxx()` 方法 |
| `RxParserBaseVisitor.h/.cpp` | Visitor 基类，每个规则一个 `visitXxx` 方法（和你 Python 解释器一样） |
| `RxParserVisitor.h` | Visitor 接口 |
| `RxParserListener.*` | Listener（本课程用不上） |

### 4.2 写一个"看得见树"的小工具

你 Python 解释器项目里有 docs 讲 parse tree 结构；这里同样，**先不要急着写 AST**，先写一个 `--dump-tree` 调试模式，把 ANTLR 的 parse tree 打印出来（ANTLR C++ 自带 `tree->toStringTree(&parser)`，还可以用 `trees::Trees` 打印）。

随手拿一段 Rx 源码试试：

```rust
fn add(a: i32, b: i32) -> i32 {
    a + b
}
```

你会看到一个非常深、非常啰嗦的树：一个 `a + b` 从 `expression` 开始，穿过 `assignmentExpression`、`logicalOrExpression`、……一路到 `additiveExpression`，中间还有 `equalsSign`、`closedXxx` 这些为了消歧义存在的规则。**这正是为什么不能直接拿 parse tree 当 AST 用**：

1. 节点太多层，后面写类型检查会被绕晕；
2. parse tree 里有 `(` `)` `,` `{` `}` 这些标点的节点、`use`/lifetime 这些要丢弃的语法，还保留了"条件表达式专用"的规则变体；
3. 后续 IR/CodeGen 需要一个语义明确、干净的树。

### 4.3 顺手把错误处理接上（关键！）

ANTLR 默认的错误处理是：打印一行错误，然后**尝试恢复继续解析**。这不符合课程要求（文法注释里明确写了"A parser must never silently recover"）。你的做法：

```cpp
// 自定义错误监听器：只要有任何词法/语法错误，就记录下来
class BailListener : public antlr4::BaseErrorListener {
public:
    bool hasError = false;
    void syntaxError(antlr4::Recognizer* /*r*/, antlr4::Token* /*offending*/,
                     size_t line, size_t col, const std::string& msg,
                     std::exception_ptr /*e*/) override {
        hasError = true;
        std::cerr << line << ":" << col << ": " << msg << "\n";
    }
};

// 使用：
RxLexer lexer(&input);
lexer.removeErrorListeners();
BailListener bail;
lexer.addErrorListener(&bail);
RxParser parser(&tokens);
parser.removeErrorListeners();
parser.addErrorListener(&bail);

auto* tree = parser.crate();          // crate 是文法规定的起始规则（整个文件）
if (bail.hasError) return 1;          // 有语法错误，直接拒绝
```

类似的还有 lexer 层面一些专门的坏 token：`INVALID_NUMBER`、`INVALID_LIFETIME`、`UNTERMINATED_BLOCK_COMMENT`、`ERROR_CHAR`、`RESERVED_KEYWORD`（保留字）。它们在文法里故意留在默认通道上，Parser 规则里没有它们的位置，所以自然会导致语法错误——但前提是你**真的在报错后拒绝**，而不是恢复后继续。可以在 `--dump-tree` 阶段验证。

### 4.4 本步完成标准

- [ ] `--dump-tree` 能打印任意 `.rx` 的树
- [ ] 故意的语法错误（如 `fn f( {`）会打印错误并以 1 退出
- [ ] 确认 `use rx::core::*;`、lifetime 语法（`fn f<'a>(x: &'a i32)`）都能被 Parser 正常接受（后续丢弃）

---

## 5. 第二步：设计并构建 AST

### 5.1 AST 是什么（用你的解释器类比）

你的 Python 解释器其实是"parse tree 直接求值"。现在中间加一层：

```
parse tree（ANTLR 给的，深、杂）  ──AstBuilder──▶  AST（你自己定义的，干净、语义化）
```

AST 节点只保留"程序的含义"，比如：

```rust
a + b * 2
```

parse tree 里可能是 `expression → assignmentExpression → ... → additiveExpression(multiplicativeExpression, additiveOperator, multiplicativeExpression)`。
AST 里就是：

```
Binary(+, Path(a), Binary(*, Path(b), Literal(2, i32)))
```

后面类型检查只对着 AST 写，不用再理 ANTLR 的上下文对象。

### 5.2 AST 节点清单（按 Rx 子集）

建议用类继承 + `std::unique_ptr` 管理子节点。带源码位置（`line:col`），报错要用。

**类型（TypeRef）**：
- `BuiltinType`：`i32 / u32 / isize / usize / bool`（记住四种整数是不同类型！）
- `StructType(name)`
- `BoxType(elem)`、`VecType(elem)`
- `RefType(elem, isMut)`（`&T` / `&mut T`；`&&T` 语法上是两层 `&`）
- `ArrayType(elem, lengthConstValue)`（`[T; N]`，N 是常量）
- `UnitType`（`()`）

**顶层项（Item）**：
- `UseItem`：解析完直接丢掉（不用放进 AST）
- `FnItem`：名字、参数列表（名字、类型、是否 `mut`）、返回类型（缺省 `()`）、函数体 `Block`
- `StructItem`：名字、字段列表、derive 列表（`Copy/Clone/PartialEq/Eq`）
- `ConstItem`：名字、类型、常量值
- `ImplItem`：目标类型、关联函数/常量列表（`Self` 在这里可解析）

**语句（Stmt）**：
- `LetStmt`：模式只有简单名字（可有 `mut`）、可选类型标注、初始化表达式
- `ExprStmt`：表达式后跟分号
- 注意块有"尾表达式"：`{ ...; last }` 中 `last` 是这个块的值

**表达式（Expr）**：
- `LiteralExpr`（值 + 确定的类型 + 原始文本，比如 `0b1010u32`）
- `PathExpr`（变量、常量、`Type::NAME`、`Self::NAME`）
- `UnaryExpr`（`- ! * & &mut`）
- `BinaryExpr`（`+ - * / % & | ^ << >> == != < <= > >= && ||`）
- `AssignExpr`、`CompoundAssignExpr`
- `CallExpr`（函数调用、`Box::<i32>::new(..)`、`Vec::<i32>::new(..)`）
- `MethodCallExpr`（`recv.name(args)`；注意 `.field` 和 `.method(args)` 区分）
- `FieldExpr`、`IndexExpr`
- `ArrayExpr`（`[a, b, c]`）与 `RepeatArrayExpr`（`[value; N]`）
- `StructExpr`（`S { x: 1 }`、`Self { .. }`）
- `CastExpr`（`as`）
- `BlockExpr`、`IfExpr`、`LoopExpr`、`WhileExpr`、`BreakExpr`（可带值）、`ContinueExpr`、`ReturnExpr`（可带值）
- `ParenExpr`（可保留可消除，保留更简单）

### 5.3 AstBuilder 怎么写（两个建议，选一个）

**方案 A（推荐，简单）**：不用 ANTLR 的 `ParserBaseVisitor`，自己写一组递归函数，每个函数处理一种 `Context`，直接返回你想要的类型。因为不同节点返回类型不同，这样比 `std::any` 到处 `any_cast` 更安全、更好调试。

```cpp
std::unique_ptr<Expr> buildExpr(RxParser::ExpressionContext* ctx);
std::unique_ptr<Expr> buildAdditive(RxParser::AdditiveExpressionContext* ctx) {
    // additiveExpression : multiplicativeExpression (additiveOperator multiplicativeExpression)*
    auto ops = ctx->additiveOperator();              // 操作符列表
    auto parts = ctx->multiplicativeExpression();    // 操作数列表
    auto left = buildMultiplicative(parts[0]);
    for (size_t i = 0; i < ops.size(); ++i) {        // 左结合，逐个折叠
        auto right = buildMultiplicative(parts[i + 1]);
        left = makeBinary(/*op=*/ops[i], std::move(left), std::move(right));
    }
    return left;
}
```

**方案 B（沿用你熟悉的方式）**：继承 `ParserBaseVisitor`，`visitXxx` 返回 `std::any`，里面装 `std::unique_ptr<Expr>` 等，取出时 `std::any_cast`。可行，但节点类型一多容易写错，而且要小心 `std::unique_ptr` 不能拷进 `std::any`（用 `std::move` 或装裸指针）。

两个方案的共同点：
- 每个表达式规则大多写成"左结合折叠"：`left (op right)*`（`additiveExpression`、`bitOrExpression`、`logicalAndExpression`……）。
- `assignmentExpression` 是右结合：`lhs (op rhs)?` 只有一层，直接处理。
- Parser 里同一表达式有三套规则：普通 `xxxExpression`、条件里的 `conditionXxxExpression`、语句开头的 `statementXxxExpression`。它们结构相同，只是语法消歧用的；**AST 层完全共用同一个节点类型**，写一个公共函数处理即可（否则你要写三遍）。
- 运算符辅助规则 `shiftRight` (`GT GT_SECOND`)、`comparisonExceptLt`、`assignmentOperator`、`equalsSign` 只是把 token 组合起来，你在 builder 里把它们翻译成一个 `Op` 枚举即可。
- `>>`、`>=`、`>>=` 这些在文法里被拆成 `GT` + `GT_SECOND` + `SHR_EQ` 等 token（lexer mode 的功劳），Parser 已经处理好了——你按 `ctx` 里实际出现的运算符规则翻译就行，不用自己再做 token 拼接。

### 5.4 本步完成标准

- [ ] `--dump-ast` 能对一段综合代码打印出干净、可读的 AST
- [ ] `use` 声明和所有 lifetime 语法不进入 AST（`&'a mut T` 只保留 `RefType(mut)`）
- [ ] 语法正确但没见过的组合不崩溃（哪怕 AST 没建全，也别段错误）

---

## 6. 第三步：语义分析（本阶段最大的活）

### 6.0 先理解"语义分析"在干什么

前面 Parser 只保证"语法像回事"，不保证"意思对"。例如：

```rust
fn main() -> () {
    let x: i32 = true;      // 语法没问题，类型错了
    let y = z + 1;          // 语法没问题，z 没定义
    let mut v = Vec::<i32>::new();
    v = 3;                  // 语法没问题，类型不匹配
}
```

语义分析要做两件事：

1. **名称解析（name resolution）**：每个标识符到底指哪个东西？（局部变量、参数、函数、常量、结构体、字段、方法、内置 `print_i32`……）
2. **类型检查（type checking）**：每个表达式的类型是什么？用得对不对？给每个 AST 节点标上类型（或记录下来），阶段二生成 IR 时还会用到。

建议流程（两遍）：

```
第一遍：收集（collect）
  - 扫一遍所有顶层 Item，登记：函数签名、结构体+字段、常量、impl 里的方法/关联常量
  - 这一步不看函数体，所以"先用后声明"没问题

第二遍：检查（check）
  - 逐个检查函数体/常量值
  - 用一个"作用域栈"维护局部变量
  - 对每个表达式做类型推断/检查，返回类型
```

错误处理建议：写一个 `check` 函数返回 `std::optional<Type>` 或 `bool`，出错就收集进 `Diagnostics`（至少打印 `line:col: 原因`）。只要有一个错误，最后 `return 1`。不要用 `assert` 或抛异常中断（框架看到崩溃就判负例失败）——但 `main` 最外层还是要兜底 catch。

### 6.1 符号表与作用域（先看例子）

```rust
fn f(a: i32) -> i32 {
    let x: i32 = 1;      // 外层局部
    {
        let x: bool = true;   // 内层可以遮蔽（shadow）
        // 这里 x 是 bool
    }
    x                     // 这里 x 是 i32
}
```

要实现：

- 作用域是一个栈。进入块 `{}` push 一层，离开 pop。
- 查找规则："从内到外找最近的绑定"，找到就停，**不回退**：
  ```rust
  fn g() -> i32 { 42 }
  fn main() -> () {
      let g = 1;      // 遮蔽了函数 g
      g();            // 错误：g 现在是 i32，不能调用（不是再去找外层函数）
  }
  ```
- **命名空间互相独立**：类型名空间和值名空间分开。表达式里的 `x` 查值空间；类型位置的 `Foo` 查类型空间；`Type::member` 的类型部分查类型空间。所以结构体 `S` 和函数 `S` 可以同名。
- 顶层函数/结构体/常量/关联项的查找**与声明顺序无关**（所以第一遍先全收集）。
- 结构体的所有 `impl` 块共享一个"关联项空间"：重复方法/关联函数/关联常量（哪怕签名不同）都是错误。
- 字段名在结构体自己的空间里，且字段和固有方法可以同名（`s.x` 是字段、`s.x()` 是方法）。

### 6.2 名称、保护名、入口函数

- 保护名（不能随便占用的名字）：
  - 类型空间：`i32 u32 isize usize bool Box Vec Copy Clone PartialEq Eq`
  - 值空间：`get_i32 print_i32 println_i32`
- 规则细节：
  - `struct i32 {..}`、顶层函数叫 `print_i32` 等**重声明**是编译错误（要拒绝）；
  - 但字段叫 `Vec`、关联项叫 `print_i32` 是合法的（不同空间）；
  - 给 `i32`、`Box` 这类内建类型写 `impl`（固有 impl）是错误；
  - "局部变量/参数和可见常量重名"以及"局部叫 `print_i32`"属于规范里的 UB——**测试里不会出现**，不用专门处理，但也别写代码主动接受或拒绝出奇怪行为。
- `main` 入口：必须是 `fn main() -> ()`（返回类型可省略）。以官方 `semantic/entry`、`source-main-calls` 的测试为准，第一遍收集时就检查。

**名字查找的落点类型**（后面会用）：
- 局部变量/参数 → 它的声明类型
- 常量 → 声明类型
- 函数 → 只能用 `f(...)` 调用（规范：函数不能当值使用，凡"函数作为值"是 UB，测试不考）
- `Box::<T>::new`、`Vec::<T>::new`、`println_i32` 等 → 内建，直接在检查器里特判

### 6.3 类型怎么表示

建议定义：

```cpp
class Type {
public:
    enum Kind { I32, U32, ISIZE, USIZE, BOOL, UNIT, NEVER,
                STRUCT, REF, ARRAY, BOX, VEC } kind;
    // STRUCT: 结构体名字/指针
    // REF: elem + isMut
    // ARRAY: elem + 长度
    // BOX/VEC: elem
};
```

- 整数四种互不相同（`i32` 和 `u32` 不能混算）。
- `NEVER` 表示发散表达式（`return`/`break` 之类"没有值"的类型），赋值/分支合并要点会用到。
- 结构体是名义类型（按名字），数组/引用/Box/Vec 是结构类型（按内部类型比较）。
- 类型可以直接用指针/共享对象，相等比较写个 `typeEqual(a, b)`（比较结构）。
- **每个表达式检查后都会得到一个类型**，把它存在节点上（阶段二用）。

### 6.4 整数字面量：推断顺序是重点

Rx 的整数字面量类型按这个顺序定：

1. 有后缀就用后缀：`1u32` → u32；`1isize` → isize
2. 没后缀就看"期望类型"（expected type）：能接受整数的上下文期待什么，就是什么
3. 都没有 → 默认 `i32`

例子：

```rust
let a = 1;            // 无后缀、无期望 → i32
let b: u32 = 1;       // 期望 u32 → 1 是 u32
let c = 1u32;         // 后缀 → u32
print_i32(1);         // 形参是 i32 → 1 是 i32
let mut v = Vec::<i32>::new();
v.push(1);            // push 的形参是 T=i32 → 1 是 i32
v[0];                 // 索引期望 usize → 0 是 usize
```

**"期望类型"从哪里来（必须记牢）**：
- 带类型标注的 `let` 的初始化器：`let b: u32 = <这里期望 u32>`
- 赋值右边：`x = <期望 x 的类型>`
- 函数/方法调用的实参：`<期望形参类型>`
- 函数体最后一个表达式 / `return` 的值：`<期望返回类型>`
- 结构体字段初始化：`S { x: <期望字段类型> }`
- 常量初始化器（有声明类型时）
- 这些会**穿过去**：括号、块尾表达式、`if` 两分支、同一 loop 的所有 `break` 值

**不会穿过去**（规范为了保证可判定，规定这些情况直接是 UB，测试不考）：

```rust
let x: u32 = 1 + 2;    // 期望不穿透二元运算 → 规范定 UB，测试里不会出现
```

所以你的实现只需要处理"会穿透"的场合，遇到"不穿透"的写法不用费劲猜。

### 6.5 表达式类型规则（常用表）

| 表达式 | 规则 | 例子 |
|---|---|---|
| `a + b`, `a * b`, `a % b` | 两边都是同一个整数类型（允许一边是 `&T`） | `1 + 2` 都是 i32 → i32 |
| `a & b`, `a \| b`, `a ^ b` | 两边同为某个整数类型或同为 bool | `true & false` → bool |
| `!x` | x 是整数或 bool | `!true` → bool |
| `-x` | x 是有符号整数 | `-1` → i32 |
| `a << b`, `a >> b` | a 是整数，b 是**任意**整数（不要求同类型） | `1u32 << 2` → u32 |
| `a == b` | 两边**同一类型**，且该类型实现 PartialEq | 见 6.4 节 |
| `a < b` | 两边都是整数/bool（引用会自动调整） | |
| `&&`, `\|\|` | 两边都必须是 bool | |
| `x as T` | 整数↔整数，bool→整数 | `1 as u32`（1 仍是 i32 再转） |
| `&x`, `&mut x` | 得到 `&T`/`&mut T`，`&mut` 要求 x 是可变的 place | |
| `*p` | 只对引用和 `Box`（`Vec` 不行） | |
| `x[i]` | 数组或 `Vec`；i 必须是 `usize` | |
| 比较的"类型相同" | 不做隐式转换、**不会**从对面推断字面量类型 | `1 == 1i32` 合法；`1 == 1u32` 是 UB（不考） |

> 注意：写代码时不用刻意处理 UB 的情况（它们不会出现在测试里）。你的目标是：合法程序全接受、规范里明确定义的编译错误全拒绝。

### 6.6 place 与可变性（写赋值检查必须要懂）

"place"就是"可以放东西的位置"：变量、字段、索引、`*p`。判断"能不能赋值/能不能可变借用"叫 place 可变性检查：

```rust
let x = 1;
x = 2;                 // 错误：x 不是 mut

let mut y = 1;
y = 2;                 // 合法

let r = &y;            // 共享引用（&i32）
*r = 3;                // 错误：通过共享引用写

let mr = &mut y;       // 可变引用，要求 y 是 mut
*mr = 3;               // 合法

let mut v = Vec::<i32>::new();
v.push(1);             // push 需要 &mut self，v 是 mut，合法
v[0] = 5;              // 合法（v 可变）
let e = &v[0];         // 共享借用
```

规则核心：
- 局部变量只有声明时带 `mut` 才是可变 place。
- `&mut place`：place 必须可变，得到可变引用。
- `*p`：p 是 `&mut T` 才是可变 place；是 `&T` 就是只读 place。
- 字段/索引/Box 解引用继承"根"的可变性，但**一旦路径上碰到共享引用，后面就不可变**。
- 引用本身是个值：`let r = &mut y;` 之后 `r = &mut z;` 是给引用变量重新赋值，需要 `let mut r`。

### 6.7 方法查找、Box/Vec、内建 I/O

**方法调用 `.name(args)` 的解析顺序**（Rx 规定）：

1. 接收者类型记为 T；
2. 候选顺序：先 T 自己；然后如果 T 是 `&U`、`&mut U` 或 `Box<U>`，接着看 U，如此逐层解引用（**Vec 不解引用**）；
3. 对每个候选类型，还要考虑以 `&T`、`&mut T` 作为接收者的方法（比如 `len(&self)`）；
4. 找到唯一匹配就选中；**同一个候选类型上有多个不同方法匹配 = UB（不考）**；一个都没有 = 编译错误；
5. 选中后再检查：接收者是否够可变（调 `&mut self` 要求可变 place）、实参类型对不对。

例子：

```rust
let mut v = Vec::<i32>::new();
v.push(1);          // push(&mut self, value: T) → 需要 v 是可变 place
let n = v.len();    // len(&self) → 共享借用即可，n: usize
let b = v.is_empty();
let x = v.remove(0);  // remove(&mut self, index: usize) -> T
```

**内建容器（必须用显式类型实参）**：

```rust
let b = Box::<i32>::new(7);     // Box<i32>，存 7
let mut v = Vec::<i32>::new();  // 空 Vec<i32>
```

**内建 I/O**：

```rust
fn get_i32() -> i32；            // 从 stdin 读一个整数
fn print_i32(value: i32) -> ()； // 打印，无换行
fn println_i32(value: i32) -> ()；// 打印并换行
```

其他整数类型必须显式 cast：`print_i32(x as i32)`。

**字段访问** `a.b`：按接收者的结构体类型找字段（引用/Box 会自动解引用），没有该字段就是编译错误。
**结构体构造** `S { x: 1, y: 2 }`：字段必须写全、类型匹配；`Self { .. }` 在 impl 内指当前结构体。

### 6.8 常量求值（数组长度就靠它）

常量只允许出现在三个地方：
1. `const` 项的初始化器
2. 数组类型长度 `[T; N]`
3. 重复数组长度 `[v; N]`

而"常量值"的语法被限制得很死，只有：整数字面量、`true`/`false`、常量路径（`N`、`Config::LIMIT`、`Self::LIMIT`）、`-` 加一个值、以及括号。所以：

```rust
const N: usize = 3;
const M: isize = -1isize;      // 注意负数要么有后缀，要么声明类型是 i32 等（细节看规范）
const K: usize = N + 1;        // 错误！ConstValue 不允许加法（K 的值必须是受限形式）

fn main() -> () {
    let a: [i32; N] = [1, 2, 3];   // N 求值为 3
    let b = [7; 4];                // 长度 4
}
```

要实现：
- 收集所有常量（顶层 + 关联），建立"常量名 → 值"的求值；常量可以互相引用，**声明顺序无关**；
- 检测循环依赖（`A` 引用 `A` 自己，或 `A→B→A`）= 编译错误；
- 数组长度必须是合法的常量，值转成 `usize`；
- `const` 项初始化器的类型按"期望类型"规则确定（有声明类型时）。

### 6.9 derive 与内建 trait（Copy/Clone/PartialEq/Eq）

```rust
#[derive(Clone, PartialEq)]
struct Point { x: i32, y: i32 }
```

derive 不生成新语法，只在编译期给结构体"标记能力"：

- `Copy`：每个字段都得是 `Copy`；而且这个结构体必须**同时显式 derive 了 Clone**。
- `Clone`：每个字段都得是 `Clone`（`&mut T` 不是）。
- `PartialEq`：每个字段都得是 `PartialEq`，`==`/`!=` 才能用。
- `Eq`：每个字段都是 `Eq`，且必须已经 derive 了 `PartialEq`。

基础类型的能力表（背下来）：
- 整数、bool：四种能力全有
- `&T`：Copy、Clone；相等看 T
- `&mut T`：不是 Copy 也不是 Clone；相等看 T
- `[T; N]`：跟着 T
- `Box<T>`、`Vec<T>`：不是 Copy；Clone 当 `T: Clone`；相等当 `T` 有对应能力
- 结构体：看它自己 derive 了什么

递归类型（`struct Node { children: Vec<Node> }`）也要能检查（避免死循环，可以记录"正在检查"的集合）。

### 6.10 其它必须覆盖的检查（对照规范）

- **强转与自动调整（coercion）**：`&mut T → &T`、`&S → &T`（沿字段/解引用链）、never → 任何类型。不自动把 `Box<T>` 变成 `&T`（要写 `&boxed`）。
- **递归布局**：按值包含自己的结构体/数组（`struct A { a: A }`）没有有限大小 = 编译错误；`Box`/`Vec`/`&` 打断环，允许。
- **never 与不可达代码**：`return` 等表达式类型是 never；`if` 没 else 时类型是 `()`；不可达代码里的名称/类型错误仍要报（place mutability 错误除外，那是 UB）。
- **数组**：`[elem; N]` 长度 >1 时 elem 必须 Copy；`[T; N]` 各元素独立做 coercion；索引类型必须是 usize。
- **复合赋值**：`x += y` 等价要求 x 是可变 place、基元类型匹配；不从 x 的类型反推 y 的字面量类型（`n: u32; n += 1` 是 UB，不考）。
- **求值顺序**：`place = value` 先算右边，再算 place 地址；普通二元先左后右。本阶段检查类型即可，顺序记录给阶段二用。

### 6.11 本步完成标准

- [ ] 所有正例 `exit 0`，所有负例 `exit 1`
- [ ] 用 `--dump-types`（可选）能看到每个表达式推断出的类型
- [ ] 45 个 suite 全绿（第 7 节有顺序建议）

---

## 7. 测试、调试与自测

### 7.1 怎么跑

```sh
# 全部 semantic（最常用）
make test FILTER=official:semantic STAGE=semantic

# 只跑某个 suite（目录名用 : 连接）
make test FILTER=official:semantic:names-and-shadowing VERBOSE=true

# 跑你自己的测试
make test FILTER=custom VERBOSE=true

# 编译超时不够时（比如综合测试文件大）
make test FILTER=official:semantic COMPILE_TIMEOUT=60
```

- `VERBOSE=true`：每个用例一行 PASS/FAIL，最方便
- 默认模式：按 suite 打点（`.` 通过，`F` 失败），最后汇总
- 失败详情里有 diff 和日志路径，形如 `target/tests/run-xxxx/0001/`，里面 `compile.command`（实际执行的命令）、`compile.stdout`、`compile.stderr` 都是宝

### 7.2 怎么加自己的测试

在 `tests/custom/` 下建目录，放 `.rx` + `manifest.json`：

```json
[
  { "source": "acc-ok.rx",  "stage": "semantic", "compilation_success": true,  "description": "合法程序" },
  { "source": "err-bad.rx", "stage": "semantic", "compilation_success": false, "description": "类型错误" }
]
```

建议按主题分目录：`custom/names/`、`custom/types/`、`custom/const/`…… 每写完一个功能立刻加正/负两条。

### 7.3 推荐点亮顺序（从简到繁）

1. 字面量/算术/移位/复合赋值/短路（`casts-and-literals`、`integer-arithmetic`、`shifts`、`compound-assignment`、`boolean-and-short-circuit`）
2. 名称与作用域（`names-and-shadowing`、`namespace-errors`、`protected-names`、`lifetimes-and-use`）
3. 类型推断与分支合并（`expected-types`、`blocks-if-and-never`、`loop-state-merges`、`runtime-value-merges`、`unreachable-checks`）
4. 引用与可变性（`references-and-mutability`、`reference-coercions`、`reference-lub`、`scalar-reference-operators`、`vec-index-mutability`）
5. 聚合类型（`arrays`、`structs-and-fields`、`nested-containers`、`recursive-layout`、`aggregate-arguments-and-reference-fields`）
6. 函数/方法（`calls-recursion-and-abi`、`methods-and-self`、`evaluation-order-and-temporaries`、`live-state-calls`、`loops-and-jumps`）
7. 容器与 trait（`box-and-moves`、`vec-operations`、`copy-clone-and-equality`、`trait-dispatch-and-reference-equality`）
8. 常量（`constants-and-paths`、`constant-errors`）、impl（`invalid-impls-and-generics`、`recursive-traits`）
9. 入口（`entry`、`builtin-io`、`source-main-calls`）
10. 综合（`comprehensive-*` 5 个）

### 7.4 调试小技巧

- 每个失败用例先把 `target/tests/run-*/xxxx/compile.stderr` 打开看，再手动跑 `compile.command` 里那条命令复现。
- 给自己的编译器加 `--dump-ast`、`--trace-types` 之类的调试开关，不要用 `printf` 满屏乱打。
- 遇到"规范看不懂"的，先看同 suite 的 `.rx` 和 `manifest.json` 的 `description`，再用 rustc 验证行为（注意：以规范为准，有分歧发邮件问助教）。
- 负例失败最怕"崩溃"：在本地跑一遍确认是干净的 `exit 1`。

---

## 8. 四周排期（每周有明确的完成标志）

**第 1 周：环境 + 前端管道**
- 修 CRLF、初始化子模块、编译 ANTLR runtime、跑通生成命令
- 搭好 `compiler/`、CMake、`main.cpp`、`config.mk`
- 完成 `--dump-tree` + 语法错误拒绝
- 完成 AST 节点定义 + AstBuilder 主干
- 完成标志：`make test` 跑起来不报环境错，正例至少能解析

**第 2 周：AST 完成 + 名称解析**
- AST 全量节点 + `--dump-ast` 可读
- 第一遍收集（item/字段/方法/常量）、第二遍作用域栈、遮蔽、命名空间
- 基础表达式类型检查（字面量、路径、二元、一元、赋值、简单调用）
- 点亮第 1–3 组 suite
- 完成标志：正例大部分 `exit 0`，负例能识别"未定义/重定义/类型不符"

**第 3 周：完整类型检查**
- expected type 传播、LUB、coercion、never
- place/可变性检查
- 方法查找、Box/Vec、内建 I/O、数组、结构体
- const 求值、derive 能力、递归布局
- 点亮第 4–8 组 suite
- 完成标志：只剩入口和综合 suite

**第 4 周：收尾 + 验收准备**
- 第 9–10 组 suite、全部负例专项（确认干净 exit 1）
- 大文件编译时间检查（<30s），清理调试输出、补注释
- Code Review 预演：自己讲一遍 AST 设计图、类型检查流程、错误处理
- 完成标志：`make test FILTER=official:semantic` 全绿；**周日 23:59 前搞定**

---

## 9. 课程事务清单（别忘了）

- [ ] **每周邮件答疑记录**：强制提交；没有提问也要发邮件说明"本周无记录"
- [ ] **Code Review**：一学期至少 4 次（2–3 常规 + 1–2 抽查）；本阶段讲清楚自己的 AST/类型检查设计；避免"看不懂、像 AI 整段生成"的观感
- [ ] **第 4 周考试**：编译器设计内容；复习词法/语法/类型系统/推断
- [ ] 线下答疑（预计每周四晚）和线上邮件答疑按通知参加
- [ ] AI 使用规范：辅助可以，整段生成的那部分没分；核心代码自己写

## 10. 待确认问题（建议写进每周邮件问 TA）

- [ ] AST 验收的具体形式：现场 demo？只看 `make test`？要不要检查 AST 打印？
- [ ] `main` 缺失或签名不对时的期望行为（编译错误还是 UB）
- [x] ~~`tests/official/lexer`、`parser` 目录怎么用~~ → 已查清：runner 跳过这两个 stage；官方用例是片段测试，按 `metadata.entry` 选解析入口并检查 EOF 后，g4 生成物与官方预期 495/495 一致（详见 2.3.1），可作为 parser 自测使用。若想确认"是否需要交 lexer/parser 自测结果"，仍可问 TA
