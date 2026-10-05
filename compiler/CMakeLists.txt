cmake_minimum_required(VERSION 3.15)
project(rxcc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ANTLR 运行时（已装在 ~/antlr4-4.13.1；HINTS 让 VSCode 插件不带 -DCMAKE_PREFIX_PATH 时也能找到）
find_package(antlr4-runtime 4.13.1 REQUIRED HINTS "$ENV{HOME}/antlr4-4.13.1")
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
target_include_directories(rxcc PRIVATE ${GEN_DIR} ${ANTLR4_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rxcc PRIVATE antlr4_static)
