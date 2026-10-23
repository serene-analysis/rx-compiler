cmake_minimum_required(VERSION 3.15)
project(rxcc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 供 VSCode IntelliSense / clangd 读取编译参数（-I 生成目录、ANTLR include 等）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ANTLR 运行时（已装在 ~/antlr4-4.13.1；HINTS 让 VSCode 插件不带 -DCMAKE_PREFIX_PATH 时也能找到）
find_package(antlr4-runtime 4.13.1 REQUIRED HINTS "$ENV{HOME}/antlr4-4.13.1")

# 默认使用仓库里已生成好的 ANTLR C++ 源码，构建不依赖 Java/ANTLR jar。
# 改过 grammar 后本地执行：cmake -S compiler -B build -DRX_REGENERATE_ANTLR=ON
option(RX_REGENERATE_ANTLR "Regenerate ANTLR C++ sources (requires Java and ANTLR jar)" OFF)

set(GEN_SRCS
    RxLexer.cpp
    RxParser.cpp
    RxParserBaseVisitor.cpp
    RxParserBaseListener.cpp
    RxParserListener.cpp
    RxParserVisitor.cpp)

if (RX_REGENERATE_ANTLR)
    find_package(Java REQUIRED COMPONENTS Runtime)
    set(ANTLR_JAR /usr/local/lib/antlr-4.13.1-complete.jar CACHE FILEPATH "ANTLR complete jar")
    set(WRAPPER_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/grammar)
    set(ORIG_GRAMMAR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../grammar)
    set(GEN_DIR          ${CMAKE_CURRENT_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${GEN_DIR})
    list(TRANSFORM GEN_SRCS PREPEND "${GEN_DIR}/")
    add_custom_command(
        OUTPUT ${GEN_SRCS}
        COMMAND ${Java_JAVA_EXECUTABLE} -jar ${ANTLR_JAR} -Dlanguage=Cpp -visitor
                -lib ${ORIG_GRAMMAR_DIR}
                -o ${GEN_DIR}
                ${WRAPPER_DIR}/RxLexer.g4 ${WRAPPER_DIR}/RxParser.g4
        DEPENDS ${WRAPPER_DIR}/RxLexer.g4 ${WRAPPER_DIR}/RxParser.g4
                ${ORIG_GRAMMAR_DIR}/Lexer.g4 ${ORIG_GRAMMAR_DIR}/Parser.g4
        COMMENT "Generating ANTLR C++ sources")
else()
    set(GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)
    list(TRANSFORM GEN_SRCS PREPEND "${GEN_DIR}/")
    foreach (src ${GEN_SRCS})
        if (NOT EXISTS ${src})
            message(FATAL_ERROR "Missing generated ANTLR source: ${src}. Run cmake with -DRX_REGENERATE_ANTLR=ON.")
        endif()
    endforeach()
endif()

file(GLOB_RECURSE MY_SRCS CONFIGURE_DEPENDS src/*.cpp)

add_executable(rxcc ${MY_SRCS} ${GEN_SRCS})
target_include_directories(rxcc PRIVATE ${GEN_DIR} ${ANTLR4_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rxcc PRIVATE antlr4_static)
