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
Stage 4：C/C++ 指针/引用值模型最小闭环已建立，进入 vector<T> / lvalue 扩展闭环。
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
```

### 未完成

```text
1. 尚无独立 typechecker；Stage 3 仅在解释器内做最小运行时类型检查。
2. 尚无 backend plugin 示例。
3. parser 仍是最小闭环，尚未覆盖 struct/lambda/for/range-for。
4. interpreter 尚未实现 vector/struct/lambda/backend/any/BuiltinRegistry。
5. const 指针、const 引用的完整静态语义尚未实现；当前只保留基础 const 变量写保护。
```

### 下一步

```text
Stage 5：
1. 实现 vector<T> 值类型运行时表示，先覆盖 vector<int>。
2. 实现 initializer list、index lvalue、push/len/back/front 的最小方法分发。
3. 实现 vector<T>& 参数与 index 返回 lvalue，验证 vector 元素写回。
4. 增加 vector interpreter tests 与 smoke example。
5. 继续为 Stage 3/4/5 已支持语义拆出独立 typechecker。
6. build + ctest + commit。
```

### 风险与未决

```text
1. repeat(n) 对负数的语义尚需实现时最终确认；当前推荐执行 0 次。
2. const T& 是否绑定 prvalue 暂未完整实现；当前推荐 v0 禁止，降低解释器复杂度。
3. signed overflow 语义按 C/C++ 风险模型，不做额外保护；解释器实现细节需避免无意引入宿主 UB 污染调试，可先使用宿主类型直接执行并记录风险。
4. operator ** %% <? >? |> 的精确优先级需测试固定。
5. 用户自定义 operator 的 v0 范围可继续收缩，不能阻塞主线。
```

### 最近提交

```text
ca49a01 docs: replace Abel design with agent manual
4ff4184 docs: record manual replacement progress
18d97c6 build: add Qt C++23 project skeleton
1073d7d parser: add Abel lexer and parser baseline
de2fa03 interpreter: add Stage 3 tree runner
待本次 Stage 4 pointer/reference 提交后追加 commit hash。

说明：
- 本区记录已经完成且可回滚的实质提交。
- 不要求在同一个提交中记录自身 hash；那会形成自指 hash 循环。
- 后续 Agent 完成实质提交后，应在下一次 AGENTS.md 更新中追加上一实质提交 hash。
```
