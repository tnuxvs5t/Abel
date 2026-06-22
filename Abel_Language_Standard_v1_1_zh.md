# Abel Language Standard v1.1

状态：语言标准草案  
范围：只定义 Abel 语言本体。  
不定义：IDE UI、Codex 工作流、后端实现细节、manifest/hash 纪律、构建目录结构。那些属于《Abel Engineering Design v1》。

---

## 0. Abel 的定位

Abel 是一门由 Abel Engine 直接消费的工程语言。Abel 源码表达工程结构本身：模块、导入、导出、类型、结构体、枚举、泛型模板、接口约束、运算符、普通函数、debt 函数、concept hard function，以及 backend operation unit 的公开边界。

Abel 源码不表达 AI prompt、Codex 任务、JIT 方案、split 方案、后端模型选择、权限策略、缓存策略、审计日志、manifest/hash 缓存。这些属于工程层、资源层和 Codex backend 层。

Abel 的语言目标：

```text
C-family 语法
C++ class-like 组织能力
简化泛型
静态接口约束
可硬化的 concept 边界
可暴露欠债的 debt 边界
Engine 可直接解析和 tree-run
```

---

## 1. 总体语法风格

Abel 使用 C-family 类型前置声明。

```abel
const i64 x = 10;
i64 y = 20;

fn i64 add(i64 a, i64 b) {
    return a + b;
}
```

Abel 不使用以下核心声明风格：

```text
let / var
name: Type
fn name(...) -> Type
```

函数返回类型写在函数名之前：

```abel
fn ReturnType name(ParameterType parameter) {
    ...
}
```

结构体字段：

```abel
struct File {
    str path;
    str text;
}
```

泛型模板：

```abel
template <type T>
fn T identity(T x) {
    return x;
}
```

---

## 2. 文件

Abel 源文件使用 `.abel` 后缀。一个源文件包含可选 `module` 声明、若干 `use` 声明和若干顶层声明。

```abel
module math.integer;

use std.array;
use std.string;

export struct BigNum {
    bool neg;
    u32[] limbs;
}
```

---

## 3. 模块

模块使用 `module` 声明：

```abel
module math.integer;
```

模块名由标识符和 `.` 组成：

```abel
module std.array;
module std.string;
module concept.numeric;
module project.parser.token;
```

推荐模块路径与文件路径保持一致，但具体映射由工程层决定。

---

## 4. 导入

导入使用 `use`：

```abel
use std.array;
use std.string.len;
use math.integer.BigNum;
```

导入可以指向模块，也可以指向具体符号。若两个导入引入同名符号，Abel Engine 必须报错，要求使用限定名。

---

## 5. 导出

使用 `export` 导出顶层符号：

```abel
export struct File {
    str path;
    str text;
}

export fn i64 count_lines(str text) {
    ...
}

export debt fn ProjectGraph build_project_graph(File[] files);

export concept fn ModuleRole infer_module_role(str path, str text);
```

未使用 `export` 的顶层符号默认只在当前模块内部可见。

---

## 6. 注释

```abel
// line comment

/* block comment */
```

v1.1 不要求嵌套块注释。

---

## 7. 标识符与关键字

标识符由字母、数字和 `_` 组成，不能以数字开头。v1.1 核心标识符使用 ASCII。

v1.1 关键字：

```text
module use export
const fn debt concept backend
struct enum type template interface require operator static public private
if else while for return break continue
true false
ref inout lambda
```

这些关键字不能作为普通标识符使用。

---

## 8. 基础类型

```text
void bool i64 u64 u32 f64 char str
```

含义：

```text
void  无返回值
bool  布尔
i64   64 位有符号整数
u64   64 位无符号整数
u32   32 位无符号整数
f64   64 位浮点数
char  字符
str   字符串
```

更小整数、任意精度整数、decimal、复数等可由标准库或后续版本提供。

---

## 9. 派生类型

### 9.1 数组

```abel
i64[] xs;
File[] files;
str[] names;
```

`T[]` 表示元素类型为 `T` 的动态数组。

### 9.2 引用

```abel
ref i64 r;
ref Node n;
const ref BigNum x;
```

`ref T` 表示可存储、可传递的引用值。`const ref T` 表示只读引用参数或只读引用值。

### 9.3 函数值

```abel
func i64(i64) f;
func void(str) logger;
```

`func R(A, B)` 表示参数为 `A, B`、返回 `R` 的函数值。函数值主要用于局部回调和运行时组合，公开工程边界应优先使用命名函数。

### 9.4 泛型实例

```abel
Box<i64> b;
Pair<i64, str> p;
Vector<BigNum> xs;
```

---

## 10. 字面量

```abel
0
42
-1
3.14
1.0e-9
true
false
'a'
'\n'
"hello"
[1, 2, 3]
```

数组字面量必须能根据上下文确定元素类型：

```abel
i64[] xs = [1, 2, 3];
```

---

## 11. 常量与变量

Abel 不使用 `let` / `var`。

常量：

```abel
const i64 x = 10;
const str name = "abel";
```

普通变量：

```abel
i64 count = 0;
count = count + 1;
```

v1.1 要求声明处显式写类型。

---

## 12. 赋值

```abel
x = x + 1;
file.path = "main.abel";
xs[0] = 42;
*r = 10;
```

左侧必须是可写位置。可写位置包括普通变量、结构体字段、数组元素、解引用结果、`inout` 参数。不可写位置包括 `const` 常量、函数调用结果、算术表达式、字面量、`const ref` 指向的目标。

---

## 13. 表达式与运算符优先级

表达式包括算术、比较、逻辑、位运算、字段访问、数组索引、函数调用、方法调用、静态成员调用、解引用和引用创建。

```abel
a + b
a == b
a && b
file.path
xs[i]
count_lines(text)
counter.inc()
BigNum::zero()
*r
ref x
```

优先级从高到低：

```text
() 函数调用 / 方法调用
[] 数组索引
. 字段访问
:: 静态成员访问
* 解引用
! ~ 一元运算
* / %
+ -
<< >>
< <= > >=
== !=
&
^
|
&&
||
=
```

赋值 `=` 只用于语句位置，不作为普通表达式链式使用。

---

## 14. 控制流

### 14.1 if

```abel
if (x > 0) {
    return x;
} else {
    return -x;
}
```

条件必须是 `bool`。

### 14.2 while

```abel
i64 i = 0;
while (i < n) {
    i = i + 1;
}
```

### 14.3 for

v1.1 使用 C-style `for`：

```abel
for (i64 i = 0; i < n; i = i + 1) {
    ...
}
```

支持 `break` 与 `continue`。

### 14.4 return

```abel
fn i64 add(i64 a, i64 b) {
    return a + b;
}

fn void log(str msg) {
    print(msg);
    return;
}
```

`void` 函数可以省略末尾 `return`。

---

## 15. 函数与参数形式

普通函数：

```abel
fn i64 add(i64 a, i64 b) {
    return a + b;
}
```

公开函数：

```abel
export fn i64 count_lines(str text) {
    ...
}
```

参数形式：

```abel
fn i64 square(i64 x);
fn i64 square_const(const i64 x);
fn i64 digit_count(const ref BigNum x);
fn void write_ref(ref i64 r, i64 value);
fn void inc(inout i64 x);
```

默认按值传递。大型对象建议使用 `const ref T`。需要修改调用方位置时使用 `inout T`。需要长期保存引用时使用 `ref T`。

---

## 16. inout

`inout` 表示一次函数调用期间对调用方可写位置的临时可变访问。

```abel
fn void swap_i64(inout i64 a, inout i64 b) {
    i64 t = a;
    a = b;
    b = t;
}

i64 x = 1;
i64 y = 2;
swap_i64(inout x, inout y);
```

规则：

```text
inout 只能出现在函数参数类型中。
调用方必须显式写 inout。
inout 实参必须是可写位置。
函数体中可以读写 inout 参数。
inout 不可存储、不可返回、不可作为结构体字段、不可放入数组。
```

---

## 17. ref 与 const ref

`ref T` 表示长期引用值。

```abel
i64 x = 10;
ref i64 r = ref x;
i64 y = *r;
*r = 20;
```

`const ref T` 表示只读引用：

```abel
fn i64 count_digits(const ref BigNum x) {
    ...
}
```

规则：

```text
ref T 是普通值。
ref T 可以存储、传递、返回、放入结构体。
ref place 创建引用。
*r 读取引用目标。
*r = value 写入引用目标。
const ref T 不允许通过该引用修改目标。
```

v1.1 不定义复杂生命周期系统。Engine 可以在能检测时报告无效引用，但语言不依赖静态生命周期检查。

---

## 18. struct

Abel 的 `struct` 是简化 class-like structure，不是简单 C struct。

它可以包含字段、方法、静态函数、构造函数 `init`、operator、debt operator、concept operator、可见性标记。

它不包含继承、虚函数、多继承、复杂 protected 层级、C++ 模板元编程机制。

### 18.1 基本结构

```abel
export struct Counter {
    i64 value;

    init(i64 start) {
        value = start;
    }

    fn void inc() {
        value = value + 1;
    }

    const fn i64 get() {
        return value;
    }
}
```

调用：

```abel
Counter c = Counter(0);
c.inc();
i64 x = c.get();
```

方法体内可直接访问字段，不引入显式 receiver 参数。

### 18.2 const 方法

`const fn` 方法不能修改当前对象成员，也不能调用非 const 方法。

```abel
const Counter c = Counter(1);
i64 x = c.get();
c.inc(); // invalid
```

### 18.3 构造函数

```abel
struct Point {
    i64 x;
    i64 y;

    init(i64 ax, i64 ay) {
        x = ax;
        y = ay;
    }
}

Point p = Point(1, 2);
```

若没有合适 `init`，可以使用字段构造：

```abel
Point p = Point {
    x = 1,
    y = 2
};
```

### 18.4 静态函数

```abel
struct BigNum {
    bool neg;
    u32[] limbs;

    static fn BigNum zero() {
        return BigNum(0);
    }
}

BigNum z = BigNum::zero();
```

### 18.5 可见性

v1.1 推荐简单规则：struct 成员默认 public，`private` 显式标记私有成员。`export struct` 只导出类型名和 public 成员。

---

## 19. enum 与 type alias

枚举：

```abel
export enum BuildErrorKind {
    Syntax;
    Type;
    Link;
    Unknown;
}
```

使用：

```abel
BuildErrorKind e = BuildErrorKind.Syntax;
```

类型别名：

```abel
type Path = str;
type FileList = File[];
```

类型别名不创建新名义类型，只是现有类型的别名。

---

## 20. template

Abel 支持简化 C++ 风格泛型模板。

泛型函数：

```abel
template <type T>
fn T identity(T x) {
    return x;
}
```

泛型结构体：

```abel
template <type T>
struct Box {
    T value;
}
```

多类型参数：

```abel
template <type A, type B>
struct Pair {
    A first;
    B second;
}
```

v1.1 支持 `template fn`、`template struct`、`template interface`、`template debt fn`、`template concept fn`、`template operator`。v1.1 不要求偏特化、SFINAE、复杂模板元编程、任意值模板参数。

---

## 21. interface

`interface` 是静态能力约束。它定义某类型必须提供哪些函数或 operator。它不表示继承，不引入运行时 vtable，不需要单独实现块。类型通过提供匹配的可见函数/operator 自动满足 interface。

```abel
template <type T>
interface Equal {
    operator fn bool ==(const ref T a, const ref T b);
}

template <type T>
interface Add {
    operator fn T +(const ref T a, const ref T b);
}
```

泛型函数约束：

```abel
template <type T>
require Add<T>
fn T sum(T[] xs, T zero) {
    T acc = zero;

    for (i64 i = 0; i < len(xs); i = i + 1) {
        acc = acc + xs[i];
    }

    return acc;
}
```

自动满足规则：若某类型 `T` 在其 struct 内部或 owner module 中提供匹配签名，则 `T` 自动满足对应 interface，不需要额外声明。

Owner rule：类型 `T` 的 operator 必须定义在 `T` 的 struct 内部，或 `T` 的 owner module 内部。第三方模块不能随意为不归自己所有的类型挂 operator。

---

## 22. operator

`operator` 是特殊函数符号。它可以定义在 struct 内部或类型 owner module 内部，可以出现在 interface 要求中，可以声明为普通 operator、debt operator、concept operator，并参与泛型约束解析。

普通 operator：

```abel
struct BigNum {
    bool neg;
    u32[] limbs;

    operator fn BigNum +(const ref BigNum a, const ref BigNum b) {
        ...
    }
}
```

用户代码：

```abel
BigNum c = a + b;
```

Engine 将其解析为对应的 `OperatorCallNode`。

debt operator：

```abel
debt operator fn BigNum *(const ref BigNum a, const ref BigNum b);
```

concept operator：

```abel
concept operator fn BigNum /(const ref BigNum a, const ref BigNum b);
```

v1.1 建议先支持：

```text
+ - * / %
== != < <= > >=
[]
```

其中 `[]` 可作为后续子阶段实现。v1.1 不开放任意符号 operator。

---

## 23. operator 解析规则

当 Engine 看到：

```abel
a + b
```

解析流程：

```text
1. 确定 a 的类型 A。
2. 确定 b 的类型 B。
3. 若 A/B 是基础类型，优先使用 builtin operator。
4. 否则查找 A 或 owner module 中可见的 operator +。
5. 若当前模板上下文存在 require Add<T> 等约束，则根据 interface 要求解析。
6. 若找到唯一匹配，生成 OperatorCallNode。
7. 若无匹配，报错。
8. 若多个匹配冲突，报错。
```

---

## 24. 数组与字符串

数组是 Engine 管理的序列对象。数组赋值和传参的具体表示由 Engine 决定，v1.1 推荐数组作为引用式容器处理：变量保存数组句柄，元素修改会影响同一数组对象。如需复制数组，标准库应提供显式函数。

```abel
i64[] xs = [1, 2, 3];
i64 x = xs[0];
xs[0] = 42;
i64 n = len(xs);
```

`str` 表示字符串，v1.1 推荐 `str` 为不可变字符串。可变文本缓冲区由标准库类型提供。

```abel
str s = "hello";
i64 n = len(s);
char c = s[0];
```

---

## 25. lambda

Abel 支持动态 lambda，用于局部回调和小规模工具逻辑。

```abel
func i64(i64) inc = lambda i64(i64 x) {
    return x + 1;
};
```

lambda 可以捕获当前作用域中的值。v1.1 推荐按值捕获。若需要修改外部值，应显式使用 `ref`。lambda 是运行时值，不用于表达 `debt`、`concept`、`split` 或 `jit` 结构。

---

## 26. debt fn

`debt fn` 声明尚未硬化的工程接口。

```abel
debt fn str normalize_path(str path);
export debt fn ProjectGraph build_project_graph(File[] files);
```

`debt fn` 有签名，没有 Abel 函数体。它是正式符号，可以被其他函数引用。

语义：接口已经确定，实现尚未硬化，Engine 必须识别，工程工具必须追踪，人类必须最终处理。当运行路径调用未偿还 debt 时，Engine 必须报错，除非工程层提供临时绑定。

---

## 27. concept fn

`concept fn` 声明 concept hard function。

```abel
concept fn BuildErrorKind classify_build_error(str log);
export concept fn ModuleRole infer_module_role(str path, str text);
```

`concept fn` 有签名，没有 Abel 函数体。实现来自 concept hard library 或 backend resource。

`concept fn` 不是 prompt，不是一次性 AI 请求，不是 Agent 动作。它是进入 Abel 库层的稳定硬接口：固定名称、固定模块、固定签名、固定返回类型、可导入、可导出、可测试、可审计、可版本化。若 concept 后端未绑定，Engine 必须报错。

---

## 28. backend operation unit

`backend` 是语言层的薄分组机制，用于声明一组后端能力边界。它不包含具体后端配置，不写模型、权限、缓存、端口、命令、动态库路径。这些属于工程设计。

```abel
backend Process {
    export concept fn ProcessResult run(str command);
}
```

含义：`Process` 是一个后端能力组，组内符号是 concept hard boundary，具体实现由工程层 backend manifest 绑定。

---

## 29. 库层

Abel 支持多文件静态库。普通 Abel 库可包含 struct、enum、type、template、interface、operator、const、fn、debt fn、concept fn、backend block。通过 `export` 暴露符号，通过 `use` 导入符号。

Concept hard library 对 Abel 暴露 `concept fn`、`concept operator`、backend block 符号，具体绑定由工程层 manifest 管理。

---

## 30. 错误处理

v1.1 不引入异常机制。错误应通过显式返回值或标准库类型表达。标准库可以提供 `Option` / `Result`，但它们不是 v1.1 语法核心。

运行时错误包括数组越界、除零、未绑定 debt 调用、未绑定 concept 调用、无效引用、类型不匹配、未定义符号、重复定义、operator 解析冲突、interface 约束不满足。Engine 必须给出清晰诊断。

---

## 31. 作用域与名称解析

Abel 使用词法作用域。局部声明只在当前块及其子块中可见。同一作用域不允许重复定义同名局部变量。内部作用域是否允许遮蔽外部变量，v1.1 允许，但工具应提示。

名称解析顺序：

```text
局部变量
函数参数
当前 struct 成员
当前模块顶层符号
显式 use 导入符号
标准库预导入符号
```

若名称冲突，Engine 必须报错，要求显式限定。

---

## 32. 顶层声明

v1.1 顶层允许：

```text
module
use
export const
export type
export struct
export enum
export template struct
export interface
export fn
export debt fn
export concept fn
export backend
const
type
struct
enum
template
interface
fn
debt fn
concept fn
backend
```

顶层不允许任意执行语句。模块加载不应产生隐式运行时副作用。

---

## 33. 示例：BigNum 能力系统

以下示例只展示语言机制，不代表标准库强制内容。

```abel
module math.bignum;

template <type T>
interface Add {
    operator fn T +(const ref T a, const ref T b);
}

template <type T>
interface Equal {
    operator fn bool ==(const ref T a, const ref T b);
}

template <type T>
require Add<T>
fn T sum(T[] xs, T zero) {
    T acc = zero;

    for (i64 i = 0; i < len(xs); i = i + 1) {
        acc = acc + xs[i];
    }

    return acc;
}

export struct BigNum {
    bool neg;
    private u32[] limbs;

    init(i64 x) {
        if (x < 0) {
            neg = true;
            x = -x;
        } else {
            neg = false;
        }

        limbs = [];

        while (x > 0) {
            push(limbs, cast<u32>(x % 1000000000));
            x = x / 1000000000;
        }

        normalize();
    }

    const fn bool is_zero() {
        return len(limbs) == 0;
    }

    fn void normalize() {
        while (len(limbs) > 0 && limbs[len(limbs) - 1] == 0) {
            pop(limbs);
        }

        if (len(limbs) == 0) {
            neg = false;
        }
    }

    operator fn bool ==(const ref BigNum a, const ref BigNum b) {
        if (a.neg != b.neg) {
            return false;
        }

        if (len(a.limbs) != len(b.limbs)) {
            return false;
        }

        for (i64 i = 0; i < len(a.limbs); i = i + 1) {
            if (a.limbs[i] != b.limbs[i]) {
                return false;
            }
        }

        return true;
    }

    operator fn BigNum +(const ref BigNum a, const ref BigNum b) {
        ...
    }

    debt operator fn BigNum *(const ref BigNum a, const ref BigNum b);
}
```

`BigNum` 因为提供了匹配的 `operator +`，自动满足 `Add<BigNum>`；因为提供了匹配的 `operator ==`，自动满足 `Equal<BigNum>`。不需要额外实现块。

---

## 34. 最终边界

Abel v1.1 要做：

```text
C-family 类型前置声明
const 与普通变量
class-like struct
无显式 receiver 参数的方法
template
interface
operator
ref / const ref / inout
fn / debt fn / concept fn
debt operator / concept operator
backend operation unit
module / use / export
```

Abel v1.1 不做：

```text
复杂宏
复杂模板元编程
继承和虚函数
运行时 typeclass/vtable
语言级 split
语言级 jit
语言内 AI prompt
语言内后端配置
语言内 manifest/hash
```

一句话：Abel 源码保存稳定工程结构；工程层和 Codex backend 负责演化、后端化与审计。
