# CODEX.md：Abel 工程内 Codex 系统提示词

你是 Codex，运行在 Abel 仓库中时必须以本文件作为项目内行为准则。  
你的目标不是炫技式重构，而是辅助人类稳定推进 Abel：让人类理解每次修改的模型、边界、验证和回滚点。

---

## 0. 第一纪律

进入仓库后，任何写入前必须执行：

```bash
pwd
git status --short
ls
```

如果不是 Git 仓库，停止并询问。  
如果工作树不干净，先确认哪些改动是用户的，不要覆盖。

所有源码、文档、配置创建/修改/删除必须通过显式 patch。  
禁止用以下方式写文件：

```text
cat > file
echo > file
tee
sed -i
perl -pi
任意绕过 patch 审查的重定向写入
```

每个实质任务必须：

```text
1. 从 clean tree 或确认过的工作树开始。
2. 形成一个逻辑 commit。
3. 尽可能运行相关 build/check/test。
4. 更新 AGENTS.md 末尾「工程进度 / 强制更新区」。
5. 不伪造测试结果。
```

---

## 1. 项目定位

Abel v0 只做三件事：

```text
1. 语言核心
2. backend 核心
3. Qt/C++ 插件资源节点核心
```

不要把 Abel 扩成：

```text
abel split
abel jit
大型 VM
大型 IDE
复杂包管理
manifest/hash 系统
Codex context exporter
AI 工程脚手架
```

除非用户明确改变目标，否则任何建议、补丁和测试都必须守住 v0 方向。

Abel 的语言哲学：

```text
C/C++ 值模型
+ Qt 字符串/字符
+ vector<T>
+ lambda / any / any...
+ backend block / Qt plugin
+ tree-run interpreter
```

Abel 不是 JS/Python/Kotlin 式去指针化语言，也不是 Rust 式所有权语言。  
保留 C/C++ 风险：空指针、悬挂引用、vector 扩容后引用失效、越界、未初始化读取等不做语言级兜底。

---

## 2. 工具链固定

默认使用：

```text
Qt:       /home/tnuzy/Qt/6.11.1/gcc_64
CMake:    /home/tnuzy/Qt/Tools/CMake/bin/cmake
Ninja:    /home/tnuzy/Qt/Tools/Ninja/ninja
GCC/G++:  /usr/bin/gcc /usr/bin/g++
C++:      C++23
```

标准配置：

```bash
/home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64 \
  -DCMAKE_C_COMPILER=/usr/bin/gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_CXX_STANDARD=23
```

标准构建：

```bash
/home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
```

测试必须卡 4GB 内存：

```bash
/bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
```

如果只是文档修改，可以不跑重型测试，但必须明确说明“未运行重型测试，原因是纯文档修改”。

---

## 3. 当前工程地图

```text
src/abelcore/lexer.*              词法
src/abelcore/parser.*             语法
src/abelcore/ast.*                AST
src/abelcore/type.*               类型表示
src/abelcore/typechecker.*        静态检查
src/abelcore/value.*              AbelValue
src/abelcore/runtime.*            storage/location/frame/runtime context
src/abelcore/interpreter.*        tree-run interpreter
src/abelcore/builtin_registry.*   内建函数/方法/operator 注册
src/abelcore/backend_*            backend 接口、registry、binder、plugin base
src/abelcore/resource_node.*      ResourceNode JSON 与 QPluginLoader
src/abelcli/main.cpp              CLI
plugins/examples/math_backend/    示例 Qt plugin
examples/smoke/                   smoke Abel 程序
tests/                            QTest
```

读代码顺序：

```text
1. AGENTS.md
2. README.md / TUTORIAL_zh.md
3. examples/smoke/*.abel
4. tests 中对应功能测试
5. 对应 src/abelcore 文件
```

不要凭文件名猜实现。先读局部上下文，再打 patch。

---

## 4. 修改闭环

任何语言功能修改必须按层推进：

```text
题意/需求压缩
→ v0 边界判断
→ AST/Token 是否需要改
→ Parser
→ TypeChecker
→ Runtime/Interpreter
→ BuiltinRegistry/BackendRegistry 如需要
→ CLI 如需要
→ QTest
→ smoke
→ AGENTS.md 更新
→ commit
```

不要只改 interpreter 让例子“能跑”。  
Abel 的稳定性依赖 parser、typechecker、runtime 三层咬合。

---

## 5. Parser 特别警戒

parser 曾出现错误恢复不前进导致内存爆炸的问题。  
改 parser 时必须检查：

```text
1. 每个循环在正常/错误路径都消费 token 或退出。
2. synchronize/recover 不会卡在同一个 token。
3. `::`、`< >`、`[]`、`()` 这类组合语法被完整消费。
4. 错误诊断后不会继续构造无限 AST。
5. 新语法有 parser test，至少包含一个错误输入。
```

运行 parser 相关测试也必须用 4GB 限额。

---

## 6. TypeChecker 原则

TypeChecker 必须维护：

```cpp
ExprType {
    Type type;
    ValueCategory category; // LValue or PRValue
    bool isMutable;
}
```

必须区分：

```text
lvalue:
  变量名
  引用变量名
  *p
  obj.field
  ptr->field
  vector[i]
  front/back
  返回 T& 的函数调用

prvalue:
  字面量
  算术/比较结果
  返回 T 的函数调用
  指针值
  lambda
  constructor
```

赋值规则：

```text
lhs 必须 mutable lvalue。
rhs 必须可复制/转换到 lhs type。
T& 初始化要求 T lvalue。
any 只在显式边界装箱。
cast<T>(any) 静态要求源为 any。
```

如果某个 feature 绕过 TypeChecker 直接在 runtime “碰运气”，应视为设计错误。

---

## 7. Runtime / Interpreter 原则

Abel runtime 用 storage/location 表达 C/C++ 值模型，但这只是解释器机器，不表示语言是对象引用语义。

保持：

```text
变量拥有存储。
赋值复制值。
引用绑定 location。
指针保存地址。
vector 元素有 location。
struct 字段有 location。
函数参数默认按值。
T& / T* 才能写回调用方。
```

控制流不要用 C++ exception 表达，继续使用：

```cpp
enum class FlowKind {
    Normal,
    Return,
    Break,
    Continue
};
```

不要引入大型 VM、bytecode、JIT 或复杂调度器来解决 v0 问题。

---

## 8. BuiltinRegistry 原则

内建能力必须尽量走 registry，不要散落在 parser/typechecker/interpreter 中硬编码。

优先保持：

```text
BuiltinRegistry
  registerFunction
  registerMethod
  registerOperator
```

当前重点能力：

```text
vector methods
to_str
build_string
print / println
str_to_chars / chars_to_str
cast / pipe 相关检查与执行
```

如果用户要求新增 `scan`、新 vector method、新 stringify 类型，先问：

```text
这是 parser 语法？
这是 builtin function？
这是 builtin method？
这是 typechecker 特例？
这是 runtime callback？
```

能放 registry 的，不要塞进 parser。

---

## 9. Backend / ResourceNode 原则

backend 三层必须一致：

```text
Abel backend block:
  backend MathSystem { fn int fast_add(int a, int b); }

C++ plugin:
  bind("MathSystem.fast_add", ...)

resource.json:
  "backendId": "MathSystem"
  "symbols": ["MathSystem.fast_add"]
```

加载路径：

```text
ResourceNode JSON
→ QPluginLoader
→ IAbelBackend
→ BackendRegistry
→ Interpreter backend call
```

插件必须链接同一个 `libabelcore.so`。  
不要把核心类型静态复制进 plugin。

新增 backend 类型映射时：

```text
1. 改 backend_binder.h。
2. 增加 plugin 示例或测试 plugin。
3. 增加 backend/resource QTest。
4. 确认引用参数能写回。
```

---

## 10. CLI 原则

v0 CLI 只做：

```text
abel check <file>
abel run <file>
abel resources check <resource.json>
abel run --resource <resource.json> <file>
abel version
```

不要新增：

```text
abel split
abel jit
abel manifest
abel graph
abel codex context
```

除非用户明确重开产品边界。

---

## 11. 测试策略

优先窄测试：

```text
lexer/parser 改动       → parser tests
typechecker 改动        → typechecker tests
runtime/interpreter 改动 → interpreter tests
builtin 改动            → builtin + typechecker + interpreter tests
backend 改动            → backend/resource tests + CLI resource smoke
CLI 改动                → smoke command
```

最终或较大改动再跑：

```bash
/bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
```

严禁说“测试通过”但没有运行。  
如果没有运行，说清楚：

```text
未运行测试；本次仅文档变更。
```

---

## 12. Commit / GitHub 原则

每个逻辑任务一个 commit。  
提交信息用简洁英文：

```text
docs: add Abel tutorial and Codex guide
builtins: add scan registry entry
parser: add module declaration parsing
backend: extend binder type support
```

提交前：

```bash
git diff --check
git status --short
```

推送前确认：

```bash
git remote -v
git log -1 --oneline --decorate
```

仓库当前 public URL：

```text
https://github.com/tnuxvs5t/Abel
```

许可证是 proprietary/all-rights-reserved。  
不要把 README、教程或回复写成“开源项目”。正确说法是：

```text
public but not open source
source-visible proprietary repository
```

---

## 13. 与人类协作方式

默认不要长时间停在细枝末节验证。用户已经明确偏好：

```text
先搞大块推进。
```

但“大块推进”不等于乱改。正确节奏：

```text
1. 先给 1-3 句当前计划。
2. 读必要文件。
3. 用 patch 做集中修改。
4. 跑最相关验证。
5. 更新 AGENTS.md。
6. commit。
7. 简短总结 commit、验证、剩余风险。
```

遇到不确定：

```text
先找最小稳定对象：一个测试、一个样例、一个不变量、一个诊断。
不要继续套大模板。
```

用户语气急时，不要防御，不要解释过多，直接收敛到行动和结果。

---

## 14. 输出模板

完成任务后优先输出：

```text
完成：
- 改了什么
- commit hash
- 是否 push

验证：
- 运行了什么
- 结果是什么

注意：
- 未跑什么
- 剩余风险
```

调试任务输出：

```text
问题：
原因：
最小反例：
修复：
验证：
剩余风险：
```

不要泄露隐藏推理流水账。展示可接管的结构、证据和下一步。

---

## 15. 当前 v0 后续大块

如果用户说“继续推进 v0 后续”，优先从这些大块选：

```text
1. scan
2. 完整 const 指针/引用矩阵
3. backend binder 类型矩阵
4. struct private/public 与高级项
5. 用户自定义 operator
6. module/use/export
```

每一块都必须明确：

```text
v0 当前边界
新增语义
语法变化
静态检查
运行时行为
测试矩阵
不做什么
```

这份 CODEX.md 的核心要求：  
**先守住 Abel 的值模型和工程边界，再用 AI 放大实现速度。**
