# Abel Agent Manual

状态：Abel v0 唯一工作规格与 Agent 操作手册。  
作用：替代旧的 `Abel_*_zh.md` 设计文档。任何 Agent 进入本仓库，必须先读完本文件，再读代码。  
当前方向：Abel 不再追求大型工程幻想；不做 hard split/jit；不做 context exporter；不做 manifest/hash 系统；聚焦语言核心、backend 核心、Qt 插件资源节点核心。  
总原则：Abel 不是 JS、Python、Kotlin 式去指针化语言；Abel 采用 C/C++ 值模型与能力面，保留 C/C++ 的复杂度和风险，同时在语法、内建能力、Qt backend、插件开发体验上做高杠杆加法。

---

## 0. Agent 进入仓库的第一纪律

本仓库使用 Git 作为唯一工程审计与回滚机制。旧设计里的 proposal/audit/manifest/hash 全部废除；由 Git commit 取代。

### 0.1 必须先执行

进入仓库后，任何写入前必须执行：

```bash
pwd
git status --short
ls
```

若不是 Git 仓库，必须先停止并询问是否初始化。当前仓库已经初始化过 Git，初始基线 commit 为旧设计文档提交；但未来 Agent 仍必须检查。

### 0.2 写入纪律

1. 任何源码、文档、配置创建/修改/删除都必须通过显式 patch。
2. 不允许用 `cat > file`、`echo > file`、`tee`、`sed -i` 等绕过审查的方式写文件。
3. 每轮任务从 clean tree 起步。若工作树不干净，先向用户确认哪些改动是用户的。
4. 每轮任务形成一个逻辑 commit。
5. commit 前必须尽可能运行相关 build/check/test。
6. 用户认为修改无效时，立刻 rollback；默认用 `git revert` 保留历史，除非用户明确要求 reset。
7. 不伪造测试结果，不声称未运行的命令已运行。

### 0.3 本文件强制更新

本文件末尾有「工程进度 / 强制更新区」。任何完成实质修改的 Agent 必须更新该区：

- 更新当前阶段；
- 更新已完成事项；
- 更新下一步；
- 更新最近验证命令；
- 更新风险与未决问题；
- 若完成 commit，记录 commit hash。

不更新此区视为任务未闭环。

---

## 1. Abel 的根目标

Abel v0 只做三件事：

```text
1. 语言核心
2. backend 核心
3. 资源节点可用 Qt/C++ 动态链接操作核心
```

语言核心必须能：

- lex / parse Abel 源码；
- 构建 AST；
- 做基础名称解析；
- 做基础类型检查；
- 区分 lvalue/prvalue；
- 解释执行 AST；
- 支持 C/C++ 值模型、指针、引用、vector、lambda、any、控制流、backend 调用。

backend 核心必须能：

- 解析 `backend` block；
- 通过资源节点 JSON 加载 Qt plugin；
- 把 Abel 调用分发给插件；
- 插件开发者能用低样板 C++ 注册函数；
- backend 地位囊括旧 concept；旧 concept 已废除。

资源节点核心必须能：

- 用 JSON 描述一个 Qt plugin；
- 用 `QPluginLoader` 加载；
- 校验 IID/backendId/symbol；
- 暴露给解释器调用。

明确不做：

```text
abel split
abel jit
Codex context exporter
manifest/hash discipline
大型 VM
大型 IDE
复杂包管理
复杂模板元编程
跨 Qt 版本 ABI
跨编译器 ABI
```

未来可以做 JIT，但 v0 不实现 split/jit 命令。当前设计保留 C/C++ 值模型、明确类型、vector、函数边界、backend 插件，是为了将来 JIT 方便。

---

## 2. 固定工具链

Abel v0 全体实现使用 Qt + C++23。

本机锁定：

```text
Qt version:      6.11.1
Qt kit:          /home/tnuzy/Qt/6.11.1/gcc_64
qmake:           /home/tnuzy/Qt/6.11.1/gcc_64/bin/qmake
qt-cmake:        /home/tnuzy/Qt/6.11.1/gcc_64/bin/qt-cmake
CMake:           /home/tnuzy/Qt/Tools/CMake/bin/cmake
CMake version:   3.30.5
Ninja:           /home/tnuzy/Qt/Tools/Ninja/ninja
Ninja version:   1.12.1
C compiler:      /usr/bin/gcc
C++ compiler:    /usr/bin/g++
GCC/G++ version: 14.2.0
C++ standard:    C++23
Qt mkspec:       linux-g++
```

标准配置命令：

```bash
/home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64 \
  -DCMAKE_C_COMPILER=/usr/bin/gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_CXX_STANDARD=23

/home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
```

Qt 主要用于：

- `QString` / `QChar`；
- `QFile` / `QDir`；
- `QJsonDocument` / `QJsonObject`；
- `QPluginLoader`；
- `QObject` plugin interface；
- `QCommandLineParser`；
- `QTest`。

C++23 主要用于：

- `std::variant`；
- `std::optional`；
- `std::unique_ptr`；
- `std::vector`；
- `std::unordered_map`；
- `std::function`；
- concepts 可用于 C++ 插件 binder 内部，但不要把 Abel v0 语言做成 C++ template metaprogramming。

---

## 3. 项目结构目标

推荐结构：

```text
Abel/
  AGENTS.md
  CMakeLists.txt
  .gitignore

  src/
    abelcore/
      source_span.h
      diagnostic.h

      token.h
      lexer.h
      lexer.cpp

      ast.h
      ast.cpp
      parser.h
      parser.cpp

      type.h
      type.cpp
      symbol.h
      resolver.h
      resolver.cpp
      typechecker.h
      typechecker.cpp

      value.h
      value.cpp
      runtime.h
      runtime.cpp
      interpreter.h
      interpreter.cpp

      backend_interface.h
      backend_registry.h
      backend_registry.cpp
      backend_plugin_base.h
      backend_binder.h
      resource_node.h
      resource_node.cpp

    abelcli/
      main.cpp

  plugins/
    examples/
      math_backend/

  tests/
    lexer/
    parser/
    typechecker/
    interpreter/
    backend/

  examples/
    smoke/
```

`abelcore` 必须是共享库：

```text
libabelcore.so
```

主程序和 Qt plugin 都链接同一个 `abelcore`。不要把核心类型各编进一份静态库，避免 ABI、RTTI、QObject、全局状态分裂。

---

## 4. CLI 范围

v0 CLI 只做：

```bash
abel check <files-or-project>
abel run <file-or-entry>
```

可选早期辅助：

```bash
abel version
abel resources check <resource.json>
```

不做：

```bash
abel split
abel jit
abel graph
abel manifest
abel codex context
```

入口规则：

- `abel run file.abel` 默认查找 `fn int main()` 或 `fn void main()`；
- `int main()` 返回进程退出码；
- `void main()` 返回 0；
- 后续允许 `abel run module.symbol`，v0 可以先单文件。

---

## 5. Abel 语言总定位

Abel v0 的语言哲学：

```text
C/C++ 值模型
+ Qt 字符串/字符
+ vector<T> 内建容器
+ C++ 风格 lambda capture
+ any 与可扩展内建函数
+ backend block / Qt plugin
+ 几个高实用 operator
+ 更方便的 variadic 函数
```

不要把 Abel 做成：

- JS/Python/Kotlin 式一切都是对象引用；
- Rust 式所有权/借用系统；
- C++ 模板元编程复刻；
- 带 GC 的动态语言；
- prompt DSL；
- AI 工程脚手架。

Abel 接受 C/C++ 的复杂度。空指针、悬挂引用、vector reallocation 后引用失效、越界、未初始化读取等不做语言级兜底。v0 可以在实现层为了不崩进程做最小断言，但语义上不承诺安全。

---

## 6. 基础类型

固定宽度类型：

```text
void
bool

i8 i16 i32 i64
u8 u16 u32 u64

f64
char
str
any
```

C++ 兼容前端别名：

```text
int    -> i32
long   -> i64
ll     -> i64
double -> f64
```

不支持或暂不支持：

```text
unsigned int
unsigned long
short
size_t
auto
decltype
```

原因：这些会增加 parser 与类型系统负担，收益不够。`u32/u64` 足以表达 unsigned。

Qt 映射：

```text
char = QChar
str  = QString
```

`str <-> vector<char>` 不做隐式转换，只通过内建函数：

```abel
fn vector<char> str_to_chars(str s);
fn str chars_to_str(vector<char> cs);
```

---

## 7. C/C++ 值模型

根规则：

```text
变量拥有对象存储。
普通赋值复制值。
指针 T* 保存地址值。
引用 T& 是对象别名，必须初始化，不可重绑。
&x 取 lvalue 地址。
*p 解引用指针，得到 lvalue。
函数参数默认按值传递。
要修改调用方对象，用 T& 或 T*。
```

示例：

```abel
fn int main() {
    int x = 0;

    int& r = x;
    r = 1;

    int* p = &x;
    *p = *p + 1;

    return x; // 2
}
```

引用规则：

```text
T& 必须初始化。
T& 不能重新绑定。
r = y 表示把 y 的值写入 r 绑定的对象。
v0 禁止引用字段。
v0 允许指针字段。
v0 禁止指针算术。
```

指针规则：

```text
T* 可为空，使用 nullptr。
支持 *p、&x、p == q、p != q。
v0 不支持 p + 1、p - 1、p2 - p1。
v0 不支持 void*。
v0 不支持 reinterpret_cast。
```

const：

```text
const T       只读对象。
const T*      指向只读 T 的指针。
T* const      指针本身不可改指向。
const T&      只读引用。
```

v0 可以先实现常用的 `const T`、`const T&`、`const T*`，但 parser 结构必须预留 `T* const`。

---

## 8. vector<T> 内建数组对象

Abel 废除旧 `T[int]` 表示，统一使用：

```abel
vector<int> xs;
vector<str> names;
vector<char> chars;
```

底层表示：

```cpp
std::vector<T>
```

语义：

```text
vector<T> 是值类型。
赋值复制 vector。
传参默认复制 vector。
要修改原 vector，用 vector<T>& 或 vector<T>*。
```

示例：

```abel
fn void mutate(vector<int>& xs) {
    xs[0] = 10;
    xs.push(20);
}

fn int main() {
    vector<int> xs = {1, 2, 3};
    mutate(xs);
    return xs[0]; // 10
}
```

索引：

```abel
xs[i]
```

`xs[i]` 是内建 vector 索引语义，返回 `T` lvalue。它不是用户可重载 `operator[]`。

越界不检查；底层可用 `operator[]` 而不是 `at()`。

vector reallocation 后旧引用/指针是否悬挂：不兜底。

### 8.1 vector 内建方法

对标 `std::vector`，但名字简化：

```abel
xs.len();        // int
xs.empty();      // bool
xs.push(x);      // void, push_back
xs.pop();        // T, pop_back + return removed value
xs.clear();      // void
xs.reserve(n);   // void
xs.resize(n);    // void
xs.front();      // T&
xs.back();       // T&
```

### 8.2 vector prvalue receiver 规则

Abel 的方针是不兜底，不做过度安全限制。因此 vector 内建方法的 receiver 不应因为是 prvalue 被静态拒绝。

必须允许：

```abel
str ans = "abc";
int n = str_to_chars(ans).len();

while (str_to_chars(ans).len() < len) {
    ...
}
```

若后续加入短名，也同理：

```abel
stoc(ans).len();
```

语义：

```text
prvalue vector receiver 会物化成临时 vector 对象。
读方法读临时对象。
写方法修改临时对象；修改结果是否有用由用户负责。
front/back/index 若从临时对象产生引用或指针，生命周期风险不兜底。
```

因此这些不应被类型系统以“receiver 不是 lvalue”为由拒绝：

```abel
make_vector().len();
make_vector().empty();
make_vector().push(1);
make_vector().front();
```

只应检查：

```text
receiver 类型必须是 vector<T>。
参数类型必须匹配。
方法名必须存在。
const/mutability 只限制对已知 const 对象的写入；不能用 prvalue/lvalue 区分替代 const 规则。
```

实现要求：

```text
TypeChecker 不得要求 vector method receiver 必须是 lvalue。
Interpreter 不得对所有 builtin method 一律 evalLocation(receiver)。
Interpreter 应对 prvalue receiver 进行临时物化；需要 location 的方法从临时 storage 取得 location。
```

可后续追加：

```abel
xs.insert(pos, value);
xs.erase(pos);
xs.find(value);
xs.sort();
```

但核心结构必须支持方便新增，而不是把每个内建硬编码成不可扩展分支。见「内建函数与能力注册结构」。

---

## 9. any 类型

`any` 是 Abel 的动态值容器，用于 variadic、插件、通用工具函数。

```abel
any x = 123;
int y = cast<int>(x);
```

规则：

```text
any 可以装任意 Abel 值。
从 any 取出必须 cast<T>()。
cast 类型不匹配：runtime error。
不做 fallback。
不做隐式转换。
```

`any` 不是把 Abel 变成动态语言；它是一个显式边界类型。

实现建议：

```cpp
class AbelValue;    // 通用值
class AbelAny;      // 可直接用 AbelValue 包装，也可独立封装
```

`any` 与 backend plugin 高度相关。插件 binder 必须能接收：

```cpp
AbelValue
std::vector<AbelValue>
AbelAny
```

---

## 10. variadic 函数：让新增可能且方便

用户提出 `build_string("My Old is ", old, " , My School is ", school, "\n")`。这不是单纯要新增 `build_string`，而是要求 core 具备方便扩展的多参数能力。

Abel v0 加入强类型运行时 variadic：

```abel
fn str build_string(any... args);
fn void print(any... args);
fn void println(any... args);
fn vector<any> scan(any... specs);
```

v0 只要求 parser/typechecker 支持 `any...`。规则：

```text
最多一个 variadic 参数。
variadic 参数必须是最后一个。
v0 只允许 any...，不做模板 variadic。
调用时普通参数先匹配，剩余实参打包成 vector<any>。
每个实参保留类型 tag。
```

调用：

```abel
str s = build_string("My Old is ", old, " , My School is ", school, "\n");
println("x=", x, ", y=", y);
```

内部等价：

```text
build_string(vector<any>{...})
```

但用户不用手写 vector。

### 10.1 build_string / print 的 stringify 结构

必须存在可扩展 stringify 机制。不是把所有类型硬写死在一个 switch 里。

最小内建：

```abel
fn str to_str(str x);
fn str to_str(char x);
fn str to_str(bool x);
fn str to_str(int x);
fn str to_str(long x);
fn str to_str(ll x);
fn str to_str(double x);
fn str to_str(any x);
```

用户可定义：

```abel
struct Student {
    str name;
    int age;
}

fn str to_str(Student s) {
    return build_string(s.name, "(", s.age, ")");
}
```

`build_string` 解析顺序：

```text
1. 基础类型使用 builtin formatter。
2. any 展开内部值再 stringify。
3. 若存在可见 fn str to_str(T)，调用它。
4. 无法 stringify：runtime/typecheck error，不兜底。
```

### 10.2 scan

`scan` 可新增，但重点是 core 支持新增。建议 v0 内建：

```abel
fn vector<any> scan(any... specs);
```

或后续提供更友好的：

```abel
fn void scan(any... refs);
```

例如：

```abel
int age;
str school;
scan(&age, &school);
```

`scan` 若接收指针，应按 C/C++ 能力模型直接写目标。v0 可以先实现 `print/println/build_string`，`scan` 留接口位置。

### 10.3 Core 扩展结构

内建函数不应该散落在 parser、typechecker、interpreter 各处。必须有注册结构：

```cpp
class BuiltinRegistry {
public:
    void registerFunction(BuiltinFunctionDesc desc);
    void registerMethod(BuiltinMethodDesc desc);
    void registerType(BuiltinTypeDesc desc);
    void registerOperator(BuiltinOperatorDesc desc);
};
```

注册项包含：

```text
symbol/name
signature
variadic flag
typecheck callback
runtime callback
doc string
```

这就是「让更多新增可能且方便」的核心，不是手工新增几个函数。

---

## 11. 函数声明与函数值

普通函数保留 Abel 风格：

```abel
fn int add(int a, int b) {
    return a + b;
}
```

函数值：

```abel
func int(int, int) f;
```

函数参数默认按值。引用参数：

```abel
fn void inc(int& x) {
    x = x + 1;
}
```

指针参数：

```abel
fn void inc_ptr(int* p) {
    *p = *p + 1;
}
```

返回引用：

```abel
fn int& first(vector<int>& xs) {
    return xs[0];
}
```

返回引用不做生命周期兜底。

---

## 12. lambda

lambda 进入 v0，采用 C++ capture list。这个设计已确认完美，保留。

```abel
fn int main() {
    int x = 1;
    int y = 2;

    func int() f = lambda [=] int() {
        return x + y;
    };

    func void() g = lambda [&] void() {
        x = x + 1;
    };

    func int() h = lambda [x, &y] int() {
        y = y + 1;
        return x + y;
    };

    g();
    return h();
}
```

规则：

```text
[=]      按值捕获用到的外部变量。
[&]      按引用捕获用到的外部变量。
[x]      按值捕获 x。
[&x]     按引用捕获 x。
[x, &y] 混合捕获。
引用捕获不做生命周期兜底。
```

lambda 表达式产生函数值。实现可把 lambda 降为 closure object：

```text
capture storage
function body AST
call operator
```

但 v0 不开放用户自定义 `operator()`。

---

## 13. struct 与方法

```abel
struct Counter {
    int value;

    init(int start) {
        value = start;
    }

    fn void inc() {
        value = value + 1;
    }

    const fn int get() {
        return value;
    }
}
```

规则：

```text
struct 是值类型。
struct 赋值复制整个对象。
字段未初始化不兜底。
struct 成员默认 public。
private 可显式声明。
v0 禁止引用字段。
v0 允许指针字段。
```

构造：

```abel
Counter c = Counter(0);
```

方法 receiver：

```text
非 const 方法：this 类型为 Self*
const 方法：this 类型为 const Self*
```

方法体内字段裸名：

```abel
value = value + 1;
```

等价于：

```text
this->value = this->value + 1
```

v0 支持显式 `this`：

```abel
this->value = this->value + 1;
```

---

## 14. 控制流

if：

```abel
if (cond) {
    ...
} elseif (cond2) {
    ...
} else {
    ...
}
```

注意是 `elseif`，不是 `else if`。

C-style for：

```abel
for (int i = 0; i < n; i = i + 1) {
    ...
}
```

repeat：

```abel
repeat(n) {
    ...
}
```

语义：执行 `n` 次。`n` 应为整数表达式；若为负，不兜底，v0 可直接执行 0 次或视为错误，需要实现时选一个并记录。推荐负数执行 0 次，保持简单。

vector range-for：

```abel
for (a in V) {
    a = a + 1;
}
```

规则：

```text
V 必须是 vector<T>。
a 是 T& 引用对象。
修改 a 会修改 V 中元素。
```

示例：

```abel
vector<int> xs = {1, 2, 3};

for (x in xs) {
    x = x + 10;
}

// xs = {11, 12, 13}
```

其他控制流：

```abel
while (cond) { ... }
return expr;
break;
continue;
```

条件必须是 bool。不做 C/C++ 的 int 到 bool 隐式条件转换，除非后续明确改变。

---

## 15. operator

v0 废掉这些用户自定义 operator：

```text
operator()
operator[]
operator<>
```

注意：

```text
函数调用 ()
vector 索引 []
vector<T> 类型参数 <>
```

仍然是语法，只是不可由用户重载。

保留内建 C/C++ 常用 operator：

```text
+ - * / %
== != < <= > >=
&& || !
& 取地址
* 解引用
. 字段访问
-> 指针字段访问
= 赋值
```

v0 加入实用新 operator，作为 Abel 优势点：

```text
**   power
%%   Euclidean modulo
<?   min
>?   max
|>   pipe
```

建议语义：

```abel
a ** b       // power(a, b)
a %% b       // 数学模，结果非负，若无法定义则错误
a <? b       // min(a, b)
a >? b       // max(a, b)
x |> f       // f(x)
x |> f(a)    // f(x, a)
```

`|>` v0 可先做语法糖，不开放重载。

`** %% <? >?` 可以进入 operator overload 集合，但 v0 可先只支持内建数值类型。

不要让 operator 系统阻塞解释器主线。先保守实现内建，用户自定义 operator 可只支持普通二元算术/比较，或延后。

---

## 16. debt 与 backend

旧 `concept` 废除。

`debt` 保留：

```abel
debt fn int parse_project(str path);
```

含义：

```text
debt 表示未来用 Abel 填充，或者转为 backend。
debt 有签名，无函数体。
运行触达未处理 debt，报错。
```

`backend` 囊括旧 concept 地位。backend block 是一个外部系统声明，类似 struct，但没有函数体：

```abel
backend MathSystem {
    fn int fast_add(int a, int b);
    fn void sort(vector<int>& xs);
    fn str make_title(str prefix, int id);
}
```

含义：

```text
backend 是外部能力系统。
内部声明 fn 签名。
没有 Abel 函数体。
由 backend manager 绑定到 Qt plugin。
```

调用：

```abel
int x = MathSystem::fast_add(1, 2);
```

backend block 必须进入 AST 和 symbol table。typechecker 确认调用签名。runtime 通过 BackendRegistry 分发。

---

## 17. backend 插件开发者友好

插件开发者不应该手写大量 `AbelValue` 拆箱代码。核心必须提供高层 binder。

底层接口：

```cpp
class IAbelBackend {
public:
    virtual ~IAbelBackend() = default;

    virtual QString backendId() const = 0;
    virtual QList<AbelBackendFunction> functions() const = 0;

    virtual AbelValue call(
        const QString& symbol,
        const QList<AbelValue>& args,
        AbelRuntimeContext& ctx
    ) = 0;
};

#define IAbelBackend_iid "org.abel.IAbelBackend/1.0"
Q_DECLARE_INTERFACE(IAbelBackend, IAbelBackend_iid)
```

插件加载：

```cpp
QPluginLoader loader(path);
QObject* obj = loader.instance();
auto* backend = qobject_cast<IAbelBackend*>(obj);
```

开发者友好层：

```cpp
class AbelBackendPluginBase;
class AbelBackendBinder;
```

理想插件：

```cpp
class MathBackend final : public AbelBackendPluginBase {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IAbelBackend_iid)
    Q_INTERFACES(IAbelBackend)

public:
    MathBackend() {
        bind("MathSystem.fast_add", [](int a, int b) {
            return a + b;
        });

        bind("MathSystem.sort", [](std::vector<int>& xs) {
            std::sort(xs.begin(), xs.end());
        });

        bindVariadic("Std.build_string", [](AbelVariadicArgs args) {
            return args.buildString();
        });
    }

    QString backendId() const override {
        return "MathSystem";
    }
};
```

`abelcore` 负责：

```text
AbelValue -> C++ typed args
C++ return -> AbelValue
vector<T>& 传引用
QString / QChar 映射
any 映射
variadic 映射
diagnostic 包装
```

支持类型最小集：

```text
int/i32
long/i64
ll/i64
double/f64
bool
char/QChar
str/QString
vector<T>
T&
T*
any/AbelValue
vector<any>/std::vector<AbelValue>
```

插件可以直接获得受控 native 引用，例如 `std::vector<int>&`。这是 Abel 的能力点。由于 Qt kit、compiler、abelcore 共享库全部锁定，不追求跨 ABI。

---

## 18. ResourceNode JSON

一个 JSON 描述一个 Qt plugin：

```json
{
  "id": "math.backend",
  "kind": "qt_plugin",
  "path": "plugins/libmath_backend.so",
  "iid": "org.abel.IAbelBackend/1.0",
  "backendId": "MathSystem",
  "qtVersion": "6.11.1",
  "kit": "gcc_64",
  "symbols": [
    "MathSystem.fast_add",
    "MathSystem.sort"
  ]
}
```

JSON 只描述资源，不作为调用协议。

ResourceNode 字段：

```text
id
kind
path
iid
backendId
qtVersion
kit
symbols
state
lastError
```

状态：

```text
unloaded
loaded
failed
```

v0 插件加载后不卸载，进程结束释放。不要做 hot reload。

---

## 19. AST 节点清单

所有 AST 节点带：

```cpp
SourceSpan span;
```

SourceSpan：

```cpp
struct SourceSpan {
    QString file;
    int startOffset;
    int endOffset;
    int startLine;
    int startColumn;
    int endLine;
    int endColumn;
};
```

Program：

```text
ProgramNode
SourceFileNode
ModuleDeclNode
UseDeclNode
```

Declarations：

```text
VarDeclNode
ConstDeclNode
FunctionDeclNode
ParameterDeclNode
StructDeclNode
FieldDeclNode
MethodDeclNode
ConstructorDeclNode
EnumDeclNode
TypeAliasDeclNode
OperatorDeclNode
DebtFunctionDeclNode
BackendBlockNode
BackendFunctionDeclNode
TemplateDeclNode
InterfaceDeclNode
```

`ConceptFunctionDeclNode` 不要再建；concept 已废除。

Types：

```text
BuiltinTypeNode
NamedTypeNode
ConstTypeNode
PointerTypeNode
ReferenceTypeNode
VectorTypeNode        // vector<T>
FunctionTypeNode      // func R(A, B)
AnyTypeNode
VariadicTypeNode      // any...
```

Statements：

```text
BlockStmtNode
DeclStmtNode
ExprStmtNode
IfStmtNode            // branches include elseif
WhileStmtNode
ForStmtNode           // for(init; cond; step)
RepeatStmtNode
RangeForStmtNode      // for(a in V)
ReturnStmtNode
BreakStmtNode
ContinueStmtNode
DeleteStmtNode        // if new/delete enters v0
```

Expressions：

```text
LiteralExprNode
NameExprNode
ThisExprNode
NullptrExprNode
InitListExprNode

UnaryExprNode
BinaryExprNode
AssignExprNode

AddressOfExprNode
DerefExprNode

FieldAccessExprNode
PointerFieldAccessExprNode
IndexExprNode         // vector indexing only

CallExprNode
MethodCallExprNode
StaticCallExprNode
ConstructorCallExprNode
BackendCallExprNode

CastExprNode
NewExprNode
LambdaExprNode
PipeExprNode          // may be lowered to CallExpr
```

AST 所有权：

```text
std::unique_ptr tree。
不要 QObject 化 AST。
需要索引用 node id 或裸指针观察引用。
```

---

## 20. Lexer / Parser 关键点

关键字：

```text
module use export
const fn debt backend
struct enum type
template interface require operator static public private
if elseif else while for in repeat return break continue
true false nullptr
lambda
any vector func
new delete cast
this
```

注意：

- `concept` 不再是关键字；若旧文件出现 concept，报错并提示使用 backend。
- `elseif` 是关键字；不要解析成 `else if`。
- `vector<T>` 使用 `< >` 类型参数语法；但 `operator<>` 废除。
- `[]` 只用于 vector indexing；但 `operator[]` 废除。
- `()` 用于分组/调用；但 `operator()` 废除。

表达式优先级建议从高到低：

```text
primary: literal, name, this, nullptr, lambda, constructor, grouped
postfix: call (), method ., static ::, pointer field ->, index []
unary:   !, -, +, &, *, new, cast<T>
power:   **
mul:     * / % %%
add:     + -
compare: < <= > >=
minmax:  <? >?
eq:      == !=
and:     &&
or:      ||
pipe:    |>
assign:  =
```

`**` 是否右结合需要定。推荐右结合。

`|>` 建议低于 `||` 高于 `=` 或低于 `||`，实现时固定并写测试。

---

## 21. 类型检查核心

必须区分：

```text
lvalue
prvalue
```

表达式类型结果：

```cpp
struct ExprType {
    Type type;
    ValueCategory category; // LValue or PRValue
    bool isMutable;
};
```

产生 lvalue：

```text
变量名
引用变量名
*p
obj.field
ptr->field
vector[i]
返回 T& 的函数调用
```

产生 prvalue：

```text
字面量
算术表达式
比较表达式
返回 T 的函数调用
指针值
lambda 表达式
constructor 返回值
```

赋值：

```text
lhs 必须 mutable lvalue。
rhs 可转换/可复制到 lhs type。
引用变量赋值写其绑定对象。
```

引用初始化：

```text
T& r = expr;
expr 必须是 T lvalue。
const T& 可绑定到 T lvalue 或 T prvalue；是否延长临时生命周期 v0 可不做复杂承诺，推荐先禁止绑定 prvalue，保持实现简单。
```

指针：

```text
&expr 要求 expr 是 lvalue，结果 T*。
*expr 要求 expr 是 T*，结果 T lvalue。
```

vector：

```text
vector<T> v = { ... };
每个元素必须可转为 T。
v[i] 要求 i 为 int，结果 T lvalue。
vector 内建 method receiver 可为 lvalue 或 prvalue；prvalue receiver 物化为临时对象，不做生命周期兜底。
len/empty 返回 prvalue。
push/pop/clear/reserve/resize 可作用于临时 vector；修改临时对象是否有用由用户负责。
front/back 返回元素 lvalue；若 receiver 是临时对象，引用/指针悬挂风险不兜底。
```

function：

```text
普通参数按值。
T& 参数要求实参是 T lvalue。
const T& 参数允许 const 兼容。
T* 参数要求实参是 T* prvalue。
any 参数接受任意值并装箱。
any... 接收剩余任意参数。
```

lambda：

```text
捕获列表解析外层符号。
[=] 按值复制捕获。
[&] 捕获 location/reference。
lambda body 内符号解析先查参数，再查局部，再查捕获。
```

backend：

```text
BackendSystem::fn(args)
解析到 BackendFunctionDecl。
按声明签名检查参数。
runtime 分发。
```

debt：

```text
debt fn 可被解析和调用。
runtime 触达直接 diagnostic/runtime error。
```

---

## 22. Runtime / Interpreter

不造 bytecode VM。v0 直接 tree-run：

```cpp
evalExpr(expr)
execStmt(stmt)
callFunction(fn)
callMethod(method)
callLambda(lambda)
callBackend(backendFn)
```

控制流不用 C++ exception：

```cpp
enum class FlowKind {
    Normal,
    Return,
    Break,
    Continue
};

struct ExecResult {
    FlowKind kind;
    AbelValue value;
};
```

Runtime 必须表达 C/C++ 存储模型：

```text
局部变量有存储。
struct 字段有存储。
vector 元素有存储。
T* 保存地址。
T& 绑定地址。
```

实现对象建议：

```cpp
class AbelValue;
class AbelType;
class AbelLocation;
class AbelFrame;
class AbelRuntimeContext;
class AbelFunctionValue;
class AbelVectorValue;
class AbelStructValue;
class AbelPointerValue;
class AbelReferenceValue;
```

即使实现内部有 location/store，也不能把语言变成 JS 对象引用语义。store 是解释器机器，不是用户语义。

不做兜底：

```text
越界检查
悬挂引用检查
空指针兜底
vector generation 检查
未初始化读取保护
```

但实现中为了诊断可保留 debug assert。不要向用户承诺安全。

---

## 23. BuiltinRegistry 设计

这是当前用户特别强调的核心：不是单纯新增 `build_string` 或 `scan`，而是让新增能力可能且方便。

必须把内建类型、函数、方法、operator 统一注册，不要散落在 parser/typechecker/interpreter 中。

建议：

```cpp
struct BuiltinFunctionDesc {
    QString name;
    AbelFunctionSignature signature;
    bool variadic = false;
    std::function<TypeCheckResult(const TypeCheckArgs&)> typecheck;
    std::function<AbelValue(const RuntimeArgs&)> runtime;
};

struct BuiltinMethodDesc {
    QString receiverTypePattern; // vector<T>, str, any...
    QString name;
    AbelFunctionSignature signature;
    bool variadic = false;
    ...
};

struct BuiltinOperatorDesc {
    QString op;
    AbelFunctionSignature signature;
    ...
};

class BuiltinRegistry {
public:
    void registerType(...);
    void registerFunction(BuiltinFunctionDesc);
    void registerMethod(BuiltinMethodDesc);
    void registerOperator(BuiltinOperatorDesc);

    LookupResult findFunction(...);
    LookupResult findMethod(...);
    LookupResult findOperator(...);
};
```

必须优先用 registry 实现：

```text
vector methods
str_to_chars / chars_to_str
to_str
build_string
print / println
scan
** %% <? >? |>
```

Parser 只负责语法。Typechecker 通过 registry 查能力。Interpreter 调 registry runtime callback。

这让 Abel 能快速加能力，不把语言核心写死。

---

## 24. Module / Use / Export

v0 可先单文件，但 AST 保留：

```abel
module math.counter;
use std.io;
export fn int main() { ... }
```

规则：

```text
module 可选。
use 支持模块级导入。
export 控制顶层符号是否外部可见。
同名冲突报错。
限定名后续支持。
```

不要把模块系统作为第一阶段阻塞项。先让单文件、内建、backend 跑通。

---

## 25. Template / Interface

v0 parser 可以接受：

```abel
template <type T>
interface Add { ... }
```

但 typechecker 遇到实际使用时报：

```text
not implemented in v0
```

注意：`vector<T>` 是内建泛型类型，不依赖通用 template 实现。

旧 concept 已废除。不要再实现 concept。

---

## 26. Error / Diagnostic

Diagnostic 必须结构化：

```cpp
enum class Severity { Error, Warning, Note };

struct Diagnostic {
    Severity severity;
    QString code;
    QString message;
    SourceSpan primary;
    QList<SourceSpan> related;
    QString explanation;
    QStringList suggestions;
};
```

错误类别建议：

```text
E01xx lexer/parser
E02xx resolve
E03xx type
E04xx operator/builtin
E05xx runtime
E06xx backend/resource
E07xx internal
```

Abel 接受 C/C++ 风险，不代表编译器不报明显静态错误。例如：

- `T& r;` 未初始化必须报错；
- 给 prvalue 取非常量引用必须报错；
- 调用不存在函数必须报错；
- backend symbol 未绑定必须报错；
- any cast 类型不匹配 runtime error。

---

## 27. 最小可运行样例

### 27.1 指针/引用

```abel
fn int main() {
    int x = 0;
    int& r = x;
    r = 5;

    int* p = &x;
    *p = *p + 1;

    return x;
}
```

期望：`6`。

### 27.2 vector

```abel
fn int main() {
    vector<int> xs = {1, 2, 3};

    int& r = xs[1];
    r = 10;

    xs.push(20);
    return xs[1];
}
```

期望：`10`。若 `push` 导致旧引用失效，语言不兜底；此例中 `r` 后续不再使用。

### 27.3 lambda

```abel
fn int main() {
    int x = 1;

    func void() f = lambda [&] void() {
        x = x + 9;
    };

    f();
    return x;
}
```

期望：`10`。

### 27.4 struct

```abel
struct Counter {
    int value;

    init(int start) {
        value = start;
    }

    fn void inc() {
        value = value + 1;
    }

    const fn int get() {
        return value;
    }
}

fn int main() {
    Counter c = Counter(0);
    c.inc();
    c.inc();
    return c.get();
}
```

期望：`2`。

### 27.5 build_string

```abel
fn int main() {
    int old = 18;
    str school = "Hakurei";
    str s = build_string("My Old is ", old, " , My School is ", school, "\n");
    print(s);
    return 0;
}
```

### 27.6 range-for

```abel
fn int main() {
    vector<int> xs = {1, 2, 3};

    for (x in xs) {
        x = x + 10;
    }

    return xs[2];
}
```

期望：`13`。

### 27.7 backend

```abel
backend MathSystem {
    fn int fast_add(int a, int b);
}

fn int main() {
    return MathSystem::fast_add(1, 2);
}
```

若 resource 绑定成功，期望：`3`；未绑定则 backend runtime error。

---

## 28. 实现顺序

不要一口吃完整语言。推荐阶段：

### Stage 1：工程骨架

- CMake；
- `libabelcore.so`；
- `abel` CLI；
- Qt kit 固定；
- QTest smoke；
- `.gitignore`；
- build 成功。

### Stage 2：Lexer / Parser 最小闭环

- token；
- 基础类型；
- 函数；
- block；
- var decl；
- return；
- int/string literal；
- binary expr；
- SourceSpan；
- diagnostic。

### Stage 3：类型与解释器最小闭环

- `int/double/bool/str/char`；
- local frame；
- assignment；
- arithmetic；
- `fn int main()`；
- `abel run` 返回值。

### Stage 4：C/C++ 值模型

- pointer；
- reference；
- address-of；
- deref；
- lvalue/prvalue；
- const 基础；
- struct + method + this。

### Stage 5：vector 与 lambda

- `vector<T>`；
- initializer list；
- indexing；
- vector methods；
- lambda capture；
- func type。

### Stage 6：BuiltinRegistry

- registry；
- `to_str`；
- `build_string`；
- `print/println`；
- variadic `any...`；
- `str_to_chars/chars_to_str`。

### Stage 7：控制流扩展

- `if/elseif/else`；
- `while`；
- `for(;;)`；
- `repeat(n)`；
- `for(a in V)`。

### Stage 8：backend

- backend block parse/typecheck；
- ResourceNode JSON；
- QPluginLoader；
- `IAbelBackend`；
- `AbelBackendPluginBase`；
- example plugin。

### Stage 9：operators

- `**`；
- `%%`；
- `<?`；
- `>?`；
- `|>`。

---

## 28.1 v1 complete 工作范围草案

v1 complete 的目标不是 v0 小修小补，而是把 Abel 从“核心可跑”推进到“完整语言 + 成熟库/后端系统 + 包管理引擎”的第一版可用生态。

一句话定义：

```text
v1 complete = 全量 Abel 语言实现 + 完整成熟的标准库/后端系统 + 自动依赖/资源解析的包管理引擎。
```

### v1 必须完成

#### A. 全量语言实现

```text
v1 必须覆盖本文件定义过的 Abel 语言面，而不是只覆盖 v0 smoke。
```

必须完成：

```text
1. Lexer / Parser：
   - // 行注释。
   - /* block comment */。
   - module / use / export。
   - debt / backend。
   - struct / enum / type alias。
   - template / interface / require 至少达到本文件声明语义的可用子集；若保留限制，必须是显式设计限制，而不是 not implemented。
   - operator 声明与可用用户自定义 operator 系统，明确排除项除外。

2. 类型系统：
   - 全部固定宽度基础类型：bool/i8/i16/i32/i64/u8/u16/u32/u64/f64/char/str/any/void。
   - C++ 兼容别名：int/long/ll/double。
   - const T / const T* / T* const / const T& 的完整矩阵。
   - T* / T& / 函数返回引用 / 引用参数。
   - vector<T>。
   - vector<T>.resize / 默认构造 / 默认插入语义。
   - func R(A...) 函数值。
   - lambda capture。
   - any / any... / cast<T>。
   - 通用 numeric conversion / numeric cast 矩阵。
   - struct 方法、const 方法、this、private/public。
   - enum / type alias。
   - backend function typecheck。
   - module 作用域、导出符号、跨文件名称解析。

3. 表达式与语句：
   - if/elseif/else。
   - while。
   - for(init; cond; step)。
   - repeat(n)。
   - range-for。
   - break/continue/return。
   - address-of / deref / field / pointer field / index。
   - constructor call。
   - static/backend call。
   - lambda call / function value call。
   - pipe。
   - ** / %% / <? / >?。
   - 用户自定义 operator 的完整可测子集。

4. lvalue/prvalue：
   - 必须保留 C/C++ 值模型。
   - 不做 Rust 式借用检查。
   - 不做 GC 动态语言模型。
   - vector/builtin method receiver 不因 prvalue 被拒绝。
   - prvalue receiver 物化为临时对象；生命周期风险不兜底。
   - `str_to_chars(ans).len()` 与短名 `stoc(ans).len()` 若存在，必须合法。

5. 默认构造与容器增长：
   - `vector<Complex> xs; xs.resize(2);` 必须有明确语义。
   - v1 必须引入“default-constructible”判定。
   - 若 T default-constructible，resize 增长时按 T 的默认构造产生元素。
   - 若 T 非 default-constructible，静态报错或明确诊断，不允许落到运行时 `<unknown>`。
   - struct default construction 规则必须写清：
     - 显式 `init()` 可作为默认构造；
     - 若无 `init()`，是否允许字段默认构造必须由 v1 规则固定；
     - 禁止靠 backend 绕过 vector<T> 泛型核心语义。

6. 通用数值转换：
   - `cast<int>(4.6)` 必须作为 numeric cast 支持，不能只支持 `cast<T>(any)`。
   - `int x = 4.6;` 是否允许必须由 v1 转换矩阵明确；草案默认按 C/C++ 风格允许 numeric assignment conversion，并对窄化给 warning 或可配置诊断。
   - 函数实参、返回值、赋值、初始化、backend 参数都必须共享同一套 numeric conversion 规则。
   - any extraction 与 numeric cast 必须分层：
     - `cast<T>(any)` 先解包/检查 any；
     - `cast<T>(numeric_expr)` 做 numeric conversion；
     - 不做隐式 dynamic fallback。
```

#### B. 成熟标准库

```text
v1 需要完整成熟的库系统，不只是几个 builtin。
```

必须完成：

```text
1. std 基础模块：
   - std.io：print/println/scan 或等价输入输出。
   - std.str：字符串构建、切分、查找、替换、str/vector<char> 转换。
   - std.char：char/QChar 常用能力。
   - std.vec：vector 方法与常用算法。
   - std.math：数值工具。
   - std.any：any 工具。
   - std.debug：调试/断言/诊断辅助。

2. stringify：
   - to_str(T) 扩展机制稳定。
   - build_string 对基础类型、vector、any、用户 struct 稳定。

3. IO / 文件 / 路径 / 环境：
   - 标准库层面给出成熟 API。
   - 具体实现可由 builtin 或 backend 提供，但用户不应手动管理底层 plugin resource JSON。

4. 错误与结果：
   - 标准库应有清晰的错误返回或诊断策略。
   - 不用安全兜底替代错误模型。
```

#### C. 完整成熟的后端系统

v1 的 backend 不是“手写 JSON + 手动复制 .so”的临时桥，而是 Abel 生态的一等能力。

必须完成：

```text
1. backend block：
   - 跨模块可见。
   - 参与包依赖解析。
   - 类型检查完整。

2. plugin SDK：
   - 安装版 Abel SDK。
   - headers 安装。
   - libabelcore.so 安装。
   - AbelConfig.cmake / AbelTargets.cmake。
   - 外部 backend 可 find_package(Abel REQUIRED)。
   - 插件可稳定链接 Abel::abelcore。

3. binder：
   - 覆盖 Abel 基础类型、str/char、any、vector<T> 常用类型。
   - 支持 T& / T* 能力面中明确可暴露的部分。
   - 支持 AbelRuntimeContext& 或等价诊断通道。
   - 支持 variadic any... 或等价的 vector<any>。
   - 支持结构化错误返回/诊断。

4. ResourceNode：
   - JSON 可作为内部资源描述格式保留。
   - 用户不需要手写依赖 JSON。
   - 包管理引擎自动生成、缓存、校验、加载 resource。
   - IID/backendId/symbol/ABI/Qt kit 校验完整。

5. 加载与运行：
   - backend plugin 自动发现。
   - 自动解析 package 内 backend artifact。
   - 自动设置或规避运行时库路径问题。
   - 清晰诊断缺失 plugin、ABI 不匹配、symbol 不匹配、Qt kit 不匹配。
```

#### D. 包管理引擎

v1 complete 必须有完整的包管理引擎。重点是：**用户不需要手动配置依赖 JSON 或 resource JSON**。

必须完成：

```text
1. 项目初始化：
   - `abel init`
   - 生成标准工程结构。
   - 生成包描述文件，格式由 Abel 选择；用户不手写底层 resource JSON。

2. 依赖声明与解析：
   - `abel add <package>`
   - `abel remove <package>`
   - `abel update`
   - 语义化版本或明确版本规则。
   - local path dependency。
   - registry dependency。
   - backend artifact dependency。

3. 锁定与缓存：
   - lockfile 由引擎生成。
   - 缓存下载/构建产物。
   - 校验版本、kit、ABI、平台。

4. 构建：
   - `abel build`
   - 自动构建 Abel modules。
   - 自动构建或获取 backend plugins。
   - 自动生成 ResourceNode 内部描述。
   - 自动连接 backend block 与 plugin symbol。

5. 运行：
   - `abel run`
   - 自动加载依赖 backend。
   - 不要求用户传 `--resource xxx.json`。
   - `--resource` 可保留为底层调试/专家选项，但不是普通用户路径。

6. 发布：
   - `abel package`
   - `abel publish` 可作为 v1 可选，但包格式必须足够支持本地/私有 registry。

7. 诊断：
   - 依赖冲突。
   - backend 缺失。
   - 版本不兼容。
   - Qt kit/ABI 不兼容。
   - symbol 签名不匹配。
```

包管理引擎负责生成/维护：

```text
package metadata
lockfile
backend resource descriptions
backend artifact cache
dependency graph
```

这些可以是 TOML/JSON/其他格式，但普通用户不应手工拼依赖 JSON 才能跑项目。

#### E. CLI / 工程系统

```text
v1 CLI 是用户入口，不只是 v0 smoke shell。
```

必须完成：

```text
abel init
abel check
abel build
abel run
abel test
abel add
abel remove
abel update
abel package check
abel resources check/load-check 作为底层专家命令保留
abel version
```

`abel run` 默认从项目描述解析入口，不再只能靠单文件路径。

#### F. 编译器/解释器一致性与调试诊断

v1 必须解决真实开发痛点。backend 不能替代语言核心、typechecker、interpreter 和诊断系统。

必须完成：

```text
1. `abel check` 与 `abel run` 类型语义一致：
   - check 通过后，run 不应再报同类静态类型错误。
   - typechecker / interpreter 对赋值、参数、返回、builtin method、backend call、numeric conversion、lvalue/prvalue 使用同一语义表。
   - 若 run 报错，应是运行期错误：越界、空指针、backend 加载失败、any cast 运行期不匹配等，而不是 check 漏掉的静态类型错误。
   - 每个曾出现 check/run 不一致的 case 必须变成回归测试。

2. 临时值 receiver：
   - `stoc(ans).len()` / `str_to_chars(ans).len()` 必须合法。
   - `.len()` / `.empty()` / `.front()` / `.back()` / 写方法在 prvalue receiver 上的语义必须由语言核心实现。
   - 不能要求 backend 修复 `.len()` 的 receiver 判定。

3. vector<struct>.resize / 默认构造：
   - `vector<Complex> xs; xs.resize(2);` 是语言核心和 runtime store 问题。
   - TypeChecker 必须判断 T 是否 default-constructible。
   - Interpreter 必须能为泛型 vector 元素执行正确默认构造或给出结构化诊断。
   - backend 只能提供外部能力，不能修泛型容器本身。

4. 通用 numeric cast / conversion：
   - `cast<int>(4.6)` 必须按 numeric cast 规则工作。
   - `int x = 4.6;` 必须按 v1 numeric assignment 规则稳定处理。
   - backend 可以提供 `round_to_int(double)`，但不能替代语言本身的 cast/conversion 语义。

5. 诊断去污染：
   - 一个根因不应引出大量 `<unknown>`、`ended without return`、未知类型连锁噪音。
   - 一旦表达式进入 error/unknown 状态，后续诊断要降级、合并或标记为 related note。
   - return analysis 不应在函数体已有致命类型错误时再追加误导性 ended-without-return。
   - Diagnostic 必须区分 primary error、related note、suppressed cascade。

6. 运行时崩溃 / 断点 / runtime error 必须输出运行栈和代码位置：
   - 每个调用帧记录 function/method/lambda/backend symbol。
   - 每帧记录 SourceSpan：file、line、column。
   - runtime error 输出 stack trace。
   - backend error 输出 Abel 调用点，而不是只输出 C++ plugin 内部信息。
   - debug breakpoint 输出当前源码位置与 Abel 调用栈，便于定位。
   - 若可能，输出当前行源码 excerpt；不能输出 excerpt 时至少输出 file:line:column。

7. 真正的 source location：
   - 所有 AST 节点必须保留可靠 SourceSpan。
   - module/use/export 后，SourceSpan 必须跨文件可靠。
   - v1 提供 `__FILE__` / `__LINE__` / `__COLUMN__` 或等价 source_location 内建。
   - backend 调用时 AbelRuntimeContext 或 BackendCall 必须携带调用点 SourceSpan。
   - 标准库 debug 能读取当前调用点 source location。
```

这些痛点必须进入 v1 回归测试矩阵：

```text
check/run 一致：
  - check 过的程序不在 run 阶段报静态类型错误。

临时 receiver：
  - while (stoc(ans).len() < len) { ... }
  - str_to_chars(ans).len()

vector<struct>.resize：
  - default-constructible struct resize 成功。
  - non-default-constructible struct resize 静态报错或明确诊断。

numeric cast：
  - cast<int>(4.6)
  - int x = 4.6
  - 函数实参和返回的 numeric conversion。

诊断去污染：
  - 单根因只产生一个 primary error，连锁信息作为 note 或被抑制。

runtime stack/source span：
  - 用户函数嵌套调用 runtime error。
  - lambda 内 runtime error。
  - backend call runtime error。
  - debug breakpoint 输出调用栈和 file:line:column。
  - __FILE__ / __LINE__ / __COLUMN__ 或等价 source_location 返回正确 Abel 调用点。
```

#### G. 测试与质量验收

```text
1. 4GB 限额全套 QTest。
2. Parser 错误恢复不允许卡死或爆内存。
3. 全量语言 feature matrix tests。
4. 标准库 tests。
5. 包管理 resolver tests。
6. backend package integration tests。
7. 编译器/解释器一致性 tests：
   - check/run 语义一致。
   - prvalue receiver。
   - vector<struct>.resize。
   - numeric cast/conversion。
   - diagnostic cascade suppression。
   - runtime stack/source span。
8. 外部 backend SDK fixture：
   - 独立目录。
   - find_package(Abel REQUIRED)。
   - 构建 Qt plugin。
   - 被 Abel package/run 自动加载。
9. CLI end-to-end：
   - 空目录 abel init。
   - abel add 一个含 backend 的包。
   - abel build。
   - abel run。
   - 不手写 resource JSON。
10. git diff --check。
```

### v1 可以不做

```text
abel split
abel jit
大型 IDE
Codex context exporter
跨 Qt 版本 ABI
跨编译器 ABI
Rust 式所有权/借用
GC 动态语言模型
完整 C++ 模板元编程复刻
```

注意：包管理引擎是 v1 必须项，不在“不做”列表。

### v1 complete 判定

只有当以下闭环都成立，才可标记 v1 complete：

```text
用户可以从空目录 `abel init` 创建 Abel 工程。
用户可以 `abel add` 依赖，不手写依赖 JSON。
用户可以 `abel build` 构建项目与 backend artifacts。
用户可以 `abel run` 自动加载依赖 backend，不手动传 resource JSON。
语言面达到本文件 v1 全量范围。
标准库达到成熟可用范围。
外部 backend SDK 可通过安装包和 CMake package 消费。
ResourceNode/JSON 退居包管理和 backend 系统内部实现细节。
`stoc(ans).len()` 或正式等价写法不因 prvalue receiver 被拒绝。
`abel check` 与 `abel run` 对类型语义一致。
vector<struct>.resize / 默认构造语义明确且测试通过。
numeric cast/conversion 语义明确且测试通过。
运行时错误、断点、backend error 都能输出 Abel 调用栈和 file:line:column。
诊断不再因单一根因污染成 `<unknown>` / ended-without-return 噪音瀑布。
所有 v1 验收测试在 4GB 内存限制下通过。
AGENTS.md / TUTORIAL_zh.md / CODEX.md 与实际行为一致。
```

---

## 29. Agent 不得回退的决定

以下是已确认设计，不要反复争辩，除非用户明确推翻：

```text
1. 使用 Qt 6.11.1 + C++23。
2. 使用 /home/tnuzy/Qt/6.11.1/gcc_64 kit。
3. 使用 GCC/G++ 14.2.0。
4. abelcore 是共享库。
5. backend 是 Qt Plugin。
6. 不做 split/jit/context exporter。
7. 不做 manifest/hash 工程系统。
8. Git commit 是审计与 rollback 机制。
9. Abel 采用 C/C++ 值模型。
10. 废除 ref/inout。
11. 支持 T* / T& / const T&。
12. vector<T> 是内建数组对象，底层 std::vector<T>。
13. 不使用 T[int]。
14. char=QChar，str=QString。
15. 加入 C++ 简单别名 int/long/ll/double。
16. 不支持 unsigned int 等复杂前端类型。
17. lambda 用 C++ capture list。
18. 加入 any。
19. 加入 variadic any...，重点是 core 易扩展。
20. if 使用 elseif，不是 else if。
21. 加入 repeat(n)。
22. 加入 for(a in V)，a 是 vector 元素引用。
23. v0 废除用户自定义 operator() / operator[] / operator<>。
24. 加入 ** %% <? >? |>。
25. 废除 concept。
26. backend block 囊括 concept 地位。
27. debt 表示未来 Abel 填充或转 backend。
28. 插件开发者友好是 backend 核心目标。
29. vector/builtin method receiver 不因 prvalue 被静态拒绝；prvalue receiver 物化为临时对象，风险不兜底。
```

---

## 30. 当前仓库状态

本文件创建时，旧的三份设计文档已被删除：

```text
Abel_Design_Closure_v1_zh.md
Abel_Engineering_Design_v1_zh.md
Abel_Language_Standard_v1_1_zh.md
```

本文件 `AGENTS.md` 是新的唯一设计入口。

当前尚未实现源码骨架。下一步应进入 Stage 1：CMake + `abelcore` shared library + `abelcli` + QTest smoke。

---

## 31. 工程进度 / 强制更新区

任何 Agent 完成实质任务后必须更新本区。

### 当前阶段

```text
v0 complete：语言核心、backend 核心、Qt 插件资源节点核心均已按本文件 v0 边界完成，并通过最终验收。
已发布 public GitHub 仓库：https://github.com/tnuxvs5t/Abel
仓库使用 proprietary/all-rights-reserved LICENSE；public visibility 不等于开源授权。
当前已修正 CODEX.md 定位：它是用户从 0 搭建 Abel 用户工程时给 Codex 使用的系统提示词，不是 Abel 编译器本体开发手册。
历史上已补充 Abel SDK v0 build-tree 消费方式、backend 从 0 搭建、ResourceNode path 与排错闭环；不写入用户测试用专项 backend 示例。
当前已补充 v1 complete 草案：v1 不是 v0 小修，而是全量语言、成熟标准库/后端系统、完整包管理引擎与自动依赖/资源解析。
当前已补充 v1 编译器/解释器一致性与调试诊断范围：check/run 语义一致、临时 receiver、vector<struct>.resize、numeric cast/conversion、诊断去污染、运行栈/source location、__FILE__/__LINE__/调用点位置。
当前已开始 v1 语义一致性落地：共享 numeric conversion，允许 `str_to_chars(ans).len()` 这类 prvalue vector receiver，`cast<int>(4.6)` 与 `int x = 4.6` 进入同一套 check/run 转换语义。
当前已开始 v1 调试诊断落地：runtime diagnostic 携带 Abel 调用栈，CLI 输出 `stack:`，函数/方法/lambda/backend/constructor 帧保留调用点 SourceSpan，parser 为主 AST 节点合成源码位置。
当前已落地 v1 `vector<struct>.resize()` 默认构造语义：容器增长走解释器默认构造回调，TypeChecker 提前拒绝非 default-constructible 元素，避免 check/run 语义分裂与 `<unknown>` 元素污染。
当前已落地 v1 调用点源码位置内建：`__FILE__` / `__LINE__` / `__COLUMN__` 作为编译器级 prvalue 标识符，TypeChecker 与 Interpreter 直接基于 AST SourceSpan 求值，不需要 lambda capture 或 backend 参与。
当前已落地 v1 TypeChecker 诊断去污染第一块：`unknown` 表示已有根因诊断，父表达式/语句静默传播，同时继续检查兄弟表达式以保留独立根因。
当前已落地 v1 包管理/项目入口第一片：新增 `abel.package.json` 包描述解析，`abel package check <project-dir>`，`abel check/run <project-dir>`，以及 package backendArtifacts 自动加载。
当前已落地 v1 `abel init` 第一片：空目录可生成最小 Abel 工程骨架，包含 `abel.package.json`、`src/main.abel`、README 与 `.gitignore`。
当前已落地 v1 package resolver/lockfile 第一片：package `dependencies` 支持本地 path 依赖，`abel update [project-dir]` 生成 `abel.lock.json`，并检测缺失依赖与循环依赖。
当前已落地 v1 package add/remove 第一片：`abel add path <dependency-dir> [project-dir]` 自动写入本地 path 依赖并刷新 lockfile，`abel remove <dependency-name> [project-dir]` 删除依赖并刷新 lockfile。
当前已落地 v1 package build 第一片：`abel build [project-dir]` 作为项目级构建门面，刷新 lockfile，检查入口 Abel 源码，并校验 package backendArtifacts 指向的插件文件存在。
当前已落地 v1 lockfile consumption 第一片：`abel check/run/build` 读取 package graph，已有 lockfile 会被消费并做 stale 检查，依赖包 backendArtifacts 会自动进入 build 校验与 run 加载。
当前已落地 v1 backend artifact cache 第一片：`abel build` 会把 package graph 中的 backendArtifacts 复制到根项目 `.abel/cache/backend/...`，`abel run` 优先加载缓存并在缓存缺失时回退源 artifact。
当前已落地 v1 安装版 Abel SDK 第一片：`cmake --install` 安装 headers、`libabelcore.so`、`abel` CLI、`AbelConfig.cmake` / `AbelTargets.cmake`，外部 backend fixture 可 `find_package(Abel REQUIRED)` 编译 plugin 并被安装版 `abel build/run` 自动缓存加载。
当前已落地 v1 backend binder 类型矩阵第一块：C++ plugin 可直接绑定 Abel 常用标量、char/QChar、常用 `vector<T>` 参数/返回、`vector<T>&` 写回，并可用末尾 `AbelRuntimeContext&` 报告结构化 backend 诊断。
当前已落地 v1 backend artifact 自动构建第一片：`backendArtifacts[].build` 支持 CMake source/buildDir/generator/target/args，`abel build` 会先构建 plugin，再复制到 `.abel/cache/backend/...`。
当前已落地 v1 backend artifact cache metadata 第一片：`abel build` 为缓存 plugin 写 `<plugin>.abel-cache.json` sidecar，`abel run` 只在缓存元数据匹配当前源 artifact 与 ResourceNode 字段时使用缓存，否则回退源 artifact。
当前已落地 v1 backend variadic binder 第一片：C++ plugin 可用 `bindVariadic` 绑定 Abel `any...`，payload 支持 `AbelVariadicArgs` 或 `std::vector<AbelValue>`，并继续支持末尾 `AbelRuntimeContext&`。
当前已落地 v1 ResourceNode Qt version / Qt kit load gate 第一片：`resources check` 仍只验证 JSON 形状，实际加载会在 `QPluginLoader` 前拒绝与当前 Abel runtime Qt version / kit 不一致的资源。
当前已落地 v1 package SemVer requirement 第一片：本地 path dependency 的 `version` 作为版本要求参与 resolver、lockfile、stale 检测与共享依赖冲突诊断。
当前已落地 v1 runtime diagnostic source excerpt 第一片：SourceSpan 保存单行源码 excerpt，CLI 对 primary error 与 stack frame callSite 输出源码行和 caret，用户函数/lambda/method/backend 错误测试覆盖 sourceLine。
当前已落地 v1 std.debug 第一片：`debug_break()` 与 `debug_assert(bool, any...)` 进入 BuiltinRegistry/TypeChecker/Interpreter，失败诊断复用 runtime stack/source excerpt/caret 通道。
当前已落地 v1 package local registry/cache 第一片：registry dependency 从本地 registry 目录选择满足 SemVer requirement 的最高版本，复制到 `.abel/cache/packages/<name>/<version>`，lockfile 记录 cached `resolvedPath`，`abel add registry` 进入 CLI。
当前已落地 v1 definite-return 诊断第一片：非 void function/method/lambda 的明显漏 return 会在 TypeChecker 阶段报出，`abel run` 前先被 `abel check` 拦住；若函数体已有根因诊断，不再追加误导性 missing-return 噪音。
当前已落地 v1 runtime conversion source span 第一片：运行期参数/返回/赋值/backend 参数转换错误指向实参、`return expr`、RHS 或 backend 调用点，不再默认落到声明参数/函数头。
当前已落地 v1 package multi-file/module 第一片：Parser 接受 `module/use` 顶层声明，package 输入会收集根项目 `src/**/*.abel` 并把 entry 文件放到最后合并 check/run/build，诊断继续保留各文件 SourceSpan。
当前已落地 v1 dependency source consumption 第一片：package graph 中 path/registry 依赖包的非 entry `src/**/*.abel` 库源码会进入根项目 check/run/build，依赖 entry 默认排除以避免 main 冲突。
当前已落地 v1 dependency export enforcement 第一片：跨包访问依赖包顶层 `fn/struct/backend` 需要依赖声明 `export`；依赖包内部仍可调用自己的非 export helper。
当前已落地 v1 package resolver conflict diagnostic 第一片：同一个 package name 若在依赖图中解析到不同 version/source/resolvedPath，会在 resolver 阶段报 dependency conflict，不再悄悄生成多份同名包 lock entries。
当前已落地 v1 package-aware function resolution 第一片：普通函数解析优先当前 package，同名 private helper 不再跨包污染，依赖包内部 helper 调用和根包同名 helper 调用可并存。
当前已落地 v1 package/module-aware struct/backend resolution 第一片：同名 struct/backend 按 package/module 保存候选，当前 package/module 优先，跨包只允许 export 可见符号；struct 类型在 TypeChecker/Interpreter 内部使用 package+module-qualified 名称，避免根包、依赖包与同包不同模块同名 struct 混淆。
当前已落地 v1 module/use 可见性第一片：CLI 为每个源文件标记 module/imports，TypeChecker 与 Interpreter 在函数、struct、backend 和 lambda 上共享 module/use 上下文；同包跨模块访问必须显式 `use`，依赖包跨模块访问仍要求 `use + export`，check/run 保持一致。
当前已落地 v1 显式 re-export 第一片：Parser 接受 `export use module.path;`，CLI 在合并 package 多文件时展开 re-export import 闭包；使用 facade module 会获得被 re-export 模块的可见符号，普通 `use` 不传播，TypeChecker 与 Interpreter 继续共享同一份 expanded imports。
当前已落地 v1 module-qualified function call 第一片：`cli.lib.a::value()` 可显式定位已 import 模块中的函数，用于解开同名 import 歧义；限定名不会绕过 `use` 可见性，TypeChecker 与 Interpreter 规则保持一致。
当前已用端到端回归锁定 v1 module-qualified backend call：`cli.lib.math::MathSystem::fast_add()` 可通过已 import 模块定位 backend block，并经 package backendArtifacts 自动加载真实 Qt plugin；限定 backend 调用同样不能绕过 `use`。
当前已落地 v1 module-qualified struct/type 第一片：`cli.lib.a::Point` 可作为类型名和构造调用使用，用于解开同名 imported struct 歧义；限定 struct 不绕过 `use`，同包不同模块同名 struct 在内部类型名上不再混淆。
当前已落地 v1 import ambiguity diagnostic 第一片：同名 imported function/struct/backend 报错会列出候选 module/package，CLI 输出 related declaration span/source excerpt 与 suggestions；backend block 重复判断改为 package+module 级别，允许同包不同模块同名 backend 进入 use/qualified 解析与歧义诊断。
当前已落地 v1 import alias 第一片：`use cli.lib.a as A;` 可用于限定函数、限定 struct 类型/构造和限定 backend 调用，alias 不绕过 `use`，TypeChecker 与 Interpreter 共享 alias map，重复 alias 在 CLI parse 阶段报错。
当前已落地 v1 `abel test` 第一片：项目级 `abel test [project-dir]` 会扫描根项目 `tests/**/*.abel`，每个测试独立合并依赖库源码、根项目非 entry 源码与测试 entry，先执行静态 check，再运行测试 main；退出码 0 为通过，非 0 或诊断为失败，并复用 package backendArtifacts 与额外 `--resource`。
当前已落地 v1 `std.test` 断言 builtin 第一片：`test_assert` / `test_eq` / `test_ne` 进入 BuiltinRegistry、TypeChecker 与 Interpreter，失败产生 E0599，并复用 runtime stack/source excerpt/caret；`abel test` 回归已用断言覆盖正例与失败诊断。
当前已落地 v1 const reference 第一片：AbelType 携带 const，`const T&` 变量/函数参数/lambda 参数/函数值参数作为只读 lvalue 引用参与 check/run 一致规则，`T&` 不可绑定已知 const lvalue，backend binder 可把 `const std::vector<T>&` 暴露为 Abel `const vector<T>&`。
当前已落地 v1 readonly location 传播第一片：`AbelLocation` 携带 `isReadOnly`，只读性从 name slot 扩展到字段、vector index/front/back、解引用、range-for 与 builtin receiver，非 name lvalue 的 const 突变开始保持 check/run 一致。
当前已落地 v1 全量固定宽度整数第一片：`i8/i16/i32/i64/u8/u16/u32/u64` 全部进入 Type/Value/TypeChecker/Interpreter/Builtin stringify/Backend binder 数值语义，check/run 对基础整数转换、默认构造、算术提升与 SDK 描述保持一致。
当前已落地 v1 enum/type alias 第一片：Parser 接受 `enum` 与 `type`，TypeChecker/Interpreter 收集 package+module 级候选，`type Name = T;` 展开为编译期别名，`enum Color { ... }` 作为 `i32` 背书类型，`Color.Green` 经 field-access 路径求值并保持 check/run 一致。
当前已落地 v1 struct public/private 第一片：Parser 接受 `public:` / `private:` 标签，AST 保留字段/构造/方法 privacy，TypeChecker 与 Interpreter 同步拒绝外部访问 private 字段、private 方法、private 构造与含 private 字段的外部 positional construction。
当前已落地 v1 std.str builtin methods 第一片：`str.len/empty/contains/find/substr/slice/replace` 进入 BuiltinRegistry，TypeChecker 与 Interpreter 对字符串方法保持 check/run 一致，字符串 prvalue receiver 可直接调用只读方法。
当前已落地 v1 std.vec builtin methods 第一片：`vector.insert/erase/find/sort` 进入 BuiltinRegistry，TypeChecker 与 Interpreter 对参数、receiver mutability、orderable 元素与越界诊断保持 check/run 一致。
当前已落地 v1 std.io `scan` 第一片：`scan(&x, ...)` 进入 BuiltinRegistry/TypeChecker/Interpreter，按 pointer 写入 bool/整数/f64/char/str/any，CLI stdin token 输入、readonly/const pointer 与 parse error 诊断保持 check/run 大体一致。
```

### 已完成

```text
1. 初始化 Git 仓库。
2. 建立旧设计文档 baseline commit。
3. 用户推翻旧 split/jit/context/manifest 工程幻想。
4. 锁定 Qt 6.11.1 + C++23 + GCC 14.2.0。
5. 锁定三核心：语言核心、backend 核心、Qt 插件资源节点核心。
6. 锁定 C/C++ 值模型：T* / T& / const / lvalue/prvalue。
7. 废除 ref/inout/concept。
8. 锁定 vector<T>、QChar/QString、lambda、any、variadic、backend block。
9. 删除旧三份 md。
10. 创建本 AGENTS.md 作为唯一工作规格。
11. 创建 Qt/C++23 CMake 工程骨架。
12. 创建 libabelcore.so 最小共享库。
13. 创建 abel CLI shell。
14. 创建 QTest smoke 测试。
15. 创建 examples/smoke/hello.abel。
16. 实现 token / lexer。
17. 实现 AST 基础节点。
18. 实现 parser 最小闭环：fn、backend、block、var decl、return、if/elseif/else、while、repeat、表达式、vector<T>、index。
19. 接入 `abel check <file>` 的 lex/parse 流程。
20. 增加 parser QTest。
21. 实现 Type/Value/Runtime 基础。
22. 实现 tree-run Interpreter 最小闭环：函数表、`fn int main()` / `fn void main()`、局部变量、赋值、return、if/while/repeat、break/continue、用户函数调用。
23. 支持 Stage 3 基础值：int/i32、long/ll/i64、double/f64、bool、str、char、void。
24. 支持 Stage 3 基础表达式：算术、比较、相等、逻辑短路、字符串拼接、`**`、`%%`、`<?`、`>?` 的基础解释执行。
25. 接入 `abel run <file>`。
26. 增加 interpreter QTest。
27. Runtime 从变量直接存值升级为 storage/location 模型，变量名、引用、指针可共享同一对象存储。
28. 实现 T& 引用变量初始化、引用赋值写回、引用参数按调用方 lvalue 绑定。
29. 实现 T*、`&x`、`*p`、`nullptr`、指针与 nullptr/指针相等比较。
30. 增加 pointer/reference interpreter tests：引用写回、指针解引用写回、引用参数写回、nullptr 比较、拒绝未初始化引用与 prvalue 引用绑定。
31. 实现 vector<T> 运行时类型和值表示，当前覆盖已支持元素类型，重点验证 vector<int>。
32. 实现 vector initializer list、vector 值复制、index 读写 lvalue。
33. 实现 vector 方法最小分发：len/empty/push/pop/front/back。
34. 实现 vector<T>& 参数按调用方 lvalue 绑定，函数内修改原 vector。
35. 增加 vector interpreter tests：索引写回、方法、赋值复制、引用参数写回。
36. 实现 BuiltinRegistry 最小接口：function/method 描述、arity 检查、运行时回调、默认注册入口。
37. 将 vector.len/empty/push/pop/front/back 从 Interpreter 硬编码迁入 BuiltinRegistry。
38. Interpreter 只负责解析 receiver/args 并委托 BuiltinRegistry 调用 method/function。
39. 增加 builtin registry QTest，验证默认 vector methods 与自定义 builtin function 注册/调用。
40. 实现 any 显式装箱值容器，`any` 赋值会保留内部 AbelValue。
41. 实现 user function 的 `any...` 最小运行时打包：剩余实参打包为 `vector<any>`。
42. Parser 增加 `any...` 规则校验：最多一个、必须最后、只允许 `any...`。
43. BuiltinRegistry 接入 `to_str` / `build_string` / `print` / `println` 最小闭环。
44. Interpreter 可调用 builtin function；`build_string("x=", x)` 通过 registry 执行。
45. 增加 builtin/interpreter/parser tests，覆盖 string builtins 与 `any...` 参数。
46. 新增独立 TypeChecker，覆盖当前 Stage 3-7 已支持语义：函数表、main 签名、变量/作用域、return、if/while/repeat 条件、break/continue、lvalue/prvalue、引用/指针、vector、用户函数、`any...` 与当前 builtin。
47. `abel check` 从 lex/parse 升级为 lex/parse/typecheck；`abel run` 在解释执行前先执行静态检查。
48. 增加 typechecker QTest，覆盖合法算术 main、非法 int 条件、非法 prvalue 引用绑定、vector 引用参数与 builtin、错误 push 参数、未知函数、循环外 break。
49. AST/parser/typechecker/interpreter 增加 C-style `for(init; cond; step)` 最小闭环。
50. AST/parser/typechecker/interpreter 增加 `for (x in vector)` range-for 最小闭环，循环变量按元素引用绑定，赋值可写回 vector 元素。
51. 增加 parser/typechecker/interpreter tests，覆盖 C-style for、continue 后 step、range-for 写回、非法 for 条件、非法非 vector range-for。
52. BuiltinRegistry 补齐 vector.clear/reserve/resize，并保留 arity 与非负 size 运行时诊断。
53. TypeChecker 补齐 vector.clear/reserve/resize 的静态检查，并将 vector.front()/back() 标记为 lvalue，const vector 的 front/back 赋值会被拒绝。
54. Interpreter 支持 vector.front()/back() 作为可赋值 location；新增 builtin/typechecker/interpreter tests 覆盖 resize/clear/default element、front/back 写回与非法 resize 参数。
55. Type/Value/Runtime 增加 struct 类型和值表示，struct 字段通过 location 读写，struct 赋值按值复制字段。
56. Parser 增加 struct 声明、字段、init 构造、方法、const 方法、this 表达式。
57. TypeChecker 增加 struct 收集、字段/构造/方法检查、字段访问、方法调用、构造调用、this 作用域；v0 仍禁止引用字段。
58. Interpreter 增加 struct 构造、字段访问/写回、方法调用、构造体内字段裸名、this 变量、const 方法基础只读框架。
59. 增加 parser/typechecker/interpreter tests，覆盖 Counter 示例、未知字段诊断、struct 赋值复制。
60. Type/AST/parser 增加 `func R(A, B)` 函数值类型与 `lambda [...] R(...) { ... }` 表达式解析。
61. Runtime 增加函数值 AbelValue 表示，闭包保存 value captures 与 reference captures。
62. Runtime frame 增加函数边界标记与 `visibleVariables()`，用于 lambda 默认捕获且避免函数体越界访问调用者局部变量。
63. TypeChecker 增加 function value call 检查、lambda 参数/返回检查、显式/默认 capture 检查，并隔离 lambda 体内未捕获外部变量。
64. TypeChecker 修正 lambda 体控制流边界：lambda 内 `break/continue` 不能继承创建处外层 loop 许可。
65. Interpreter 增加 lambda 创建、按值捕获复制、按引用捕获写回、混合捕获、函数值变量调用和非直接 callee 调用。
66. Interpreter 修正函数/方法/构造调用的实参求值顺序：实参在调用者作用域先求值，再进入被调函数/方法/构造体边界帧。
67. 增加 parser/typechecker/interpreter tests，覆盖 func type、lambda value/reference/mixed capture、未捕获变量拒绝、函数值实参类型错误、lambda 内非法 break。
68. TypeChecker 增加 backend block 收集与重复 backend / 重复 backend function 诊断。
69. TypeChecker 增加 `Backend::fn(args)` 静态签名检查，覆盖参数个数、参数类型、引用参数 lvalue 要求、返回类型。
70. Interpreter 增加 backend block 运行时表，`Backend::fn(args)` 可定位 backend/function；当前未绑定插件时返回结构化 E0607 诊断。
71. 增加 typechecker/interpreter tests，覆盖 backend 静态调用通过、错误参数拒绝、未知 backend function 拒绝、未绑定 backend 运行时诊断。
72. 实现 BackendRegistry 最小核心：backend function descriptor 注册、完整/短 symbol 规范化、find/has、bind runtime callback、call 分发。
73. BackendRegistry 增加结构化 backend/resource 诊断：未绑定 backend / 未绑定 runtime 使用 E0607，缺失 symbol 使用 E0608，非法注册/绑定使用 E0610，重复注册使用 E0611。
74. Interpreter 收集 backend block 时同步注册 BackendRegistry descriptor；运行 `Backend::fn(args)` 时先按签名求值/转换实参，引用参数保留 AbelLocation，再委托 BackendRegistry 分发。
75. 实现 ResourceNode JSON 解析与校验骨架：id/kind/path/iid/backendId/qtVersion/kit/symbols/state/lastError；校验 qt_plugin、IAbelBackend IID、symbol 归属 backend。
76. CLI 增加 `abel resources check <resource.json>`，合法资源输出 ok，非法资源输出 E0612 诊断。
77. 新增 `plugins/examples/math_backend/resource.json` 示例资源节点。
78. 新增 backend/resource QTest，覆盖 registry 注册/绑定/诊断、非法外部 symbol、ResourceNode 合法/非法 JSON。
79. 增加 `backend_interface.h`：`IAbelBackend`、`AbelBackendFunction`、IID `org.abel.IAbelBackend/1.0` 与 `Q_DECLARE_INTERFACE`。
80. 增加 `backend_plugin_base.h` / `backend_binder.h` 最小开发者友好层，支持低样板 `bind("Backend.symbol", lambda)`，当前覆盖 int/bool/i64/double/str/AbelValue/vector<int>& 的基础映射。
81. 建立 `plugins/examples/math_backend` Qt plugin 编译目标，链接同一个 `libabelcore.so`，输出 `build/plugins/libmath_backend.so`。
82. `MathBackend` 示例插件导出 `MathSystem.fast_add(int,int)` 与 `MathSystem.sort(vector<int>&)`。
83. ResourceNode loader 接入 `QPluginLoader`，校验 IID、backendId、symbols、签名，并把 plugin 函数绑定到 `BackendRegistry`。
84. `BackendRegistry` runtime callback 增加 `AbelRuntimeContext&`，便于 plugin/binder 报告结构化运行时诊断。
85. Interpreter 增加外部 `BackendRegistry` 注入入口；若已有 plugin 绑定，收集 backend block 时校验 Abel 声明与绑定签名一致，不再重复注册破坏绑定。
86. ResourceNode loader 调用 plugin 时保留引用参数的 `AbelLocation` 当前值，覆盖 `vector<int>&` 从 Abel 调到 Qt plugin 后写回。
87. backend/resource QTest 增加动态加载闭环：加载 math backend、registry 调用 fast_add、registry 调用 sort 写回 vector、Interpreter 注入 registry 后执行 `MathSystem::fast_add` 和 `MathSystem::sort`。
88. BuiltinRegistry 接入 `str_to_chars(str) -> vector<char>` 与 `chars_to_str(vector<char>) -> str`。
89. TypeChecker 增加 `str_to_chars` / `chars_to_str` 静态参数与返回类型检查，拒绝非 `str` / 非 `vector<char>` 实参。
90. 增加 builtin/typechecker/interpreter tests，覆盖字符串到字符向量、字符向量修改后转回字符串、非法 `chars_to_str(vector<int>)`。
91. 明确并锁定 `repeat(n)` 负数语义：执行 0 次；interpreter test 覆盖 `repeat(-3)` 不进入循环体。
92. AST/parser/typechecker/interpreter 增加 `cast<T>(any)`：静态要求源表达式为 `any`，运行时检查内部值类型完全匹配，不做 fallback。
93. TypeChecker/Interpreter 增加 pipe 表达式 `x |> f` 与 `x |> f(args...)`：当前支持命名普通函数、函数值变量和 builtin function。
94. 增加 parser/typechecker/interpreter tests，一组覆盖 `cast<T>(any)`、`|>`、`**`、`%%`、`<?`、`>?` 的解析、静态检查与执行。
95. CLI 增加 `abel run --resource/-r <resource.json> file.abel`，运行前加载 ResourceNode Qt plugin，保持 loader 存活，并把绑定后的 BackendRegistry 注入 Interpreter。
96. 新增 `examples/smoke/backend.abel`，覆盖 CLI 通过 ResourceNode 加载 `MathSystem.fast_add` 与 `MathSystem.sort(vector<int>&)` 后执行 backend 调用。
97. BuiltinRegistry 的 stringify 机制增加运行时回调入口，内建 stringify 可递归处理 any/vector，struct 交给用户 `to_str(T)`。
98. TypeChecker 对 `to_str` / `build_string` / `print` / `println` 增加 stringifiable 检查：基础类型、any、pointer/nullptr、可 stringify 元素 vector、以及存在 `fn str to_str(T)` 的 struct。
99. Interpreter 在调用 string builtin 时注入 stringify 回调；struct 值会调用用户定义的 `fn str to_str(Struct)`，返回值按 str 检查。
100. 增加 typechecker/interpreter tests，覆盖 `build_string("student=", Student)` 调用用户 `to_str(Student)`，以及无 `to_str` 的 struct 被静态拒绝。
101. 完成 v0 剩余差距审计：将 `scan`、完整 const 指针/引用矩阵、完整 backend binder 类型矩阵、struct private/public 与高级生命周期、用户自定义 operator 系统明确列为 v0 后续，不再阻塞 v0 complete。
102. 明确 v0 complete 验收口径：核心样例、语言核心、BuiltinRegistry 易扩展结构、Qt backend/resource 动态加载、CLI check/run/resource run、4GB 限额测试全部通过即可视为 v0 完成。
103. 执行最终验收：构建、4GB 限额全套 QTest、CLI check/run hello、ResourceNode JSON 检查、`abel run --resource` 后端样例全部通过。
104. 添加公开仓库用 README.md，说明 Abel v0 范围、构建/测试/smoke 命令与非开源状态。
105. 添加 proprietary/all-rights-reserved LICENSE，明确 public visibility 不授予使用、复制、修改、分发或训练等权利。
106. 创建 public GitHub 仓库 `tnuxvs5t/Abel` 并推送本地 `master` 到 `origin/master`。
107. 添加 TUTORIAL_zh.md，面向人类讲解 Abel 学习路线、语言核心、backend/plugin/resource 使用与 Abel 工程搭建方式。
108. 添加 CODEX.md，作为 Codex 在 Abel 工程内辅助开发时的项目系统提示词，强调 AGENTS.md、patch 写入、4GB 测试限制、v0 边界与协作节奏。
109. 重写 CODEX.md，将定位从“辅助开发 Abel 编译器本体”修正为“用户从 0 搭建 Abel 用户工程的 Codex 系统提示词”，覆盖 Abel CLI 发现、最小项目结构、.abel 语法边界、backend plugin 引入条件与用户工程验证流程。
110. 补充 TUTORIAL_zh.md 与 CODEX.md 的 backend 操作闭环：外部 backend 工程结构、CMake IMPORTED libabelcore 配置、plugin 编译命令、ResourceNode 绝对/相对 path 规则、运行命令与排错顺序。
111. 补充 Abel SDK v0 范围：当前只有 build-tree SDK，没有 install target / AbelConfig.cmake；外部 plugin 需 include `$ABEL_ROOT/src`、链接 `$ABEL_BUILD/libabelcore.so`、使用同 Qt/C++ ABI；记录 backend binder 当前参数/返回类型矩阵。
112. 应用户澄清，未将测试性专项 backend 接口写入正式教程；仅保留通用 backend SDK 类型、CMake、链接、加载、运行与排错闭环。
113. 设计层锁定 vector/builtin method receiver 不因 prvalue 被静态拒绝；prvalue receiver 物化为临时对象，生命周期风险按 Abel 不兜底方针处理。
114. 修正 v1 complete 草案：v1 complete 范围提升为全量 Abel 语言实现、成熟标准库、完整成熟 backend 系统、安装版 Abel SDK、包管理引擎、自动依赖/资源解析、项目级 CLI 与端到端验收。
115. 明确 v1 包管理引擎是必须项：用户通过 `abel init/add/build/run` 管理依赖和 backend artifacts，不需要手动配置依赖 JSON 或 resource JSON；ResourceNode/JSON 退居内部实现细节。
116. 明确 v1 必须解决 `abel check` 与 `abel run` 类型语义不一致：check 通过后 run 不应再报同类静态类型错误。
117. 明确 v1 必须在语言核心支持临时值 receiver：`stoc(ans).len()` / `str_to_chars(ans).len()` 合法，不能交给 backend 绕过。
118. 明确 v1 必须解决 `vector<struct>.resize()`、struct 默认构造、default-constructible 判定与容器增长语义。
119. 明确 v1 必须支持通用 numeric cast/conversion：`cast<int>(4.6)` 与 `int x = 4.6` 进入统一转换矩阵。
120. 明确 v1 必须做诊断去污染：单根因不再扩散成 `<unknown>`、误导性 `ended without return` 等噪音瀑布。
121. 明确 v1 runtime error / 崩溃 / debug breakpoint / backend error 必须输出 Abel 调用栈和 `file:line:column`。
122. 明确 v1 必须提供真正 source location：AST 跨文件 SourceSpan、`__FILE__` / `__LINE__` / `__COLUMN__` 或等价内建、backend 调用点 SourceSpan 传递。
123. v1 语义一致性第一块落地：`canAssignValue` / `convertValue` 支持 F64 到整数、整数到 F64、整数宽度转换，赋值/初始化/参数/返回/backend 参数共享同一转换入口。
124. `cast<T>(expr)` 从只支持 `any` 扩展为 numeric cast/conversion；`cast<T>(any)` 保留运行期内容检查。
125. TypeChecker 允许 vector builtin method 接收 prvalue receiver；写方法可作用于临时 vector，const lvalue 仍被拒绝。
126. Interpreter 对 builtin method receiver 先求值并物化临时 storage，不再一律 `evalLocation(receiver)`；`str_to_chars(ans).len()`、临时 `.push()`、临时 `.front()` 可执行。
127. 增加 typechecker/interpreter 回归测试，覆盖 `str_to_chars(ans).len()`、临时 vector 写方法、`cast<int>(4.6)`、`int x = 4.6`、函数实参 numeric conversion、backend 参数 numeric conversion。
128. v1 调试诊断第一块落地：Diagnostic 增加 stackTrace，RuntimeFrame 增加 symbol/callSite，RuntimeFrameGuard 保证错误产生时当前调用帧仍在栈内。
129. Interpreter 为普通函数、struct 方法、lambda、struct constructor、backend call 推入带 symbol 与调用点的 runtime frame；返回值转换、ended-without-return、backend unbound 等错误不再因提前 popFrame 丢失当前帧。
130. CLI diagnostic 输出增加 `stack:` 段，逐帧显示 `at <symbol> (<file>:<line>:<column>)`。
131. Parser 为 Program/decl/type/block/stmt/expr 主节点补 SourceSpan 合成，运行时错误可落到真实 Abel 源码位置，而不是空 file。
132. 增加 interpreter 回归测试，覆盖用户函数嵌套除零、lambda 除零、method 除零、unbound backend 调用的 stackTrace 顺序与源码位置。
133. v1 `vector<T>.resize(n)` 增长逻辑从 `defaultValueForType(T)` 改为 builtin method 调用解释器提供的 default constructor callback；缩小时仍直接裁剪。
134. TypeChecker 增加 `isDefaultConstructible`：基础值、指针、vector、any 可默认构造；引用、函数、unknown 不可默认构造；struct 递归检查字段或识别零参 `init()`。
135. TypeChecker 对无初始化变量与 `vector.resize` 元素类型执行同一套 default-constructible 检查；有显式字段参数的 `Struct(a,b,...)` 不被默认构造规则误伤。
136. Interpreter 增加 `defaultConstructValue`：无显式构造的 struct 递归默认构造字段，零参 `init()` 会创建对象并执行构造体；显式非零参 `init(...)` 的默认构造在运行时也拒绝。
137. 增加 typechecker/interpreter 回归测试，覆盖 `vector<Point>.resize(2)`、零参 `init()` 默认构造、非默认构造 struct resize/default var 拒绝、字段构造仍可用。
138. v1 调用点 source location 第一块落地：`__FILE__` 静态类型为 `str`，`__LINE__` / `__COLUMN__` 静态类型为 `int`，均为不可赋值 prvalue。
139. Interpreter 对 `__FILE__` / `__LINE__` / `__COLUMN__` 直接读取该 NameExpr 的 SourceSpan，返回当前 Abel 文件名、起始行、起始列。
140. lambda 内使用 source location 内建不需要捕获外部变量；它们不是普通变量，不进入闭包捕获表。
141. 增加 typechecker/interpreter 回归测试，覆盖 lambda 内 source location 内建静态通过，以及运行期返回 `<test>`、行号、列号。
142. TypeChecker 增加 `unknown` 传播纪律：return、条件、循环、引用初始化、一元/二元/cast/assignment/index/field/method/call/backend/builtin 等父节点不再对已知根因追加 `<unknown>` 噪音诊断。
143. TypeChecker 在 suppress cascade 的同时继续检查兄弟实参和独立表达式，避免一个 unknown 使同层其他真实错误被吞掉。
144. Builtin stringify、vector method、函数/函数值/constructor/backend 参数检查跳过 unknown 实参的派生错误，但保留参数表达式自身检查。
145. 增加 typechecker 回归测试，覆盖 unknown 函数不再引发 return/binary/condition/method receiver 级联，并确认两个独立 unknown sibling 都会报告。
146. 新增 PackageManifest 核心结构与解析器：读取 `abel.package.json` 的 name/version/entry/backendArtifacts，并校验 entry 文件存在。
147. `backendArtifacts` 可把 package metadata 转为内部 ResourceNode：支持 backendId、path、symbols，短 symbol 自动补成 `Backend.symbol`，path 相对 package root 解析。
148. CLI 新增 `abel package check <project-dir>`，用于检查项目包描述。
149. CLI `abel check <project-dir>` / `abel run <project-dir>` 支持读取 package entry，不再只能传单文件。
150. CLI `abel run <project-dir>` 会自动加载 package backendArtifacts；`--resource` 保留为底层调试/专家选项。
151. 新增 `examples/project` 最小 package 示例与 `examples/project_backend` backendArtifacts 示例。
152. 新增 package manifest QTest，覆盖 entry/backendArtifacts 解析、缺失 entry 诊断、package directory 识别。
153. README / TUTORIAL_zh.md / CODEX.md 同步项目入口骨架：普通用户优先使用 `abel.package.json` + `abel check/run .`，不再把手写 ResourceNode 当普通路径。
154. 新增 `PackageInitOptions` / `PackageInitResult` 与 `initPackageProject`，支持从空目录创建最小 Abel package。
155. `abel init [project-dir]` 已接入 CLI：生成 package manifest、`src/main.abel`、README.md、`.gitignore`，并拒绝覆盖已有关键文件。
156. package QTest 增加初始化测试：生成项目后能被 package parser 读取，且已有 manifest 时拒绝覆盖。
157. README / TUTORIAL_zh.md / CODEX.md 同步 `abel init`：从 0 搭 Abel 用户工程优先走 CLI 生成骨架。
158. package manifest 新增 `dependencies` 数组解析，当前支持 `{"name","kind":"path","path"}` 本地路径依赖，非 path kind 明确诊断为暂不支持。
159. 新增 `PackageDependency`、`PackageLockEntry`、`PackageLockResult` 与 `resolvePackageLock` / `updatePackageLock`，为 v1 包管理 resolver/lockfile 建立核心数据面。
160. `abel update [project-dir]` 已接入 CLI：解析 package graph，写出 `abel.lock.json`，并输出 lockfile 路径与锁定包数量。
161. lockfile JSON 当前包含 `formatVersion`、root package 信息与 resolved packages；每个依赖记录 name/version/kind/source/resolvedPath。
162. path dependency resolver 会校验依赖目录是 Abel package、依赖名与目标 manifest name 一致，并检测循环 path 依赖。
163. package QTest 增加本地 path dependency 写 lockfile 与缺失 path dependency 拒绝测试。
164. 新增 `PackageDependencyChangeResult`、`addPathPackageDependency`、`removePackageDependency`，把 manifest 依赖修改与 lockfile 刷新收束到 package core API。
165. `abel add path <dependency-dir> [project-dir]` 已接入 CLI：读取依赖 package name/version，向当前项目写入 `{"name","kind":"path","path","version"}`，并刷新 `abel.lock.json`。
166. `abel remove <dependency-name> [project-dir]` 已接入 CLI：按依赖名从 manifest 删除依赖，并刷新 `abel.lock.json`；删除动作不依赖未来 unknown kind 的完整解析。
167. package QTest 增加 add/remove 依赖闭环：验证 manifest 写入、manifest 删除、lockfile package 数量同步更新。
168. README / TUTORIAL_zh.md / CODEX.md 同步当前包管理事实：普通用户可用 `abel init/add/remove/update/check/run` 管理本地 path dependency，不再手写 dependencies JSON。
169. CLI 新增 `abel build [project-dir]` 项目级构建门面：读取 package manifest，刷新 lockfile，检查 backend artifact 文件存在性，并对 entry 执行 lex/parse/typecheck。
170. CLI check/run/build 共享 `checkSourceText` 静态检查路径，减少 check 与 build 入口语义分裂。
171. `abel init` 生成的 README 已同步 `abel update .` 与 `abel build .`，从 0 工程默认进入 package/update/build/check/run 闭环。
172. README / TUTORIAL_zh.md / CODEX.md 同步当时 `abel build` 边界：早期项目 build 门面先不假装具备 backend artifact 自动构建/cache；后续第 195-198 项已推进 CMake 自动构建第一片。
173. 新增 `PackageGraphResult` / `PackageResolvedResource`，把 root package、lockfile entries、依赖 package 与 backendArtifacts 汇总为 package graph。
174. 新增 `packageLockFromFile`、`packageGraphFromDirectory`、`updatePackageGraph`：已有 `abel.lock.json` 会被读取，缺失时内存 resolve，`abel build` 会刷新后再消费 graph。
175. package graph 对已有 lockfile 做 root/name/path 与 entries stale 检查；manifest 依赖变更后若未 update/build，`abel check/run` 会提示 lockfile stale。
176. CLI `abel build` 改为基于 `updatePackageGraph`，会校验根包与依赖包 backendArtifacts；输出 locked package 数和检查过的 backend artifact 数。
177. CLI `abel check/run <project-dir>` 改为基于 `packageGraphFromDirectory`，`run` 会按每个依赖包 root 自动加载依赖包 backendArtifacts。
178. package QTest 增加 package graph 回归：从 lockfile 收集依赖包 backendArtifacts，以及 stale lockfile 诊断。
179. README / TUTORIAL_zh.md / CODEX.md 同步 package graph consumption：依赖包 backendArtifacts 可被 build/run 自动消费，普通运行仍不需要手传 `--resource`。
180. 新增 `PackageCachedResource` / `PackageBackendCacheResult`，以及 `packageCacheRoot`、`packageBackendCacheDir`、`updatePackageBackendCache`、`cachedPackageBackendArtifacts`，把 backend artifact 缓存纳入 package core API。
181. `abel build [project-dir]` 在刷新/消费 package graph 与检查 entry 后，会把根包和依赖包 backendArtifacts 复制到根项目 `.abel/cache/backend/<package>/<backendId>/<plugin-file>`。
182. `abel run <project-dir>` 会优先加载 `.abel/cache/backend/...` 下的缓存 artifact；缓存不存在时回退到 package manifest/lock graph 中声明的源 artifact 路径。
183. package QTest 增加 backend cache 回归：覆盖依赖包 artifact 复制、缓存路径写入、`run` 资源优先缓存，以及源 artifact 缺失诊断。
184. README / TUTORIAL_zh.md / CODEX.md 同步 backend artifact cache 操作闭环：用户工程先 `abel build .` 写缓存，再 `abel run .` 自动加载。
185. CMake 新增安装版 SDK 第一片：安装 `abelcore`、`abel`、`src/abelcore/*.h`、`AbelConfig.cmake`、`AbelConfigVersion.cmake` 与 `AbelTargets.cmake`。
186. `abelcore` 导出为 `Abel::abelcore`，外部 backend 工程可 `find_package(Abel REQUIRED)` 后直接链接 `Abel::abelcore`，不再默认手写 build-tree `IMPORTED_LOCATION`。
187. 安装目标写入 Abel 安装 lib 与当前 Qt kit lib 的 RPATH，降低安装版 CLI/plugin 误加载系统 Qt 或找不到 `libabelcore.so` 的概率。
188. 新增外部 backend SDK fixture：独立 `tests/sdk_backend` 目录使用安装后的 `AbelConfig.cmake` 编译 Qt plugin，并由安装版 `abel build/run` 自动缓存加载，返回期望退出码 7。
189. README / TUTORIAL_zh.md / CODEX.md 同步安装版 SDK 第一片：推荐 `cmake --install ... --prefix $ABEL_PREFIX` 与 `find_package(Abel REQUIRED)`，build-tree SDK 降级为无安装包时的临时 fallback。
190. 扩展 `AbelBackendBinder`：支持 `QChar` / `char`、常用 `std::vector<T>` 参数、常用 `std::vector<T>` 返回、`std::vector<T>&` 写回，以及末尾 `AbelRuntimeContext&` 诊断通道。
191. `MathBackend` 示例插件新增 `first_char`、`char_code`、`make_range`、`sum_f64`、`flip_bools`、`fail_if_negative`，覆盖 char/vector 返回/vector 写回/backend 诊断。
192. backend/resource QTest 扩展类型矩阵回归：直接 registry 调用与 interpreter 注入路径均覆盖新 binder 能力。
193. 安装版 SDK fixture 扩展为外部 plugin 使用 `vector<char>` 返回、`vector<long>` 参数、`qint64` 汇总与 `AbelRuntimeContext&` guard，继续通过安装版 `abel build/run` 自动缓存加载。
194. README / TUTORIAL_zh.md / CODEX.md 同步 v1 binder 常用类型矩阵，删除旧的“char/QChar 未支持、vector 返回需手动 AbelValue、不能接 AbelRuntimeContext&”说明。
195. Package manifest 的 `backendArtifacts` 新增 package 层 `build` metadata，不污染 ResourceNode 运行时 JSON；运行时仍只消费已产出的 Qt plugin resource。
196. `updatePackageBackendCache` 在复制前会对带 `build` 的 backend artifact 执行 CMake 配置/构建，支持 `system`、`cmake`、`source`、`buildDir`、`generator`、`target`、`configureArgs`、`buildArgs`。
197. package QTest 新增 CMake backend artifact 自动构建回归：临时依赖包声明 `backendArtifacts[].build`，缓存阶段自动构建 `libdep_backend.so` 并复制到根项目 `.abel/cache/backend/...`。
198. README / TUTORIAL_zh.md / CODEX.md 同步 backend artifact 自动构建第一片：`abel build` 对带 build spec 的 artifact 先构建后缓存，但 registry/semver/download/ABI cache invalidation 仍未完成。
199. backend artifact cache 新增 `<plugin>.abel-cache.json` sidecar，记录 formatVersion、packageName、backendId、源 artifact canonical path、size、mtime、kind、iid、qtVersion、kit、symbols、declaredPath 与 sourcePathInput。
200. `cachedPackageBackendArtifacts` 不再盲用旧缓存：只有缓存 `.so` 存在且 sidecar 能解析并匹配当前源 artifact 与 ResourceNode 字段时，`abel run` 才使用 `.abel/cache/backend/...`；否则回退 package manifest/lock graph 中声明的源 artifact 路径。
201. package QTest 增加 backend cache metadata 回归：覆盖 sidecar 写入、缓存路径暴露，以及源 artifact 变化后 run resources 回退源 artifact。
202. 新增 `AbelVariadicArgs` 与 `AbelBackendPluginBase::bindVariadic`：后端插件可直接绑定 Abel `any...`，并使用 `args.value(i)` / `args.values()` / `args.buildString()` 读取已拆箱实参。
203. `AbelBackendBinder::describeVariadic` 会导出 `any...` 形态签名：return type 来自 C++ lambda，params 为单个 `any`，`variadic=true`，可与 Abel backend block 的 `fn R f(any... args)` 做 resource/registry 签名校验。
204. MathBackend 与安装版 SDK fixture 增加 variadic backend 回归：`join_debug` / `count_variadic`、`join` / `count` 均通过直接 registry 调用、解释器 backend call、安装版 `abel build/run` 链路。
205. README / TUTORIAL_zh.md / CODEX.md 同步 `bindVariadic` 操作方式：普通 `bind` 是固定 arity，Abel `any...` 后端必须用 `bindVariadic`。
206. `abelcore` 公共导出 `ABEL_QT_KIT_NAME` 编译定义，并新增 `currentAbelQtVersion()` / `currentAbelQtKit()` 作为 ResourceNode 与 package backend artifact 的统一当前运行时兼容字段来源。
207. `loadBackendResourceNode` 在 `QPluginLoader` 前检查 ResourceNode 声明的 `qtVersion` / `kit`，不匹配时返回结构化 E0613，避免错误 kit 的 plugin 进入加载链路。
208. package `backendArtifacts` 未显式声明 `qtVersion` / `kit` 时，默认使用当前 Abel runtime Qt version / kit；cache sidecar 仍记录并校验这些 ResourceNode 字段。
209. backend/resource QTest 增加回归：`resources check` / JSON parse 不因外来 Qt version / kit 失败，而实际 load 会拒绝 Qt version mismatch 与 Qt kit mismatch。
210. README / TUTORIAL_zh.md / CODEX.md 同步 ResourceNode 操作闭环：`resources check` 只查 JSON 形状，`run --resource` / package 自动加载才执行 Qt version / kit 兼容门禁。
211. package 版本语义第一片落地：包自身 `version` 必须是 `major.minor.patch` SemVer core，dependency `version` 字段解释为版本要求而不是锁定结果。
212. 本地 path dependency resolver 支持空要求 / `*` / 精确版本 / `^` / `~` / `< <= > >=` 组合条件；不满足要求时拒绝解析并给出 E0801 诊断。
213. lockfile entry 新增 `versionRequirement`，`abel update/build` 会同时记录实际解析版本和声明要求；`packageGraphFromDirectory` 消费旧 lockfile 时会重新校验实际包版本与 requirement。
214. lockfile stale 检测纳入 version requirement：manifest 只改依赖版本要求也会提示运行 `abel update` 或 `abel build`。
215. resolver 修正共享 path dependency 的 version requirement 校验：同一 package 已 seen 时仍先检查当前边的 requirement，避免左依赖满足、右依赖冲突被跳过。
216. package QTest 增加 SemVer requirement 回归：满足版本要求、拒绝不满足版本、拒绝非法 requirement、共享 path dependency 版本冲突、requirement 改动导致 stale lockfile。
217. README / TUTORIAL_zh.md / CODEX.md 同步包管理版本语义第一片：本地 path dependency 已有 SemVer requirement，但 registry、远程下载与完整 solver 仍未完成。
218. SourceSpan 新增 `sourceLine`，Lexer 在 token span 创建时保存当前源码行，Parser 合成 span 时沿用首 token 行，Runtime diagnostic 与 stackTrace 因此能携带单行 excerpt。
219. CLI diagnostic 输出新增源码 excerpt/caret：primary error 直接显示源码行，stack frame callSite 缩进显示调用点源码行。
220. interpreter 回归测试扩展 runtime error stack：用户函数嵌套、lambda、method、unbound backend 均校验 primary sourceLine 和关键调用帧 sourceLine。
221. README / TUTORIAL_zh.md / CODEX.md 同步运行期诊断操作闭环：先看 primary excerpt/caret，再沿 stack 调用点定位，backend error 要同时看 Abel 调用点和 plugin/resource 咬合。
222. BuiltinRegistry 新增 `debug_break()`：运行即产生 E0596 debug breakpoint 诊断，复用 RuntimeContext stackTrace 与 SourceSpan。
223. BuiltinRegistry 新增 `debug_assert(bool, any...)`：条件 true 继续执行，条件 false 产生 E0598，message 使用现有 stringify 通道拼接。
224. TypeChecker 增加 debug builtin 静态规则：`debug_break` 必须无参，`debug_assert` 至少一个 bool 条件，后续 message 参数必须 stringifiable；pipe 形态 `cond |> debug_assert(...)` 也参与检查。
225. builtin/typechecker/interpreter 回归测试覆盖 debug builtins 注册、断言 true 继续、断言 false 诊断、debug breakpoint stack/sourceLine。
226. package dependency 新增 `kind:"registry"` 本地 registry 第一片：manifest 支持 `registry` 字段，resolver 在 `<registry>/<name>/<version>/abel.package.json` 中选择满足 SemVer requirement 的最高版本。
227. registry dependency 会复制解析到的 package 到根项目 `.abel/cache/packages/<name>/<version>`，lockfile entry 记录 `kind:"registry"`、`source`、`versionRequirement` 与 cached `resolvedPath`，package graph 后续从 cached `resolvedPath` 消费。
228. 新增 `addRegistryPackageDependency` core API 与 `abel add registry <package-name> <version-requirement> <registry-dir> [project-dir]` CLI，普通用户不需要手写 registry dependency JSON。
229. package QTest 增加 registry 回归：最高满足版本选择、缓存目录写入、已有 lockfile 消费 cached package、无满足版本拒绝、add registry 写 manifest 并刷新 lockfile。
230. README / TUTORIAL_zh.md / CODEX.md 同步本地 registry/cache 操作闭环与边界：这是本地目录 registry，不是远程 registry/download/full solver。
231. TypeChecker 新增保守 definite-return 分析：`return`、所有 if/elseif/else 分支返回、`while(true)` 且 body 返回会被识别为 guaranteed return。
232. 非 void 普通函数、struct 方法与 lambda 若没有 guaranteed return，会在 check 阶段报 `may end without returning ...`，减少 check/run 语义分裂。
233. missing-return 诊断会在当前 callable 已有根因诊断时静默，避免未知函数/类型错误再连锁污染成缺 return 噪音。
234. typechecker QTest 增加 definite-return 回归：普通函数缺 return、方法缺 return、lambda 缺 return、if/else 和 while(true) definite return、已有根因时 suppressed missing-return。
235. README / TUTORIAL_zh.md / CODEX.md 同步 non-void callable return 规则与排错顺序。
236. ExecResult 新增 return SourceSpan，return flow 携带 `return` 语句位置，函数/method/lambda 返回值转换错误使用该 span 报告。
237. Interpreter 参数转换与引用绑定诊断改用实参 span：普通函数、method、pipe 调用、function value、backend call 均优先指向调用点的错误实参。
238. 变量初始化与赋值转换诊断改用 initializer/RHS span，减少错误落到声明或整条赋值表达式的不精确情况。
239. interpreter QTest 增加 runtime conversion sourceLine 回归：普通函数实参、函数 return、lambda return、method 实参与 backend 实参转换错误均校验 primary sourceLine。
240. README / TUTORIAL_zh.md / CODEX.md 同步运行期 conversion diagnostic 排错规则。
241. module-qualified function call 第一片落地：`module.path::fn(args)` 会在 TypeChecker 阶段按模块名解析普通函数，而不是只尝试 backend static call。
242. Interpreter 同步支持 `module.path::fn(args)`，运行时按当前 package/module/import context 解析目标函数，避免 check/run 分裂。
243. 限定函数调用不绕过 `use`：同包跨模块即使用 `cli.lib.a::value()` 也必须显式 `use cli.lib.a;`。
244. 同名 import 可用限定名解歧：`use cli.lib.a; use cli.lib.b;` 后裸 `value()` 报 ambiguous，`cli.lib.a::value()` / `cli.lib.b::value()` 可通过。
245. CLI 多文件 package 回归测试扩展：覆盖限定名正例、限定名缺 use 负例、同名 import 裸调用歧义负例。
246. CLI 多文件/backend package 回归测试扩展：覆盖 `cli.lib.math::MathSystem::fast_add()` 正例，通过 package backendArtifacts 自动加载真实 MathBackend plugin 并返回 42。
247. 限定 backend 调用缺 `use` 负例已锁定：即使写全 `cli.lib.math::MathSystem::fast_add()`，当前模块未 `use cli.lib.math;` 也必须在 check 阶段报 backend 不可见。
248. Parser 支持限定类型名：`module.path::Type`、多段 `A::B::Type` 以及限定类型后的指针/引用声明可被识别为类型声明。
249. TypeChecker 支持 module-qualified struct type 与 constructor call：`cli.lib.a::Point p = cli.lib.a::Point(10);` 会按目标 module 解析 struct，并继续执行参数/字段初始化检查。
250. Interpreter 同步执行 module-qualified struct constructor，check/run 对限定 struct 类型和构造调用保持一致。
251. struct 内部类型名从 package-qualified 提升为 package+module-qualified，允许同包不同模块存在同名 struct，并避免值类型在字段访问/赋值时混淆。
252. CLI 多文件 package 回归测试扩展：覆盖同名 imported struct 的限定构造正例、限定 struct 缺 use 负例、裸 `Point` 歧义负例。
253. CLI diagnostic 输出扩展为完整消费 Diagnostic 的 explanation、related spans 与 suggestions；related span 会输出 file:line:column、source excerpt 与 caret。
254. TypeChecker 的 imported function/struct/backend 歧义诊断会附带所有候选声明 span，并在 explanation 中列出候选 module-qualified 名称和 package。
255. backend block duplicate 检查从 package 级收窄到 package+module 级，TypeChecker 与 Interpreter 保持一致，支持同包不同模块声明同名 backend 后由 `use`/qualified name 决定可见性。
256. CLI 多文件 package 回归测试扩展：覆盖同名 imported function、struct、backend 歧义均会列出 candidate names 与 related spans。
257. Lexer/Parser 增加 `as` 关键字和 `use module.path as Alias;` 语法，`UseDeclNode` 保存 alias。
258. CLI 为每个源文件收集 import alias map，并把 alias map 标记到顶层 decl、struct method 与 backend function 上；同一文件重复 alias 直接报 `E0208 duplicate import alias`。
259. TypeChecker 在 decl context 中保存/恢复 alias map，限定函数、限定 struct 类型/构造、限定 backend 调用都会先把 alias 解析到真实 module 再执行已有 `use`/export 可见性规则。
260. Interpreter 同步保存/恢复 alias map；函数值/lambda 捕获 package/module/import context 时也保留 alias map，避免 check/run 语义分裂。
261. import alias 不绕过 `use`：alias 只来自显式 `use ... as ...`，可作为限定名前缀解歧，但最终仍按真实 module 检查可见性。
262. CLI 多文件 package 回归测试扩展：覆盖 alias 函数调用、alias struct 类型与构造、alias backend 调用，以及重复 alias 诊断。
263. CLI 新增 `abel test [project-dir]`：项目级扫描根项目 `tests/**/*.abel`，每个测试以独立 entry 运行，复用 package graph、dependency/root library sources、package backendArtifacts 和额外 `--resource`。
264. CLI 多文件 package 回归测试扩展：覆盖 `abel test` 正例汇总、失败测试 exit code 诊断与失败汇总。
265. README / TUTORIAL_zh.md / CODEX.md 同步 `abel test` 用户操作闭环：有 tests 目录时用 `abel test .` 运行项目级测试。
266. BuiltinRegistry 新增 `test_assert(bool, any...)`、`test_eq(actual, expected, any...)`、`test_ne(actual, expected, any...)`，断言失败产生 E0599，message 使用 stringify 通道并携带 runtime stack/source span。
267. TypeChecker 增加 test builtin 静态规则：断言条件必须 bool，`test_eq/test_ne` 前两个值必须可比较且可 stringify，后续 message 参数必须可 stringify；pipe 形态也参与检查。
268. builtin/typechecker/interpreter/CLI 回归测试覆盖 test assertions：通过断言继续执行，失败断言输出 E0599、源码行和调用栈，`abel test` 能报告 assertion failure。
269. README / TUTORIAL_zh.md / CODEX.md 同步 `std.test` 第一片：用户测试优先用 `test_assert` / `test_eq` / `test_ne`，不要只靠手写 exit code。
270. Parser 支持 `export use module.path;`，`UseDeclNode` 记录 exported 状态；普通 `use` 仍不传播。
271. CLI 合并 package 多文件时收集每个模块的 exported use，并对当前文件 direct imports 展开 re-export 闭包，再把 expanded imports 标记到顶层 decl、struct method 与 backend function。
272. CLI dependency source 回归测试扩展：依赖包 `dep.api` 通过 `export use dep.lib.math;` 做 facade，根项目只 `use dep.api;` 即可访问被 re-export 且 exported 的函数/struct；普通 `use` facade 不传播并被 check 拒绝。
273. README / TUTORIAL_zh.md / CODEX.md 同步 `export use` 第一片与边界：alias 不随 re-export 传播，完整 public/private 模块系统仍未完成。
274. AbelType 增加 `isConst` 与 `makeConstType`，display/type equality/assignability 开始区分并按值复制时剥离顶层 const，TypeChecker 与 Interpreter 的 TypeNode 转换会在 pointer/reference 包装前保留 base const。
275. TypeChecker 统一 reference 参数/返回检查：`const T&` 返回/参数产生只读 lvalue，函数、方法、构造、backend、pipe、函数值和 lambda 调用共享 `checkParameterArgument`；`const T&` 当前只允许绑定 lvalue，不做 prvalue lifetime extension。
276. Interpreter 同步 const reference 运行时绑定：函数、方法、lambda/function value 与局部 reference slot 会把 `const T&` 定义为只读别名，修改会报错；非 const `T&` 对明显 const name lvalue 的绑定在运行期也被拒绝。
277. Backend binder 对 `const std::vector<T>&` 生成 Abel `const vector<T>&` 签名，避免插件 SDK const vector 引用与 Abel 声明脱节。
278. TypeChecker / Interpreter 回归覆盖 `const int&` 只读读取、拒绝经 `const T&` 修改、拒绝非 const 引用绑定 const lvalue、拒绝 `const T&` 绑定 prvalue；README / TUTORIAL_zh.md / CODEX.md 同步当前 const reference 第一片边界。
279. Runtime `AbelLocation` 增加 `isReadOnly`，storage、vector element、struct field 与 alias location 都可携带只读标记，`defineValueVariable` / `defineVariable` 会把 const 变量变成 readonly storage/alias。
280. Interpreter 把 readonly 来源贯穿到非 name lvalue：字段访问继承 const receiver/readonly receiver/const field，`*const T*` 解引用产生 readonly alias，`vector[i]` 与 `front/back` 继承 const vector 或 const element，range-for over readonly vector 绑定 readonly 元素引用。
281. Interpreter 的函数、方法、构造、lambda/function value、pipe 与 backend reference 参数绑定不再只看 `NameExprNode` const slot，而是统一检查实参 location 的 `isReadOnly`；非 const 引用不能绑定 readonly lvalue。
282. BuiltinRegistry 在运行时拒绝 mutating vector builtin method 作用于 readonly receiver；struct 方法调用同步拒绝 readonly receiver 调用非 const method，方法体字段 slot 继承 receiver 与字段 readonly。
283. TypeChecker 同步补齐 readonly container/struct 规则：const vector index/front/back 返回不可变 lvalue，range-for over const vector 的循环变量只读，const struct receiver/const field 不可写，非 const method 不能作用于 const receiver。
284. TypeChecker / Interpreter 回归覆盖 const vector index/front/push/range-for、const struct field 与 const struct receiver 调 mutable method，锁定 readonly 非 name lvalue 的 check/run 一致性第一片。
285. AbelType 增加 `I8/I16/U8/U16/U32/U64`，并提供 signed/unsigned integer 分类与 bit width，`typeFromName` 接受 v1 清单中的全部固定宽度整数类型。
286. AbelValue 对全量整数做默认构造、debug/stringify、numeric conversion 与按目标位宽归一化；`cast<i8/i16/u8/u16/u32/u64>`、赋值/参数转换共享同一 `canAssignValue` / `convertValue` 路径。
287. TypeChecker 与 Interpreter 对整数二元运算共享“至少提升到 32 位、保留 64 位与 unsigned 参与”的第一片结果类型规则；`build_string` / `test_eq` 等 builtin 也能处理新增整数类型。
288. Backend binder 类型矩阵扩展到 Qt 固定宽度整数：`qint8/qint16/qint64/quint8/quint16/quint32/quint64` 可生成 Abel 签名、拆箱入参、装箱返回，并覆盖 vector 元素通道。
289. TypeChecker / Interpreter / Backend 回归覆盖固定宽度整数声明、转换、vector、stringify、运行期 wrap 语义与 backend binder 描述/调用。
290. Parser 新增 `enum Name { A, B }` 与 `type Name = T;` 顶层声明，AST 保存 export、名称与枚举项/目标类型。
291. TypeChecker 新增 enum/type alias 收集、可见性解析与递归 alias 检测；别名展开复用现有类型系统，enum 首片按 `i32` 值类型参与赋值、算术和参数转换。
292. Interpreter 同步 enum/type alias runtime 解析，`Color.Member` 经普通 field-access AST 求值为枚举序号，避免 check/run 语义分裂。
293. Parser / TypeChecker / Interpreter 回归覆盖 enum trailing comma、alias 嵌套 vector、alias 到 enum、错误枚举项、递归 alias 与 enum runtime 值。
294. Parser 支持 struct 内 `public:` / `private:` 标签，成员默认 public，标签切换后续字段、`init` 构造与方法的 `isPrivate` 标记。
295. TypeChecker 对 private 字段读取/写入、private 方法调用、private 构造调用、外部 positional construction 初始化 private 字段、以及 `vector<T>.resize()` 触发 private 默认构造做静态诊断。
296. Interpreter 保留当前 struct 执行上下文，运行时同步限制 private 字段、方法、构造与默认构造访问；struct 内部方法/构造仍可访问本 struct 私有成员，lambda 捕获保留创建处 struct 上下文。
297. Parser / TypeChecker / Interpreter 回归覆盖 struct public/private 正例与外部 private 字段、方法、构造、positional construction、private default resize 负例。
298. BuiltinRegistry 新增 `str.len()`、`str.empty()`、`str.contains(str)`、`str.find(str)`、`str.substr(int,int)`、`str.slice(int,int)`、`str.replace(str,str)` 第一片；`find` 未找到返回 `-1`，`substr/slice` 运行时拒绝负 start/len。
299. TypeChecker 增加 str builtin method 静态规则：参数数目、字符串 needle/replacement、整数 start/len 均在 check 阶段诊断；非 vector/str receiver 不再误报为只要求 vector。
300. Interpreter 通过现有 builtin method 调度执行 str 方法，字符串 prvalue receiver 会物化为临时 storage 后调用只读方法，保持和 vector prvalue receiver 方针一致。
301. builtin/typechecker/interpreter 回归覆盖 str 方法注册、运行结果、坏参数静态拒绝和负 slice 运行期诊断。
302. BuiltinRegistry 新增 `vector.insert(pos,value)`、`vector.erase(pos)`、`vector.find(value)`、`vector.sort()` 第一片。
303. TypeChecker 增加 std.vec 方法静态规则：insert/erase index、insert/find element 类型、sort orderable 元素、mutable receiver 与 prvalue receiver 规则保持一致。
304. builtin/typechecker/interpreter 回归覆盖 vector insert/erase/find/sort 正例、错误参数、非 orderable sort 与越界运行期诊断。
305. BuiltinRegistry 新增 `scan(any... refs)` 第一片：参数运行时必须为 pointer，按 stdin token 写入 bool、整数、f64、char、str、any 目标。
306. TypeChecker 增加 scan 静态规则：参数必须 pointer，readonly lvalue 取地址产生 const pointer，scan 拒绝 pointer-to-const 与不支持的 pointer target，并锁定 pipe 形态。
307. CLI/builtin/typechecker 回归覆盖 scan 注册、fake token 写回、坏 token 诊断、静态误用拒绝与真实 stdin 输入。
```

### v0 complete 边界审计

```text
已满足 v0 必须项：
1. 语言核心：lex/parse/AST/typecheck/tree-run 已覆盖函数、变量、控制流、指针、引用、vector、lambda、any、variadic、struct、backend call。
2. BuiltinRegistry：函数/method 描述、arity、运行时回调、vector methods、string builtins、str/char 转换、用户 to_str(T) stringify 回调已闭环。
3. Backend 核心：backend block parse/typecheck/runtime dispatch、BackendRegistry、ResourceNode JSON、QPluginLoader、IAbelBackend、plugin base/binder、示例 MathBackend 已闭环。
4. CLI：`abel check`、`abel run`、`abel resources check`、`abel run --resource` 已闭环。
5. Operators：`**`、`%%`、`<?`、`>?`、`|>` 已实现并测试；用户自定义 operator 不进入 v0。

明确延期到 v0 后：
1. `scan`：AGENTS.md 原文允许 v0 先实现 `print/println/build_string`，`scan` 留接口位置；输入系统会引入交互/IO 设计，不阻塞 v0 core complete。
2. 完整 const 指针/引用矩阵：当前保留基础 const 变量写保护、const 方法基础框架；`T* const`、`const T*`、`const T&` 完整兼容与 prvalue 绑定策略延期。
3. Backend binder 完整类型矩阵：当前覆盖示例与核心通路所需 int/bool/i64/double/str/AbelValue/vector<int>&；全类型矩阵与更多 vector<T>& 延期。
4. struct 高级项：private/public、复杂 const receiver、引用返回生命周期、指针字段高级场景延期；当前 value struct、字段、构造、方法、this、const 方法只读基础已闭环。
5. 用户自定义 operator 系统延期；v0 仅保留内建 operator 与 pipe 语法糖。

剩余 must-fix：
1. 最终运行一次 4GB 限额全套测试。
2. 最终运行 CLI smoke：check/run hello、resources check、run --resource backend。
3. 若全部通过，更新本区为 v0 complete 并提交最终验收 commit。
```

### 最近验证

```text
2026-06-22:
- 确认 /home/tnuzy/Qt/6.11.1/gcc_64 存在。
- 确认 /home/tnuzy/Qt/Tools/CMake/bin/cmake version 3.30.5。
- 确认 /home/tnuzy/Qt/Tools/Ninja/ninja version 1.12.1。
- 确认 /usr/bin/g++ version 14.2.0。
- 确认仓库已 git init。
- 配置命令通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja
  -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64
  -DCMAKE_C_COMPILER=/usr/bin/gcc
  -DCMAKE_CXX_COMPILER=/usr/bin/g++
  -DCMAKE_CXX_STANDARD=23
- 构建命令通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- CLI smoke 通过：
  build/abel version
- 测试通过：
  /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure
- `build/abel check examples/smoke/hello.abel` 输出 ok。
- 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
- 修复 parser 在错误恢复不前进、且 `::` 未消费时可能无限分配并 bad_alloc 的问题；新增 `StaticAccessExprNode` 支持 `MathSystem::fast_add` 这类静态/backend 调用语法。
- Stage 3 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 3 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
- CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 4 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 4 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
- Stage 4 CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 5 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 5 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
- Stage 5 CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 6 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 6 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
- Stage 6 CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 7 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 7 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
- Stage 7 CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 8 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 8 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
- Stage 8 CLI check smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel check examples/smoke/hello.abel'
  输出 ok。
- Stage 8 CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 9 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 9 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
- Stage 9 CLI check smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel check examples/smoke/hello.abel'
  输出 ok。
- Stage 9 CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 10 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 10 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
- Stage 10 CLI check smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel check examples/smoke/hello.abel'
  输出 ok。
- Stage 10 CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 11 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 11 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
- Stage 11 CLI check smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel check examples/smoke/hello.abel'
  输出 ok。
- Stage 11 CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 12 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 12 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
- Stage 12 CLI check smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel check examples/smoke/hello.abel'
  输出 ok。
- Stage 12 CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 13a 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 13a 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
- Stage 13a CLI check smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel check examples/smoke/hello.abel'
  输出 ok。
- Stage 13a CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 13b diff whitespace 检查通过：
  git diff --check
- Stage 13b 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 13b 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
  输出 6/6 tests passed。
- Stage 13b CLI check smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel check examples/smoke/hello.abel'
  输出 ok。
- Stage 13b CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 13b ResourceNode CLI smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel resources check plugins/examples/math_backend/resource.json'
  输出 ok。
- Stage 13c diff whitespace 检查通过：
  git diff --check
- Stage 13c 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 13c 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
  输出 6/6 tests passed。
- Stage 13c CLI check smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel check examples/smoke/hello.abel'
  输出 ok。
- Stage 13c CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 13c ResourceNode CLI smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel resources check plugins/examples/math_backend/resource.json'
  输出 ok。
- Stage 14a diff whitespace 检查通过：
  git diff --check
- Stage 14a 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 14a 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
  输出 6/6 tests passed。
- Stage 14a CLI check smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel check examples/smoke/hello.abel'
  输出 ok。
- Stage 14a CLI run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run examples/smoke/hello.abel; printf "exit=%s\n" "$?"'
  输出 exit=0。
- Stage 14a ResourceNode CLI smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel resources check plugins/examples/math_backend/resource.json'
  输出 ok。
- Stage 14b 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 14b 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
  输出 6/6 tests passed。
- Stage 14b CLI ResourceNode run smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel run --resource plugins/examples/math_backend/resource.json examples/smoke/backend.abel; printf "exit=%s\n" "$?"'
  输出 exit=4。
- Stage 14c 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
- Stage 14c 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
  输出 6/6 tests passed。
- v0 final 构建通过：
  /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  输出 ninja: no work to do。
- v0 final 在 4GB 虚拟内存上限下测试通过：
  /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
  输出 6/6 tests passed。
- v0 final CLI smoke 通过：
  /bin/bash -lc 'ulimit -v 4194304; build/abel check examples/smoke/hello.abel && build/abel run examples/smoke/hello.abel; printf "hello_exit=%s\n" "$?" && build/abel resources check plugins/examples/math_backend/resource.json && build/abel run --resource plugins/examples/math_backend/resource.json examples/smoke/backend.abel; printf "backend_exit=%s\n" "$?"'
  输出 ok、hello_exit=0、ok、backend_exit=4。
- 2026-06-23 public 发布准备：
  - git working tree 从 clean 状态开始；
  - gh 2.92.0 可用，但当前 GitHub CLI keyring token 对 tnuxvs5t 无效，需要重新认证后才能创建/推送 GitHub 仓库；
  - 本轮只改文档与 LICENSE，未运行重型测试，避免无必要消耗和内存风险。
- 2026-06-23 GitHub 发布验证：
  - gh repo create Abel --public --source=. --remote=origin --push
    成功创建并推送到 https://github.com/tnuxvs5t/Abel；
  - git remote -v 显示 origin 为 https://github.com/tnuxvs5t/Abel.git；
  - gh repo view tnuxvs5t/Abel --json name,visibility,url,defaultBranchRef
    输出 visibility=PUBLIC、defaultBranchRef.name=master。
- 2026-06-23 教程/Codex 文档准备：
  - 从 clean tree 开始；
  - 本轮新增 TUTORIAL_zh.md 与 CODEX.md，并更新本强制区；
  - 纯文档变更，计划仅运行 git diff --check，不运行重型 QTest。
- 2026-06-23 CODEX.md 定位修正：
  - 从 clean tree 开始；
  - 将 CODEX.md 改为用户从 0 搭建 Abel 用户工程时使用的 Codex 系统提示词；
  - 纯文档变更，计划仅运行 git diff --check，不运行重型 QTest。
- 2026-06-23 backend 搭建文档补充：
  - 从 clean tree 开始；
  - 阅读 resource_node.cpp，确认相对 plugin path 按 QCoreApplication::applicationDirPath() 即 ABEL_BIN 所在目录解析；
  - 阅读 backend_plugin_base.h / backend_binder.h，确认外部 plugin CMake 需要 include Abel/src、链接 Abel/build/libabelcore.so，并说明当前 binder 常用类型；
  - 纯文档变更，计划仅运行 git diff --check，不运行重型 QTest。
- 2026-06-23 Abel SDK 范围补充：
  - 复读 backend_interface.h / backend_plugin_base.h / backend_binder.h / value.h / type.h；
  - 确认当前没有 install/export 的 CMake package，外部项目应走 build-tree SDK；
  - 确认 `str -> QString`，`void/bool/int/qint64/double/QString/AbelValue` 可作为直接返回，`std::vector<int>&` 是当前 binder 明确写回路径；
  - 纯文档变更，计划仅运行 git diff --check，不运行重型 QTest。
- 2026-06-23 v1 complete 草案修正：
  - 将 vector/builtin method prvalue receiver 规则写入语言设计与不得回退决定；
  - 将 v1 complete 范围从“小修 v0/SDK 闭环”修正为“全量语言 + 成熟标准库/后端系统 + 包管理引擎”；
  - 明确包管理引擎负责 dependency graph、lockfile、backend artifact cache、ResourceNode 内部生成与自动加载；
  - 纯文档变更，计划仅运行 git diff --check，不运行重型 QTest。
- 2026-06-23 v1 诊断与语义一致性草案修正：
  - 将 check/run 类型语义一致、临时 receiver、vector<struct>.resize、numeric cast/conversion 写入 v1 必须项；
  - 将诊断去污染、runtime stack trace、breakpoint/source location、`__FILE__` / `__LINE__` / `__COLUMN__` 或等价内建写入 v1 必须项；
  - 纯文档变更，已运行 git diff --check，通过；未运行重型 QTest。
- 2026-06-23 v1 语义一致性第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 typechecker/interpreter 测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "typechecker|interpreter" --output-on-failure -j1'
  - 全套 QTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 6/6 tests passed。
- 2026-06-23 v1 runtime stack/source location 第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 interpreter stack/source-location 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "interpreter" --output-on-failure -j1'
  - 全套 QTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 6/6 tests passed。
  - diff whitespace 检查通过：
    git diff --check
- 2026-06-23 v1 vector<struct> 默认构造语义落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 typechecker/interpreter/builtin 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "typechecker|interpreter|builtin" --output-on-failure -j1'
    输出 3/3 tests passed。
  - 全套 QTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 6/6 tests passed。
  - diff whitespace 检查通过：
    git diff --check
- 2026-06-23 v1 调用点 source location 内建落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 typechecker/interpreter 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "typechecker|interpreter" --output-on-failure -j1'
    输出 2/2 tests passed。
  - 全套 QTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 6/6 tests passed。
- 2026-06-23 v1 TypeChecker 诊断去污染第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 typechecker 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "typechecker" --output-on-failure -j1'
    输出 1/1 tests passed。
  - 全套 QTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 6/6 tests passed。
  - diff whitespace 检查通过：
    git diff --check
- 2026-06-23 v1 包管理/项目入口第一片落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
    输出 ninja: no work to do。
  - 项目入口 CLI smoke 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; build/abel package check examples/project && build/abel check examples/project && build/abel run examples/project; printf "project_exit=%s\n" "$?" && build/abel package check examples/project_backend && build/abel run examples/project_backend; printf "backend_project_exit=%s\n" "$?"'
    输出 ok、ok、project_exit=0、ok、backend_project_exit=4。
  - 全套 QTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 7/7 tests passed。
- 2026-06-23 v1 `abel init` 第一片落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 package/init 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "package" --output-on-failure -j1'
    输出 1/1 tests passed。
  - CLI init smoke 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; build/abel init build/abel_init_smoke/project && build/abel package check build/abel_init_smoke/project && build/abel check build/abel_init_smoke/project && build/abel run build/abel_init_smoke/project; printf "init_project_exit=%s\n" "$?"'
    输出 created、ok、ok、hello from Abel、init_project_exit=0。
  - 全套 QTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 7/7 tests passed。
- 2026-06-23 v1 package resolver/lockfile 第一片落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 package 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "package" --output-on-failure -j1'
    输出 1/1 tests passed。
  - 全套 QTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 7/7 tests passed。
- 2026-06-23 v1 package add/remove 第一片落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 package 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "package" --output-on-failure -j1'
    输出 1/1 tests passed。
  - CLI add/remove smoke 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; build/abel init build/abel_add_remove_smoke2/dep && build/abel init build/abel_add_remove_smoke2/app && build/abel add path build/abel_add_remove_smoke2/dep build/abel_add_remove_smoke2/app && build/abel package check build/abel_add_remove_smoke2/app && build/abel remove dep build/abel_add_remove_smoke2/app && build/abel package check build/abel_add_remove_smoke2/app; printf "add_remove_exit=%s\n" "$?"'
    输出 add_remove_exit=0。
  - 全套 QTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 7/7 tests passed。
- 2026-06-23 v1 package build 第一片落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - CLI build smoke 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; d=build/abel_build_smoke; test ! -e "$d" && build/abel init "$d/app" && build/abel build "$d/app" && build/abel check "$d/app" && build/abel run "$d/app"; printf "build_smoke_exit=%s\n" "$?"'
    输出 build_smoke_exit=0。
  - 全套 QTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 7/7 tests passed。
- 2026-06-23 v1 lockfile consumption 第一片落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 package 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "package" --output-on-failure -j1'
    输出 1/1 tests passed。
  - CLI lockfile consumption smoke 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; build/abel add path build/abel_lock_consumption_smoke/dep build/abel_lock_consumption_smoke/app && build/abel build build/abel_lock_consumption_smoke/app && build/abel run build/abel_lock_consumption_smoke/app; printf "lock_consumption_exit=%s\n" "$?"'
    输出 lock_consumption_exit=4。
  - 全套 QTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 7/7 tests passed。
- 2026-06-23 v1 backend artifact cache 第一片落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 package 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "package" --output-on-failure -j1'
    输出 1/1 tests passed。
  - 全套 QTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 7/7 tests passed。
  - CLI backend cache smoke 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; build/abel add path build/abel_cache_smoke_v1_backend_cache/dep build/abel_cache_smoke_v1_backend_cache/app && build/abel build build/abel_cache_smoke_v1_backend_cache/app && test -e build/abel_cache_smoke_v1_backend_cache/app/.abel/cache/backend/dep/MathSystem/libmath_backend.so && build/abel run build/abel_cache_smoke_v1_backend_cache/app; printf "cache_exit=%s\n" "$?"'
    输出 cache_exit=4。
- 2026-06-23 v1 安装版 SDK 第一片落地：
  - 重新配置与构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64 -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_CXX_STANDARD=23
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 SDK install/export/backend fixture 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "sdk" --output-on-failure -j1'
    输出 4/4 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 11/11 tests passed。
- 2026-06-23 v1 backend binder 类型矩阵第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 backend/sdk 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "backend|sdk" --output-on-failure -j1'
    输出 5/5 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 11/11 tests passed。
- 2026-06-23 v1 backend artifact 自动构建第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
    输出 ninja: no work to do。
  - 定向 package 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "package" --output-on-failure -j1'
    输出 1/1 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 11/11 tests passed。
  - diff whitespace 检查通过：
    git diff --check
- 2026-06-23 v1 backend artifact cache metadata 第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 11/11 tests passed。
  - diff whitespace 检查通过：
    git diff --check
- 2026-06-23 v1 backend variadic binder 第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 backend/sdk 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "backend|sdk" --output-on-failure -j1'
    输出 5/5 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 11/11 tests passed。
- 2026-06-23 v1 ResourceNode Qt version / Qt kit load gate 第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
    输出 ninja: no work to do。
  - 定向 backend/sdk 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "backend|sdk" --output-on-failure -j1'
    输出 5/5 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 11/11 tests passed。
- 2026-06-23 v1 package SemVer requirement 第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 package 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "package" --output-on-failure -j1'
    输出 1/1 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 11/11 tests passed。
  - diff whitespace 检查通过：
    git diff --check
- 2026-06-23 v1 runtime diagnostic source excerpt 第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 interpreter 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "interpreter" --output-on-failure -j1'
    输出 1/1 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 11/11 tests passed。
  - diff whitespace 检查通过：
    git diff --check
- 2026-06-23 v1 std.debug builtin 第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 builtin/typechecker/interpreter 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "builtin|typechecker|interpreter" --output-on-failure -j1'
    输出 3/3 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 11/11 tests passed。
  - diff whitespace 检查通过：
    git diff --check
- 2026-06-23 v1 package local registry/cache 第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 package 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "package" --output-on-failure -j1'
    输出 1/1 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 11/11 tests passed。
  - diff whitespace 检查通过：
    git diff --check
- 2026-06-23 v1 definite-return 诊断第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 定向 typechecker 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build -R "typechecker" --output-on-failure -j1'
    输出 1/1 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 11/11 tests passed。
  - diff whitespace 检查通过：
    git diff --check
- 2026-06-23 v1 runtime conversion source span 第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
    输出 ninja: no work to do。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 11/11 tests passed。
  - diff whitespace 检查通过：
    git diff --check
- 2026-06-23 v1 package multi-file/module 第一块落地：
  - 重新配置通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64 -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_CXX_STANDARD=23
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 12/12 tests passed。
- 2026-06-23 v1 dependency source consumption 第一块落地：
  - 重新配置通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64 -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_CXX_STANDARD=23
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 13/13 tests passed。
- 2026-06-24 v1 固定宽度整数类型第一块落地：
  - 构建通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build'
  - 定向 typechecker/interpreter/backend/SDK 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R "typechecker|interpreter|backend" -j1'
    输出 7/7 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 13/13 tests passed。
  - diff whitespace 检查通过：
    git diff --check
  - diff whitespace 检查通过：
    git diff --check
- 2026-06-24 v1 enum/type alias 第一块落地：
  - 构建通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build'
  - 定向 parser/typechecker/interpreter 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R "parser|typechecker|interpreter" -j1'
    输出 3/3 tests passed。
  - 全套 QTest/CTest 与 whitespace 检查在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
    输出 13/13 tests passed，git diff --check 无输出。
- 2026-06-24 v1 struct public/private 第一块落地：
  - 构建通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build'
  - 定向 parser/typechecker/interpreter 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R "parser|typechecker|interpreter" -j1'
    输出 3/3 tests passed。
  - 全套 QTest/CTest 与 whitespace 检查在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
    输出 13/13 tests passed，git diff --check 无输出。
- 2026-06-23 v1 dependency export enforcement 第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 13/13 tests passed。
- 2026-06-23 v1 package resolver conflict diagnostic 第一块落地：
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 13/13 tests passed。
- 2026-06-23 v1 package-aware function resolution 第一块落地：
  - 重新配置通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64 -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_CXX_STANDARD=23
  - 构建通过：
    /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 13/13 tests passed。
- 2026-06-23 v1 package-aware struct/backend resolution 第一块落地：
  - 重新配置、构建、定向依赖源码 CLI 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64 -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_CXX_STANDARD=23 && /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R cli_dependency_sources -j1'
    输出 1/1 tests passed。
  - 构建与全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 13/13 tests passed。
- 2026-06-23 v1 module/use 可见性第一块落地：
  - 构建通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build'
  - 定向 module/use 与 dependency source CLI 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R "cli_multifile_project|cli_dependency_sources" -j1'
    输出 2/2 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 13/13 tests passed。
- 2026-06-23 v1 module-qualified function call 第一块落地：
  - 构建与定向多文件 module CLI 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R cli_multifile_project -j1'
    输出 1/1 tests passed。
  - 全套 QTest/CTest 与 whitespace 检查在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
    输出 13/13 tests passed，git diff --check 无输出。
- 2026-06-23 v1 module-qualified backend call 回归锁定：
  - 构建与定向多文件/backend CLI 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R cli_multifile_project -j1'
    输出 1/1 tests passed。
  - 全套 QTest/CTest 与 whitespace 检查在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
    输出 13/13 tests passed，git diff --check 无输出。
- 2026-06-23 v1 module-qualified struct/type 第一块落地：
  - 构建与定向多文件 module CLI 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R cli_multifile_project -j1'
    输出 1/1 tests passed。
  - 构建、全套 QTest/CTest 与 whitespace 检查在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
    输出 13/13 tests passed，git diff --check 无输出。
- 2026-06-23 v1 import ambiguity diagnostic 第一块落地：
  - 构建与定向多文件 module CLI 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R cli_multifile_project -j1'
    输出 1/1 tests passed。
  - 全套 QTest/CTest 与 whitespace 检查在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
    输出 13/13 tests passed，git diff --check 无输出。
- 2026-06-23 v1 import alias 第一块落地：
  - 构建与定向多文件 module CLI 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R cli_multifile_project -j1'
    输出 1/1 tests passed。
  - 构建、全套 QTest/CTest 与 whitespace 检查在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
    输出 13/13 tests passed，git diff --check 无输出。
- 2026-06-23 v1 `abel test` 第一块落地：
  - 构建与定向多文件 CLI 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R cli_multifile_project -j1'
    输出 1/1 tests passed。
  - 全套 QTest/CTest 与 whitespace 检查在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
    输出 13/13 tests passed，git diff --check 无输出。
- 2026-06-23 v1 `std.test` 断言 builtin 第一块落地：
  - 构建与定向 builtin/typechecker/interpreter/CLI 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R "builtin|typechecker|interpreter|cli_multifile_project" -j1'
    输出 4/4 tests passed。
- 2026-06-23 v1 显式 re-export 第一块落地：
  - 构建与定向 parser / dependency source CLI 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R "parser|cli_dependency_sources" -j1'
    输出 2/2 tests passed。
  - 构建、全套 QTest/CTest 与 whitespace 检查在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
    输出 13/13 tests passed，git diff --check 无输出。
- 2026-06-23 v1 const reference 第一块落地：
  - 构建、全套 QTest/CTest 与 whitespace 检查在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
    输出 13/13 tests passed，git diff --check 无输出。
- 2026-06-24 v1 readonly location 传播第一块落地：
  - 构建通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build'
  - 定向 typechecker/interpreter 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R "typechecker|interpreter" -j1'
    输出 2/2 tests passed。
  - 全套 QTest/CTest 在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
    输出 13/13 tests passed。
- 2026-06-24 v1 std.vec builtin methods 第一块落地：
  - 构建通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build'
  - 定向 builtin/typechecker/interpreter 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R "builtin|typechecker|interpreter" -j1'
    输出 3/3 tests passed。
  - 构建、全套 QTest/CTest 与 whitespace 检查在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
    输出 13/13 tests passed，git diff --check 无输出。
- 2026-06-24 v1 std.io scan 第一块落地：
  - 构建通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build'
  - 定向 builtin/typechecker/cli_scan 回归测试在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -R "builtin|typechecker|cli_scan" -j1'
    输出 3/3 tests passed。
  - 构建、全套 QTest/CTest 与 whitespace 检查在 4GB 虚拟内存上限下通过：
    /bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
    输出 14/14 tests passed，git diff --check 无输出。
```

### 未完成

```text
无 v0 阻塞项；v1 complete 仍在推进中。
```

### 下一步

```text
v1 后续：
1. 优先按大块推进 v1 complete，不做细枝末节验证拖延：全量语言语义、成熟标准库、成熟 backend/SDK、包管理引擎。
2. 包管理下一批大块：在本地 registry/cache 与同名包冲突诊断第一片之上推进更完整 solver 策略、远程 registry/download cache、backend build spec 成熟化、安装版 SDK 成熟化与完整 ABI/semver 级缓存失效，而不是停留在 path dependency、local registry、SemVer requirement、lockfile consumption、缓存复制、sidecar 校验与 CMake 自动构建第一片。
3. Backend/SDK 下一批大块：明确 pointer/reference 暴露矩阵，继续推进完整 ABI/semver/platform 兼容校验与诊断（Qt version / kit 字符串门禁已完成第一片），继续扩展 backend binder 到更完整 Abel 类型面。
4. 语言语义下一批：在 package 多文件/依赖库源码合并、跨包 export enforcement、package/module-aware function/struct/backend resolution、module/use 可见性、显式 re-export、限定函数/backend/struct/type、import alias 与导入冲突诊断第一片之上推进多文件 source map API、更细 per-span label、更多 const/引用/生命周期边界、泛型/模板/接口实际语义，以及 parser/resolver 级诊断恢复。
5. 每个后续大块仍需保持 Git 审计、4GB 测试限制、AGENTS.md 强制区更新。
6. 标准库/backend/SDK/包管理引擎应作为下一批大块推进，不要继续只做零碎局部修补。
```

### 风险与未决

```text
1. signed overflow 语义按 C/C++ 风险模型，不做额外保护；解释器实现细节使用宿主整数，后续若需要可单独收紧。
2. v0 后续延期项已在「v0 complete 边界审计」列明；最终验收不得把延期项重新扩张为阻塞项，除非用户明确改目标。
3. public GitHub 仓库即使使用 proprietary/all-rights-reserved LICENSE，GitHub 用户仍可能按 GitHub 服务条款进行查看/分叉；若需要更强访问控制，应改 private。
4. v1 语义一致性必须由 typechecker/interpreter 共享规则解决；不能通过 backend 特判掩盖。
5. v1 source location 已开始贯穿 AST、runtime frame、backend call context 与常见 runtime conversion 错误，并已能在 CLI 输出单行源码 excerpt/caret；后续仍需覆盖多文件/module source map、更完整 span 合成、breakpoint/source map API 与更完整诊断恢复。
6. 零参 `init()` 被视为默认构造入口；字段初始 raw default 后再执行 constructor body。若未来开放函数字段/更复杂不可默认字段，需要补更细的字段初始化证明与诊断。
7. 当前 `__FILE__` / `__LINE__` / `__COLUMN__` 与 CLI sourceLine excerpt 已能在 package 多文件和依赖库源码合并时保留各文件 AST SourceSpan；跨包顶层 `export` enforcement、package/module-aware function/struct/backend resolution、module/use 可见性、显式 re-export、module-qualified function/backend/struct/type、import alias 与导入冲突诊断已完成第一片，但更细 related span label 与更完整模块系统仍未完成。依赖包 entry 默认排除以避免 `main` 冲突。合成 AST 节点的 sourceLine 当前沿用首 token 行，多行 span 的完整 excerpt 仍是后续工作。
8. 当前诊断去污染已覆盖 TypeChecker 表达式/语句级 unknown 传播与 definite-return 第一片；parser/resolver、多文件符号恢复、更精细控制流返回分析仍需后续大块继续做。
9. 当前 package/init/update/add/remove/build/graph/cache/install-sdk 只是 v1 包管理与 backend SDK 入口前几片：resolver 支持本地 path dependency、本地 registry dependency、SemVer requirement 第一片与同名包多解析冲突诊断第一片，registry cache 只是把本地目录包复制到 `.abel/cache/packages`，backend artifact 自动构建只支持 CMake 第一片，安装版 SDK 只是第一片；backend cache sidecar 只覆盖源 artifact 路径/size/mtime 与 ResourceNode 字段匹配，还没有远程 registry、完整 semver solver、网络 download cache、完整 SDK 成熟化、ABI hash/semver 级校验或完整版本化缓存失效。
10. 当前 backend binder 已覆盖常用 scalar/vector/诊断通道与 `any...` 变长后端第一片，但仍不承诺任意 struct/class 自动拆装箱、任意 pointer/reference 矩阵、任意 T& 写回或跨 Qt/编译器 ABI 稳定；`AbelRuntimeContext&` 仅支持作为 C++ lambda 最后一个参数。
11. 当前 ResourceNode Qt version / kit gate 只比较 ResourceNode 声明字符串与 Abel runtime 当前字符串，能提前挡住明显错 kit/错 Qt 资源，但不等价于完整 ABI hash、编译器 ABI、平台 ABI、semver 或二进制内容校验。
12. 当前 `abel test` 是项目级测试入口第一片：测试发现固定为根项目 `tests/**/*.abel`，每个测试文件独立作为 entry/main 运行；还没有 test filter、并行调度、expect-fail、测试 fixture 生命周期、覆盖率或更成熟 report 格式。
13. 当前 `std.test` 只是断言 builtin 第一片：`test_eq/test_ne` 使用 Abel 运行时值相等与 stringify，不承诺浮点 eps、深度自定义比较、近似比较、集合 diff、fixture API 或成熟测试报告。
14. 当前 `export use` 只是 re-export 第一片：传播的是真实 module imports，不传播 alias；没有 per-symbol re-export、hide/rename、cycle diagnostic、完整 public/private module surface 或跨包发布索引。
15. 当前 const/reference 仍只是前两片：`const T&` 仍要求 lvalue，不做 prvalue lifetime extension；readonly location 已覆盖 const vector/struct 字段、index、front/back、解引用、range-for 与 mutating builtin receiver 的第一批非 name lvalue，但 `const T*` / `T* const` 完整矩阵、临时生命周期、更多 pointer/reference 暴露边界仍需后续大块推进。
16. 当前 struct public/private 是成员级第一片：已覆盖字段、构造、方法与默认构造访问控制，但还没有 per-symbol module private/public、friend、protected、nested type、多个构造 overload、private static 成员或更成熟封装诊断。
17. 当前 std.vec 只是 builtin methods 第一片：`insert/erase/find/sort` 已保持 check/run 一致，但还没有完整 iterator、stable erase 语义文档、用户 comparator、二分查找、去重、map/filter/reduce 或成熟容器标准库分层。
18. 当前 std.io scan 只是 whitespace token 输入第一片：支持 `scan(&x, ...)` 写入基础标量、str、any，但还没有格式化输入、文件流、行输入、错误返回对象、成熟 std.io module 分层或可注入输入源 API。
```

### 最近提交

```text
ca49a01 docs: replace Abel design with agent manual
4ff4184 docs: record manual replacement progress
18d97c6 build: add Qt C++23 project skeleton
1073d7d parser: add Abel lexer and parser baseline
de2fa03 interpreter: add Stage 3 tree runner
8805f8c runtime: add pointer and reference locations
6e4a3b3 runtime: add vector value semantics
3cde439 builtins: add registry for vector methods
be0424a builtins: add any variadic string functions
36cbb49 typechecker: add Stage 8 static checks
5972478 control-flow: add for loop execution
58d89f7 builtins: complete vector methods
dab7040 struct: add fields constructors and methods
dee8b16 lambda: add function values and captures
2cde7e4 backend: typecheck static calls
78de6ff backend: add registry and resource nodes
01cf078 backend: load Qt plugin resources
5aedd02 builtins: add string char conversions
f75f7c8 v0: wire cast pipe and resource run
a5d527e builtins: call user to_str for stringify
df76822 docs: audit v0 completion boundary
6d6e079 docs: mark v0 complete
89d4235 docs: add public repository readme and license
2067259 docs: record GitHub publication
9e2cbc2 docs: add Abel tutorial and Codex guide
7d19226 docs: retarget Codex guide to Abel user projects
6a1f50b docs: document Abel SDK backend workflow
ea291db docs: define v1 complete scope
3b043c7 docs: add v1 diagnostics and semantic consistency scope
fecab8c typechecker: align numeric casts and prvalue vector receivers
d17e1bb runtime: add Abel stack traces
131e01c runtime: default construct vector struct elements
72aa43b runtime: add source location builtins
184520f typechecker: suppress unknown diagnostic cascades
c857692 package: add project entry manifests
6861f1c package: add project initialization
315fa3a package: add local lockfile resolver
0a6841f package: add dependency add remove commands
c6d2ab2 package: add project build command
e9e2f06 package: consume lockfile package graph
49265e5 package: cache backend artifacts
a56254e sdk: install Abel CMake package
20fe36d backend: expand binder type matrix
33f6d7d package: auto-build backend artifacts
d8ec734 package: validate backend cache metadata
470932e backend: add variadic binder
886c9ea backend: gate resources by Qt kit
82cb270 package: add semver requirements
58b076d diagnostics: print source excerpts
2fdd107 builtins: add debug assertions
9f5306d package: add local registry cache
5bb2c81 typechecker: add definite return checks
7a42e65 runtime: improve conversion diagnostic spans
d7ea702 package: add multi-file module entry
a8368a7 package: consume dependency Abel sources
1dfd5be typechecker: enforce dependency exports
5c2e874 package: diagnose dependency resolution conflicts
1c34e40 package: resolve functions by package context
41ab3fd package: resolve structs and backends by package
41ff06b module: enforce use-based lookup
be57e51 module: resolve qualified function calls
a35e4b0 module: test qualified backend calls
1cb1af0 module: resolve qualified struct types
5b6c6ee diagnostics: explain module import ambiguities
a64788f module: add import aliases
97aeb6a cli: add project test command
dcebf20 builtins: add test assertions
7c20a43 module: add re-exported imports
f353399 typechecker: add const reference bindings
b60c196 runtime: propagate readonly locations
4ed6520 types: add fixed-width integers
df499ad language: add enum and type aliases
d1165fb struct: add public private members
6d9aefa builtins: add string methods
4fe36b7 builtins: add vector algorithms

说明：
- 本区记录已经完成且可回滚的实质提交。
- 不要求在同一个提交中记录自身 hash；那会形成自指 hash 循环。
- 后续 Agent 完成实质提交后，应在下一次 AGENTS.md 更新中追加上一实质提交 hash。
```
