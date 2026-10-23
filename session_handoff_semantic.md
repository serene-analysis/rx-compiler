# Rx 编译器 Semantic 阶段交接文档

> 用途：开新 session 时，让新 assistant 读这个文件即可接上进度。
> 建议新 session 第一句话：
>
> "读 session_handoff_semantic.md 和 now_todolist_3.md，以及 compiler/src/semantic/ 下的 type.h、symbol.h、checker.h、checker.cpp，继续 Phase 4。我写代码为主，你负责 review/解释/按需帮我改；遵守 rule_explanation.md 的 AI 使用规定。"

---

## 1. 项目背景

- 仓库：`/mnt/c/Users/Lenovo/Desktop/大二上/Compiler/rx-compiler`
- 目标：Rx（Rust 子集）编译器，阶段：AST（已完成并验收）→ Semantic（当前）→ IR（第 8 周节点）→ CodeGen → Optimize。
- 语义阶段验收命令：
  ```sh
  make test FILTER=official:semantic STAGE=semantic
  ```
  正例 exit 0，负例 exit 1，不许崩溃/超时。当前结果：**150 passed / 86 failed**（共 236 个 semantic 用例）。
- 课程规定：
  - `rule_explanation.md`：整段 AI 生成 = **连续超过 300 行（不含空行）**；CI/CD 从下次 Review 起必须有记录。
  - 用户自己写代码为主，assistant 负责讲解、review、指出 bug；用户明确要求时才直接改。
  - Code Review 需要能讲清楚实现，不要大段生成后不解释。

## 2. 构建 / 环境

- 构建：
  ```sh
  cmake --build target/build -j
  ```
- 单 suite 测试：
  ```sh
  make test FILTER=official:semantic:references-and-mutability STAGE=semantic VERBOSE=true
  ```
- 环境已修好：
  - `compiler/CMakeLists.txt` 有 `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)`；
  - `.vscode/c_cpp_properties.json` 的 includePath 有 `${workspaceFolder}/target/build/generated`，compileCommands 指向 `target/build/compile_commands.json`；
  - `.github/workflows/test.yml` 已从 `.disabled` 恢复并补了 Java、ANTLR 4.13.1（jar + C++ runtime 装到 `$HOME/antlr4-4.13.1`）、reference `librx.rlib` 构建；
  - CI 目前跑全量 `make test`（用户选择"保持全量、暂时红"），semantic 完成前会红。
- ANTLR 生成头在 `target/build/generated`；改 `ast_builder.cpp` 后直接 `cmake --build`。
- 旧 session 的调试经验在 `log.md`；`/tmp/opencode` 可能被清空，不要依赖。

## 3. Semantic 文件现状

### 3.1 `type.h`（已完成）

`semantic::Type`：

```cpp
enum kTypeKind { BUILTIN, STRUCT, BOX, VEC, REF, ARRAY, UNIT, NEVER, ERROR };
kTypeKind kind_ = ERROR;
std::string name_ = "AAAunnamedType";
Type* elem_ = nullptr;      // REF/ARRAY/BOX/VEC
bool isMut_ = false;        // REF
unsigned arrayLength_ = 0;  // ARRAY
```

工厂：`makeBuiltin/makeStruct/makeBox/makeVec/makeRef/makeArray/makeUnit/makeNever/makeError`（全部 `new`，不释放）。

`typeEqual`：
- 先比指针；null → false；
- **任一方 ERROR → true**（错误不级联）；
- kind 不同 → false；
- BUILTIN/STRUCT 按名字；BOX/VEC 递归 elem；REF 比 `isMut_ + elem`；ARRAY 比长度 + elem；UNIT/NEVER 同 kind 即 true；
- 最后 `throw "typeEqual(no type met)"`（防御性，正常到不了）。

### 3.2 `symbol.h`（已完成）

```cpp
using semType = semantic::Type;

struct funcSignature {
    std::vector<semType*> params_;   // 不含 self
    semType* retType_ = nullptr;
    bool isMethod_ = false;          // 当前含义：有接收者（见下）
    bool selfRef_ = false;           // &self / &mut self
    bool selfMut_ = false;           // &mut self / mut self
};

struct structSignature {
    std::vector<std::pair<std::string, semType*>> fields_; // 声明序
    std::vector<std::string> derives_;
};

struct Constant { bool isBool_ = false; long long value_ = 0; Constant(bool isBool=false, long long value=0); };

struct constSignature {
    semType* type_ = nullptr;
    ConstValue* value_ = nullptr;    // AST 语法节点
    enum kState { UNTOUCHED, CALCULATING, DONE };
    kState state_ = UNTOUCHED;
    Constant real_;                  // 求值缓存
};

struct globalTable {
    std::map<std::string, funcSignature> funcSig;
    std::map<std::string, structSignature> structSig;
    std::map<std::string, constSignature> constSig;
    std::map<std::string, std::map<std::string, funcSignature>> methods;
    std::map<std::string, std::map<std::string, constSignature>> associatedConsts;
};

struct loopInfo {
    semType* expected_ = nullptr;    // loop 的 expected（来自 expect）
    std::vector<semType*> breaks_;   // break 值类型（源码顺序）
    bool barrier_ = false;           // while 条件期的屏障
};

struct localShadowing { semType* type_ = nullptr; bool isMut_ = false; ... };
struct stackShadowing { name_, shadow_, depth_ };

struct scopeStack {
    std::map<std::string, std::stack<localShadowing>> memory_;
    std::stack<stackShadowing> stk_;
    int currentDepth = 0;
    void levelUp(); void levelDown();
    void push(name, localShadowing);
    localShadowing* find(name);      // 从内到外；会判空栈（memory_ 留空栈是正常的）
};
```

注意：
- `isMethod_` 当前语义 = **有接收者**（by-value `self` 和 `&self`/`&mut self` 都 true；impl 里的关联函数为 false）。后续方法查找要用它区分 dot call 候选。
- `param.isMut_` 的 AST 坑：`&mut self` 会把 `Param::isMut_` 置 true（那个 mut 属于引用类型），所以方法签名里 `&mut self` 用 `self.type_->isMut_` 判断，`mut self` 才用 `Param::isMut_`。

### 3.3 `checker.h`

当前方法列表：

```cpp
void checkCrate(Crate*);
// collect 三遍 + 常量
Constant calculateConst(ConstValue*, std::string owner);
Constant manifestConst(std::string owner, std::string name);
constSignature* findConst(const std::string& owner, const std::string& name);
bool checkConstInit(const ConstValue*, semType* expected, const std::string& owner);
void collect(Crate*);
void evaluateConstants(Crate*);
semType* transformType(Type*);
void evaluateTypes(Crate*);
// check
semType* lub(std::vector<semType*> types);
void checkItem(Item*);
void checkFunction(funcItem*, const std::string& owner);
semType* checkBlock(blockExpr*, semType* wish);
semType* checkStatement(Stmt*);
semType* checkLoop(loopExpr*, semType* wish);
bool isPlaceable(Expr*);
bool isMutablePlaceable(Expr*);
semType* lookupValue(std::string name);
semType* infer(Expr*);
semType* expect(Expr*, semType* wish);
void error(std::string);

globalTable table_;
scopeStack scope_;
std::vector<loopInfo> loops_;
semType* currentRet_ = nullptr;
std::string currentStruct_;
bool reject_ = false;
```

成员/常量：
- `checkCrate` 顺序：`collect` → `evaluateConstants` → `evaluateTypes` → 遍历 items `checkItem`；每步前有 `if (reject_) return;`。
- `error(msg)` 打印到 stderr 并 `reject_ = true`。
- `main.cpp`：`Checker checker; checker.checkCrate(crate); if (checker.reject_) throw 0;`，`throw 0` 走 `catch(...)` 打印 `internal error` 后 return 1。**功能正确但观感差**，可改成 `return checker.reject_ ? 1 : 0;`（可选）。

### 3.4 `checker.cpp` 现状（1126 行，1095 非空）

**文件内静态 helper：**

```cpp
intSuffix(text)            // 后缀 usize/isize/u32/i32，返回后缀或 ""
isIntegerType/isSignedType/isBoolType(semType*)
isNever(semType*)
matches(actual, expected) = isNever(actual) || typeEqual(actual, expected)
digOneRef(t)               // REF -> elem，否则原样
canImplicitlyTurn(from,to) // 目前：never / typeEqual / (&mut T -> &T 且 referent 相等)
```

**collect 三遍 + 常量求值（已完成）：**

1. 第 1 遍登记：
   - 顶层 fn/const 共享值空间，交叉查重；函数参数重名；protected I/O 名（顶层 fn/const 报错；impl 内允许）；
   - struct：重名、protected `i32/u32/isize/usize`、字段重名；derives 在外层循环/3b 填；
   - impl：目标必须是 STRUCT（非 STRUCT 当场报错）；impl 内的方法/关联常量共用一张内层 map，交叉查重；跨 impl 重复也算错；
   - 末尾扫 `methods`/`associatedConsts` 的 key 是否在 `structSig`（未知 struct 的 impl 报错）；
   - **内置函数在这里注册**：
     ```cpp
     get_i32() -> i32
     print_i32(i32) -> ()
     println_i32(i32) -> ()
     ```
2. 第 2 遍 `evaluateConstants`：遍历 crate items，对每个顶层/关联 const 调 `manifestConst(owner,name)` 强制求值，环检测靠三态。
3. 第 3 遍 `evaluateTypes`：
   - 3a：解析所有常量声明类型（顶层 owner ""，关联 owner=impl 目标，`currentStruct_` save/restore）；
   - 3a-check：`checkConstInit` 对每个常量初始化式与声明类型做检查；
   - 3b：struct 字段类型 + `derives_`，`currentStruct_ = 结构体名`（支持 `Self`），字段按声明序；
   - 3c：顶层函数签名 + impl 方法签名。方法：跳过 self，填 `params_/retType_`，`isMethod_=有接收者`，`selfRef_/selfMut_` 按 self 类型/`mut self` 设置；**3c 不再重复解析关联常量类型**。

**常量求值细节：**

- `findConst(owner,name)`：只查不插入；owner 空查顶层，否则查 `associatedConsts[owner]`。
- `manifestConst`：find → 不存在报 unresolved；DONE 返回缓存；CALCULATING 报循环；否则置 CALCULATING → `calculateConst(value_, owner)` → 写缓存 → DONE。
- `calculateConst`：
  - INT：去后缀、去 `_`、识别 `0o/0b/0x`，`std::stoll(literal, 0, base)`；
  - NEG：inner 是 bool → 错；inner 是带 `u32/usize` 后缀的 INT → 错；否则取负；
  - PATH：1 段 → `manifestConst("", path)`；2 段 `Self::X` → owner 空报错，否则 `manifestConst(owner, X)`；`Type::X` → `manifestConst(Type, X)`；3 段以上报错；
  - BOOL：`text_ == "TRUE"`（AST builder 存大写）。
- `checkConstInit(value, expected, owner)`：BOOL→bool；INT 有后缀须等于 expected，无后缀 expected 必须是四种整数；NEG→expected 必须 signed，递归 inner；PATH→目标 `type_` 与 expected `typeEqual`；expected ERROR 直接放行。

**transformType：**

- BUILTIN → makeBuiltin；STRUCT → `Self` 用 `currentStruct_`（空报错），否则查 `structSig`（不存在报错，返回 `makeError()`）；
- BOX/VEC/REF 递归；UNIT；
- ARRAY：
  - 长度是 INT：有后缀且不是 `usize` → 错；无后缀 → expected usize；
  - 长度是 PATH：解析 1/2 段（`Self` 用 `currentStruct_`），`findConst` 查目标，要求 `type_ == usize`；
  - BOOL/NEG → 错；
  - 最后 `calculateConst(arrayLength_, currentStruct_)` 取数值，`makeArray`。
- 失败都 `return semantic::makeError()`；末尾 `throw` 防御。

**check 阶段（Phase 2 + Phase 3 已完成大部分）：**

- `lookupValue(name)`：`scope_.find` → 顶层 `findConst("", name)`；函数不作值（UB）。
- `isPlaceable`：1 段 path 且局部存在；field/index 递归；deref 恒真；其他 false。
- `isMutablePlaceable`：局部看 `isMut_`；field/index 递归；deref 看操作数类型 REF 且 isMut（Box 待 Phase 5）。
- `infer` 已实现分支：
  - intLit（后缀或 i32）、boolLit、unit、block（`checkBlock(block, nullptr)`）；
  - pathExpr：1 段 `lookupValue`；2 段 `Self::`（currentStruct_ 空报错）或 `Type::` 查 associated consts；3+ 段报错；
  - callExpr：单段 path；局部同位报 "shadowed"；查 `funcSig`；参数个数 + 逐个 `expect`；返回 `retType_`；2 段 path 暂 `return makeError()`（Phase 5）；
  - unary：`-` 要求 signed；`!` 要求 int/bool；返回操作数类型；
  - if：cond bool；有 else → `lub({then,else})`；无 else → then 必须 unit/never，返回 unit；
  - while：push barrier 检查 cond → pop；push unit loopInfo 检查 body → pop；返回 unit；
  - loop：`checkLoop(loop, nullptr)`；
  - break：`loops_` 空报错；栈顶 barrier 报错；tail 有 expected 时 `expect`，否则 infer；push 到 `top.breaks_`；返回 never；
  - continue：`loops_` 空/栈顶 barrier 报错；返回 never；
  - return：tail `expect(currentRet_)`；无 tail 且 `currentRet_` 非 unit 报错；never；
  - cast：`ltype == ERROR` 静默返回 `rtype`；只允许 int/bool → int；
  - deref：操作数 ERROR 静默；REF → elem；否则报错（Box 待 Phase 5）；
  - ref：`makeRef(infer(operand), isMut)`（不做 place 检查）；
  - binary：
    - `=`：`isMutablePlaceable(lhs)`，`expect(rhs, infer(lhs))`，unit；
    - 复合赋值：10 种全部；lhs 可变异 place；
      - `+= -= *= /= %=`：lhs 整数，rhs `digOneRef` 后同类型；
      - `&= |= ^=`：lhs 整数或 bool，rhs 剥一层后同类型；
      - `<<= >>=`：lhs 整数，rhs 剥一层后任意整数；
    - 非赋值：
      - `+ - * / %`：两边整数，rhs `digOneRef`，同类型 → lhs 类型；
      - `& | ^`：整数或 bool，rhs `digOneRef`，同类型 → lhs 类型；
      - `<< >>`：lhs 整数，rhs `digOneRef` 后整数 → lhs 类型；
      - `< <= > >=`：lhs 与 rhs（`digOneRef`）同类型，整数或 bool → bool；
      - `== !=`：`typeEqual(lhs, rhs)`（不剥引用）→ bool；
      - `&& ||`：同类型且 bool → bool；
  - 未实现（返回 makeError 占位）：fieldExpr、indexExpr、structInitExpr、arrayExpr、repeatedArrayExpr、methodCallExpr、2 段 call。
- `expect(expr, wish)`：
  - wish null → infer；
  - intLit：无后缀且 wish 整数 → wish；有后缀须等于 wish；
  - block → `checkBlock(block, wish)`；
  - loop → `checkLoop(loop, wish)`；
  - if：cond bool；有 else → `expect(then,wish)+expect(else,wish)` → wish；无 else → then 按 unit，结果 unit（wish 必须 unit）；
  - arrayExpr：wish 是 ARRAY → 长度匹配 + 每个元素 `expect(elem, wish->elem_)`；
  - fallback：`infer` + `matches(type,wish)`，返回 wish。**注意：还没有把 canImplicitlyTurn 接进来（Phase 4 要做）。**
- `checkBlock`：
  - `levelUp`；`reachable=true`；逐条 `checkStatement`，`isNever(last) → reachable=false`（后续仍继续查）；
  - 有 tail → `expect(tail, wish)`；无 tail → `reachable ? unit : never`；
  - `wish != nullptr && !matches(ret, wish)` → 报错（`levelDown` 后返回 `makeError`）；
  - `levelDown` 返回 ret。
- `checkLoop(loop, wish)`：
  - push `loopInfo{wish,{}}`；`checkBlock(loop->block_, unit)`；pop；
  - breaks 空 → never；wish 非空 → wish；否则 `lub(info.breaks_)`。
- `checkStatement`：
  - let：先 `expect(init, 声明类型或 nullptr)`，再 push（`isNever(init) ? init : unit`）；
  - exprStmt：`infer(expr)`；若 `!hasSemi_ && !matches(type, unit)` 报错；返回 type。
- `lub(types)`：
  - 按源码顺序；跳过 never；ERROR 直接返回；
  - 第一个非 never 作 target；`canImplicitlyTurn(u,t)` → 保持；否则 `canImplicitlyTurn(t,u)` 且所有先前结果都能转到 u → 换 target；否则报 "lub failed"；
  - 全 never → never。
- `canImplicitlyTurn` 目前最小：never / 相等 / `&mut T → &T`（referent 相等）。

**AST 相关改动：**

- `ast.h` 的 `exprStmt` 加了 `bool hasSemi_ = true;`
- `ast_builder.cpp` 的 `visitStatement` 里对 `expressionWithBlock` 设 `now->hasSemi_ = (ctx->SEMI() != nullptr);`
- 其余 AST 未动。注意之前发现但**未修**的 AST bug：混合 `<<`/`>>` 链在 `ast_builder.cpp` 的 8 个 shift visitor 里（用户后来用统一的 `buildShiftChain` 方案修过；如果新 session 发现 shift AST 问题，先查 `buildShiftChain`）。**当前各 suite 里 shift 相关 semantic 用例都过，说明修好了。**
- `Param.isMut_` 对 `&mut self` 为 true 的坑见 3.2。

## 4. 测试现状（关键）

### 已全绿

Phase 2 六个 suite：**35/35**
- `names-and-shadowing`
- `casts-and-literals`
- `integer-arithmetic`
- `shifts`
- `compound-assignment`
- `boolean-and-short-circuit`

Phase 3 五个 suite：**20/21**
- `expected-types`
- `blocks-if-and-never`
- `loops-and-jumps`
- `loop-state-merges`
- `runtime-value-merges` ← 唯一失败：`acc-input-dependent-reference-aggregate-and-owner-results.rx`，用了 struct/数组/Box/Vec/方法，等 Phase 5。

### 全量失败分布（86 个，按 suite）

```
12  copy-clone-and-equality        Phase 6
10  vec-index-mutability           Phase 4/5
 7  structs-and-fields             Phase 5
 6  vec-operations                 Phase 5
 5  methods-and-self               Phase 5
 4  reference-coercions            Phase 4
 4  recursive-layout               Phase 6
 4  entry                          Phase 6
 4  arrays                         Phase 5
 3  scalar-reference-operators     Phase 4
 3  references-and-mutability      Phase 4
 3  nested-containers              Phase 5
 3  invalid-impls-and-generics     多为 Phase 6 收尾
 3  box-and-moves                  Phase 5
 2  trait-dispatch-and-reference-equality  Phase 5/6
 2  recursive-traits               Phase 6
 2  evaluation-order-and-temporaries Phase 5
 2  constant-errors                repeat array（Phase 5）
 1  runtime-value-merges           Phase 5
 1  constants-and-paths            repeat array（Phase 5）
```

注意：不少 positive 能过是因为未实现节点返回 ERROR、`typeEqual(ERROR, X)` 为 true；**不要用通过数判断功能是否真的实现**，以 negative 为准。

## 5. 下一步：Phase 4（引用与可变性）

按顺序做：

1. **补全 `canImplicitlyTurn`**（coercion 表）：
   - never → 任意；
   - 严格相等；
   - `&mut T → &T`（referent 相等）；
   - `&S` / `&mut S → &T`：从 S 沿 `&U→U`、`&mut U→U`、`Box<U>→U` 逐层解引用，直到 referent 等于 T，再取 `&T`；
   - `&mut S → &mut T`：外层必须 `&mut`，路径上不能经过共享引用；
   - Vec 不解引用；Box 本身不能直接变 T/&T。
2. **把 coercion 接到 one-to-one 位点**：`expect` 的 fallback 改成
   ```cpp
   if (!matches(type, wish) && !canImplicitlyTurn(type, wish)) error(...);
   ```
   这样 `let` 标注、赋值右侧、调用实参、`return`、函数尾都会自动支持 `&mut→&`、`&Box<T>→&T`。注意别破坏字面量 expected、block/if/loop 穿透。
3. **完整 `isMutablePlaceable`**：
   - field/index：基址可变才算；**穿过共享引用后不可恢复**；
   - `*p`：`&mut T` 可写、`&T` 不可写；`Box<T>` 看 Box 这个 place 是否可变（Phase 5 细化）；
   - 局部看 `localShadowing::isMut_`。
4. **借用检查**：
   - `&mut e`：若 e 是 place 形状，要求 `isMutablePlaceable(e)`；e 是字面量/临时值（`&mut 1`、`&mut &1`）放行；
   - `&e`：不做严格 place 检查（测试允许借字面量/临时值）。
5. **scalar-reference-operators**：
   - 算术/位运算：两边各自 `digOneRef` 后同类型（现在只剥了 rhs，lhs 没剥）；
   - 比较 `< <= > >=`：两边各自剥一层后同类型，且引用的 mut 必须匹配（`&i32` vs `&mut i32` 不能比）；
   - `== !=`：整体 `typeEqual`，不剥。
6. `vec-index-mutability` 依赖 Vec::new/index/Vec 方法，和 Phase 5 交叉，建议 Phase 5 一起做。

测试：
```sh
make test FILTER=official:semantic:references-and-mutability STAGE=semantic
make test FILTER=official:semantic:reference-coercions STAGE=semantic
make test FILTER=official:semantic:reference-lub STAGE=semantic
make test FILTER=official:semantic:scalar-reference-operators STAGE=semantic
```

## 6. 之后：Phase 5（聚合/方法/内建，最大头）

要补的 `infer` 分支/逻辑：

1. **fieldExpr**：接收者类型，自动解引用（`&T`/`&mut T`/`Box<T>` 逐层，Vec 不自动解），查 `structSig[struct].fields_`；字段不存在报错。
2. **indexExpr**：接收者是 ARRAY 或 VEC；index 的 expected 是 `usize`；元素类型；Vec 索引的隐式借用/可变性留到 `vec-index-mutability`。
3. **structInitExpr**：path 是类型名（1 段即可），查 struct；字段必须齐全、无重复、类型匹配（按字段声明类型 `expect`），返回 STRUCT。
4. **arrayExpr / repeatedArrayExpr**：
   - 数组字面量：元素 LUB；长度 = 元素个数；有 expected `[T;N]` 时已在 expect 里查；
   - repeat：value 类型；长度是常量上下文，必须 usize；返回 `[T; N]`；`[v; N]` 的 N 是 ConstValue，求值和 usize 检查复用 `transformType` 的逻辑/helper。
5. **methodCallExpr + 方法查找**：
   - 候选链：T 本身；T 是 `&U`/`&mut U`/`Box<U>` 时逐层解引用（Vec 不解）；每层把 `&T`/`&mut T` 作为接收者的方法也加入；
   - 找唯一匹配；选中后检查接收者可变性（`&mut self` 要求可变 place；by-value 处理 move/copy 先不做）；
   - 实参逐个 `expect`（Phase 4 后换 `coerceTo`）。
6. **2 段 call path**：`S::assocFn()`、`Self::f()`、`Box::<T>::new`、`Vec::<T>::new`、`Box::<T>::clone`。
   - path 的 turbofish 类型实参在 `path_[0].types_`；
   - 用户 struct：查 `methods[head][name]`；`isMethod_` 为 true（有 receiver）时不能当无接收者调用；
   - Box/Vec 内建：`new`、`clone` 等，校验类型实参数量并 `transformType`。
7. **内建**：
   - `Box<T>`：`Box::<T>::new(v)`；`.clone()` 需 T: Clone（Phase 6 能力）；
   - `Vec<T>`：`Vec::<T>::new()`、`.len()->usize`、`.is_empty()->bool`、`.push(v)`、`.remove(i)->T`、`.clone()`；
   - `[T; N]`：`.len()->usize`；
   - `Box<T>` 的 `*b` 已能通过（deref REF；Box 要在 deref 分支补 `BOX -> elem`）。
8. `evaluation-order-and-temporaries`、`live-state-calls` 等主要是类型检查，不需要真的模拟求值顺序。

## 7. 最后：Phase 6（derive/布局/入口）

1. **derive 能力表**（`copy-clone-and-equality` 12 个、`recursive-traits` 2 个）：
   - `Copy`：所有字段 Copy，且**必须同时 derive Clone**；
   - `Clone`：所有字段 Clone（`&mut T` 不是）；
   - `PartialEq`：所有字段 PartialEq；
   - `Eq`：所有字段 Eq，且**必须已 derive PartialEq**；
   - 基础表：整数/bool 全有；`&T` Copy/Clone/PartialEq（看 T）；`&mut T` 无 Copy/Clone，PartialEq 看 T；`[T;N]` 跟随 T；`Box<T>`/`Vec<T>` 无 Copy，Clone 看 T，PartialEq 按元素；struct 看 derive；
   - 递归 struct 要带"正在检查"集合；
   - `==`/`!=` 要求 PartialEq；`.clone()` 要求 Clone。
2. **递归布局**（`recursive-layout` 4 个）：按值 struct/array 环报错；Box/Vec/引用打断环；内联数组不打断。
3. **main 入口**（`entry` 4 个）：`fn main() -> ()`，无参数、无泛型；返回可省略。
4. **invalid impls / 保护名收尾**（`invalid-impls-and-generics` 剩余 3 个）。
5. `trait-dispatch-and-reference-equality` 2 个：引用相等性/PartialEq 能力。

## 8. 已知坑 / 设计决定

- **ERROR 类型是"已出错/未实现"**：`typeEqual` 与任何类型相等；`matches` 只额外放行 never。新增检查时遇到 ERROR 应静默返回，避免级联误报（deref/cast 已加）。
- **never**：`return/break/continue` 返回 NEVER；无 tail 的块按"最后可达语句是否 never"决定块类型；有 tail 时 tail 决定。`checkBlock` 用 `matches` 比较 wish。
- **loop**：break 值即使在不可达代码里也要收集；while 条件用 `loopInfo.barrier_` 阻止跳外层；break 通过 `loops_.back()` 记录，嵌套 loop push 自己的 context 天然分层。
- **Self**：`currentStruct_` 在 3a 关联常量类型、3b 字段、3c 方法签名、`checkFunction` 函数体、`transformType` 的 ARRAY 长度计算里都要设置；`Self::` 在顶层报错。
- **命名空间**：函数/常量共享值空间；struct 在类型空间；函数与 struct 可同名；局部遮蔽函数后不回退。
- **常量**：pass 2 必须全量强制求值（否则没人引用的环漏检）；pass 3 先解析所有常量声明类型，再检查初始化式（顺序依赖）。
- **方法签名**：`isMethod_` = 有 receiver；`&mut self` 用 `type_->isMut_`，`mut self` 用 `Param::isMut_`。
- **coercion 目前只在 `canImplicitlyTurn` 里用于 LUB**，还没接入 `expect`（Phase 4）。
- `typeEqual` 的 UNIT/NEVER 分支：同 kind 才 true；跨 kind 由 `matches` 处理 never。
- `main.cpp` 的 `throw 0` hack 会给负例打印 "internal error"，不影响 exit code；可清理。
- AST 的 `hasSemi_` 是为"非末尾无分号 block 语句必须 unit"加的；修改 AST 后记得同步 ast_dump（不必显示）。
- CI 的 `test.yml` 已有 ANTLR/Java/reference rlib；semantic 全绿前 CI 仍会红（用户接受）。

## 9. 建议的新 session 起手动作

1. 读本文件 + `now_todolist_3.md` + `compiler/src/semantic/{type.h,symbol.h,checker.h,checker.cpp}`。
2. 跑：
   ```sh
   cmake --build target/build -j
   make test FILTER=official:semantic STAGE=semantic
   ```
   确认基线 150/86。
3. 从 Phase 4 第 1 步（完整 `canImplicitlyTurn`）开始；每改一块跑对应 suite，防止回归 Phase 2/3。
4. 用户仍以自己写代码为主，assistant 负责指出具体问题、给最小修正建议；用户说"你帮我改"时再动文件。
5. 注意 `rule_explanation.md`：连续超过 300 行非空 AI 生成才违规；小段修复/示例没问题。
