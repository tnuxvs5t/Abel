# Abel v1.2 中文教程

> 状态：第一版教程草稿。先按当前仓库源码和测试中已经确认的 Abel v1.2 行为写成完整教程；后续再逐项和实现矩阵比对、修正措辞。

Abel 是一门小型 C-like 本地工程语言。它的核心思想是：

```text
静态核心尽量小。
动态边界必须显式。
复杂 native 能力交给 backend。
Abel 源码负责组合、流程和可读接口。
```

一个最小程序：

```abel
fn int main() {
    println("hello from Abel");
    return 0;
}
```

运行入口必须是：

```abel
fn int main() { ... }
// 或
fn void main() { ... }
```

`main` 不能有参数。

---

## 1. 文件、注释、字面量

### 1.1 注释

```abel
// 行注释

/*
   块注释
*/
```

### 1.2 字面量

```abel
123             // int
3.14            // f64
"hello\n"       // str
'x'             // char
'\n'            // char
true false      // bool
nullptr         // nullptr
```

字符串和字符支持常用转义：`\n`、`\t`，以及其他反斜杠后字符的直接转义。

---

## 2. 类型系统

### 2.1 基本类型

| Abel 类型 | 含义 |
|---|---|
| `void` | 无返回值 |
| `bool` | 布尔 |
| `i8 i16 i32 i64` | 有符号整数 |
| `u8 u16 u32 u64` | 无符号整数 |
| `int` | `i32` 别名 |
| `long` / `ll` | `i64` 别名 |
| `f64` / `double` | 64 位浮点 |
| `char` | Qt `QChar` 风格字符 |
| `str` | 字符串 |
| `any` | 显式动态值 |
| `vector<T>` | 静态元素类型向量 |
| `func R(A, B)` | 函数值类型 |
| `T*` | 指针 |
| `T&` | 引用 |
| `const T` | 只读类型/绑定 |

示例：

```abel
fn int main() {
    int a = 1;
    long b = 2;
    f64 x = 3.5;
    str s = "Abel";
    vector<int> xs = {1, 2, 3};
    return a + cast<int>(b) + xs[0];
}
```

### 2.2 默认构造

不写初始化时，支持默认构造的类型会得到默认值：

```abel
fn int main() {
    int x;          // 0
    bool ok;        // false
    str s;          // ""
    vector<int> v;  // 空 vector
    int* p;         // 空指针
    any a;          // 装箱 void
    return x;
}
```

引用和函数值不能默认构造。结构体是否可默认构造取决于字段和构造函数。

### 2.3 类型别名

```abel
type Index = int;
type Scores = vector<Index>;

fn int main() {
    Scores xs = {10, 20};
    return xs[1];
}
```

---

## 3. 变量、const、引用和指针

### 3.1 变量声明

```abel
int x = 1;
const int y = 2;
```

`const` 变量不能被重新赋值，也不能通过其字段、索引、引用等路径修改。

### 3.2 引用

```abel
fn void inc(int& x) {
    x = x + 1;
}

fn int main() {
    int a = 1;
    int& r = a;
    inc(r);
    return a; // 2
}
```

规则：

- 非 const 引用必须绑定到可写 lvalue。
- `const T&` 可以读取，但不能写。
- 引用变量必须初始化。
- 引用字段不支持。

```abel
fn int read(const int& x) {
    return x;
}
```

### 3.3 指针

```abel
fn int main() {
    int x = 3;
    int* p = &x;
    *p = 4;
    return x;
}
```

规则：

- `&expr` 要求 `expr` 是 lvalue。
- `*ptr` 解引用指针并得到 lvalue。
- `nullptr` 可赋给指针。
- 支持 `p->field` / `p->method()` 访问结构体指针。
- 当前 Abel 不支持指针算术、`void*`、手动 `delete` 所有权模型。

---

## 4. 表达式和运算符

### 4.1 一元运算符

```abel
!b      // bool 取反
+x      // 数值正号
-x      // 数值负号
&x      // 取地址
*p      // 解引用
```

### 4.2 二元运算符

按优先级大致为：

```text
**
* / % %%
+ -
<? >?
< <= > >=
== !=
&&
||
|>
=
```

`**` 右结合；赋值 `=` 右结合。`&&` 和 `||` 运行时短路。

数值运算：

```abel
a + b
a - b
a * b
a / b
a % b      // 余数
a %% b     // 非负模：负余数会调整到非负
a ** b     // 幂
a <? b     // min
a >? b     // max
```

比较：

```abel
a == b
a != b
a < b
a <= b
a > b
a >= b
```

字符串：

```abel
str s = "A" + "bel";
```

### 4.3 赋值

左侧必须是可写 lvalue：

```abel
x = 3;
xs[0] = 10;
obj.field = 5;
*p = 7;
```

赋值表达式本身返回赋值后的值，但不是 lvalue。

### 4.4 显式 cast

```abel
int x = cast<int>(3.9);
f64 y = cast<f64>(x);
```

`cast<T>(expr)` 支持：

- 数值之间转换；
- 可赋值类型转换；
- `any` 到目标类型的运行期动态 cast；
- `vector<T>` 的逐元素动态 cast；
- exact struct 类型恢复；
- exact `func` 签名恢复。

`cast` 目标必须是值类型，不能是 `void` 或引用类型。

---

## 5. 控制流

### 5.1 if / elseif / else

```abel
fn int sign(int x) {
    if (x < 0) {
        return -1;
    } elseif (x == 0) {
        return 0;
    } else {
        return 1;
    }
}
```

条件必须是 `bool`。

### 5.2 while

```abel
fn int sum_to(int n) {
    int s = 0;
    int i = 1;
    while (i <= n) {
        s = s + i;
        i = i + 1;
    }
    return s;
}
```

### 5.3 repeat

```abel
repeat (3) {
    println("tick");
}
```

`repeat (count)` 执行固定次数。负数次数运行时按 0 次执行。

### 5.4 C-style for

```abel
for (int i = 0; i < 10; i = i + 1) {
    println(i);
}
```

三段都可按语法省略：

```abel
for (; ; ) {
    break;
}
```

### 5.5 range-for

```abel
fn int main() {
    vector<int> xs = {1, 2, 3};
    int s = 0;
    for (x in xs) {
        x = x + 1;    // x 是元素引用，可写回 xs
        s = s + x;
    }
    return s;         // 9
}
```

range-for 的右侧必须是 `vector<T>`。

### 5.6 break / continue / return

```abel
while (true) {
    break;
}

return 0;
```

`break` 和 `continue` 只能在循环内使用。非 `void` 函数必须保证返回。

---

## 6. 函数

### 6.1 普通函数

```abel
fn int add(int a, int b) {
    return a + b;
}
```

### 6.2 void 函数

```abel
fn void log(str s) {
    println(s);
}
```

### 6.3 重载

Abel 支持普通函数重载：

```abel
fn int size(int x) {
    return x;
}

fn int size(str s) {
    return s.len();
}
```

重载解析按当前实现的简单评分选择最匹配候选；不承诺完整 C++ overload ranking。

### 6.4 默认参数和命名参数

```abel
fn int inc(int x, int by = 1) {
    return x + by;
}

fn int main() {
    int a = inc(10);             // inc(10, 1)
    int b = inc(x: 10, by: 2);   // inc(10, 2)
    return a + b;
}
```

规则：

- positional 参数必须在 named 参数之前；
- named 参数必须匹配声明参数名；
- named 参数不能重复；
- 缺失参数必须有默认值；
- 默认值在声明上下文检查；
- `func` 函数值调用不支持 named/default。

### 6.5 any... 变参

```abel
fn str join(any... xs) {
    return build_string(...xs);
}

fn int main() {
    vector<any> tail = {1, "x", true};
    println("tail=", ...tail);
    return join(...tail).len();
}
```

规则：

- 只支持 `any...`；
- 变参必须是最后一个参数；
- `...expr` spread 只展开到 `any...` 尾部；
- 当前 spread 支持 `vector<any>` 或已有 `any...` 参数；
- 不展开 `vector<int>` 到固定参数；
- 不支持 named spread。

### 6.6 debt 函数

```abel
debt fn int later(int x);
```

`debt fn` 是显式运行期错误边界：可以声明，但调用时会因为没有 Abel 函数体而产生运行期诊断。用户 operator overload 不能是 debt。

---

## 7. lambda 和函数值

### 7.1 func 类型

```abel
fn int inc(int x) {
    return x + 1;
}

fn int main() {
    func int(int) f = inc;
    return f(3);
}
```

唯一的非重载函数名可以作为函数值。重载函数名不能直接当作函数值，需要用 lambda 包一层。

模块限定函数也可作为函数值：

```abel
func int(int) f = Math::inc;
```

### 7.2 lambda 语法

```abel
fn int main() {
    int bias = 10;
    func int(int) f = lambda [=] int(int x) {
        return x + bias;
    };
    return f(1);
}
```

捕获列表：

```abel
lambda []        int(int x) { return x; }       // 不捕获
lambda [=]       int(int x) { return x + a; }   // 默认值捕获
lambda [&]       int(int x) { a = a + x; return a; } // 默认引用捕获
lambda [a, &b]   int() { b = b + a; return b; } // 指定捕获
```

lambda 参数写法是：

```abel
lambda [captures] ReturnType(Type1 name1, Type2 name2) {
    ...
}
```

lambda 内部的 `break` / `continue` 不继承外层循环。

### 7.3 func 与 any

函数值可以装进 `any`，再用 exact 签名恢复：

```abel
fn int inc(int x) { return x + 1; }

fn int main() {
    any f = inc;
    func int(int) g = cast<func int(int)>(f);
    return g(2);
}
```

---

## 8. struct

### 8.1 字段和默认构造

```abel
struct Point {
    int x;
    int y;
}

fn int main() {
    Point p = Point(1, 2); // 无 init 时按字段顺序构造
    return p.x + p.y;
}
```

如果没有显式 `init`，结构体构造参数按字段顺序传入；无参构造会默认构造所有字段。

### 8.2 init 构造函数

```abel
struct Point {
    int x;
    int y;

    init(int x, int y = 0) {
        this.x = x;
        this.y = y;
    }
}

fn int main() {
    Point p = Point(x: 3, y: 4);
    return p.x + p.y;
}
```

支持构造函数重载、named/default 参数。

### 8.3 方法和 this

```abel
struct Counter {
    int value;

    fn void inc() {
        this.value = this.value + 1;
    }

    const fn int get() {
        return this.value;
    }
}
```

当前语法中 const 方法写作：

```abel
const fn ReturnType name(...) { ... }
```

不是 C++ 风格的尾置 `fn ... name(...) const`。

`this` 是当前对象 lvalue，不是指针。

### 8.4 public / private

```abel
struct Box {
private:
    int secret;

public:
    init(int x) {
        secret = x;
    }

    const fn int get() {
        return secret;
    }
}
```

默认成员可见性是 public。`private:` 后的字段、构造函数、方法只能在本结构体方法内访问；`public:` 恢复 public。

### 8.5 方法重载

```abel
struct Box {
    int x;
    fn int get() { return x; }
    fn int get(int add) { return x + add; }
}
```

---

## 9. enum

```abel
enum Color {
    Red,
    Green,
    Blue,
}

fn int main() {
    Color c = Color.Green;
    return Color.Red + Color.Blue; // 0 + 2
}
```

enum 当前运行表示是 `i32`。枚举项从 0 开始递增。访问枚举项使用点号：

```abel
Color.Red
```

---

## 10. module / use / export

### 10.1 模块声明

```abel
module app.math;

export fn int add(int a, int b) {
    return a + b;
}
```

`module` 使用点分限定名。

### 10.2 导入模块

```abel
use app.math;

fn int main() {
    return add(1, 2);
}
```

### 10.3 模块别名

```abel
use app.math as M;

fn int main() {
    return M::add(1, 2);
}
```

### 10.4 export

可以导出函数、结构体、enum、type alias、backend block，也可以 re-export 模块：

```abel
export use app.math;
export fn int api() { return 1; }
export struct Point { int x; int y; }
export enum Color { Red, Green }
export type Index = int;
```

跨 package 依赖中，未 export 的声明不可见。

### 10.5 限定访问

函数、结构体构造、backend 静态调用使用 `::`：

```abel
Math::add(1, 2)
BackendName::call(3)
```

枚举项和普通字段使用 `.`：

```abel
Color.Red
p.x
```

---

## 11. vector<T>

### 11.1 初始化和索引

```abel
fn int main() {
    vector<int> xs = {1, 2, 3};
    xs[1] = 10;
    return xs[0] + xs[1];
}
```

`{...}` initializer list 需要目标类型，主要用于 `vector<T>` 初始化。

### 11.2 vector 内建方法

| 方法 | 返回 | 说明 |
|---|---:|---|
| `len()` | `int` | 长度 |
| `empty()` | `bool` | 是否为空 |
| `push(x)` | `void` | 追加元素 |
| `pop()` | `T` | 删除并返回末尾元素 |
| `clear()` | `void` | 清空 |
| `reserve(n)` | `void` | 预留容量，`n >= 0` |
| `resize(n)` | `void` | 改长度；扩张时元素类型必须可默认构造 |
| `front()` | `T` lvalue | 首元素 |
| `back()` | `T` lvalue | 尾元素 |
| `insert(i, x)` | `void` | 在位置 `i` 插入 |
| `erase(i)` | `T` | 删除并返回位置 `i` |
| `find(x)` | `int` | 首个等于 `x` 的下标，不存在为 `-1` |
| `contains(x)` | `bool` | 是否包含 |
| `count(x)` | `int` | 等于 `x` 的元素数 |
| `extend(other)` | `void` | 追加另一个同类型 vector |
| `slice(start, len)` | `vector<T>` | 拷贝切片；越界长度会截断 |
| `sort()` | `void` | 原地排序，要求元素可排序 |
| `reverse()` | `void` | 原地反转 |
| `unique()` | `void` | 删除相邻重复元素 |
| `binary_search(x)` | `bool` | 在已排序 vector 中二分查找 |
| `lower_bound(x)` | `int` | lower bound 下标 |
| `upper_bound(x)` | `int` | upper bound 下标 |

可排序元素包括数值、`bool`、`char`、`str`。

---

## 12. str 和 char

### 12.1 str 方法

| 方法 | 返回 | 说明 |
|---|---:|---|
| `len()` | `int` | 字符数 |
| `empty()` | `bool` | 是否为空 |
| `contains(s)` | `bool` | 是否包含子串 |
| `find(s)` | `int` | 子串位置，不存在为 `-1` |
| `starts_with(s)` | `bool` | 前缀 |
| `ends_with(s)` | `bool` | 后缀 |
| `substr(start, len)` | `str` | 子串 |
| `slice(start, len)` | `str` | `substr` 别名 |
| `replace(before, after)` | `str` | 替换所有匹配 |
| `trim()` | `str` | 去掉两端空白 |
| `lower()` | `str` | 小写 |
| `upper()` | `str` | 大写 |
| `split(sep)` | `vector<str>` | 按非空分隔符切分 |
| `join(xs)` | `str` | 用 receiver 连接 `vector<str>` |
| `parse_int()` | `int` | 解析 i32 |
| `parse_long()` | `long` | 解析 i64 |
| `parse_double()` | `f64` | 解析 f64 |
| `parse_bool()` | `bool` | 接受 `true/false/1/0` |

示例：

```abel
fn int main() {
    str s = " abel ";
    str t = s.trim().upper();
    vector<str> parts = "a,b,c".split(",");
    return t.len() + parts.len();
}
```

### 12.2 char / str 函数

```abel
vector<char> str_to_chars(str s)
str chars_to_str(vector<char> xs)

int  char_code(char c)
char char_from_code(int code)
bool char_is_digit(char c)
bool char_is_letter(char c)
bool char_is_alnum(char c)
bool char_is_space(char c)
bool char_is_upper(char c)
bool char_is_lower(char c)
char char_upper(char c)
char char_lower(char c)
str  char_to_str(char c)
```

`char_from_code` 接受 `0..65535`。

---

## 13. any 和 Dynamic Waterworks

`any` 是 Abel 的显式动态值核心。静态检查器不证明 `any` 内部结构；运行期 cast、动态索引和动态运算失败会给诊断。

### 13.1 装箱和恢复

```abel
fn int main() {
    any a = 42;
    int x = cast<int>(a);

    any s = "Abel";
    str name = cast<str>(s);
    return x + name.len();
}
```

在有目标类型的位置，`any` 可触发隐式运行期 cast：

```abel
fn int add1(int x) { return x + 1; }

fn int main() {
    any a = 41;
    int x = a;          // any -> int runtime cast
    x = a;              // assignment
    return add1(a);     // function argument
}
```

引用绑定不会靠 `any` 绕过 lvalue/ref 规则。

### 13.2 any introspection

```abel
str  any_type(any x)
str  any_debug(any x)
bool any_is(any x, str typeName)
bool any_is_bool(any x)
bool any_is_int(any x)
bool any_is_double(any x)
bool any_is_char(any x)
bool any_is_str(any x)
bool any_is_vector(any x)
bool any_is_pointer(any x)
bool any_is_struct(any x)
bool any_is_func(any x)
```

`any_is(x, "integer")` 可匹配任意整数；`"numeric"` / `"number"` 可匹配数值；动态对象的类型形如 `dynamic:tuple`、`dynamic:strmap`。

### 13.3 dynamic tuple：`[[...]]`

```abel
fn int main() {
    any row = [[1, "Abel", true]];
    int id = cast<int>(row[0]);
    str name = cast<str>(row[1]);
    row[0] = id + 1;
    return cast<int>(row[0]) + name.len();
}
```

规则：

- `[[...]]` 的静态类型是 `any`；
- 下标运行期必须是整数；
- `t[i]` 返回 `any`；
- `t[i] = value` 写回动态对象；
- 越界是运行期诊断；
- equality 当前是动态对象身份相等。

### 13.4 dynamic strmap：`[{"key" = value}]`

```abel
fn int main() {
    any row = [{"name" = "Nitori", "score" = 39}];
    row["score"] = cast<int>(row["score"]) + 3;
    return cast<int>(row["score"]);
}
```

规则：

- `[{"k" = v}]` 的静态类型是 `any`；
- 第一片 key 必须是字符串字面量；
- 重复 key 是 check-time 错误；
- 访问 key 时运行期 key 必须是 `str`；
- 缺 key 是运行期诊断；
- `m["k"]` 返回 `any`；
- `m["k"] = value` 写回动态对象。

### 13.5 any 动态运算符

如果任一操作数是 `any`，下列运算进入动态 operator 通道：

```abel
+ - * / % %% ** <? >?
== != < <= > >=
```

算术/`<?`/`>?` 结果静态为 `any`；比较结果静态为 `bool`。

```abel
fn int main() {
    any a = 2;
    any b = 3;
    any c = a + b;
    return cast<int>(c);
}
```

动态索引：

```abel
any xs = vector<any>{1, 2, 3}; // 见下方注意
```

实际写 vector 初始化时仍使用目标类型：

```abel
vector<any> raw = {1, 2, 3};
any xs = raw;
xs[0] = 10;
```

当前 `any` 索引支持 `any` 包裹的 vector，以及 backend/dynamic object 协议对象。

---

## 14. pipe：`|>` 和 `_`

### 14.1 基础 pipe

```abel
fn int add(int a, int b) {
    return a + b;
}

fn int main() {
    int x = 3;
    int y = x |> add(_, 4);  // add(x, 4)
    int z = y |> add(10, _); // add(10, y)
    return z;
}
```

无 `_` 时，右侧必须是可调用目标：

```abel
x |> f       // f(x)
```

### 14.2 method / field receiver

```abel
fn int main() {
    str s = " abel " |> _.trim().upper();
    return s.len();
}
```

### 14.3 v1.2 generalized pipe expression

`_` 可以出现在 pipe RHS 的一般表达式中：

```abel
fn int main() {
    any row = [{"name" = "Nitori", "score" = 39}];
    any pair = row |> [[_["name"], _["score"] + 3]];
    row |> (_["score"] = cast<int>(_["score"]) + 3);
    return cast<int>(row["score"]);
}
```

规则：

- `_` 只在 pipe RHS 内有效；
- LHS 只求值一次；
- 多个 `_` 指向同一个 pipe 值；
- 如果同一调用边界会把同一个 hole 绑定到多个 mutable ref / mutable receiver，会被拒绝；
- prvalue pipe temporary 不能绕过 mutable reference 规则。

---

## 15. 用户二元 operator overload

支持声明 concrete 二元 operator overload：

```abel
struct Point {
    int x;
    int y;
}

fn Point operator +(Point a, Point b) {
    return Point(a.x + b.x, a.y + b.y);
}

fn bool operator ==(Point a, Point b) {
    return a.x == b.x && a.y == b.y;
}
```

当前可声明的 operator 符号：

```text
+ - * / % %% ** <? >? == != < <= > >=
```

限制：

- operator 函数必须是普通顶层函数；
- 参数必须正好 2 个；
- 不能是 `debt`；
- 当前不支持用户重载 `[]`、`[]=`, `=`, `&&`, `||`, `.`, `->`, `::`, `&`, 解引用 `*`, `|>`, `cast`, call `()`。

`any` catch-all 可用于动态 operator fallback：

```abel
fn any operator +(any a, any b) {
    return build_string(any_debug(a), any_debug(b));
}
```

---

## 16. backend block

Abel 通过 `backend` 声明 native 插件提供的函数：

```abel
backend MathBackend {
    fn int fast_add(int a, int b);
    fn void bump(int& x);
    fn any call(any f, int x);
}

fn int main() {
    int x = 1;
    MathBackend::bump(x);
    return MathBackend::fast_add(x, 2);
}
```

规则：

- backend 调用使用 `BackendName::function(...)`；
- backend block 中只有函数声明，没有 Abel 函数体；
- backend 函数支持 named/default 参数声明形态；
- backend 参数可接收 `any` 动态值；
- backend 可通过 SDK 接收 Abel callable 并回调 Abel；
- 实际 backend 符号、ABI、插件路径由 package/resource 元数据连接。

---

## 17. 全局内建函数

### 17.1 字符串化和输出

```abel
str  to_str(x)
str  build_string(any... xs)
void print(any... xs)
void println(any... xs)
```

`build_string` / `print` / `println` 支持 `...vector<any>` spread。

可字符串化类型：

- 基本类型；
- `any`；
- pointer / nullptr；
- vector，只要元素可字符串化；
- struct，前提是可见的 `fn str to_str(StructType x)` 存在。

### 17.2 输入

```abel
void scan(T*...)
```

示例：

```abel
fn int main() {
    int x;
    str s;
    scan(&x, &s);
    return x + s.len();
}
```

`scan` 参数必须是可写指针。支持读入目标：

```text
bool, numeric, char, str, any
```

读入 `any` 时会把 token 当作 `str` 装箱。

### 17.3 源码位置伪内建

```abel
__FILE__    // str
__LINE__    // int
__COLUMN__  // int
```

它们按使用位置返回文件、行、列。

---

## 18. 数学内建

```abel
abs(x)
sqrt(x)
floor(x)
ceil(x)
round(x)
trunc(x)
sin(x)
cos(x)
tan(x)
asin(x)
acos(x)
atan(x)
atan2(y, x)
exp(x)
log(x)
log10(x)
pow(x, y)
gcd(a, b)
lcm(a, b)
min(a, b)
max(a, b)
clamp(x, low, high)
```

返回类型：

- `abs(x)` 返回 `x` 的类型；
- `sqrt/floor/ceil/round/trunc/sin/cos/tan/asin/acos/atan/atan2/exp/log/log10/pow` 返回 `f64`；
- `gcd/lcm` 要求整数，返回整数合成类型；
- `min/max/clamp` 返回数值合成类型。

`clamp` 运行时要求 `low <= high`。

---

## 19. 文件、路径、环境内建

```abel
str         read_text(str path)
void        write_text(str path, str content)
void        append_text(str path, str content)
vector<str> read_lines(str path)
void        write_lines(str path, vector<str> lines)

bool path_exists(str path)
bool path_is_file(str path)
bool path_is_dir(str path)
void copy_file(str src, str dst)
void move_path(str src, str dst)
void remove_path(str path)

str path_join(str parent, str child)
str path_dirname(str path)
str path_basename(str path)
str path_ext(str path)
str path_absolute(str path)
str path_clean(str path)
void mkdirs(str path)

str  current_dir()
bool env_exists(str name)
str  env_get(str name)
```

文件和路径错误是运行期诊断。

---

## 20. debug / test 内建

### 20.1 debug

```abel
void debug_break()
void debug_assert(bool condition, any... message)
```

`debug_break()` 总是产生诊断。`debug_assert(false, ...)` 产生诊断；条件为 true 时继续执行。

### 20.2 test

```abel
void test_assert(bool condition, any... message)
void test_eq(actual, expected, any... message)
void test_ne(actual, expected, any... message)
void test_close(actual, expected, eps, any... message)
```

这些函数用于 `abel test`。失败会产生测试诊断。

`test_close` 要求三个数值参数，`eps >= 0`。

---

## 21. 测试文件和 fixtures

项目测试放在：

```text
tests/**/*.abel
```

每个测试文件和项目源码一起编译，但测试文件本身作为入口。测试文件仍需要 `fn int main()` 或 `fn void main()`。

测试文件内可以声明：

```abel
fn void setup() {
    // main 前运行
}

fn void teardown() {
    // main 后运行，即使 main 返回非 0 或产生诊断也会尝试运行
}
```

`setup` / `teardown` 只在同一个测试文件内作为 fixture 使用；库文件里的同名函数不会自动成为 fixture。签名必须是 `fn void setup()` / `fn void teardown()`。

---

## 22. CLI 和 package

### 22.1 常用命令

```bash
abel version
abel init [project-dir]
abel package check <project-dir>
abel update [project-dir]
abel build [project-dir]
abel check <file-or-project-dir>
abel run <file-or-project-dir>
abel test [project-dir]
```

依赖：

```bash
abel add path <dependency-dir> [project-dir]
abel add registry <package-name> <version-requirement> <registry-dir> [project-dir]
abel remove <dependency-name> [project-dir]
```

本地 registry：

```bash
abel package publish [--overwrite] <project-dir> <registry-dir>
abel package registry index <registry-dir>
abel package registry check <registry-dir>
abel package registry list <registry-dir>
```

资源：

```bash
abel resources check <resource.json>
abel run --resource resource.json <file-or-project-dir>
abel test --resource resource.json <project-dir>
```

测试选项：

```bash
abel test --filter substring .
abel test --expect-fail substring .
abel test --report-json report.json .
abel test --report-junit report.xml .
```

### 22.2 abel.package.json

最小 manifest：

```json
{
  "name": "my-project",
  "version": "0.1.0",
  "entry": "src/main.abel"
}
```

`version` 必须是 SemVer `major.minor.patch`。

依赖：

```json
{
  "dependencies": [
    {
      "name": "local-lib",
      "kind": "path",
      "path": "../local-lib",
      "version": "0.1.0"
    },
    {
      "name": "registry-lib",
      "kind": "registry",
      "registry": "../registry",
      "version": "^1.0.0"
    }
  ]
}
```

支持 path dependency、本地 registry、`file://` registry mirror。HTTP/network registry 不属于当前 v1/v1.2 本地闭环。

### 22.3 package 源文件规则

项目源码从 `src/**/*.abel` 收集，`entry` 文件最后加入。测试时还会加入对应 `tests/**/*.abel` 测试文件作为入口。

### 22.4 backendArtifacts

package 可声明 backend artifact：

```json
{
  "backendArtifacts": [
    {
      "backendId": "MathBackend",
      "path": "backend/build/libmath_backend.so",
      "symbols": ["fast_add", "bump"]
    }
  ]
}
```

`symbols` 可以写短名，加载时会归属为 `backendId.symbol`；也可以写完整名，但必须属于该 backend。

可选 `build` 目前支持 CMake：

```json
{
  "backendId": "MathBackend",
  "path": "backend/build/libmath_backend.so",
  "symbols": ["fast_add"],
  "build": {
    "system": "cmake",
    "source": "backend",
    "buildDir": "backend/build",
    "target": "math_backend",
    "configureArgs": [],
    "buildArgs": []
  }
}
```

---

## 23. backend SDK 概念速览

C++ backend 实现 `IAbelBackend`，通过 `backendId()`、`functions()` 和 `call()` 暴露函数。常用 SDK 类型：

```text
bool / int / qint64 / double / QString / QChar
这些类型的可写 T& 参数
std::vector<T>
abel::AbelValue
abel::AbelVariadicArgs
abel::AbelRuntimeContext&
abel::AbelCallable
abel::AbelDynamicObject
abel::AbelValueKey
abel::AbelBackendHandleStore<T>
```

backend 可：

- 接收和返回 scalar / string / vector / any；
- 对 scalar `T&` 和 `std::vector<T>&` 写回；
- 接收 `AbelValue` 保存动态值；
- 接收 `AbelCallable` 回调 Abel 函数值；
- 返回 backend-owned dynamic object，并参与 `[]` / `[]=` / `==` / `any_debug`。

复杂对象推荐通过 backend-owned handle 或 `AbelDynamicObject` 承载，不进入 Abel core TypeKind。

---

## 24. 当前明确不支持或不要依赖的能力

下面内容不是当前 Abel v1.2 surface：

```text
用户 template / interface / require / 泛型函数 / 泛型 struct
tuple<T...>
core map/dict/object TypeKind
普通 object literal / dict literal / symbol literal
动态字段访问 obj.name
dynamic backend_invoke(...)
operator() 用户重载
operator[] / operator[]= 用户源码重载
赋值、&&、||、成员访问、pipe、address-of、dereference 用户重载
指针算术、void*、reinterpret_cast、manual delete
borrow checker / 完整 lifetime proof
完整 C++ overload ranking / ADL / 默认参数进入 func ABI
JIT / bytecode VM / HTTP registry / IDE 协议完整化
```

词法中 `new` / `delete` 是关键字，但当前 parser 没有对应表达式语法；不要在 Abel 源码中使用它们。

---

## 25. 一个综合示例

```abel
module demo.main;

struct Score {
private:
    int value;

public:
    init(int x = 0) {
        value = x;
    }

    fn void add(int x) {
        value = value + x;
    }

    const fn int get() {
        return value;
    }
}

fn str to_str(Score s) {
    return build_string("Score(", s.get(), ")");
}

fn int main() {
    Score s = Score(39);
    s.add(3);

    any row = [{"name" = "Nitori", "score" = s.get()}];
    any projected = row |> [[_["name"], _["score"] + 1]];

    str name = cast<str>(projected[0]);
    int score = cast<int>(projected[1]);

    println("name=", name, ", score=", score, ", object=", any_debug(row));
    test_assert(score == 43, "score should be 43");
    return 0;
}
```

下次写 Abel 时先抓住这条线：

```text
普通数据：静态类型、函数、struct、vector。
异构数据：any + cast + dynamic literal。
流程组合：pipe + _。
重能力：backend block + package artifact。
验证：test_* + abel test。
```
