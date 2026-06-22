# Abel Engineering Design v1

状态：工程设计草案  
范围：定义 Abel Engine、CLI、Codex backend、split/jit 命令、后端资源、manifest/hash 纪律。  
不定义：Abel 语言语法细节。语言语法见《Abel Language Standard v1.1》。

---

## 0. 工程设计总目标

Abel v1 不先造大型 IDE。Abel v1 不先造大型 VM。Abel v1 先实现：

```text
Abel Engine
Hashed Typed Node Graph
Tree-run Engine
Codex-backed split/jit
Ubuntu CLI
Manifest/hash discipline
Backend Resource Layer
```

核心命令：

```bash
abel split
abel jit
```

这两个命令构成 Abel 的工程循环：

```text
split：把 debt / 大黑箱拆碎，暴露结构。
jit：把稳定函数 / operator / struct / 子树黑箱化，后端化。
```

二者都不是 Abel 源码语法。二者都是 CLI / 工程层命令。二者都必须使用 Codex backend。

---

## 1. 总体架构

```text
Abel Source
    ↓
Abel Engine
    - parse
    - resolve
    - type check
    - build Hashed Typed Node Graph
    - tree-run
    - backend dispatch
    ↓
Codex Backend Layer
    - advice
    - split proposal
    - patch generation
    - backend generation
    - test generation
    - manifest update
    ↓
Resource Layer
    - backend resource
    - binding resource
    - generated artifact
    - audit record
    - manifest/hash cache
```

原则：

```text
Abel Engine 不装聪明。
Abel Engine 负责结构、类型、节点、hash、边界、检查、运行。
Codex backend 负责复杂工程动作。
Abel Engine 是合法性权威。
Codex backend 是工程执行者。
```

---

## 2. 不造大 VM

Abel v1 不以 bytecode VM 为核心。

正确路线：

```text
Source
    ↓
AST
    ↓
Resolved AST
    ↓
Hashed Typed Node Graph
    ↓
Specialized tree-run
    ↓
optional backend fast path
```

错误路线：

```text
Source
    ↓
AST
    ↓
大型复杂 IR
    ↓
bytecode VM
    ↓
JIT
```

Abel 的核心价值是工程结构控制，不是第一阶段性能极限。因此源码结构、执行结构、审计结构、后端入口结构必须保持接近。

---

## 3. Hashed Typed Node Graph

Abel Engine 的核心内部形态是 Hashed Typed Node Graph。它保留 AST 的树结构，同时附加：

```text
已解析符号
已确定类型
模板实例
interface 约束
operator 解析结果
debt / concept 标记
backend binding key
稳定 hash
轻量执行特化信息
```

它不是独立 VM IR。它是 AST 的可执行、可审计、可缓存、可后端化形态。

---

## 4. Node 分类

Abel 内部统一看作节点图。主节点类型：

```text
AST Node
Resource Node
```

AST Node：

```text
ModuleNode
SourceFileNode
StructNode
EnumNode
TypeAliasNode
TemplateNode
InterfaceNode
FunctionNode
MethodNode
OperatorNode
DebtFunctionNode
ConceptFunctionNode
DebtOperatorNode
ConceptOperatorNode
StatementNode
ExpressionNode
CallNode
OperatorCallNode
```

Resource Node：

```text
BackendResourceNode
BackendBindingNode
CodexTaskNode
CodexAdviceNode
CodexPatchNode
GeneratedArtifactNode
TestNode
AuditNode
ManifestNode
HashNode
CacheNode
```

原则：

```text
一切交给后端的对象都是 AST Node 或 Resource Node。
后端不面对无结构文本。
Codex 不面对盲目的文件夹。
```

---

## 5. Hash 体系

Abel Engine 为重要节点计算稳定 hash。

建议 hash 类型：

```text
file_hash
directory_hash
module_hash
symbol_hash
signature_hash
body_hash
typed_body_hash
dependency_hash
resource_hash
```

用途：

```text
增量检查
Codex 上下文缓存
split 前后映射
jit 后端绑定校验
AI patch 审计
public API 变化检测
backend resource 失效检测
```

例子：

```text
FunctionNode math.add
    signature_hash = hash("fn i64 add(i64, i64)")
    body_hash = hash("return a + b")
    typed_body_hash = hash("return builtin_i64_add(a:i64, b:i64)")
    dependency_hash = hash([builtin_i64_add])
```

---

## 6. Typed AST 特化

Typed AST 节点可以被轻量特化，缩短 AST Node 与后端的距离。

```text
BinaryOpNode(+)
    type = i64
    resolved_operator = builtin_i64_add
```

可特化为：

```text
BinaryAddI64Node
```

operator 调用：

```text
OperatorCallNode
    operator = "+"
    lhs_type = BigNum
    rhs_type = BigNum
    resolved_operator = BigNum::operator+
```

concept 调用：

```text
CallNode
    call_kind = concept
    backend_binding_key = ...
```

---

## 7. Tree-run Engine

Tree-run Engine 直接执行 Hashed Typed Node Graph 中的 AST 节点。

基础接口：

```text
run_stmt(StmtNode)
eval_expr(ExprNode)
call_function(FunctionNode)
call_method(MethodNode)
call_operator(OperatorNode)
dispatch_concept(ConceptFunctionNode)
dispatch_backend(BackendBindingNode)
```

tree-run 的价值：实现简单、错误定位接近源码、适合调试、适合审计、适合 Codex patch 校验、适合早期 Abel。后端 fast path 可以消费局部节点，但不替代 tree-run 的基准语义。

---

## 8. Abel CLI

最小命令：

```bash
abel check
abel run
abel graph
abel split
abel jit
```

### 8.1 check

```bash
abel check
```

执行解析源码、构建 AST、名称解析、类型检查、operator 解析、interface 约束检查、生成 Hashed Typed Node Graph、检查 manifest/hash 状态、报告 diagnostics。

### 8.2 run

```bash
abel run
abel run <entry>
```

使用 tree-run engine 执行入口。若遇到 backend binding，可分发到对应 backend resource。若遇到未偿还 debt，必须报错。

### 8.3 graph

```bash
abel graph
abel graph --node math.bignum.BigNum
abel graph --node math.bignum.BigNum::operator*
abel graph --json
```

展示节点图、hash、依赖、backend binding、debt 状态。

### 8.4 split

```bash
abel split <node>
abel split <node> --advice_first
abel split <node> --apply <proposal_id>
```

### 8.5 jit

```bash
abel jit <node>
abel jit <node> --advice_first
abel jit <node> --backend dylib
abel jit <node> --backend bash
abel jit <node> --backend stdio-json
abel jit <node> --backend port
```

---

## 9. Codex backend 是上层原生资源

Abel split/jit 必须使用 Codex backend。Codex backend 不是可有可无的外部脚本，而是 Abel Tooling Layer 的一等资源。

Abel 工程层必须原生支持：

```text
CodexBackendResource
CodexTaskNode
CodexAdviceNode
CodexPatchNode
CodexGeneratedArtifactNode
CodexAuditNode
```

原则：Abel Engine 提供精确节点，Codex backend 处理复杂工程任务，Abel Engine 验证结果，用户批准关键写入。

---

## 10. --advice_first

`--advice_first` 是 `split` 和 `jit` 的重要选项。

### 10.1 split --advice_first

```bash
abel split build_project_graph --advice_first
```

行为：只要求 Codex backend 给拆分建议，不直接写源码，不直接生成最终 patch。输出候选结构、风险、推荐节点、边界说明。

输出应包括目标节点、当前签名、建议拆分节点、每个节点类型、风险、需要测试、是否会改变 public API。

### 10.2 jit --advice_first

```bash
abel jit math.bignum.BigNum::operator* --advice_first
```

行为：只要求 Codex backend 判断是否值得后端化，不生成后端资源，不修改源码，不绑定 backend。

输出应包括目标节点、是否适合 jit、推荐 backend form、是否保留 fallback、需要的测试、ABI 风险、数据布局风险、public API 风险。

---

## 11. abel split

`split` 是结构暴露命令。

输入：

```text
DebtFunctionNode
FunctionNode
MethodNode
OperatorNode
LargeSubtreeNode
```

目标：把大 debt / 大黑箱拆成更小的 Abel 节点，暴露工程结构，让 AI 与人类有更小抓手。

流程：

```text
1. Engine 定位目标节点。
2. Engine 构造 SplitRequest。
3. Codex backend 生成 advice 或 patch。
4. Engine 验证 patch 是合法 Abel。
5. Engine 检查 public API 边界。
6. 用户批准。
7. 写回源码。
8. 更新 node graph。
9. 更新 .abel.manifest 与 .abel.hash。
10. 写入 audit。
```

SplitRequest 应包含：

```text
target_node
source_slice
signature
local symbol graph
type graph
callers
callees
debt context
public API boundary
tests
directory manifests
project rules
```

---

## 12. abel jit

`jit` 是后端化命令。

输入：

```text
FunctionNode
MethodNode
OperatorNode
StructNode
TemplateInstanceNode
HotSubtreeNode
```

目标：把稳定 Abel 结构主动黑箱化，生成 backend resource，建立 backend binding，保留或替换 Abel fallback。

流程：

```text
1. Engine 定位目标节点。
2. Engine 提取签名、类型布局、依赖、hash、测试。
3. Engine 构造 JitRequest。
4. Codex backend 生成 advice 或后端实现。
5. 构建 backend resource。
6. 运行测试。
7. 生成 BackendBindingNode。
8. 用户批准。
9. 写入 backend manifest。
10. 更新 manifest/hash。
11. 写入 audit。
```

JitRequest 应包含：

```text
target_node
node kind
signature
type layout
operator/interface constraints
body hash
typed body hash
dependency graph
source fallback
tests
backend preference
ABI constraints
resource constraints
```

---

## 13. split 与 jit 的关系

```text
split exposes structure.
jit hides stable structure behind backend boundary.
```

中文：

```text
split 暴露结构。
jit 后端化结构。
```

工程循环：

```text
大 debt / 大黑箱
    ↓ split
小 fn / 小 debt / 小 concept / 小 operator
    ↓ 人类修正 + Codex 偿还
稳定 Abel 实现
    ↓ jit
BackendResourceNode / Concept hard boundary
    ↓ audit + tests + binding
可复用后端能力
```

---

## 14. Backend forms

Abel 后端必须为多种实现形式预留空间。

后端形式至少包括：

```text
dylib backend
bash backend
stdio-json backend
port/socket backend
process backend
service/http backend
native-tree backend
AI-generated backend
```

它们都通过 `BackendResourceNode` 和 `BackendBindingNode` 接入 Abel。Abel 源码不写具体后端细节。

---

## 15. dylib backend

适合稳定高性能函数/operator/struct 核心实现。

输出：

```text
.so 动态库
ABI symbol
backend manifest
tests
audit
```

示例 manifest：

```json
{
  "kind": "dylib",
  "node": "math.bignum.BigNum::operator*",
  "signature_hash": "...",
  "body_hash": "...",
  "library": ".abel/backend/bignum_mul/libbignum_mul.so",
  "symbol": "abel_bignum_mul",
  "abi": "abel-c-abi-v1",
  "fallback": "tree-run"
}
```

---

## 16. bash backend

适合 Linux glue、构建任务、小型系统操作。协议必须明确，不允许随意字符串拼接。

推荐协议：

```text
stdin:  JSON encoded arguments
stdout: JSON encoded return value
stderr: diagnostics
exit:   backend status
```

示例 manifest：

```json
{
  "kind": "bash",
  "node": "build.package",
  "script": ".abel/backend/build_package/run.sh",
  "protocol": "json-stdin-stdout",
  "timeout_ms": 30000,
  "fallback": "none"
}
```

---

## 17. stdio-json backend

适合 AI 生成的小工具、Python/C++/Go/Rust 进程包装。

```json
{
  "kind": "stdio-json",
  "node": "tool.classifier",
  "command": ["python3", ".abel/backend/tool_classifier/main.py"],
  "request": "abel-call-json-v1",
  "response": "abel-return-json-v1"
}
```

---

## 18. port/socket backend

适合长期运行服务，例如模型、数据库、复杂 worker。

```json
{
  "kind": "port",
  "node": "parser.service.parse",
  "protocol": "abel-msgpack-v1",
  "host": "127.0.0.1",
  "port": 49321,
  "startup": "./.abel/backend/parser_service/start.sh",
  "fallback": "tree-run"
}
```

---

## 19. AI-generated backend

AI-generated backend 不直接进入 Abel 源码。它进入资源层：

```text
GeneratedArtifactNode
BackendResourceNode
BackendBindingNode
TestNode
AuditNode
```

流程：Engine 提取精确节点上下文，Codex backend 生成实现，工具构建资源，工具运行测试，Engine 验证签名和绑定，用户批准，写入 manifest/audit。

---

## 20. BackendAdapter 接口

所有后端适配器应遵守统一接口：

```text
BackendAdapter:
    can_handle(node, request)
    prepare(node, context)
    generate_or_bind(node, context)
    build(resource)
    test(resource)
    emit_manifest(resource)
```

可能的适配器：

```text
DylibBackendAdapter
BashBackendAdapter
StdioJsonBackendAdapter
PortBackendAdapter
ProcessBackendAdapter
CodexBackendAdapter
```

CodexBackendAdapter 可以调用 AI。其他 adapter 负责具体构建和绑定。

---

## 21. 项目目录

推荐目录结构：

```text
project/
    abel.json
    src/
        main.abel
        math/
            bignum.abel

    .abel/
        graph/
            nodes.json
            hashes.json

        split/
            proposals/
            patches/

        backend/
            bignum_mul/
                backend.json
                source/
                build.sh
                libbignum_mul.so
                tests.json

        audit/
            events.jsonl
```

`abel.json` 描述工程配置：入口模块、源码目录、库路径、backend resource 目录、Codex backend 配置、测试配置、审计目录。

---

## 22. Abel Codex Manifest Discipline

Abel's Codex 必须遵守 manifest/hash 目录纪律。

目的：避免盲目递归读工程，避免重复消耗上下文，避免忘记库结构，保证 Codex 对目录的理解可缓存、可验证、可更新。

每个重要目录可以包含：

```text
.abel.manifest
.abel.hash
```

---

## 23. .abel.hash

`.abel.hash` 是目录身份。

推荐 Merkle 风格目录 hash：

```text
FileHash = hash(canonical_file_content)
DirHash = hash(sorted(child_name, child_kind, child_hash))
```

忽略项：

```text
.abel.hash
.abel.manifest
.git
.abel/cache
.abel/backend/build
临时文件
编译产物
```

`.abel.hash` 示例：

```text
abel-hash-v1 sha256 9f1c7a6e...
```

---

## 24. .abel.manifest

`.abel.manifest` 是给 Codex 和工具共同使用的短摘要。它必须短、硬、结构化、可被程序粗解析、可被 Codex 快速理解。它不是 README，不是长文档，不是聊天记录。

推荐格式：

```text
[abel-manifest]
version = 1
scope = directory
hash = 9f1c7a6e...

[purpose]
This directory implements integer arithmetic and numeric interfaces.

[public_api]
- struct BigNum
- operator BigNum +(BigNum, BigNum)
- operator BigNum *(BigNum, BigNum)

[key_nodes]
- Struct BigNum = sign flag plus base-1e9 limb array.
- operator+ = sign-aware addition.
- operator* = debt/backend candidate.

[dependencies]
- std.array
- std.string
- numeric.interfaces

[debt]
- BigNum::operator* = not hardened.

[backend]
- none

[hazards]
- Do not change limb base without updating all operators and tests.
- Normalized zero must have neg = false.

[tests]
- tests/bignum_basic.abel
- tests/bignum_operator.abel
```

---

## 25. Manifest-first traversal

Codex 进入目录时必须执行：

```text
1. 计算当前目录 hash。
2. 若存在 .abel.hash 且匹配：
       先读 .abel.manifest。
       不盲目递归源码。
       只在任务明确需要时读取相关文件。
3. 若 .abel.hash 缺失或不匹配：
       递归检查子项。
       汇总结构。
       重写 .abel.manifest。
       重写 .abel.hash。
```

这称为 Manifest-first traversal。

---

## 26. 程序级强制

Manifest discipline 不能只靠 prompt。Abel CLI 必须程序级约束。

建议命令：

```bash
abel manifest check
abel manifest update <dir>
abel codex context <node-or-dir>
abel codex changed <dir>
```

Codex backend 不应该直接无限制 `ls -R` 和 `cat *`。它应该通过 Abel 工具获取上下文包。

---

## 27. Codex system prompt 纪律

Codex backend 的 system prompt 必须包含根规则：

```text
Before recursively reading a directory, check Abel manifest/hash status.

If .abel.hash matches the current directory hash, read .abel.manifest first.

Do not recursively scan a hash-clean directory unless the current task explicitly requires source-level inspection.

If .abel.hash is missing or stale, inspect changed children recursively, then update .abel.manifest and .abel.hash before finishing.

Never leave a modified directory with stale .abel.manifest or stale .abel.hash.

When producing split/jit patches, update manifests and hashes for every affected directory.
```

中文版根纪律：

```text
进入目录时，先验 hash。
hash 相同，先读 manifest，不许盲目递归。
hash 不同，递归下探，读完重写 manifest 和 hash。
改动目录后，必须更新该目录及祖先目录 hash。
完成 split/jit 后，相关 manifest 不得过期。
```

---

## 28. split/jit 与 manifest/hash

`abel split` 会修改 AST 节点和源码结构，所以必须更新被改文件所在目录、相关父目录、node graph、`.abel.manifest`、`.abel.hash`、audit log。

`abel jit` 可能不改源码，但会修改资源图，所以必须更新 `.abel/backend/...`、backend manifest、backend binding、resource hash、resource manifest、audit log。

规则：

```text
split 改 AST node。
jit 改 Resource node。
二者都必须维护 manifest/hash clean 状态。
```

---

## 29. hash 相同不代表禁止读源码

规则是：hash 相同，manifest 是第一入口；源码阅读必须按任务需要精确打开。

例如执行：

```bash
abel jit math.bignum.BigNum::operator*
```

即使目录 hash 匹配，也需要读取目标 operator、BigNum 类型定义、相关 helper、相关 tests，但不需要读整个项目。

---

## 30. Codex context package

给 Codex backend 的上下文应是压缩而精确的包，而不是整项目文本。

SplitContext：

```text
target node
source slice
typed signature
symbol graph
caller/callee summary
interface/operator requirements
directory manifests
tests
public boundary
hazards
```

JitContext：

```text
target node
typed signature
type layout
body hash
dependency hash
operator/interface constraints
source fallback
available tests
backend preference
ABI constraints
directory manifests
hazards
```

Codex backend 输出：

```text
advice
patch
generated backend source
tests
manifest update proposal
audit note
```

---

## 31. 审计

所有 Codex-backed 操作必须产生 audit record。

AuditNode 至少包含：

```text
operation: split / jit
target_node
old_hash
new_hash
Codex task id
Codex model/config
advice id
patch id
test result
human approval status
timestamp
affected directories
manifest/hash updated status
```

审计记录写入：

```text
.abel/audit/events.jsonl
```

---

## 32. 安全与边界

Codex backend 不拥有语言语义权威。Codex backend 不能绕过 Engine 检查，不能擅自改变 public API，不能留下过期 manifest/hash，不能直接把未验证代码作为 concept hard function 绑定。

Engine 必须验证：

```text
语法合法
类型合法
符号合法
interface/operator 约束满足
public API 边界未被非法改变
backend signature 匹配
tests 通过
manifest/hash clean
```

---

## 33. 第一阶段实现目标

第一阶段不要做 GUI IDE。第一阶段目标：

```text
1. Abel parser
2. 基础 AST
3. 简单类型检查
4. Hashed Typed Node Graph
5. tree-run engine
6. .abel.hash / .abel.manifest 工具
7. Codex backend context packaging
8. abel split --advice_first
9. abel jit --advice_first
10. 最小 backend manifest
```

第二阶段：

```text
1. split patch apply
2. jit dylib backend
3. jit bash backend
4. tests/audit 自动化
5. concept binding
6. operator/interface 完整检查
```

第三阶段：

```text
1. port/socket backend
2. stdio-json backend
3. struct-level jit
4. template instance jit
5. 更强 graph query
6. GUI IDE 或 TUI
```

---

## 34. 最终原则

```text
Abel 源码保存结构。
Abel Engine 维护节点。
Codex backend 执行复杂工程动作。
split 暴露结构。
jit 后端化结构。
manifest/hash 约束 Codex 阅读纪律。
Resource node 承载后端。
Tree-run 是语义基准。
后端是优化与硬化，不是语言本体。
```

一句话：Abel v1 的核心不是 IDE，也不是 VM，而是 AST Engine + Codex-backed split/jit + Manifest Discipline + Backend Resource Graph。
