# Abel Agent Manual

状态：Abel core v1.2 已发布；VSCode/LSP 当前按 v0.1 维护。
作用：任何 Agent 进入本仓库，先读本文件，再读代码。
原则：本文件只保留当前有效的工程纪律、架构、验证方式、语言边界和近期风险；历史流水账看 Git log。

---

## 0. 进入仓库第一纪律

进入仓库后，任何写入前必须执行：

```bash
pwd
git status --short
ls
```

当前仓库应在：

```text
/home/tnuzy/桌面/Lab/Abel
```

若不是 Git 仓库，停止并询问是否初始化。

### 写入纪律

1. 所有源码、文档、配置创建/修改/删除默认必须通过 `apply_patch`。
2. 禁止 `cat > file`、`echo > file`、`tee`、`sed -i` 等绕过审查的写入方式。
3. 唯一例外：大规模机械性文档替换或重复迁移，且必须先说明目标文件、范围和原因，执行后审查 diff。
4. 每轮从 clean tree 起步；若不干净，先确认改动来源。
5. 每轮形成一个逻辑 commit。
6. commit 前尽可能运行相关 build/check/test。
7. 测试必须限制 4GB 虚拟内存，避免系统死亡。
8. 不伪造测试结果，不声称未运行的命令已运行。
9. 完成实质修改后更新本文末尾「当前状态」。

标准验证命令：

```bash
/bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
```

文档-only 最小验证：

```bash
git diff --check
```

推送命令优先：

```bash
git -c http.version=HTTP/1.1 -c http.lowSpeedLimit=1 -c http.lowSpeedTime=30 push origin master
```

---

## 1. 当前产品线与版本口径

### 1.1 Abel core / CLI / SDK

当前公开口径：

```text
Abel core v1.2 released
```

v1.2 的主题是 **Dynamic Waterworks**：

```text
Static where cheap.
Dynamic where expressive.
Pipe as data plumbing.
Backend/runtime where complex.
Diagnostics everywhere.
```

v1.2 已发布能力：

```text
any dynamic object literal:
  [[...]]                 # anytuple
  [{"k" = v, ...}]        # strmap

dynamic object protocol:
  [] / []=
  == / !=
  any_type / any_debug

generalized pipe:
  lhs |> _ + 1
  row |> _["name"]
  row |> [{"name" = _["name"], "score" = _["score"]}]
  obj |> (_["count"] = cast<int>(_["count"]) + 1)
```

v1.2 不改变的核心边界：

```text
不新增 core TypeKind::Tuple/Map/Object/Dict。
不恢复用户泛型/模板。
不做 object schema inference。
不做 dynamic field access m.name。
不把 pipe 变成可重载 operator。
不让 prvalue pipe 绕过 mutable ref / lifetime 边界。
不引入 borrow checker；只做局部调用边界 alias 风险检查。
```

v1.3 核心增量口径：

```text
v1.3 只引入 instant lambda / do expression。
不改变 v1.2 pipe 语义。
不新增 pipe operator。
不改变 [[...]] / [{"k" = v}] 动态容器语法和语义。
不扩张 dynamic key / patch merge / pipe manifold。
```

v1.3 的目标是让复杂 pipe RHS 可以局部展开，而不是重做水路系统：

```abel
req |> do {
    str cmd = cast<str>(_["body"]["cmd"]);
    int timeout = cast<int>(_["body"]["timeout"]);
    return Bash::run_wire(cmd, timeout);
}
```

### 1.2 VSCode / LSP

当前公开口径：

```text
VSCode/LSP v0.1
```

v0.1 定位：

```text
可用的编辑器体验第一版，不宣称完整 IDE。
TypeChecker AnalysisIndex 是语义真源。
LSP 不重写第二套 lookup/type/value-category 规则。
默认 CLI check/run/build/test 不为 IDE 分析付性能税。
```

当前已支持：

```text
diagnostics
document symbols / outline
workspace symbol
completion 基础支持
hover
definition
references
documentHighlight
signatureHelp
foldingRange
semanticTokens/full
prepareRename / rename 基础支持
```

v0.1 仍需谨慎表述的边界：

```text
全量重算为主，尚未完成成熟增量缓存/取消。
字段/方法/模块/enum/type binding 仍需继续补全。
链式/调用 receiver 成员补全仍需继续增强。
精确 overload binding、inlay hints、code actions 仍非完整 IDE 级。
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

测试：

```bash
/bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
```

---

## 3. 仓库架构

```text
Abel/
  AGENTS.md
  CMakeLists.txt
  README.md
  README.zh-CN.md
  README.ja.md
  Tutorial.zh-CN.md

  src/
    abelcore/
      lexer / parser / AST
      TypeChecker
      Interpreter / runtime value model
      BuiltinRegistry
      backend interface / binder / registry / plugin base
      package manifest / resource node
      AnalysisIndex

    abelcli/
      abel CLI

    abellsp/
      LSP protocol / analyzer / server

  vscode/abel-vscode/
    VSCode thin client for abel-lsp

  plugins/examples/
  examples/
  tests/
```

`abelcore` 是共享库：

```text
libabelcore.so
```

主程序、LSP、测试与 Qt plugin 必须链接同一份 `abelcore`，避免 ABI/RTTI/QObject/全局状态分裂。

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

## 5. Abel 语言核心边界

Abel 定位：

```text
C/C++ 值模型
+ Qt str/char
+ vector<T>
+ struct / lambda / func / any / any...
+ backend block / Qt plugin
+ package/module/use/export
+ tree-run interpreter
+ v1.2 dynamic data plumbing
```

### 5.1 类型

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

保留尖括号语法只限 core type / cast：

```text
vector<T>
func R(A, B)
cast<T>(x)
```

用户泛型/模板路线不属于当前 Abel surface：

```text
不做 function/struct/type alias/operator template。
不做 template+interface / require。
不做 variadic template / tuple<T...>。
不为了 map/tuple/object schema 扩张泛型系统。
```

### 5.2 值模型

```text
变量拥有对象存储。
普通赋值复制值。
T& 是对象别名，必须初始化，不可重绑。
T* 保存地址值。
&x 要求 lvalue。
*p 得到 lvalue。
函数参数默认按值；修改调用方对象用 T& 或 T*。
```

Abel 不做 C/C++ 风险兜底：

```text
空指针、悬挂引用、越界、vector 扩容失效等仍是风险。
const T& 当前不承诺完整 prvalue lifetime extension。
完整 borrow checker / lifetime proof 不进当前路线。
```

### 5.3 struct / function / overload

支持：

```text
struct fields
init constructors
zero-arg default construction
methods / const methods
this
public/private labels
constructor overload
method overload
ordinary function overload
binary operator overload-set
```

不承诺：

```text
完整 C++ overload ranking。
返回类型 overload。
默认参数进入 func type ABI。
ADL。
friend/protected/nested type 完整面向对象系统。
operator() / operator<> 用户重载。
```

### 5.4 module/use/export

支持：

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

### 5.5 any / cast / dynamic object

`any` 是 Abel 的动态值核心，不是 TypeChecker 要追踪内部 schema 的对象。

原则：

```text
T -> any：允许，进入动态边界。
any -> T：通过 cast<T>(x) 或目标类型位置的 runtime dynamic cast。
动态失败是合法 runtime diagnostic，不是 check/run 分裂 bug。
诊断必须有 expected/actual、source span 和 Abel/backend stack。
```

v1.2 dynamic literal：

```abel
any t = [[1, "x", true]];
any m = [{"name" = "Abel", "version" = 12}];
any x = t[0];
m["version"] = cast<int>(m["version"]) + 1;
```

实现边界：

```text
tuple/strmap 是 runtime dynamic object，不进 core TypeKind。
[] / []= 走 dynamic object/operator 协议。
strmap 当前 key 是 string literal；duplicate key 应 check-time diagnostic。
missing key / bad index / out-of-range 是 runtime diagnostic。
```

### 5.6 pipe

v1.2 pipe 是带 `_` hole 的一般表达式上下文：

```abel
x |> _ + 1
row |> _["name"]
row |> [{"name" = _["name"], "passed" = _["score"] >= 42}]
```

规则：

```text
lhs 只求值一次。
`_` 代表同一个 pipe value。
若 lhs 是 lvalue，`_` 保留同一个 location。
若 lhs 是 prvalue，可读取/索引/值语义使用，但不能绕过 mutable ref 绑定。
多 hole 读用法允许。
同一调用边界把同一 location 绑定到多个 mutable ref / mutable receiver aliases 必须拒绝。
无 `_` 的 RHS 保持旧 callable insertion 兼容。
```

v1.3 不改 pipe：

```text
不要把 |> 拆成按值 / 按引用 / sink 多套 operator。
不要新增 |=> / |+> / |&> / |!> / |&!>。
不要改变 lhs 是 lvalue 时 `_` 保留 location 的既有行为。
需要局部复杂逻辑时，用 do expression 承接 pipe context。
```

### 5.7 instant lambda / do expression

v1.3 计划新增 `do { ... }` 表达式，定位是“立即执行的局部表达式块”，不是函数值、不是普通 lambda 的语法糖。

基本形态：

```abel
any out = req |> do {
    str cmd = cast<str>(_["body"]["cmd"]);
    int timeout = cast<int>(_["body"]["timeout"]);
    return Bash::run_wire(cmd, timeout);
};
```

规则：

```text
do 是 expression。
do 立即执行，不生成 func value。
do 有自己的局部 scope。
do 可出现在普通表达式位置，也可出现在 pipe RHS。
处于 pipe RHS 时，do 内天然可使用当前 pipe context 的 `_`。
do 内 return 只返回 do 表达式结果，不返回外层函数。
do 不改变外层 function/lambda 的 return 语义。
```

实现边界：

```text
Parser 增加 DoExprNode，而不是脱糖成 LambdaExprNode。
TypeChecker 对 do 使用独立 return context，推导 do 表达式类型。
Interpreter 执行 do block 时捕获 do-local return。
非 void do 必须所有路径 return。
多个 return 的类型必须按现有可赋值规则形成稳定结果类型。
break/continue 不得跳出 do expression 边界。
```

禁止借 v1.3 do expression 扩张：

```text
不新增 pipe operator。
不改变 |> 的 location / value-category 规则。
不改变 dynamic literal key 规则。
不新增 #expr dynamic key。
不新增 patch merge。
不把 dynamic container 重新定义为 pipe manifold。
```

后续性能优化允许但不得改变语义：

```text
[{"k" = v}] strmap 可在实现层做 literal key pre-run/precomputed hash。
bare key、dynamic key、general hash-key map 不属于当前 v1.3 核心增量。
性能优化必须保持现有 string-literal key 语义和诊断。
```

---

## 6. 标准库当前面

### 字符串

```text
str.len/empty/contains/find/substr/slice/replace
starts_with/ends_with/trim/lower/upper/split
str.join(vector<str>)
str.parse_int/parse_long/parse_double/parse_bool
```

### vector

```text
len empty push pop clear reserve resize front back
insert erase find contains count extend slice
sort reverse unique binary_search lower_bound upper_bound
```

`vector<struct>.resize()` 通过 default constructor callback 构造新增元素；TypeChecker 应提前拒绝非 default-constructible 元素。

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
any_type any_debug
any_is any_is_bool/int/double/char/str/vector/pointer/struct/func
```

---

## 7. Backend / SDK

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

当前 SDK/binder 重点能力：

```text
常用 scalar / QString / QChar。
常见 vector。
部分 T& 写回。
abel::AbelValue。
std::vector<abel::AbelValue>。
abel::AbelVariadicArgs。
abel::AbelRuntimeContext&。
abel::AbelCallable。
backend-owned dynamic object protocol。
handle store helper。
deep copy / equality / hash / debug helper。
```

不承诺：

```text
任意 C++ struct/class 自动拆装箱。
完整 pointer/reference 矩阵。
跨 Qt/跨编译器稳定二进制 ABI。
完整二进制内容 ABI hash / 供应链安全系统。
```

ResourceNode compatibility gate 会检查 Qt version、kit、platform、compiler、compilerVersion、cxxStandard、Abel ABI 字符串。它不是完整二进制 ABI hash。

---

## 8. 包管理

支持：

```text
abel.package.json
abel.lock.json
path dependencies
local registry dependencies
file:// registry mirror cache
基础 SemVer requirement
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

不进当前路线：

```text
HTTP/network registry。
远程账号 / 签名发布 / 全球包索引。
完整 SAT/MaxSAT solver。
成熟 download cache。
完整 semver/ABI 级缓存失效。
```

---

## 9. TypeChecker / Interpreter / AnalysisIndex / LSP

核心原则：

```text
TypeChecker 是静态语义真源。
Interpreter 必须复用或等价实现 TypeChecker 已确认的转换、绑定和 overload 规则。
非 dynamic boundary 的 check/run 分裂是 core bug。
dynamic boundary 的 runtime 失败是合法诊断，但必须稳定、可定位、可测试。
AnalysisIndex 是 LSP/IDE 的 opt-in 产物。
```

AnalysisIndex 性能边界：

```text
默认 TypeChecker::check(program) 不收集大型 index。
只有 LSP/测试显式请求 analysis 时启用。
Interpreter/runtime 不依赖 AnalysisIndex，不进入 run 热路径。
AnalysisIndex 只记录必要 span/type/binding/symbol，不复制 AST 大对象。
任何新增记录函数都必须先判断 analysis 是否启用。
CLI 若未来需要 JSON analysis，必须新命令或新 flag，不改变现有 check/run 默认成本。
```

LSP 开发规则：

```text
优先消费 AnalysisIndex。
不要靠全局同名 token 替代语义 binding。
rename 只改可证明同一 symbol 的 ranges。
prepareRename 在不可证明位置返回空。
completion 可以先做局部启发式，但不能污染 CLI 语义。
```

---

## 10. 诊断与调试

诊断目标：

```text
TypeChecker 根因优先，unknown 传播去污染。
Parser 常见错误恢复，避免吞掉后续声明。
Runtime diagnostic 带 source excerpt/caret。
Runtime stack frame 包含 function/method/lambda/backend/constructor 调用点。
转换错误尽量指向实参、return expr、assignment RHS、backend call。
dynamic object 错误必须带 key/index/operator/runtime type。
```

排错顺序：

```text
1. 看 primary message 和 caret。
2. 看 Abel stack，从内层到外层。
3. 若 check 过 run 才炸，先判断是否处于显式 dynamic boundary。
4. 非 dynamic boundary：优先修 TypeChecker/Interpreter 共享规则。
5. backend 问题同时看 Abel 声明、C++ bind symbol、resource/backendId/IID/ABI。
6. package 问题先看 lockfile stale、registry index stale、dependency conflict。
7. LSP 问题先看 AnalysisIndex 是否已有 binding，再看 Analyzer fallback。
```

---

## 11. 实现原则

1. 不做 hard split / JIT / bytecode VM / IDE 巨系统复活。
2. Git 是唯一审计与回滚机制。
3. Parser 只管语法；类型能力查 TypeChecker / BuiltinRegistry / symbol tables。
4. 内建函数、方法、operator 要集中在 `BuiltinRegistry`，不要散落硬编码。
5. TypeChecker 和 Interpreter 对参数转换、引用绑定、receiver mutability、overload 选择必须保持一致。
6. backend 不能掩盖语言核心问题；非 dynamic boundary 的 check/run 分裂必须在 core 修。
7. 不为了安全幻想牺牲 Abel 的 C/C++ 能力面；但明显静态错误必须诊断。
8. 每次大块推进要有测试覆盖正例、负例、静态层 check/run 一致性和动态边界 runtime diagnostic。
9. v1.2 之后只做 bugfix、诊断质量、SDK helper、库层包装、LSP v0.1 增强和 v1.3 instant lambda；不要以“补 v1.2/v1.3”为名扩张 core schema。

---

## 12. 当前重点路线

当前阶段：

```text
Abel core v1.2 released。
VSCode/LSP v0.1 继续补语义体验。
v1.3 核心增量已收敛为 instant lambda / do expression。
主线回到 v1/v1.2 released 后的稳定化、矩阵审计、诊断质量、文档一致性和 do expression 最小实现。
```

优先级从高到低：

```text
1. v1/v1.2 总矩阵审计：确认没有 parser-only 幻影和非 dynamic check/run 分裂。
2. v1.3 instant lambda / do expression：只实现局部表达式块和 do-local return，不改 pipe。
3. LSP v0.1：补 module/use/type/enum binding、链式/调用 receiver completion、缓存/增量/取消、code action、inlay hints。
4. const/pointer/reference/value-category 的最小矩阵继续收口；不做 pointer arithmetic/lifetime proof。
5. backend/SDK：继续强化 dynamic object helper、diagnostic、installed SDK fixture。
6. package/diagnostic 文档 smoke 保持绿色，但不压过 core 稳定化。
```

明确禁止回退路线：

```text
不恢复用户泛型/模板。
不新增 core TypeKind::Tuple/Map/Object/Dict。
不做 object schema inference。
不做 dynamic field access m.name。
不改变 v1.2 |> pipe 语义。
不新增 |=> / |+> / |&> / |!> / |&!>。
不改变 [{"k" = v}] string-literal key 规则。
不新增 #expr dynamic key 或 patch merge。
不把 LSP 分叉成第二套 TypeChecker。
不让 CLI 默认路径为 IDE index 付性能税。
不做 HTTP registry / JIT / debugger DAP 作为当前主线。
```

---

## 13. 文档边界

当前主文档：

```text
AGENTS.md          仓库操作手册和工程边界。
README.md          用户入口文档。
README.zh-CN.md    中文用户入口文档。
README.ja.md       日文用户入口文档。
Tutorial.zh-CN.md  中文教程。
```

文档原则：

```text
AGENTS.md 不写历史流水账。
README/Tutorial 写用户可见能力，不写未落地路线承诺。
路线变更必须同步删除冲突旧描述。
需要历史时看 Git log，不在 AGENTS.md 手工复制提交列表。
```

---

## 14. 当前状态

```text
当前阶段：
  Abel core v1.2 released。
  VSCode/LSP 当前按 v0.1 维护。
  v1.3 核心增量 do expression 已在当前代码树闭环；不改 pipe 和 dynamic container 语义。

本轮文档状态：
  AGENTS.md / README / Tutorial 记录 v1.3 最小方向：只加 do expression。
  明确保留 v1.2 |> location/value-category 语义。
  明确不改 [[...]] / [{"k" = v}]，仅允许后续实现层 key pre-run/precomputed hash 性能优化。

本轮工程状态：
  已闭环 v1.3 do expression 最小实现切片：
    Parser 增加 DoExprNode。
    TypeChecker 增加 do-local return 推导、缺失 return 诊断和 do 边界 break/continue 诊断。
    Interpreter 增加 do block 立即执行和 do-local return 捕获。
    pipe RHS 支持 do expression 使用当前 `_` pipe context，不改变 v1.2 pipe operator 语义。
    complex tests 覆盖 [[...]] / [{"k" = v}] 嵌套、do 混合嵌套和信息流写回。
    LSP v0.1 最简支持 do：语义分析通过、do block 局部符号可 hover/definition/completion、semantic token 标记 do 关键字。

最新提交：
  不手工固化；使用 `git log --oneline -1` 查看。
```

最近验证命令模板：

```bash
/bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/cmake --build build && /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1 && git diff --check'
```

文档-only 可用：

```bash
git diff --check
```
