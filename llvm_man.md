# Rx -> LLVM IR 语法速查

> 目标：RV32IM / ILP32，LLVM 22，opaque pointer（`ptr`）。
> 这份手册只列 Rx 编译器可能用到的 LLVM IR 语法和 lowering 形状。
> 所有例子是示意，函数名、mangling、helper 名字可以自己定。

---

## 0. 一个最小模块

```llvm
target triple = "riscv32-unknown-none-elf"

declare i32 @get_i32()
declare void @println_i32(i32)

define i32 @main() {
entry:
  call void @println_i32(i32 42)
  ret i32 0
}
```

- `target triple` 放在模块开头；
- `declare` 表示外部函数，只有签名；
- `define` 表示本文件定义的函数；
- 函数体由基本块组成；
- 每个基本块必须以 `ret` / `br` / `unreachable` 等终止指令结束。

---

## 1. 注释、标识符、标签

```llvm
; 这是注释

%local = alloca i32       ; 局部 SSA 名/栈槽
@global = global i32 0    ; 全局符号
entry:                    ; 基本块标签
```

- SSA 值和全局名以 `%` / `@` 开头；
- 基本块标签以 `:` 结尾；
- 名字可以自己生成：`%t0`、`%t1`、`%bb0`；
- 不要用 `%` 后接纯数字以外的不安全字符；用递增编号最省事。

---

## 2. 类型

| Rx 类型 | LLVM 类型 |
|---|---|
| `i32` / `isize` | `i32` |
| `u32` / `usize` | `i32` |
| `bool` | SSA 用 `i1`；内存用 `i8` |
| `&T` / `&mut T` | `ptr` |
| `Box<T>` | `ptr` |
| `Vec<T>` | `%Vec = type { ptr, i32, i32 }` |
| `[T; N]` | `[N x T]` |
| struct `S` | `%struct.S = type { ... }` |
| `()` | `void`（函数返回/无值） |
| `!` | 不表示成值；生成 `unreachable` |

### 2.1 命名 struct

```llvm
%struct.Point = type { i32, i32 }
%struct.S = type { i8, i32 }       ; bool 字段用 i8
%struct.Pair = type { i32, ptr }   ; ptr 是引用/Box
```

字段顺序必须与源码声明顺序一致。LLVM 会按 ABI 在字段之间插入 padding，但 GEP 字段索引仍然按声明顺序：

```llvm
%p = getelementptr inbounds %struct.S, ptr %s, i32 0, i32 1
```

### 2.2 Vec

```llvm
%Vec = type { ptr, i32, i32 }   ; data, len, cap
```

`Vec<T>` 的元素类型只影响 data 指向的存储和 stride；LLVM 结构本身可以复用同一个 `%Vec`。

### 2.3 指针

LLVM 22 使用 opaque pointer：

```llvm
%p = alloca i32          ; 类型是 ptr，指向 i32
%q = load ptr, ptr %p    ; q 是 ptr 值
```

不存在 `i32*` 这种写法；GEP 需要显式写源元素类型。

---

## 3. 常量

### 3.1 整数/bool/指针

```llvm
i32 0
i32 -7
i1 true
i1 false
ptr null
```

### 3.2 聚合零值

```llvm
%struct.Point zeroinitializer
[3 x i32] zeroinitializer
%Vec zeroinitializer
```

### 3.3 字符串常量

```llvm
@.fmt_i32 = private unnamed_addr constant [3 x i8] c"%d\00"
@.fmt_i32_nl = private unnamed_addr constant [4 x i8] c"%d\0A\00"
```

- `\00` 是字符串结尾 NUL；
- `\0A` 是换行；
- `c"..."` 是字节字符串；
- 用 `getelementptr` 取首地址：

```llvm
%fmt = getelementptr inbounds [3 x i8], ptr @.fmt_i32, i32 0, i32 0
```

它对应的 C 概念就是：

```c
static const char fmt_i32[3] = { '%', 'd', '\0' };
const char *p = &fmt_i32[0];
printf(p, value);
```

逐段解释：

```llvm
@.fmt_i32 = private unnamed_addr constant [3 x i8] c"%d\00"
```

- `@.fmt_i32`：全局符号，名字以 `.` 开头只是习惯；
- `private`：只在本 `.ll` 文件可见；
- `unnamed_addr`：地址本身不重要，可以合并；
- `constant`：只读；
- `[3 x i8]`：3 个字节的数组；
- `c"%d\00"`：内容分别是 `%`、`d`、NUL。

```llvm
getelementptr inbounds [3 x i8], ptr @.fmt_i32, i32 0, i32 0
```

- 第一个参数 `[3 x i8]`：这块数据的元素类型；
- `ptr @.fmt_i32`：全局数组的地址；
- 第一个 `i32 0`：解引用全局指针，得到数组本身；
- 第二个 `i32 0`：取数组第 0 个元素；
- 结果就是 `&fmt_i32[0]`，类型是 `ptr`。

`getelementptr` 只算地址，不读内存。`printf`/`scanf` 需要的是这个首地址。

---

## 4. 函数

### 4.1 声明

```llvm
declare i32 @get_i32()
declare void @print_i32(i32)
declare void @println_i32(i32)
declare ptr @__rx_alloc(i32, i32)
declare ptr @malloc(i32)
declare ptr @memcpy(ptr, ptr, i32)
declare ptr @memmove(ptr, ptr, i32)
declare i32 @printf(ptr, ...)
declare i32 @scanf(ptr, ...)
```

### 4.2 定义

```llvm
define i32 @add(i32 %a, i32 %b) {
entry:
  %r = add i32 %a, %b
  ret i32 %r
}
```

- 参数必须写名字才能使用：`i32 %a`；
- 返回 unit：`define void @f()`，最后 `ret void`；
- 返回 bool：`define i1 @f()`；
- 函数名要唯一，建议加前缀/mangling，避免和 `main` 或运行时符号冲突。

### 4.3 调用

```llvm
%r = call i32 @add(i32 1, i32 2)
call void @println_i32(i32 %r)
```

- 有返回值：`%r = call ...`；
- 无返回值：`call void ...`；
- 外部 C 函数按 C ABI 调用；
- Rx 内部函数可以自定 ABI，但同一项目内必须一致。

### 4.4 main wrapper

源 `main` 最好改名，例如 `@rx_main`：

```llvm
define void @rx_main() {
entry:
  ret void
}

define i32 @main() {
entry:
  call void @rx_main()
  ret i32 0
}
```

不要同时定义源 `@main` 和 wrapper `@main`。

---

## 5. 内存和地址

### 5.1 alloca / load / store

```llvm
%slot = alloca i32
store i32 42, ptr %slot
%v = load i32, ptr %slot
```

带对齐（可选）：

```llvm
%slot = alloca i32, align 4
store i32 42, ptr %slot, align 4
%v = load i32, ptr %slot, align 4
```

- `alloca` 返回 `ptr`；
- `store` 的第一个类型是**被存值的类型**；
- `store` 的第二个操作数是地址 `ptr`；
- `load` 的类型是要读出的类型；
- 局部变量、参数副本、临时值都可以用 alloca。

### 5.2 GEP：只算地址

struct 字段：

```llvm
%field = getelementptr inbounds %struct.S, ptr %s, i32 0, i32 1
```

数组元素：

```llvm
%elem = getelementptr inbounds [3 x i32], ptr %arr, i32 0, i32 %idx
```

指针指向的元素（Vec data、Box 内容等）：

```llvm
%elem = getelementptr i32, ptr %data, i32 %idx
```

- 第一个索引通常是 `i32 0`，表示“先解引用当前指针”；
- 后续索引才是字段/元素下标；
- GEP 不 load，不访问内存，只产生地址；
- 对 `ptr` 做 GEP 必须给出源元素类型。

### 5.3 聚合 load/store

```llvm
%tmp = load %struct.S, ptr %src
store %struct.S %tmp, ptr %dst
```

数组同理：

```llvm
%tmp = load [3 x i32], ptr %src
store [3 x i32] %tmp, ptr %dst
```

---

## 6. 算术/位运算/比较

### 6.1 整数

```llvm
%a = add i32 %x, %y
%s = sub i32 %x, %y
%m = mul i32 %x, %y
%d = sdiv i32 %x, %y     ; signed
%u = udiv i32 %x, %y     ; unsigned
%r = srem i32 %x, %y     ; signed
%q = urem i32 %x, %y     ; unsigned
```

Rx 的 `i32/isize` 用 signed 版本，`u32/usize` 用 unsigned 版本。

### 6.2 位运算

```llvm
%a = and i32 %x, %y
%o = or  i32 %x, %y
%x = xor i32 %x, %y
```

bool 也可以用 `i1`：

```llvm
%a = and i1 %x, %y
%o = or  i1 %x, %y
```

### 6.3 移位

LLVM 的移位量必须小于位宽，否则是 poison。Rx 规定只取低 5 位，所以要先 mask：

```llvm
%sh = and i32 %amount, 31
%l  = shl  i32 %x, %sh
%ar = ashr i32 %x, %sh    ; signed 右移
%lr = lshr i32 %x, %sh    ; unsigned 右移
```

### 6.4 比较

```llvm
%c1 = icmp eq  i32 %x, %y
%c2 = icmp ne  i32 %x, %y
%c3 = icmp slt i32 %x, %y   ; signed
%c4 = icmp sle i32 %x, %y
%c5 = icmp sgt i32 %x, %y
%c6 = icmp sge i32 %x, %y

%u1 = icmp ult i32 %x, %y   ; unsigned
%u2 = icmp ule i32 %x, %y
%u3 = icmp ugt i32 %x, %y
%u4 = icmp uge i32 %x, %y
```

bool：

```llvm
%c = icmp eq i1 %x, %y
```

### 6.5 一元

```llvm
%neg = sub i32 0, %x
%noti = xor i32 %x, -1
%notb = xor i1 %x, true
```

### 6.6 cast

```llvm
%i = zext i1 %b to i32      ; bool -> i32
%t = trunc i32 %x to i8     ; 一般不需要，Rx 整数都是 32 位
```

Rx 的 int-to-int 都是 32 位，通常不需要指令，只需要后续按目标 signedness 选指令。

---

## 7. 控制流

### 7.1 无条件/条件跳转

```llvm
br label %next

%c = icmp slt i32 %x, 0
br i1 %c, label %then, label %else
```

### 7.2 if + phi

```llvm
entry:
  %c = icmp sgt i32 %x, 0
  br i1 %c, label %then, label %else
then:
  br label %merge
else:
  br label %merge
merge:
  %r = phi i32 [ 1, %then ], [ 2, %else ]
  ret i32 %r
```

也可以不用 phi，用一个 alloca 存结果：

```llvm
%tmp = alloca i32
; then: store 1, ptr %tmp
; else: store 2, ptr %tmp
; merge: %r = load i32, ptr %tmp
```

### 7.3 while

```llvm
entry:
  br label %cond

cond:
  %c = icmp slt i32 %i, 10
  br i1 %c, label %body, label %exit

body:
  ; ...
  br label %cond

exit:
  ret void
```

### 7.4 loop / break / continue

```llvm
entry:
  br label %body

body:
  ; ...
  br i1 %cond, label %body, label %break

break:
  ; loop 之后
```

- `continue` 跳到循环 body 的起点/条件块；
- `break` 跳到循环的 exit；
- 带值的 loop 可以用 alloca 或 phi 收集 break 值。

### 7.5 return / unreachable

```llvm
ret void
ret i32 %v
ret i1 %b

unreachable
```

`ret` 必须和函数返回类型一致；never 路径用 `unreachable`。

---

## 8. 聚合值

### 8.1 struct 构造

逐字段 store：

```llvm
%s = alloca %struct.Point
%x = getelementptr inbounds %struct.Point, ptr %s, i32 0, i32 0
store i32 1, ptr %x
%y = getelementptr inbounds %struct.Point, ptr %s, i32 0, i32 1
store i32 2, ptr %y
```

或 insertvalue：

```llvm
%p0 = insertvalue %struct.Point undef, i32 1, 0
%p1 = insertvalue %struct.Point %p0, i32 2, 1
```

### 8.2 struct 读取

```llvm
%p = getelementptr inbounds %struct.Point, ptr %s, i32 0, i32 1
%y = load i32, ptr %p
```

或 extractvalue（如果值是 SSA aggregate）：

```llvm
%y = extractvalue %struct.Point %s, 1
```

### 8.3 数组

```llvm
%arr = alloca [3 x i32]
%p0 = getelementptr inbounds [3 x i32], ptr %arr, i32 0, i32 0
store i32 10, ptr %p0
%p1 = getelementptr inbounds [3 x i32], ptr %arr, i32 0, i32 1
store i32 20, ptr %p1
```

### 8.4 bool 在内存中

```llvm
%slot = alloca i8
%byte = zext i1 %b to i8
store i8 %byte, ptr %slot

%byte2 = load i8, ptr %slot
%b2 = trunc i8 %byte2 to i1
```

---

## 9. 引用 / Box / Vec 的形状

### 9.1 引用

```llvm
; &mut x
%r = alloca ptr
store ptr %x, ptr %r

; *r
%p = load ptr, ptr %r
%v = load i32, ptr %p

; *r = v
store i32 %v, ptr %p
```

`&mut T -> &T`、reborrow 在 IR 里通常没有指令，都是同一个 `ptr`。

### 9.2 Box

```llvm
; Box::<i32>::new(7)
%mem = call ptr @__rx_alloc(i32 4, i32 4)
store i32 7, ptr %mem
%b = alloca ptr
store ptr %mem, ptr %b

; *b
%bp = load ptr, ptr %b
%v = load i32, ptr %bp
```

`Box<Pair>` 字段：

```llvm
%bp = load ptr, ptr %b
%field = getelementptr inbounds %struct.Pair, ptr %bp, i32 0, i32 1
%v = load i32, ptr %field
```

### 9.3 Vec

```llvm
%Vec = type { ptr, i32, i32 }

; Vec::<i32>::new()
%v = alloca %Vec
store %Vec zeroinitializer, ptr %v

; data / len / cap
%data = load ptr, ptr %v
%lenp = getelementptr %Vec, ptr %v, i32 0, i32 1
%len = load i32, ptr %lenp

; v[i]
%data2 = load ptr, ptr %v
%ep = getelementptr i32, ptr %data2, i32 %i
%val = load i32, ptr %ep

; v[i] = x
store i32 %x, ptr %ep
```

`push/remove/clone` 可以生成 helper 调用，例如：

```llvm
call void @rx_vec_i32_push(ptr %v, i32 %x)
```

具体 helper 由你实现。

---

## 10. 外部 I/O 的参考写法

如果不在 `{runtime}` 里实现 `get_i32/print_i32/println_i32`，可以在生成 IR 里定义：

```llvm
@.fmt_i32 = private unnamed_addr constant [3 x i8] c"%d\00"
@.fmt_i32_nl = private unnamed_addr constant [4 x i8] c"%d\0A\00"

declare i32 @printf(ptr, ...)
declare i32 @scanf(ptr, ...)

define void @print_i32(i32 %v) {
entry:
  %fmt = getelementptr inbounds [3 x i8], ptr @.fmt_i32, i32 0, i32 0
  call i32 @printf(ptr %fmt, i32 %v)
  ret void
}

define void @println_i32(i32 %v) {
entry:
  %fmt = getelementptr inbounds [4 x i8], ptr @.fmt_i32_nl, i32 0, i32 0
  call i32 @printf(ptr %fmt, i32 %v)
  ret void
}

define i32 @get_i32() {
entry:
  %slot = alloca i32
  %fmt = getelementptr inbounds [3 x i8], ptr @.fmt_i32, i32 0, i32 0
  call i32 @scanf(ptr %fmt, ptr %slot)
  %v = load i32, ptr %slot
  ret i32 %v
}
```

注意：`printf`/`scanf` 是变参函数，声明要用 `...`。

---

## 11. 常见错误

1. **基本块没有 terminator**  
   每个 block 最后必须是 `ret`/`br`/`unreachable`。

2. **store 写错类型**  
   ```llvm
   store i32 %v, ptr %slot      ; 对
   store ptr %slot, i32 %v      ; 错
   ```

3. **GEP 少写源类型或索引**  
   ```llvm
   getelementptr inbounds %struct.S, ptr %s, i32 0, i32 1
   ```

4. **bool 混用 i1/i8**  
   内存用 i8，SSA 用 i1，跨边界要 zext/trunc。

5. **signedness 用错**  
   `i32` 不区分符号；除法、取余、比较、右移要按源类型选 signed/unsigned 指令。

6. **shift 没 mask 5 位**  
   LLVM 要求移位量小于位宽，否则 poison。

7. **phi incoming 和 block 不匹配**  
   phi 的每个 incoming 值必须来自对应前驱 block。

8. **源 main 和 machine main 重名**  
   源 main 改名（如 `rx_main`），再生成 C ABI wrapper `@main`。

9. **聚合按值语义没复制**  
   struct/array 赋值、函数参数/返回必须 load/store 整个 aggregate 或显式复制，不能共享同一个栈槽。

10. **函数声明和定义签名不一致**  
    参数类型/返回类型必须完全一致。

11. **opaque pointer 下写 `i32*`**  
    LLVM 22 用 `ptr`，不是 typed pointer。

---

## 12. 验证命令

```sh
./target/build/rxcc --stage ir t.rx -o t.ll

clang-22 -S -x ir t.ll \
  --target=riscv32-unknown-none-elf \
  -march=rv32im -mabi=ilp32 -O0 \
  -o t.s

: > /tmp/empty.s
xmake run -P vendor/REIMU reimu --memory=256M --stack=1M \
  -f t.s,/tmp/empty.s -o t.out
```

如果 Clang 报错，先只看第一条；后面的错误经常是第一条的类型错误级联出来的。

---

## 13. 实现 generateExpr 的建议顺序

1. `intLitExpr` / `boolLitExpr` / `unitExpr`
2. `pathExpr`：查局部 alloca，load
3. `binaryExpr`：先 `+ - *` 和整数比较
4. `callExpr`：先 `println_i32`，再用户函数
5. `if/block/loop`
6. 数组/struct/字段/索引
7. 引用/赋值/复合赋值
8. Box/Vec/method/call/derive helper

每加一类，先用一个最小 Rx 例子走一遍 Clang + REIMU。
