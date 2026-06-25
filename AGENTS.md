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
2. 所有 v1 类型/值类别/调用规则 check/run 一致；不允许 check 过而 run 才报类型错误。
3. 本地工程从 0 到运行、测试、依赖、backend artifact、SDK plugin 都有 CLI 闭环。
4. 标准库覆盖常用本地程序能力：字符串、vector、数学、文件/路径/环境、debug、test、char、any。
5. backend 支持稳定的 v1 ABI 窗口：常用 scalar/string/vector/any/reference/diagnostic/variadic；复杂对象走 AbelValue 边界。
6. 包管理是“本地完整引擎”：path dependency、本地/file registry、SemVer range、lockfile、cache、冲突诊断、backend artifact 自动构建/缓存。
7. 诊断能定位：结构化 diagnostic、源码位置、excerpt/caret、Abel 调用栈、backend/resource/package 错误路径。
8. 文档与实际行为一致，用户不需要读历史日志才能搭工程。
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
minimal template functions/types without interface constraints
any... variadic + BuiltinRegistry function/method/operator registration
backend block declaration and static backend call
debt declaration as explicit runtime error boundary
```

v1 必须做到：这些语义要么完整可用，要么在 check 阶段给出明确诊断；不能留下“能解析但运行才未知”的灰区。

### 1.3 v1 明确不做，避免无底洞

下面内容不阻塞 v1 complete，默认进入 v2+ 或研究项：

```text
JIT / split / bytecode VM / IDE
HTTP/network registry、远程账号、签名发布、全球包索引
完整 SAT/MaxSAT 级依赖求解器
跨 Qt/跨编译器稳定二进制 ABI
完整二进制内容 ABI hash 与供应链安全系统
完整 C++ 模板元编程、SFINAE、concepts 复刻
template + interface / require 约束系统
完整 C++ overload ranking、ADL、默认参数、返回类型 overload
operator() / operator[] / operator<> 用户重载
指针算术、void*、reinterpret_cast、manual delete 所有权模型
Rust 式 borrow checker / GC / 生命周期证明
完整 prvalue lifetime extension
friend/protected/nested type/完整面向对象系统
regex/locale/streaming IO/复杂 collection views/GUI 标准库
交互式 debugger 协议、DAP、断点 UI
热重载 plugin / 跨 ABI plugin 市场
```

template 在 v1 可以做最简形态：只支持无约束的类型参数实例化，用于普通函数/struct 的直接单态化或等价实现；不做 interface/require 约束，不做偏特化，不做模板元编程，不做复杂 overload ranking。

如果某项“不做”已经有 parser 入口，v1 complete 前必须把它变成：

```text
1. 明确文档标注为 reserved/v2；且
2. TypeChecker 或 Parser 给出稳定的 not implemented/reserved 诊断；且
3. 不影响 v1 承诺语法的 check/run 一致性。
```

### 1.4 v1 complete 验收形态

v1 complete 最终验收必须是端到端证据，不是口头宣称：

```text
1. 语言矩阵测试：每个承诺语法至少一组正例、一组负例、一组 check/run 一致性用例。
2. 标准库矩阵测试：每个公开 builtin/method 覆盖成功路径、静态误用、关键运行期错误。
3. backend SDK 测试：安装版 SDK + CMake plugin + binder 类型矩阵 + backend diagnostic + resource compatibility。
4. package 测试：init/add/remove/update/build/check/run/test/publish/registry/cache/lock/conflict/backend artifact。
5. diagnostic 测试：source excerpt/caret、stack trace、conversion spans、parser recovery、unknown 去污染。
6. 文档 smoke：README/TUTORIAL/CODEX 的核心命令能在当前 CLI 下成立。
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
  README.md
  TUTORIAL_zh.md
  CODEX.md
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

不完整：friend/protected/nested type/default args/template method/完整 C++ overload ranking。

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
6. backend 不能掩盖语言核心问题；check/run 分裂必须在 core 修。
7. 不为了安全幻想牺牲 Abel 的 C/C++ 能力面；但明显静态错误必须诊断。
8. 每次大块推进要有测试覆盖正例、负例和 check/run 一致性。

---

## 11. 当前重点路线

v1 后续只围绕 tight complete 补闭环，不扩张到 v2+ 无底洞。优先级从高到低：

```text
1. v1 承诺语法矩阵审计：找出 parser-only / check-run 分裂 / 未测语法，逐块收口。
2. const/pointer/reference/value-category 的 v1 最小矩阵：const object、const ref、pointer-to-const、readonly propagation；不做 pointer arithmetic/lifetime proof。
3. backend v1 ABI 窗口收口：backend overload 第一片、binder 常用类型矩阵文档化/测试化、AbelValue escape hatch。
4. package 本地完整引擎收口：local/file registry、semver range、lock/cache/conflict/backend artifact 自动生成；不做 HTTP/network。
5. minimal template 收口：实现无约束最简 template；interface/require/new/delete 等若未实现，给稳定 reserved/not implemented 诊断。
6. diagnostics 收口：source span、stack、conversion span、parser recovery、unknown 去污染的矩阵测试。
7. 文档 smoke：README/TUTORIAL/CODEX 命令与当前 CLI 对齐。
```

用户明确要求“大块推进”时，不要陷入细枝末节验证；选一个能让 v1 complete 更真的大块，做完、测完、提交。

---

## 12. 文档边界

当前主文档：

```text
README.md        面向外部读者的简明项目说明。
TUTORIAL_zh.md   面向人类学习 Abel 和搭项目。
CODEX.md         放入 Abel 用户工程的 Codex 协作提示词。
AGENTS.md        本仓库 Agent 操作手册。
```

文档必须反映当前实现，不再保留 v0 远古进度流水账。需要历史可查时看 Git log。

---

## 13. 当前进度

```text
当前阶段：v1 complete 推进中。
最新已知提交：template: add generic struct and type aliases（具体哈希以 `git log --oneline -1` 为准）。
本轮代码任务：继续收口 v1 template 承诺；在函数模板第一片之上补无约束 template struct/type 第一片，含泛型类型解析、显式 struct 构造、template type alias 展开、字段/构造/方法按实例化类型检查与解释器运行。
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
无约束 template 第一片已落地：普通函数支持 `template <type T> fn ...`、显式实参、参数推导、`vector<T>`/`func` 模式推导、按实例化检查函数体；struct/type alias 支持 `template <type T> struct Box { ... }`、`Box<int>` 类型、`Box<int>(...)` 显式构造、字段/构造/方法按实例化绑定检查、`template <type T> type Alias = ...` 展开；解释器运行同构。
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
template v1 最简无约束函数/struct/type alias 第一片已落地；后续仍需矩阵审计跨模块/export、递归默认构造、重载/诊断边界；template+interface/require 不进 v1。
package 只要求本地/file registry 完整闭环；HTTP/network registry、全球索引、签名发布不进 v1。
diagnostic 需要矩阵验收；交互式 debugger/DAP 不进 v1。
标准库只要求常用本地程序 API 稳定；regex/locale/streaming/view/GUI 不进 v1。
```

提交记录不再手工复制长列表；需要历史请用：

```bash
git log --oneline
```
