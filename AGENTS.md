# Abel Agent Manual

状态：当前 Abel v1 推进期的仓库操作手册。
作用：任何 Agent 进入本仓库，先读本文件，再读代码。
原则：删掉远古流水账，只保留当前有效决策、开发纪律、验证方式、语言/后端/包管理目标和近期风险。

---

## 0. 进入仓库第一纪律

进入仓库后，任何写入前必须执行：

```bash
pwd
git status --short
ls
```

若不是 Git 仓库，停止并询问是否初始化。当前仓库应在：

```text
/home/tnuzy/桌面/Lab/Abel
```

### 写入纪律

1. 所有源码、文档、配置创建/修改/删除默认必须通过 `apply_patch`。
2. 禁止 `cat > file`、`echo > file`、`tee`、`sed -i` 等绕过审查的写入方式。
3. 唯一例外：大规模机械性文档替换或重复迁移，且必须先说明目标文件、范围和原因，执行后审查 diff。
4. 每轮从 clean tree 起步；若不干净，先确认改动来源。
5. 每轮形成一个逻辑 commit。
6. commit 前尽可能运行相关 build/check/test。
7. 测试必须限制 4GB 虚拟内存，避免系统死亡。
8. 不伪造测试结果，不声称未运行的命令已运行。
9. 完成实质修改后更新本文件末尾「当前进度」区。

标准验证命令：

```bash
/bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
```

推送命令优先：

```bash
git -c http.version=HTTP/1.1 -c http.lowSpeedLimit=1 -c http.lowSpeedTime=30 push origin master
```

---

## 1. 当前目标

当前大目标：

```text
v1 complete：把 Abel 做成“本地可用、语义闭合、诊断可靠、工程闭环”的第一版语言/SDK/包管理系统。
```

### 1.1 tight v1 complete 定义

v1 complete 不等于“实现所有想象中的语言功能”，也不等于“当前测试过了”。
v1 complete 的最小完备闭环是：

```text
1. 所有“v1 承诺语法”都有 TypeChecker + Interpreter + CLI 行为；没有 parser-only 幻影功能。
2. 静态层 v1 类型/值类别/调用规则 check/run 一致；不允许非动态边界的静态错误 check 过而 run 才报类型错误。
3. 本地工程从 0 到运行、测试、依赖、backend artifact、SDK plugin 都有 CLI 闭环。
4. 标准库覆盖常用本地程序能力：字符串、vector、数学、文件/路径/环境、debug、test、char、any。
5. backend 支持稳定的 v1 ABI 窗口：常用 scalar/string/vector/any/reference/diagnostic/variadic；复杂对象走 AbelValue 边界。
6. 包管理是“本地完整引擎”：path dependency、本地/file registry、SemVer range、lockfile、cache、冲突诊断、backend artifact 自动构建/缓存。
7. 诊断能定位：结构化 diagnostic、源码位置、excerpt/caret、Abel 调用栈、backend/resource/package 错误路径。
8. 文档与实际行为一致，用户不需要读历史日志才能搭工程。
9. dynamic boundary 例外明确：any/cast/dynamic operator/backend dynamic object 的运行期类型失败是合法 runtime diagnostic，不算 check/run 分裂 bug。
```

### 1.2 v1 必须收束的语言面

v1 语言 complete 只承诺下面这组“可实现且高杠杆”的语言面：

```text
lex/parse/source span/diagnostic recovery
module/use/export/export use/import alias/qualified lookup
void/bool/fixed-width ints/f64/char/str/any/vector<T>
struct/field/init/method/const method/public/private
ordinary function/lambda/func type
if/elseif/else/while/for/repeat/range-for/break/continue/return
lvalue/prvalue/value copy/reference/pointer/address-of/deref
const object + const reference + readonly location propagation
function overload / struct constructor overload / method overload / binary operator overload
用户 template syntax 已从 v1/v1.1-H 语法与语义路径移除
any/cast dynamic boundary
concrete operator overload + any dynamic operator dispatch
any... variadic + BuiltinRegistry function/method/operator registration
backend block declaration and static backend call
debt declaration as explicit runtime error boundary
```

v1 必须做到：静态语义要么完整可用，要么在 check 阶段给出明确诊断；不能留下“能解析但运行才未知”的灰区。显式 dynamic boundary 除外：any/cast/dynamic operator/backend dynamic object 允许运行期诊断，但诊断必须稳定、可定位、可测试。

### 1.3 v1 明确不做，避免无底洞

下面内容不阻塞 v1 complete，默认进入 v2+ 或研究项：

```text
JIT / split / bytecode VM / IDE
HTTP/network registry、远程账号、签名发布、全球包索引
完整 SAT/MaxSAT 级依赖求解器
跨 Qt/跨编译器稳定二进制 ABI
完整二进制内容 ABI hash 与供应链安全系统
完整 C++ 模板元编程、SFINAE、concepts 复刻
所有用户 template 路线：template function/struct/type alias/operator、template+interface/require、variadic template、tuple<T...>
完整 C++ overload ranking、ADL、默认参数、返回类型 overload
operator() / operator<> 用户重载
指针算术、void*、reinterpret_cast、manual delete 所有权模型
Rust 式 borrow checker / GC / 生命周期证明
完整 prvalue lifetime extension
friend/protected/nested type/完整面向对象系统
regex/locale/streaming IO/复杂 collection views/GUI 标准库
交互式 debugger 协议、DAP、断点 UI
热重载 plugin / 跨 ABI plugin 市场
```

template 路线从 v1.1-H 起整体退休并清除。历史实现过的最简无约束 template 不再作为承诺能力扩展；代码库不再保留 Abel 层 template/interface/require token、AST、Parser 恢复分支、TypeChecker 推导/实例化路径、Interpreter 运行路径或成功/retired 专项测试。`template` / `interface` / `require` 不再是语言关键字；误用时按普通标识符语法自然失败，而不是维护一条专门 retired 入口。

如果某项“不做”已经有 parser 入口，v1 complete 前必须把它变成：

```text
1. 明确文档标注为 reserved/v2；且
2. TypeChecker 或 Parser 给出稳定的 not implemented/reserved 诊断；且
3. 不影响 v1 静态层承诺语法的 check/run 一致性。
```

### 1.4 v1.1-a/b 已完成边界与 v1.1-H 方针

状态：v1.1-a/b 当前 scope 已完成；下一阶段不是继续扩张静态类型系统，而是进入 **v1.1-H：Dynamic Core Hardening**。

v1.1-a/b 已完成阶段的核心原则：

```text
Abel surface 保持结构化和静态可检查。
复杂度进入 Abel SDK / Backend，而不是进入语言内核。
动态能力只能出现在显式 backend-backed library abstraction 或 any 边界。
TypeChecker 不内建 map/object/dict/symbol/resource/dynamic invoke 规则。
```

v1.1-H 的新核心原则：

```text
Abel 不再追求让 TypeChecker 跟踪一切 Abel 深层结构。
Abel 保留基础静态边界：名字、模块、普通类型、调用形状、ref/ptr/value-category、backend 声明。
复杂数据、异构结构、动态调用、runtime object、tuple/map-like 能力进入 any + cast + operator dispatch + Backend/SDK。
backend 承载更重负担；SDK 必须能观察、复制、诊断、调用和参与动态 operator 协议。
template 全路线 retired/removed：不再 legacy/frozen 地保留为未来选项，也不再维护 retired/reserved 兼容诊断入口。
```

#### v1.1-a：Structured Calls，方针不变

v1.1-a 只做调用层增强：

```text
|> pipe + holes `_`
named args
default args
limited spread into any... tail
```

实现形态必须是前端归一化：

```text
raw call / method call / constructor call / backend call
→ CallArgNode(positional/named/spread)
→ CallNormalizer(named/default/spread/pipe-hole)
→ existing overload/typecheck/interpreter path
```

约束：

```text
pipe/hole 不破坏 lvalue/prvalue/ref 规则。
named/default 不进入 func type ABI。
spread 第一片只允许展开到 any... tail；不展开固定参数，不做 object/dict spread。
不因为 v1.1-a 引入动态调用、动态字段、匿名对象或 runtime-only 类型错误。
```

v1.1-a 示例按当前已落地边界理解；后续只允许做 bugfix、回归测试和文档澄清，不再把 structured calls 扩成动态调用系统。

Named/default args：

```abel
fn int inc(int x, int by = 1) {
    return x + by;
}

struct Point {
    int x;
    int y;

    init(int x, int y = 0) {
        this.x = x;
        this.y = y;
    }
}

backend Fs {
    fn int run(str cmd, str& out, bool check = true);
}

fn int main() {
    int a = inc(1);
    int b = inc(x: 1, by: 2);
    Point p = Point(x: 3, y: 4);
    str out = "";
    return a + b + p.x + Fs::run(cmd: "status", out: out);
}
```

归一化后等价于：

```text
inc(1)                         -> inc(1, 1)
inc(x: 1, by: 2)               -> inc(1, 2)
Point(x: 3, y: 4)              -> Point(3, 4)
Fs::run(cmd: "status", out: out) -> Fs::run("status", out, true)
```

Named/default 约束：

```text
positional 参数必须在 named 参数之前。
named 参数必须匹配声明参数名，且不能重复。
缺失参数必须有 default，否则 check 报错。
default expr 在声明上下文 typecheck；第一片不允许 default expr 依赖 this 或后续参数。
func 类型不携带参数名/default；通过 func 值调用只接受 positional 且不自动补 default。
```

非法示例：

```abel
inc(by: 2, 1);          // positional after named
inc(x: 1, x: 2);        // duplicate named arg
inc(z: 1);              // unknown parameter name

func int(int, int) f = inc;
f(x: 1, by: 2);         // func call 不接受 named
f(1);                   // func call 不补 inc 的 default
```

Pipe + holes：

```abel
fn int add(int a, int b) {
    return a + b;
}

fn void bump(int& x) {
    x = x + 1;
}

fn int main() {
    int x = 3;
    int y = x |> add(_, 4);      // add(x, 4)
    int z = y |> add(10, _);     // add(10, y)
    int twice = x |> add(_, _);  // add(x, x)，lhs 仍只求值一次
    x |> bump(_);                // `_` 保留 x 的 lvalue，可绑定 int&
    str s = " abel " |> _.trim().upper();
    return x + y + z + twice + s.len();
}
```

归一化原则：

```text
lhs |> f                 -> f(lhs)              # 仅当 f 是 callable value/name
lhs |> f(_)              -> f(lhs)
lhs |> f(1, _)           -> f(1, lhs)
lhs |> f(_, _)           -> f(lhs, lhs)         # lhs 只求值一次，两个 hole 读同一个 pipe 临时
lhs |> _.method(a)       -> lhs.method(a)
lhs |> _.field.method()  -> lhs.field.method()
```

Pipe/hole 约束：

```text
`_` 只在 pipe RHS 内有效。
lhs 只求值一次。
所有 holes 指向同一个隐藏 pipe 临时，不是重复求值 lhs。
每个 hole 继承 lhs 的 value category；prvalue hole 不能绑定 mutable T&。
多 holes 支持读用法：by-value 参数、const ref 参数、const receiver、普通字段/方法读取。
如果任一 hole 会绑定 mutable ref、作为 mutable receiver、或触发写入语义，则该 pipe RHS 只能有一个 hole。
无 hole 的 pipe 只允许 RHS 是 callable；不把任意表达式变成动态调用。
```

非法示例：

```abel
int a = _;              // `_` 不在 pipe RHS
1 |> bump(_);           // prvalue 不能绑定 int&
x |> swap(_, _);        // 假设 swap(int&, int&)，多 mutable ref hole 必须拒绝
x |> f(_, mut_ref: _);  // 只要某个 hole 是 mutable/ref 写入语义，多 hole 必须拒绝
```

Limited spread into `any...`：

```abel
fn str join(any... xs) {
    return build_string(...xs);
}

fn int main() {
    vector<any> xs = {1, "x", true};
    println("prefix=", ...xs);
    str s = join(...xs);
    return s.len();
}
```

Spread 约束：

```text
spread expr 必须是 vector<any> 或 any... 参数本身。
spread 只能进入 any... tail。
spread 不参与固定参数匹配，不展开 vector<int> 到 int,int。
不支持 named spread、object/dict spread、map/object literal spread。
```

非法示例：

```abel
fn int add(int a, int b) { return a + b; }

vector<int> xs = {1, 2};
add(...xs);             // 不展开到固定参数

vector<any> ys = {1, 2};
add(...ys);             // 即使 vector<any> 也不填固定参数

println(name: ...ys);   // 不做 named spread
```

完整组合示例：

```abel
fn int scale(int x, int by = 2) {
    return x * by;
}

fn str report(any... xs) {
    return build_string(...xs);
}

fn int main() {
    vector<any> tail = {" units", true};
    int value = 3 |> scale(x: _, by: 4);
    str text = report("value=", value, ...tail);
    return text.len();
}
```

这组示例的设计目标是证明：v1.1-a 只是调用层结构化糖，所有东西都能在 check 阶段归一化到现有 callable/overload/backend call 规则，不引入新的动态对象模型。

#### v1.1-b：强化 Abel SDK / Backend 承载复杂度

v1.1-b 不向 Abel 内核加入 `map`、`dict`、`object`、`symbol`、`resource literal` 或 `dynamic backend invoke`。

明确禁止把下面内容做成核心语言能力：

```text
TypeKind::Map / TypeKind::Dict / TypeKind::Object
map/object/dict literal
symbol literal
resource literal
dynamic backend_invoke(...)
动态字段访问 m.name / obj.field
内核级 JSON/object schema 推导
```

v1.1-b 阶段的正确路线：

```text
Backend 提供复杂能力原语。
Abel surface 用普通 module / struct / methods / any / cast / backend fn 包装能力。
内核只理解现有机制：struct、method、any、backend fn、cast<T>(any)。
```

v1.1-H 之后，新复杂数据使用 `any + cast + operator dispatch + backend-backed dynamic ADT`，不再把 template 作为未来复杂结构路线。

典型模式是 backend-backed dynamic ADT：

```abel
module std.map;

backend __MapRuntime {
    fn long create();
    fn int len(long h);
    fn bool contains(long h, str key);
    fn void set(long h, str key, any value);
    fn any get(long h, str key);
}

export struct StrMap {
private:
    long h;

public:
    init() {
        h = __MapRuntime::create();
    }

    fn int len() const {
        return __MapRuntime::len(h);
    }

    fn bool contains(str key) const {
        return __MapRuntime::contains(h, key);
    }

    fn void set(str key, any value) {
        __MapRuntime::set(h, key, value);
    }

    fn any get(str key) const {
        return __MapRuntime::get(h, key);
    }
}
```

这里 `StrMap` 是库层普通 struct，不是内建类型。Abel 内核不知道 map，只知道一个带 backend handle 的普通 struct。静态 value 类型不由 checker 追踪，调用方通过 `any` 与 `cast<T>` 显式取回。

SDK / Backend 需要强化的不是“返回 map 内建值”，而是：

```text
AbelValue 稳定存储、深拷贝、hash/equality helper。
backend-owned handle table / opaque object registry。
AbelValue key 支持明确诊断：unsupported key type、missing key、bad cast。
AbelValue / AbelVariadicArgs / vector<any> 之间的安全桥接。
backend-backed ADT 的 C++ helper：builder/view/handle store/diagnostic helpers。
跨 backend function 的静态签名、source span、runtime stack 与 out/ref 写回继续闭环。
```

例如 C++ backend 可以承载任意复杂 map 存储，但 Abel API 仍是普通函数：

```cpp
bind("std.map.__MapRuntime.create", [&store]() {
    return store.create();
});

bind("std.map.__MapRuntime.set",
     [&store](qint64 h, QString key, abel::AbelValue value) {
         store.set(h, key, value);
     });

bind("std.map.__MapRuntime.get",
     [&store](qint64 h, QString key) {
         return store.get(h, key);
     });
```

设计边界：

```text
Map 这类能力默认是 backend handle object；普通赋值复制 handle，不承诺值语义。
如需值语义，由 Abel surface 显式提供 clone()，backend 实现深拷贝。
生命周期第一片可由 backend store 持有到进程结束；析构/RAII/finalizer 不阻塞 v1.1-b。
不要为了库容器污染 parser、TypeChecker、TypeKind 或 core value model。
```

#### v1.1-H：Dynamic Core Hardening，严格边界

v1.1-H 是 v1.2 之前的硬化阶段。它的目标不是增加漂亮语法，而是把 Abel 的 any/cast/operator 动态核心、runtime 诊断和 backend/SDK 承载能力拧紧。

口号：

```text
Static where cheap.
Dynamic where expressive.
Backend where complex.
Diagnostics everywhere.
```

TypeChecker 继续负责：

```text
1. 名字是否存在，module/use/export 是否满足。
2. 普通静态类型、函数/方法/构造/backend call 形状是否明显不匹配。
3. lvalue/prvalue、T&、const T&、T*、const T* 的最低绑定规则。
4. backend block 声明与已知 ABI/binder 能力是否能连接。
5. dynamic boundary 是否显式出现：any、cast、backend dynamic API、未来 dynamic literal。
```

TypeChecker 不再负责：

```text
1. 证明 any 内部真实类型。
2. 追踪 tuple/map/object 的字段级 schema。
3. 推导 object shape 或 map value 精确联合类型。
4. 证明 backend-owned object 的内部不变量。
5. 把所有 Abel 运行期结构静态化。
```

##### H1：template 全路线退休

目标不是冻结 template，而是把 template 从 Abel 主路线中移除。历史实现过的 template 能力视为待迁移遗留项，不再作为语言承诺。

```text
template <type T> fn ...
template <type T> struct ...
template <type T> type ...
template operator ...
template+interface / require
variadic template
tuple<T...>
任何为了 map/tuple/object schema 扩张泛型系统的路线
```

处理策略：

```text
1. 文档删除 template 作为推荐能力的叙述。
2. 示例改写为 any + cast + backend-backed dynamic ADT。
3. 删除 template 成功测试和 retired/reserved 专项测试，不保留 legacy coverage。
4. Parser / TypeChecker / Interpreter 不保留 template 专用入口；former template syntax 只会按普通语法错误或普通类型/调用错误自然失败。
5. 不为兼容 template 继续修复杂重载、跨模块实例化、template operator、template alias 等边缘问题。
```

保留的尖括号语法只限 core type syntax，不属于用户 template：

```text
vector<T>
func R(A, B)
cast<T>(x)
未来如需内部 runtime type descriptor，可用 TypeDesc，不开放用户 template。
```

##### H2：any 升级为动态值核心

`any` 不再只是逃生口；它是 Abel 的动态值核心。Abel 选择放大表达能力、缩小静态安全：checker 不再证明动态值内部结构，但 runtime 必须给稳定诊断。

`any` 必须能承载：

```text
void/null-like sentinel。
bool / int / i64 / f64 / char / str。
vector<T> 与 vector<any>。
struct value。
pointer value。
func/callable value。
backend-owned dynamic object handle。
未来 tuple/map dynamic object。
```

装箱规则：

```text
T -> any：总是允许，复制、包装或生成 backend handle view。
any -> T：允许进入 runtime cast，不由 checker 证明成功。
any -> any：固定解包/重包规则，避免 any(any(x)) 意外污染。
vector<any> / AbelVariadicArgs / std::vector<AbelValue> 桥接规则必须唯一。
```

需要强化：

```text
any_type / any_is / any_is_bool/int/i64/f64/char/str/vector/func/pointer/struct。
any_debug / AbelValue debug render。
any equality/hash：支持类型稳定结果，unsupported type 明确错误。
deep copy：支持值类型、vector、struct、callable handle、backend dynamic handle 的明确策略。
dynamic object kind：为未来 tuple/map 与 backend-owned object 保留 runtime tag，不进 TypeKind。
```

不承诺：

```text
静态 schema 推导。
编译期证明 cast 成功。
编译期证明 backend-owned object 内部结构。
动态字段访问 m.name / obj.field。
隐式把 any 当成结构化 object。
```

##### H3：cast 升级为动态世界回到静态世界的标准门

`cast<T>(x)` 是 dynamic boundary 的显式出口；此外，部分静态位置允许 checker 插入 runtime dynamic cast。

目标语义：

```text
如果 x 是 any，检查内部值。
如果 x 已经是 T，可直接返回/复制。
如果存在 Abel 明确定义的安全转换，允许。
否则产生 runtime diagnostic。
```

允许的隐式 dynamic cast 插入点：

```text
变量初始化：T x = any_expr;
赋值：x = any_expr;                    # x 的 declared type 是 T
函数实参：f(any_expr) where param is T
return：fn T f() { return any_expr; }
struct 字段赋值：obj.field = any_expr;
backend 参数绑定：Backend::f(any_expr) where backend param is T
operator concrete overload 实参：operator +(T, U) 接收 any 时 runtime cast。
```

不允许的隐式 dynamic cast：

```text
无目标类型的表达式上下文。
控制流条件之外的任意 bool 猜测。
ref 绑定需要 mutable location 时，不把 prvalue any 静默变成 T&。
pointer 解引用、address-of、lifetime 相关规则不靠 any 绕过。
```

诊断要求：

```text
失败信息包含 expected type 与 actual type。
primary span 指向 cast 表达式或造成转换的实参/return/assignment/backend call。
保留 Abel stack frame；backend 引发的 cast/get/call 错误必须带 backend frame。
cast 失败是合法动态边界错误，不等同于 check/run 分裂 bug。
```

优先覆盖目标：

```text
bool/int/i64/f64/char/str。
vector<T>，尤其 vector<any>。
struct 值。
func value / callable signature。
pointer/reference 边界内允许的形态。
```

转换策略第一片：

```text
数字之间：显式 cast 允许，溢出/NaN/非法时 runtime diagnostic。
char <-> int：显式 cast 允许。
str -> int/f64/bool：优先继续使用 parse_*；是否开放 cast 需单独决定。
T -> str：优先用 debug/stringify API；不默认把 cast<str> 做成万能 stringify。
any -> struct：只允许 exact runtime struct type。
any -> vector<T>：逐元素 runtime cast<T>，失败指向元素 index。
any -> func type：检查 callable signature。
```

##### H4：func 与其他值平等

`func` 必须成为动态边界的一等值：

```text
func 可以赋给 any。
any_is_func / any_type 能识别。
cast<func ...>(any) 或等价机制能从 any 恢复 callable，失败时诊断清楚。
backend 可以接收 Abel callable，并在 AbelRuntimeContext 中调用。
backend 调 Abel func 必须走 Abel 调用规则，不得绕过参数转换、ref 绑定和 stack/diagnostic。
lambda / named function / module-qualified function value 都要纳入测试。
overload 函数作为 func value 仍按现有规则给静态诊断；不靠 runtime 猜。
template 函数入口 retired，不再参与 func value 设计。
```

示例目标：

```abel
fn int inc(int x) {
    return x + 1;
}

backend Dyn {
    fn any call(any f, any arg);
}

fn int main() {
    any f = inc;
    any y = Dyn::call(f, 3);
    return cast<int>(y);
}
```

##### H5：operator 重载与动态 dispatch 系统

operator 不再靠 template 放大能力，而靠 concrete overload、any catch-all、runtime dispatch 和 backend dynamic protocol。

允许重载/动态分发的 operator 第一片：

```text
+ - * / % **
== != < <= > >=
<? >?
[]      # v1.2 tuple/map get 的自然入口
[]=     # v1.2 map/dynamic object set 的自然入口
```

禁止重载：

```text
=
&& ||
.
->
::
& address-of
* dereference
|> pipe
cast
call ()
```

原因：

```text
这些 operator 绑定控制流、模块解析、引用/指针语义、调用协议或 parser 稳定性；放开会破坏 Abel 的最小静态壳。
```

声明形态：

```abel
struct Vec2 {
    int x;
    int y;
}

fn Vec2 operator +(Vec2 a, Vec2 b) {
    return Vec2(a.x + b.x, a.y + b.y);
}

fn any operator +(any a, any b) {
    return DynamicOps::add(a, b);
}
```

backend dynamic operator protocol：

```abel
backend DynamicOps {
    fn any add(any a, any b);
    fn bool eq(any a, any b);
    fn any get(any obj, any key);
    fn void set(any obj, any key, any value);
}
```

check 阶段 dispatch：

```text
如果 operands 都是非 any 静态类型：
  1. 找 builtin operator。
  2. 找 visible concrete user operator。
  3. 找不到则 check error。

如果任一 operand 是 any：
  checker 允许进入 dynamic operator。
  arithmetic / indexing 结果默认 any。
  comparison 结果默认 bool。
  runtime 做动态 dispatch。
```

runtime dynamic dispatch：

```text
1. unwrap any。
2. 尝试 builtin dynamic operator。
3. 尝试 visible any operator overload。
4. 如果值是 backend-owned dynamic object，走 backend operator protocol。
5. 失败则 runtime diagnostic：operator、lhs type、rhs type、source span、stack。
```

v1.2 数据结构自然落点：

```abel
any t = [[1, "x", true]];
any x = t[0];           // operator [](t, 0)

any m = [{"name" = "Abel"}];
any name = m["name"];   // operator [](m, "name")
m["version"] = 12;      // operator []=(m, "version", 12)
```

##### H6：Backend / SDK 动态 ABI 窗口

v1.1-H 的主要工程面是让 backend 能吃下 Abel 动态值模型。

第一片必须收口：

```text
abel::AbelValue             拥有值，适合跨 backend 存储或深拷贝。
abel::AbelVariadicArgs      any... 调用视图。
std::vector<abel::AbelValue>
abel::AbelRuntimeContext&   诊断、stack、资源路径、调用环境。
abel::AbelCallable          Abel func/lambda/backend callable 的 SDK 表示。
abel::AbelDynamicObject     backend-owned dynamic object 协议句柄或等价抽象。
常用 scalar T& 写回继续保持：bool/int/i64/f64/str，后续按测试扩展。
```

第二片再推进：

```text
abel::AbelRef       临时 location view，可读/可写，不能长期保存。
abel::AbelPtr       nullable location view，不能长期保存。
abel::AbelTypeDesc  runtime type descriptor / introspection metadata。
vector/struct/any/func/ref/ptr 的 SDK view / visitor API。
```

生命周期硬边界：

```text
backend 不得长期保存 AbelRef / AbelPtr / borrowed view。
需要长期保存必须 clone 成 AbelValue，或转成 backend-owned handle。
backend-owned handle 默认进程期存活；RAII/finalizer 不阻塞 v1.1-H。
backend dynamic object 可以参与 operator dispatch，但不能绕过 Abel diagnostic。
```

##### H7：Backend 能观察 Abel 结构，但不接管 Parser

```text
backend 需要 introspect / view / visit AbelValue。
backend 不需要也不应该解析 Abel 源码 AST。
backend 看到的是 runtime value/type schema，不是 parser AST。
```

需要能力：

```text
读取 AbelValue 的 kind / display type。
读取 vector 长度与元素。
读取 struct 类型名与字段。
读取 any 内部值，规则固定。
识别 callable。
稳定 deep copy。
稳定 equality/hash；unsupported type 给错误字符串。
debug render，避免 backend 诊断不可读。
dynamic operator protocol：get/set/add/eq 等失败时能返回 structured diagnostic。
```

##### H8：check/run 分裂的新定义

```text
静态层错误必须 check 阶段报：普通类型错、名字错、调用 arity 错、非法 ref/ptr 绑定、backend 声明明显不匹配。
动态边界错误允许 run 阶段报：cast 失败、any get/call/operator 失败、backend-owned object 缺 key、unsupported dynamic key。
```

动态边界 run 报错不是 bug，但必须满足：

```text
错误路径稳定。
source span 清楚。
Abel stack/backend stack 清楚。
expected/actual 或 missing/unsupported 信息清楚。
不污染后续 unknown，不伪装成 interpreter 崩溃。
```

##### H9：v1.1-H 明确不做

```text
不保留 template 路线；不维护 retired/reserved 专用入口。
不做 tuple<T...>。
不做 object schema inference。
不做 core map/object/dict/tuple TypeKind。
不做动态字段访问 m.name / obj.field。
不做 dynamic backend_invoke(...) 核心机制。
不让 TypeChecker 跟踪 any 内部结构。
不做 borrow checker / 完整 lifetime proof。
不做完整 C++ overload ranking。
不重载 call()、pipe、address-of、dereference、assignment、member access。
不做 HTTP registry / IDE / JIT / debugger 协议。
```

##### H10：v1.1-H 验收清单

v1.1-H complete 必须有端到端证据：

```text
1. 文档宣布 template retired/removed，不再 legacy/frozen 地作为未来能力保留。
2. Parser/TypeChecker/Interpreter 无 Abel 用户 template 专用路径；former template syntax 不再有专项恢复或 retired 测试。
3. any/cast 文档升级为动态值核心与边界门。
4. any -> T 在 assignment/call/return/backend 参数等目标类型位置允许 runtime cast。
5. cast<T>(any) 诊断覆盖 scalar、str、vector、struct、func、失败路径。
6. func 可进入 any，并能识别、cast/恢复或作为 callable 传给 backend。
7. backend binder/SDK 可接收 AbelValue、AbelVariadicArgs、AbelRuntimeContext、AbelCallable。
8. backend 可调用 Abel func，保留 stack/diagnostic。
9. operator dispatch 支持 concrete overload + any dynamic dispatch；动态失败有 operator/lhs/rhs/span/stack。
10. backend dynamic object 可参与 [] / []= / eq 等 operator protocol。
11. SDK helper 支持 deep copy、equality、hash、debug string，并对 unsupported type 给明确错误。
12. 安装版 SDK fixture 覆盖 value、any、vector<any>、func-as-value、backend-calls-Abel-func、dynamic operator、dynamic diagnostic。
13. 文档明确 v1.2 tuple/map literal 将走 any + backend lowering + operator []/[]=，不走 core TypeKind/schema。
14. 4GB 上限全量 CTest 通过，git diff --check 无输出。
```

#### v1.2 简略方向：Backend-backed Dynamic Data

v1.2 暂不定死全部边界；当前只固定大方向：

```text
v1.2 建立在 v1.1-H 之上。
v1.2 引入 any tuple literal 与 str map literal，但它们是 dynamic literal，不是静态类型系统扩张。
[[...]] / [{...}] 只作为语法糖 lowering 到 backend-backed runtime。
结果第一片优先是 any 或 backend-backed dynamic ADT；不做 tuple<T...> / object schema / map value 精确推导。
TypeChecker 只检查 literal 基本形状：元素能进入 any、map key 是 str/string literal、重复 key 策略稳定。
Runtime/backend 负责真实存储、operator [] / []= / cast / call 诊断。
```

预期形态：

```abel
any at = [[1, "x", true]];
any sm = [{"name" = "Abel", "version" = 12, "ok" = true}];
```

第一片 lowering 可以等价于：

```text
[[a, b, c]]             -> DynamicRuntime::tuple_make(a, b, c)
[{ "k" = v, ... }]      -> DynamicRuntime::str_map_make("k", v, ...)
t[i]                    -> operator [](t, i)
m["k"]                  -> operator [](m, "k")
m["k"] = v              -> operator []=(m, "k", v)
```

v1.2 后续再定死：

```text
literal 结果类型到底固定为 any，还是允许显式目标 AnyTuple / StrMap<any>。
map key 是否只允许 string literal，还是允许运行期 str expr。
重复 key 是 check 拒绝、后者覆盖，还是 runtime 诊断。
tuple/map 的 standard API 名称与模块路径。
dynamic get/set/call 的 builtin 形态。
哪些 operator 直接走 DynamicOps backend，哪些走 visible any operator overload。
```

### 1.5 v1 complete 验收形态

v1 complete 最终验收必须是端到端证据，不是口头宣称：

```text
1. 语言矩阵测试：每个承诺语法至少一组正例、一组负例、一组静态层 check/run 一致性用例；动态边界另测 runtime diagnostic。
2. 标准库矩阵测试：每个公开 builtin/method 覆盖成功路径、静态误用、关键运行期错误。
3. backend SDK 测试：安装版 SDK + CMake plugin + binder 类型矩阵 + backend diagnostic + resource compatibility。
4. package 测试：init/add/remove/update/build/check/run/test/publish/registry/cache/lock/conflict/backend artifact。
5. diagnostic 测试：source excerpt/caret、stack trace、conversion spans、parser recovery、unknown 去污染。
6. 文档 smoke：AGENTS.md 的核心命令、边界和当前路线能在当前 CLI 下成立。
7. 4GB 上限全量 CTest 通过，`git diff --check` 无输出。
```

---

## 2. 工具链

本机锁定：

```text
Qt version:      6.11.1
Qt kit:          /home/tnuzy/Qt/6.11.1/gcc_64
CMake:           /home/tnuzy/Qt/Tools/CMake/bin/cmake
Ninja:           /home/tnuzy/Qt/Tools/Ninja/ninja
C compiler:      /usr/bin/gcc
C++ compiler:    /usr/bin/g++
GCC/G++ version: 14.2.0
C++ standard:    C++23
```

配置：

```bash
/home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64 \
  -DCMAKE_C_COMPILER=/usr/bin/gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_CXX_STANDARD=23
```

构建：

```bash
/home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
```

---

## 3. 仓库结构

```text
Abel/
  AGENTS.md
  LICENSE
  CMakeLists.txt

  src/
    abelcore/
      lexer/parser/ast
      type/typechecker
      value/runtime/interpreter
      builtin_registry
      backend_interface/backend_registry/backend_binder/backend_plugin_base
      package_manifest/resource_node
    abelcli/
      main.cpp

  plugins/examples/math_backend/
  examples/
  tests/
```

`abelcore` 是共享库：

```text
libabelcore.so
```

主程序与 Qt plugin 必须链接同一份 `abelcore`，避免 ABI/RTTI/QObject/全局状态分裂。

---

## 4. 当前 CLI

```bash
abel version
abel init <project-dir>
abel check <file-or-project>
abel run <file-or-project>
abel test <project-dir>
abel update <project-dir>
abel build <project-dir>
abel add path <dependency-dir> <project-dir>
abel add registry <name> <version-requirement> <registry-or-file-uri> <project-dir>
abel remove <dependency-name> <project-dir>
abel package check <project-dir>
abel package publish [--overwrite] <project-dir> <registry-dir>
abel package registry index/check/list <registry-dir>
abel resources check <resource.json>
```

测试相关 flags：

```bash
--filter <substring>
--expect-fail <substring>
--report-json <file>
--report-junit <file>
```

backend 额外资源：

```bash
abel run --resource <resource.json> <file-or-project>
abel test --resource <resource.json> <project-dir>
```

---

## 5. 语言核心当前面

Abel 定位：

```text
C/C++ 值模型
+ Qt str/char
+ vector<T>
+ struct / lambda / any / any...
+ builtin 标准库切片
+ backend block / Qt plugin
+ package/module/use/export
+ tree-run interpreter
```

### 类型

```text
void bool
int/long/ll/double aliases
i8 i16 i32 i64
u8 u16 u32 u64
f64
char str any
vector<T>
T* T& const T const T&
func R(A, B)
```

### 值模型

```text
变量拥有对象存储。
普通赋值复制值。
T& 是对象别名，必须初始化，不可重绑。
T* 保存地址值。
&x 要求 lvalue。
*p 得到 lvalue。
函数参数默认按值；修改调用方对象用 T& 或 T*。
Abel 不做 C/C++ 风险兜底：空指针、悬挂引用、越界、vector 扩容失效等仍是风险。
```

### const/reference 当前边界

```text
const T 与 const T& 已有第一片。
readonly location 已传播到变量、字段、vector index/front/back、解引用、range-for、mutating builtin receiver。
const T& 当前仍要求 lvalue，不做完整 prvalue lifetime extension。
const T* / T* const 完整矩阵仍未完成。
```

### vector

支持基础方法与常用算法：

```text
len empty push pop clear reserve resize front back
insert erase find contains count extend slice
sort reverse unique binary_search lower_bound upper_bound
```

`vector<struct>.resize()` 通过 default constructor callback 构造新增元素；TypeChecker 应提前拒绝非 default-constructible 元素。

### struct

支持：

```text
字段
init 构造
零参 init 默认构造
方法 / const 方法
this
public/private 标签
constructor overload 第一片
method overload 第一片
```

不完整且不作为当前路线：friend/protected/nested type/default args/完整 C++ overload ranking；template method 随 template 全路线 retired。

### 函数与 overload

已支持第一片：

```text
普通顶层函数 overload：direct / pipe / module-qualified。
struct constructor/method overload。
用户二元 operator overload-set。
```

当前 overload 评分是简单规则：精确值参数优先，其次引用精确绑定，再其次可转换参数，variadic any... 较弱。同分多候选 ambiguous。

未完成：backend overload、返回类型 overload、默认参数、模板 overload、完整 C++ overload ranking。

### module/use/export

支持第一片：

```text
module path;
use module.path;
use module.path as Alias;
export fn/struct/backend/type/enum;
export use module.path;
module::symbol / Alias::symbol
```

规则：

```text
同包跨模块访问必须 use。
跨包访问依赖包顶层 fn/struct/backend 要求 export。
限定名不绕过 use/export。
export use 传播真实 module import；alias 不随 re-export 传播。
```

未完成：完整 per-symbol public/private module surface、hide/rename、cycle diagnostic、发布索引级可见性。

---

## 6. 标准库当前面

### 字符串

```text
str.len/empty/contains/find/substr/slice/replace
starts_with/ends_with/trim/lower/upper/split
str.join(vector<str>)
str.parse_int/parse_long/parse_double/parse_bool
```

### 数学

```text
abs sqrt floor ceil round trunc pow
min max clamp
sin cos tan asin acos atan atan2
exp log log10 gcd lcm
```

### IO/path/env

```text
scan(&x, ...)
read_text write_text append_text
read_lines write_lines
path_exists path_is_file path_is_dir
copy_file move_path remove_path mkdirs
path_join path_dirname path_basename path_ext path_absolute path_clean
current_dir env_exists env_get
```

### debug/test

```text
debug_break()
debug_assert(bool, any...)
test_assert(bool, any...)
test_eq(actual, expected, any...)
test_ne(actual, expected, any...)
test_close(actual, expected, eps, any...)
```

### char/any

```text
char_code char_from_code
char_is_digit/letter/alnum/space/upper/lower
char_upper char_lower char_to_str
any_type any_is any_is_bool/int/double/char/str/vector/pointer
```

`any` 的真实取值仍通过 `cast<T>(any)`。

---

## 7. Backend / SDK 当前面

Abel 侧：

```abel
backend MathSystem {
    fn int fast_add(int a, int b);
}
```

C++ plugin 侧：

```cpp
class MathBackend final : public abel::AbelBackendPluginBase {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IAbelBackend_iid)
    Q_INTERFACES(abel::IAbelBackend)

public:
    MathBackend() {
        bind("MathSystem.fast_add", [](int a, int b) { return a + b; });
        bindVariadic("Std.build_string", [](abel::AbelVariadicArgs args) { ... });
    }

    QString backendId() const override { return QStringLiteral("MathSystem"); }
};
```

SDK install：

```bash
cmake --install build --prefix build/abel-sdk
```

CMake consumer：

```cmake
find_package(Abel REQUIRED)
target_link_libraries(my_backend PRIVATE Abel::abelcore)
```

当前 binder 支持常用 scalar、QString/QChar、常见 vector、部分 reference 写回、`AbelValue`、`std::vector<AbelValue>`、`AbelRuntimeContext&` 诊断、`bindVariadic`。不承诺任意 struct/class 自动拆装箱、完整 pointer/reference 矩阵或跨 Qt/编译器 ABI 稳定。

ResourceNode compatibility gate 会检查 Qt version、kit、platform、compiler、compilerVersion、cxxStandard、Abel ABI 字符串。它不是完整二进制 ABI hash。

---

## 8. 包管理当前面

支持：

```text
abel.package.json
abel.lock.json
path dependencies
local registry dependencies
file:// registry mirror cache
SemVer requirement 第一片
同名 package 多解析冲突诊断
package graph source consumption
backendArtifacts build/cache/load
local registry publish/index/check/list
```

缓存：

```text
.abel/cache/packages/<name>/<version>
.abel/cache/registries/<sanitized-uri>
.abel/cache/backend/...
<plugin>.abel-cache.json sidecar
```

当前仍未完成：HTTP/network registry、成熟 solver、成熟 download cache、完整 semver/ABI 级缓存失效、完整 binary content hash。

---

## 9. 诊断与调试

当前诊断目标：

```text
TypeChecker 根因优先，unknown 传播去污染。
Parser 常见错误恢复，避免吞掉后续声明。
Runtime diagnostic 带 source excerpt/caret。
Runtime stack frame 包含 function/method/lambda/backend/constructor 调用点。
转换错误尽量指向实参、return expr、assignment RHS、backend call。
```

排错顺序：

```text
1. 看 primary message 和 caret。
2. 看 Abel stack，从内层到外层。
3. 若 check 过 run 才炸，优先修 typechecker/interpreter 共享规则。
4. backend 问题同时看 Abel 声明、C++ bind symbol、resource/backendId/IID/ABI。
5. package 问题先看 lockfile stale、registry index stale、dependency conflict。
```

---

## 10. 实现原则

1. 不做 hard split / jit / context exporter / manifest hash audit 复活。
2. Git 是唯一审计与回滚机制。
3. Parser 只管语法；类型能力查 TypeChecker / BuiltinRegistry / symbol tables。
4. 内建函数/方法/operator 要集中在 `BuiltinRegistry`，不要散落硬编码。
5. TypeChecker 和 Interpreter 对参数转换、引用绑定、receiver mutability、overload 选择必须保持一致。
6. backend 不能掩盖语言核心问题；非 dynamic boundary 的 check/run 分裂必须在 core 修。
7. 不为了安全幻想牺牲 Abel 的 C/C++ 能力面；但明显静态错误必须诊断。
8. 每次大块推进要有测试覆盖正例、负例、静态层 check/run 一致性和动态边界 runtime diagnostic。

---

## 11. 当前重点路线

v1 后续只围绕 tight complete 与 v1.1-H 动态核心硬化补闭环，不扩张到 v2+ 无底洞。优先级从高到低：

```text
1. template retired 切除：文档删除推荐路线，清掉 Abel 层 token/parser/typechecker/interpreter/test 专用路径，不保留 retired diagnostic 兼容入口。
2. any 动态核心：装箱/解包/deep copy/debug/equality/hash/vector<any>/AbelValue bridge 规则唯一化。
3. cast 与隐式 dynamic cast：assignment/call/return/backend 参数等目标类型位置插入 runtime cast，诊断 expected/actual/span/stack。
4. func 动态化：func 进入 any、any_is_func/cast callable、backend 接收并调用 AbelCallable。
5. backend / SDK 动态 ABI 窗口：AbelValue、AbelVariadicArgs、AbelRuntimeContext、AbelCallable、dynamic object、deep copy/equality/hash/debug helper。
6. operator 系统：concrete overload + any dynamic dispatch + backend dynamic operator protocol，覆盖 [] / []= 的 v1.2 落点。
7. v1 承诺语法矩阵审计：找出 parser-only / check-run 分裂 / 未测语法，逐块收口。
8. const/pointer/reference/value-category 的 v1 最小矩阵：const object、const ref、pointer-to-const、readonly propagation；不做 pointer arithmetic/lifetime proof。
9. package 本地完整引擎收口：local/file registry、semver range、lock/cache/conflict/backend artifact 自动生成；不做 HTTP/network。
10. diagnostics 收口：source span、stack、conversion span、parser recovery、unknown 去污染的矩阵测试。
11. 文档 smoke：AGENTS.md 与 abel-engineering skill 命令同当前 CLI 对齐。
```

v1.1-a/b 已完成后，v1.1-H 只允许沿下面几条线推进：

```text
any/cast 动态边界硬化。
func 一等动态值与 backend callable。
operator concrete + dynamic dispatch。
SDK/backend introspection、deep copy、equality/hash、debug render、handle store、dynamic object protocol。
动态边界 runtime diagnostic。
为 v1.2 any tuple / str map literal 做 backend-backed runtime 准备。
禁止保留 template 路线或把 map/object/dict/tuple 做进核心 TypeKind。
```

用户明确要求“大块推进”时，不要陷入细枝末节验证；选一个能让 v1.1-H 或 v1 complete 更真的大块，做完、测完、提交。

---

## 12. 文档边界

当前主文档：

```text
AGENTS.md        本仓库唯一 Markdown 操作手册；路线、边界、验证方式和当前进度都收束在这里。
```

仓库不再保留 README.md / TUTORIAL_zh.md / CODEX.md 这类重复 Markdown 文档；需要历史可查时看 Git log。

---

## 13. 当前进度

```text
当前阶段：v1.1-a/b scope 已完成；进入 v1.1-H Dynamic Core Hardening；v1 complete 仍需最终矩阵验收。
最新已知提交：不再手工固化，具体以 `git log --oneline -1` 为准。
本轮实现任务：应用户要求继续清理 template 路线；Abel 层 template/interface/require token、Parser retired 恢复分支、explicit type arg 特判、generic type 专项诊断和相关专项测试已移除，former template syntax 仅按普通语法/类型错误自然失败，为后续 any-based typecheck 重做减负。
```

已完成的大块能力摘要：

```text
v0 core complete 已经过历史验收。
项目/包管理入口已形成第一批闭环：init/add/remove/update/build/check/run/test/package publish/registry index。
模块/use/export/export use/import alias 第一片已落地。
dependency source consumption 与 export enforcement 第一片已落地。
本地 registry/cache/file URI mirror 第一片已落地。
backend SDK install、binder 常用类型矩阵、variadic binder、artifact CMake build/cache/compatibility metadata 第一片已落地。
runtime stack/source excerpt/debug/test diagnostics 第一片已落地。
fixed-width ints、enum/type alias、const reference、readonly locations、struct public/private 已落地。
std.str/std.vec/std.math/std.io/std.path/std.env/std.char/std.any/std.debug/std.test 多个切片已落地。
用户二元 operator overload-set、普通函数 overload-set、struct constructor/method overload-set 第一片已落地。
历史无约束 template 第一片曾落地，但新路线已决定 template 全路线 retired/removed；当前已清掉 Abel 用户 template 的 lexer keyword、Parser retired 恢复/explicit type arg/generic type 专项路径、AST 字段、TypeChecker 推导/实例化路径、Interpreter 运行路径和成功/retired 专项测试。保留的尖括号语法只剩 core type / cast 语法：`vector<T>`、`func`、`cast<T>(x)`。
历史 exact-shape 二元 operator template 曾落地，但当前已随 template 路线清除；operator 能力改走 concrete overload + any dynamic dispatch + backend dynamic operator protocol。
P1~P4 语义闭环修复已落地：`this` 作为对象 lvalue 参与字段/方法 lookup；struct 字段 location 记录 declared type，嵌套构造赋值不再按 `<unknown>` 当前值转换；命名函数与模块限定函数可作为 `func` 值（overload 给静态诊断；template 路线现已 retired）；backend binder/Qt plugin 支持 bool/int/i64/f64/str 等 scalar `T&` 写回，并强化 declaration/bound signature mismatch 诊断。
v1.1-a/b 方向曾收束进文档：不做动态对象化语言扩张；复杂度由 SDK/Backend 承载。v1.1-H 起改为 any/cast/operator/backend 动态核心，不再使用 template struct/type alias 承载未来复杂数据。
v1.1-a Structured Calls 继续推进：`x |> f(_)`、`x |> f(1, _)`、`x |> f(_, _)` 对普通函数、函数值和 builtin function 可用；普通函数、module-qualified 普通函数、普通构造器、alias constructor、static-qualified / backend call 的 pipe RHS 已支持 named/default/spread 归一化，例如 `3 |> scale(x: _, by: 4)`、`3 |> scale(by: 4)`、`xs |> report("prefix", ..._)`、`3 |> lib.math::scale(x: _, by: 4)`、`xs |> lib.math::count("prefix", ..._)`、`3 |> Point(x: _, y: 4)`、`3 |> MathSystem::fast_add(a: _, b: 4)`、`xs |> MathSystem::count("prefix", ..._)`；receiver-hole RHS 已从单 receiver hole 扩展到读用多 hole：`x |> _.method()`、`x |> _.field.method()`、`s |> _.contains(_)`、`b |> _.same(_)` 可用。lhs 运行期只求值一次，hole 保留 lvalue 以绑定 `T&`；若同一个 pipe hole 会绑定多个 mutable reference 参数，或 receiver-hole 多 hole 中任一用法是 mutable receiver / mutable reference 参数，TypeChecker 与 Interpreter 都拒绝。普通函数、module-qualified 普通函数、struct constructor、struct method、backend/static call 已支持 named args、default args，并通过候选归一化保持 TypeChecker/Interpreter 一致；default expr 在声明上下文检查，运行期只在选中 overload 后求值。limited spread 第一片已支持 `vector<any>` / `any...` 展开到用户和 backend `any...` 尾部，并支持 direct 与 pipe 形态的 builtin `build_string`/`print`/`println` 展开；展开时 `vector<any>` 元素只解包一层，避免 `any(any(x))`。builtin method 与 function value call 仍保持 positional-only / 不补 default 的 ABI 边界。
v1.1-b Backend Carries Complexity 第一片 SDK helper 已落地：新增 `AbelValueKey` / `abelValueEquals`，支持 backend 对 bool/int/f64/char/str/nullptr/vector/any-unboxed key 做深拷贝、稳定 hash 与 equality，遇到 pointer/reference/struct/function/unknown 等 unsupported key 给明确错误字符串；新增 header-only `AbelBackendHandleStore<T>`，为 C++ backend 提供 long handle -> backend-owned object 的 create/find/get/remove/diagnostic helper。安装版 SDK fixture 已用这些 helper 实现 handle-backed `map_create/map_set/map_get/map_contains/map_len`，Abel 侧仍只是普通 backend fn + long handle + any 边界，未新增 `TypeKind::Map/Object/Dict`、map/object literal、dynamic invoke 或动态字段。
README.md / TUTORIAL_zh.md / CODEX.md 已按新文档收束策略删除；后续路线和操作纪律只维护 AGENTS.md，abel-engineering skill 另行按需要同步。
v1.1-H 路线已在 AGENTS.md 定死：template 全路线 retired/removed；any/cast/operator/backend 成为 v1.2 主路线；TypeChecker 只守静态边界和显式 dynamic boundary，不追踪 any/tuple/map/object schema；backend/SDK 需要强化 AbelValue、AbelVariadicArgs、AbelRuntimeContext、AbelCallable、dynamic object、deep copy/equality/hash/debug/introspection；operator 能力改走 concrete overload + any dynamic dispatch + backend dynamic operator protocol；v1.2 any tuple literal `[[...]]` 与 str map literal `[{...}]` 暂定为 backend-backed dynamic literal lowering，并通过 operator [] / []= 访问，不新增 core tuple/map/object TypeKind，不做 tuple<T...> 或 schema inference。
v1.1-H H2/H4 第一片 any introspection 已落地：`any_type` 继续返回内部 runtime type；新增 `any_debug(any) -> str`、`any_is_struct(any) -> bool`、`any_is_func(any) -> bool`，并复用 `any_is(any, "struct"|"func")`。测试覆盖 scalar/vector/struct/function value 装箱后的查询与 misuse 静态拒绝；这只是 introspection/debug 出口，不等于 cast<func> 或 backend 调 Abel func 已完成。
v1.1-H H3/H4 cast 第一片已落地：`cast<func R(A...)>(any)` 可从 any 中恢复 exact-signature function value，签名不匹配给 E0591 runtime diagnostic；`cast<vector<T>>(any)` 可从 any 中恢复 vector，并逐元素执行 dynamic cast，元素失败诊断包含 `vector element i`。这还不是完整隐式 dynamic cast 矩阵，assignment/call/return/backend 参数插入仍需继续推进。
v1.1-H H3 隐式 dynamic cast 第一片已落地：静态层 `isAssignable` 对非 reference 目标接受 source `any`，运行期 `convertOrError` 对 `any` 执行 dynamic cast。当前测试覆盖变量初始化、赋值、普通函数实参、return、`vector<T>` 目标恢复和 backend 参数；引用绑定仍不靠 any 绕过 lvalue/ref 规则。
```

最近验证命令模板：

```bash
/bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
```

当前未完成重点：

```text
v1 complete 尚未达成。
需要做 v1 承诺语法矩阵审计，确认没有 parser-only 幻影功能。
backend overload 未完成，但仅要求 v1 本地 ABI 窗口内闭环，不追求 C++ 完整 overload。
const pointer/reference/value-category 仍需 v1 最小矩阵收口；完整 lifetime proof 不进 v1。
template v1 最简无约束函数/struct/type alias 与 exact-shape 二元 operator template 历史上已落地，但现在全路线 retired/removed；当前已完成实现清理，Abel lexer/parser 不再保留 template/interface/require 关键字、`template <type ...>` 恢复分支、显式类型实参特判或非 core generic type 专项诊断，TypeChecker/Interpreter 不再保留 template bindings / instantiation / exact-shape operator template / explicit type args / generic type args 逻辑。
package 只要求本地/file registry 完整闭环；HTTP/network registry、全球索引、签名发布不进 v1。
diagnostic 需要矩阵验收；交互式 debugger/DAP 不进 v1。
标准库只要求常用本地程序 API 稳定；regex/locale/streaming/view/GUI 不进 v1。
v1.1-a 状态：当前 scope 完成。后续只允许做回归测试、文档澄清或 bugfix；builtin method 与 function value call 继续保持 positional-only 边界，不作为 v1.1-a 缺口。
v1.1-b 状态：当前 scope 完成。后续只允许扩展 SDK/backend helper 的文档/示例/诊断质量；复杂库能力仍必须通过 backend-backed Abel surface 类型承载，禁止把 map/object/dict/symbol/resource/dynamic invoke 下沉进语言内核。
v1.1-H 状态：H1 template retired/removed deep-clean 已完成；H2/H4 any introspection/debug 第一片已完成；H3/H4 explicit cast 第一片已完成（func exact cast、vector element-wise cast）；H3 隐式 dynamic cast 第一片已完成（init/assign/call/return/backend 参数）。下一步执行顺序是 AbelCallable / backend calls Abel func → SDK dynamic helper → operator dynamic dispatch → 安装版 SDK fixture 覆盖。
v1.2 状态：只固定大方向，不定死细节。any tuple `[[...]]` 与 str map `[{...}]` 必须走 any + backend lowering + operator []/[]=；后续再确定 literal 结果类型、重复 key 策略、API 名称与 dynamic get/set/call 形态。
```

提交记录不再手工复制长列表；需要历史请用：

```bash
git log --oneline
```
