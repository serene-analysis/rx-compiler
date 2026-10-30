#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "antlr4-runtime.h"
#include "RxLexer.h"
#include "RxParser.h"

#include "ast/ast.h"
#include "ast/ast_dump.h"
#include "frontend/ast_builder.h"
#include "semantic/checker.h"
#include "ir/generator.h"

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
    // 用法：rxcc [--dump-tree] [--dump-ast] --stage semantic <源文件>
    bool dumpTree = false;
    bool dumpAst = false;
    std::string stage;
    std::string sourcePath;
    std::string outputPath;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dump-tree") {
            dumpTree = true;
        } else if (arg == "--dump-ast") {
            dumpAst = true;
        } else if (arg == "--stage" && i + 1 < argc) {
            stage = argv[++i];
        } else if (arg == "-o" && i + 1 < argc) {
            outputPath = argv[++i];
        } else {
            sourcePath = arg;
        }
    }
    if ((stage != "semantic" && stage != "ir") || sourcePath.empty()) {
        std::cerr << "usage: rxcc --stage semantic|ir [--dump-tree] [--dump-ast] "
                     "[-o output.ll] <source.rx>\n";
        return 1;
    }
    if (stage == "ir" && outputPath.empty()) {
        std::cerr << "rxcc --stage ir requires -o <output.ll>\n";
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
        if (dumpTree) {
            std::cout << root->toStringTree(&parser) << "\n";
        }
        ASTVisitor visitor;
        auto* crate = std::any_cast<Crate*>(visitor.visit(root));
        if (dumpAst) {
            dumpCrate(crate);
        }

        Checker checker;
        checker.checkCrate(crate);
        if (checker.reject_) {
            return 1;
        }

        if (stage == "ir") {
            IRGenerator generator;
            generator.generate(checker, crate, outputPath);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "internal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "internal error\n";
        return 1;
    }
}
