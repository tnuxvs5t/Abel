# Abel Design Closure v1

状态：设计闭环补丁  
作用：补足《Abel Language Standard v1.1》和《Abel Engineering Design v1》尚未封口的承重语义。  
目标：不保守裁剪能力，而是把能力边界、语义契约、资源契约和机器骨架接口设计到足够清楚，使下一步可以开始搭 Abel Engine 骨架。

---

## 0. 闭环原则

Abel 不以最小语言为目标。Abel 的目标是工程能力：

```text
源码保存结构。
Engine 掌握语义。
Graph 保存可审计执行形态。
debt 暴露欠债。
concept 暴露硬边界。
split 让结构变细。
jit 让稳定结构后端化。
manifest/hash 约束上下文和资源状态。
audit 保存演化历史。
```

因此 v1 设计不缩掉 `template`、`interface`、`operator`、`ref`、`concept`、`backend`。但必须为它们补齐承重规则：

```text
对象模型
引用安全模型
const 模型
数组与字符串模型
函数与方法 receiver 模型
泛型实例化模型
interface 满足模型
operator 解析模型
backend 语义契约
node schema
hash canonicalization
patch / resource / audit 协议
diagnostic 与权限边界
```

---

## 1. Abel 语义权威分层

Abel 系统分为四层：

```text
Language Semantics
    源码语法、类型、求值、可见性、泛型、interface、operator、debt、concept 声明。

Engine Semantics
    parse、resolve、type check、node graph、tree-run、hash、diagnostic、backend dispatch。

Resource Semantics
    backend resource、binding、manifest、artifact、test、audit、permission。

Codex Semantics
    advice、split proposal、jit proposal、patch/resource generation。
```

硬规则：

```text
Language 不知道 Codex。
Language 只知道 concept/debt/backend boundary 的声明。
Engine 是语言合法性与执行语义权威。
Resource Layer 是外部能力接入权威。
Codex 只能提出或生成，不能绕过 Engine。
```

任何一次 split/jit 必须形成：

```text
request
proposal
validation
human approval
write/apply
recheck
test
manifest/hash update
audit
```

---

## 2. 对象与值模型

### 2.1 值类别

Abel 表达式求值结果分为：

```text
value       普通值
place       可定位位置
readonly    只读位置
refvalue    引用值
function    函数值
backend     后端绑定句柄，源码不可直接构造
```

`place` 可被赋值。以下表达式产生 `place`：

```text
局部变量
函数参数中非 const 的按值局部副本
inout 参数
结构体字段访问，若 receiver 可写
数组元素访问，若数组元素可写
*ref，若 ref 非 const
```

以下表达式不是 `place`：

```text
字面量
普通函数调用返回值
算术表达式
比较表达式
const 变量
const ref 解引用结果
const receiver 的字段
```

### 2.2 存储区域

Engine 运行时至少区分：

```text
stack frame object
heap object
global object
resource object
temporary object
```

数组、字符串、闭包、长期 ref 指向的对象必须有运行时对象身份。

### 2.3 移动与复制

v1 默认不引入 move-only 类型。赋值语义：

```text
基础类型：复制值。
str：复制不可变字符串句柄。
T[]：复制数组句柄，共享底层数组对象。
struct：默认字段级复制；若字段含数组/ref，则复制句柄/ref。
ref T：复制引用值。
func：复制闭包句柄。
```

标准库可以提供显式深拷贝：

```abel
clone(xs)
clone(obj)
```

Engine diagnostic 应提示隐式共享数组的危险场景。

---

## 3. const 模型

Abel v1 使用可实现的浅 const + 参数深读规则。

### 3.1 普通 const 变量

```abel
const i64 x = 1;
```

含义：变量绑定不可重新赋值。

### 3.2 const struct

```abel
const Counter c = Counter(0);
```

含义：

```text
c 不可重新赋值。
c 的字段不可通过 c 修改。
只能调用 const fn 方法。
```

### 3.3 const array

```abel
const i64[] xs = [1, 2, 3];
```

v1 规定：

```text
const T[] 变量不可重新绑定。
通过该变量访问到的元素是 readonly。
若同一数组对象还有非 const 句柄，非 const 句柄仍可修改底层数组。
```

因此 const 是访问路径属性，不是全局冻结。

### 3.4 const ref

```abel
const ref T
```

含义：通过该引用不可修改目标。

`const ref T` 可以指向可变对象，但该访问路径只读。

---

## 4. ref / inout 安全模型

Abel 不放弃长期 `ref T`。v1 采用运行时安全引用，而不是未定义行为。

### 4.1 ref 表示

`ref T` 在 Engine 运行时表示为：

```text
object_id
slot_path
mutability
generation
```

其中：

```text
object_id      指向运行时对象。
slot_path      字段 / 数组元素 / 整体对象路径。
mutability     mutable 或 readonly。
generation     用于检测对象或容器重分配失效。
```

### 4.2 引用创建

```abel
ref i64 r = ref x;
```

规则：

```text
只能对 place 或 readonly place 创建引用。
对 place 创建 ref T。
对 readonly place 创建 const ref T。
不能对 temporary 创建长期 ref，除非 temporary 被提升为 heap object。
```

### 4.3 栈逃逸

长期 `ref` 可以返回或存储，但 Engine 必须保证安全：

```text
若 ref 指向当前 stack frame object，且将逃逸当前 frame，则该对象自动提升为 heap object。
```

这称为 escape promotion。

示例：

```abel
fn ref i64 make_ref() {
    i64 x = 1;
    return ref x;
}
```

合法。Engine 将 `x` 提升为 heap object。

### 4.4 数组元素引用

数组元素引用携带数组 generation：

```abel
ref i64 r = ref xs[0];
push(xs, 10);
*r = 3;
```

若 `push` 导致底层元素存储重分配，旧元素 ref 失效。访问失效 ref 必须运行时报错，不允许 UB。

标准库应提供稳定容器类型用于长期元素引用。

### 4.5 inout

`inout T` 是非逃逸临时可写借用。

规则：

```text
inout 只能作为参数类型。
调用方必须显式写 inout。
inout 实参必须是 place。
inout 不可存储、不可返回、不可捕获进 lambda。
一次调用中，多个 inout 实参不可别名到同一可写位置。
inout 与 const ref 可同时别名，但函数体不能通过 const ref 修改。
```

别名检查：

```abel
swap_i64(inout x, inout x); // invalid
```

对复杂 place：

```abel
f(inout xs[i], inout xs[j])
```

若 Engine 无法静态证明 `i != j`，必须运行时检查或报错。v1 推荐运行时检查。

---

## 5. 函数、方法与 receiver

### 5.1 普通函数

```abel
fn R name(A a, B b) { ... }
```

默认按值传参。

### 5.2 方法 receiver

结构体内方法拥有隐式 receiver：

```text
非 const 方法：inout Self self
const fn 方法：const ref Self self
static fn：无 receiver
```

示例：

```abel
fn void inc() { value = value + 1; }
```

等价语义：

```text
fn void Counter::inc(inout Counter self)
```

字段裸名解析优先映射到 `self.field`。

### 5.3 方法调用

```abel
c.inc()
```

规则：

```text
若 inc 是非 const 方法，c 必须是 writable place。
若 inc 是 const fn，c 可以是 value/place/readonly。
```

对临时值调用非 const 方法：

```abel
Counter(0).inc()
```

v1 允许，但修改只作用于 temporary，除非方法返回该对象或其引用。工具应提示无效修改风险。

### 5.4 方法函数值

v1 区分 bound method 与 unbound method：

```abel
func void() f = c.inc;              // bound，捕获 receiver
func void(inout Counter) g = Counter::inc; // unbound
```

若实现复杂，Engine 可以先只允许 static fn 和普通 fn 转为函数值；但语言语义预留 bound/unbound。

---

## 6. lambda 与闭包

lambda 是运行时函数值，不参与 split/jit 的公开边界。

### 6.1 捕获规则

默认按值捕获：

```abel
i64 x = 1;
func i64() f = lambda i64() { return x; };
x = 2;
f(); // 1
```

若要修改外部对象，显式捕获 ref：

```abel
ref i64 rx = ref x;
func void() f = lambda void() { *rx = *rx + 1; };
```

### 6.2 lambda 限制

```text
lambda 不能声明为 debt/concept。
lambda 不作为 backend binding 目标。
lambda 可以作为普通函数内部实现细节。
lambda 捕获 inout 参数非法。
```

---

## 7. 类型与转换

### 7.1 显式 cast

补充语法：

```abel
cast<T>(expr)
```

规则：

```text
数值类型之间允许显式 cast。
bool 不允许与整数隐式互转。
ref T 与整数不可互转。
struct 之间无默认 cast。
```

### 7.2 隐式转换

v1 只允许极少隐式转换：

```text
字面量整数根据上下文确定 i64/u64/u32。
数组字面量根据上下文确定 T[]。
T place 可传给 const ref T 参数。
T place 可传给 inout T 参数，但调用处必须写 inout。
```

不允许：

```text
i64 -> f64 自动转换
u32 -> i64 自动转换
str -> char[] 自动转换
T -> bool 自动转换
```

---

## 8. 泛型 template 模型

Abel template 采用按需单态化。

### 8.1 模板实例化

当 Engine 遇到：

```abel
sum<BigNum>(xs, zero)
```

或可推导的：

```abel
sum(xs, zero)
```

生成：

```text
TemplateInstanceNode
```

包含：

```text
template_symbol
type_arguments
substitution_map
instantiated_signature
constraint_set
typed_body_hash
diagnostics
```

### 8.2 模板类型推导

v1 支持函数参数位置推导：

```abel
template <type T>
fn T identity(T x)
```

调用：

```abel
identity(1)
```

根据实参类型推导 `T = i64`。

不支持或后续支持：

```text
复杂返回类型反推
重载集合中的深度推导
值模板参数
偏特化
SFINAE
```

### 8.3 模板错误

模板错误必须定位到两处：

```text
template definition site
instantiation site
```

Diagnostic 必须显示：

```text
type arguments
failed requirement
operator/function lookup path
```

---

## 9. interface 满足模型

### 9.1 interface 是结构能力约束

```abel
template <type T>
interface Add {
    operator fn T +(const ref T a, const ref T b);
}
```

Interface 不生成运行时 vtable。

### 9.2 自动满足

类型 `T` 自动满足 interface，当且仅当：

```text
每个 required function/operator 都能在 T 的 owner scope 或 builtin scope 找到唯一匹配。
签名在替换 type 参数后完全匹配。
返回类型完全匹配。
参数类型允许 const ref 适配，但不允许任意隐式数值转换。
可见性满足调用方要求。
```

### 9.3 owner rule

类型 owner：

```text
基础类型 owner = builtin module
struct/enum owner = 定义它的 module
template instance owner = template 定义 module
```

第三方模块不能为非自己 owner 的类型定义新 operator。

允许第三方定义普通函数，但不能用它满足 operator interface，除非 interface 显式要求普通函数且该函数被导入。

### 9.4 interface cache

Engine 维护：

```text
InterfaceSatisfactionNode
```

字段：

```text
type
interface_instance
required_items
resolved_items
status
diagnostics
satisfaction_hash
```

---

## 10. operator 解析闭环

### 10.1 候选来源

对表达式：

```abel
a + b
```

候选来源按顺序：

```text
1. builtin operator，若 A/B 都是基础类型。
2. A owner module 的 operator。
3. B owner module 的 operator，若 B 与 A owner 不同。
4. 当前 struct 内部可见 operator。
5. 当前模板 require interface 引入的 required operator。
```

### 10.2 匹配规则

候选匹配条件：

```text
operator 符号相同。
参数数量相同。
实参类型与形参类型完全匹配，或 place/value 可适配到 const ref。
返回类型由上下文检查，不参与初始候选选择。
```

若多个候选同等匹配，报冲突。

### 10.3 赋值与比较

`=` 不是 overloadable operator。它是语句级赋值。

`==` 可以 overload。若类型未定义 `==`，不自动做字段比较。

### 10.4 `[]`

`[]` 作为后续 operator 支持时，语义分两类：

```text
index_get: operator fn R [](const ref T obj, I index)
index_set: operator fn void []=(inout T obj, I index, V value)
```

v1 可以先内建数组/字符串索引，再开放用户自定义 `[]`。

---

## 11. debt / concept / backend boundary

### 11.1 debt fn

`debt fn` 是已承认但未偿还的工程缺口。

```text
有签名。
无 Abel body。
可被引用。
出现在 graph。
运行路径触达时默认报错。
可被 split。
可被临时 backend binding 覆盖，但 audit 必须标记 unstable。
```

DebtFunctionNode 字段：

```text
symbol
signature
visibility
debt_reason
callers
expected_tests
owner_module
debt_hash
```

### 11.2 concept fn

`concept fn` 是稳定硬边界。

```text
有签名。
无 Abel body。
必须绑定到 concept hard library 或 backend resource。
运行前必须完成 binding。
binding 必须版本化、可测试、可审计。
```

ConceptFunctionNode 字段：

```text
symbol
signature
concept_kind
required_binding
semantic_contract
tests
version
```

### 11.3 backend block

```abel
backend Process {
    export concept fn ProcessResult run(str command);
}
```

backend block 只声明能力组，不声明实现。

Engine 将其转为：

```text
BackendCapabilityNode
ConceptFunctionNode[]
```

---

## 12. Backend Semantic Contract

所有 backend binding 必须声明语义契约。

### 12.1 contract 字段

```json
{
  "purity": "pure | reads_files | writes_files | network | process | unknown",
  "determinism": "deterministic | nondeterministic | time_dependent | external_state",
  "side_effects": ["filesystem", "network", "process", "stdout", "stderr"],
  "timeout_ms": 30000,
  "memory_limit_mb": 512,
  "error_model": "abel-result | exception-map | exit-code-map",
  "fallback": "tree-run | none | other-backend",
  "serialization": "abel-call-json-v1",
  "security_profile": "sandboxed | requires-approval | trusted-local",
  "abi": "abel-c-abi-v1 | json-v1 | msgpack-v1"
}
```

### 12.2 dispatch 前检查

Engine dispatch backend 前必须检查：

```text
signature_hash 匹配。
type_layout_hash 匹配。
contract 满足调用上下文。
权限满足 security_profile。
resource hash clean。
tests 至少曾在当前 binding hash 下通过，或用户允许 dirty run。
```

### 12.3 错误映射

Backend 返回必须映射为 Abel 运行时结果：

```text
normal return
diagnostic error
runtime error
permission error
timeout error
resource stale error
contract violation
```

不得让后端原始崩溃直接污染 Abel 语义。

---

## 13. Minimal Node Schema

所有节点共享基础 schema：

```json
{
  "node_id": "stable-node-id",
  "node_kind": "FunctionNode",
  "symbol_path": "math.bignum.BigNum::operator+",
  "source_file": "src/math/bignum.abel",
  "source_span": {"start": 120, "end": 300},
  "visibility": "public | private | module",
  "parent": "node_id",
  "children": ["node_id"],
  "dependencies": ["node_id"],
  "diagnostics": [],
  "hashes": {
    "source_hash": "...",
    "signature_hash": "...",
    "body_hash": "...",
    "typed_body_hash": "...",
    "dependency_hash": "..."
  }
}
```

### 13.1 FunctionNode

```json
{
  "node_kind": "FunctionNode",
  "signature": {
    "name": "add",
    "return_type": "i64",
    "params": [{"name": "a", "type": "i64"}, {"name": "b", "type": "i64"}]
  },
  "body": "AstNodeId",
  "generic_params": [],
  "requires": [],
  "call_kind": "normal"
}
```

### 13.2 MethodNode

```json
{
  "node_kind": "MethodNode",
  "receiver": "inout Self | const ref Self | none",
  "is_static": false,
  "is_const": false
}
```

### 13.3 TypeNode

```json
{
  "node_kind": "StructNode",
  "fields": [
    {"name": "neg", "type": "bool", "visibility": "public"},
    {"name": "limbs", "type": "u32[]", "visibility": "private"}
  ],
  "methods": [],
  "operators": [],
  "type_layout_hash": "..."
}
```

### 13.4 ResourceNode

```json
{
  "node_kind": "BackendResourceNode",
  "resource_path": ".abel/backend/bignum_mul/backend.json",
  "resource_hash": "...",
  "binding_nodes": [],
  "contract": {}
}
```

---

## 14. Stable identity 与 hash canonicalization

### 14.1 编码与换行

所有 Abel 源文件 canonical form：

```text
UTF-8
LF newline
无 BOM
文件末尾建议单 LF
```

Hash 计算前必须：

```text
CRLF -> LF
移除 UTF-8 BOM
保留普通空白
保留注释用于 file_hash/body_hash
```

### 14.2 语义 hash 与源码 hash

区分：

```text
file_hash          文件规范内容 hash，包含注释和格式。
body_hash          函数体源码切片 hash，包含源码形式。
typed_body_hash    类型解析后的语义结构 hash，不含无关格式。
signature_hash     规范签名 hash。
dependency_hash    依赖节点 id/hash 列表 hash。
layout_hash        类型布局 hash。
```

### 14.3 directory hash

```text
DirHash = hash(sorted(entries))
entry = child_name + child_kind + child_hash
```

忽略项由 `abel.json` 和内建规则共同决定。

内建忽略：

```text
.git
.abel/cache
.abel/backend/*/build
临时文件
编译产物
.abel.hash
.abel.manifest
```

### 14.4 manifest trust

`.abel.manifest` 不参与目录 hash，但 manifest 必须写入当前目录 hash：

```text
[abel-manifest]
version = 1
hash = ...
generated_by = abel manifest update
```

若 `.abel.hash` 匹配但 manifest 内 hash 不匹配，则 manifest stale。

---

## 15. Patch / Proposal 协议

Codex backend 不直接写文件。它产生 proposal。

### 15.1 SplitProposal

```json
{
  "kind": "split-proposal",
  "proposal_id": "...",
  "target_node": "...",
  "intent": "split large debt into helper functions",
  "public_api_change": "none | additive | breaking",
  "edits": [],
  "new_tests": [],
  "risks": [],
  "manifest_updates": [],
  "audit_note": ""
}
```

### 15.2 Edit 类型

v1 支持：

```text
replace_range
insert_before_node
insert_after_node
add_file
move_node
rename_symbol
```

每个 edit 必须有：

```text
target_file
old_file_hash
source_span 或 target_node
new_text 或 structured_node
reason
```

### 15.3 Apply 流程

```text
1. 检查 old_file_hash。
2. 应用到临时 workspace。
3. parse/check。
4. public API diff。
5. tests。
6. human approval。
7. 原子写回。
8. 更新 graph/hash/manifest/audit。
```

若任何一步失败，不得污染主工作区。

---

## 16. Diagnostics 标准

Diagnostic 必须结构化：

```json
{
  "severity": "error | warning | note",
  "code": "E0301",
  "message": "operator + not found for BigNum",
  "primary_span": {},
  "related_spans": [],
  "explanation": "",
  "suggestions": []
}
```

错误类别：

```text
E01xx parse
E02xx resolve
E03xx type
E04xx interface/operator
E05xx ref/inout safety
E06xx backend binding
E07xx manifest/hash
E08xx split/jit proposal
E09xx runtime
```

---

## 17. Permission model

Backend 和 Codex 操作必须声明权限：

```text
read_source
write_source
write_resource
execute_process
network
filesystem_read
filesystem_write
secret_access
```

默认：

```text
check/run/graph 不需要写源码。
split --advice_first 不写源码。
jit --advice_first 不写资源。
split --apply 需要 write_source。
jit --apply 需要 write_resource 和可能 execute_process。
service/http backend 需要 network 权限。
```

权限批准进入 audit。

---

## 18. 机器骨架可以开始的条件

当以下设计点被接受后，可以开始搭 Abel Engine skeleton：

```text
1. 源文件与模块模型明确。
2. AST 节点集合明确。
3. Minimal Node Schema 明确。
4. 值类别 / object / ref / const / inout 语义明确。
5. 函数、方法、receiver 语义明确。
6. template/interface/operator 解析模型明确。
7. debt/concept/backend boundary 明确。
8. backend semantic contract 明确。
9. hash canonicalization 明确。
10. patch/proposal/audit 协议明确。
```

本文件补齐上述 10 项。下一步可以开始机器骨架。

---

## 19. 推荐机器骨架目录

```text
abel/
    pyproject.toml 或 Cargo.toml
    src/
        abel/
            __init__.py
            cli.py
            lexer.py
            parser.py
            ast_nodes.py
            resolver.py
            types.py
            typechecker.py
            node_graph.py
            hasher.py
            manifest.py
            tree_run.py
            diagnostics.py
            backend_contract.py
            proposal.py
            audit.py
    tests/
        test_parser.py
        test_hash.py
        test_manifest.py
        test_typechecker_basic.py
        test_tree_run_basic.py
```

若目标是快速验证设计，推荐先用 Python 搭 skeleton；若目标是长期系统性能和分发，推荐 Rust。  
无论语言如何，第一台机器先实现：

```text
abel check
abel graph --json
abel manifest check
abel manifest update
```

然后再接：

```text
abel run
abel split --advice_first
abel jit --advice_first
```

---

## 20. 不再悬空的关键结论

```text
ref 不走 UB，走运行时安全引用 + escape promotion。
const 是访问路径属性，不是全局冻结。
数组是共享句柄，const array 通过该路径只读。
方法 receiver 是隐式 inout Self 或 const ref Self。
template 按需单态化。
interface 自动满足，但必须唯一、完全、可见。
operator 解析有固定候选顺序和冲突规则。
debt 是欠债符号，concept 是硬边界符号。
backend 必须声明 semantic contract。
Codex 只产 proposal，不直接拥有语义权威。
hash 有 canonical form，manifest 有 trust rule。
patch apply 必须临时区验证后原子写回。
audit 是所有演化动作的事实来源。
```

一句话：Abel 可以不保守，但不能语义悬空；本闭环补丁把大能力需要的承重齿轮先装上。
