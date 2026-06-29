# Abel

[English](README.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja.md)

**Abel 是一门 AI 辅助的本地工程语言。**

AI 构建 backend 驱动的模块。  
用户通过干净的 C-like `.abel` 前端组合这些能力。

Abel 并不试图直接替代 C++、Python、Lua 或 Bash。  
它面向的是一种新的编程分工：

```text
C++ / native backend  →  Abel module interface  →  user-written .abel frontend
```

Abel 的目标很简单：

> 让 AI 处理工程复杂度，让人类编写简短、可读、可组合的 Abel 程序。

---

## 当前状态

**当前版本：Abel v1.2**

v1.2 已完整实现并发布。  
内部全量测试通过。

v1.2 引入了 Abel 的 **Dynamic Waterworks** 层：

```abel
any tuple = [[1, "text", true]];

any object = [{"name" = "Abel", "version" = 12, "released" = true}];

any next = object["version"] |> _ + 1;
```

这使 Abel 获得动态组合能力，同时不污染静态核心类型系统。

当前代码树也已经闭环 v1.3 核心增量：`do { ... }` 表达式。
它是立即执行的局部表达式块，适合在 pipe RHS 中展开多步逻辑：

```abel
any out = req |> do {
    any body = _["body"];
    int timeout = cast<int>(body["timeout"]);
    return [{"timeout" = timeout, "body" = body}];
};
```

`do` 内的 `return` 只返回 do 表达式结果，不返回外层函数。

---

## Abel 是什么

Abel 是一门面向本地 native 工程的紧凑 C-like 语言。

它围绕五个设计原则展开：

```text
1. C-like 可读性
2. 小型静态核心
3. 显式动态边界
4. backend-first 扩展模式
5. AI 辅助模块工程
```

一个典型 Abel 项目并不完全由用户手写。

更合理的分工是：

```text
AI：
  - 编写 native backend
  - 绑定外部库
  - 生成 Abel module
  - 维护 package/resource 元数据
  - 编写测试
  - 处理重复性工程工作

用户：
  - 阅读 module 接口
  - 编写 .abel 前端逻辑
  - 组合 backend 能力
  - 快速构造本地工具
```

Abel 把 AI 生成的 backend 代码，压缩成人类可读、可控、可组合的项目接口。

---

## 为什么使用 Abel？

### 1. Abel 保留 C/C++ 的结构感，但移除日常摩擦

C++ 很强，但对于小型本地工具、胶水逻辑、自动化脚本和项目前端来说过于沉重。

Abel 保留熟悉的 C-like 心智模型：

```abel
fn int add(int a, int b) {
    return a + b;
}

fn int main() {
    int x = add(1, 2);
    println(x);
    return 0;
}
```

但 Abel 不要求每一个小程序都进入完整 C++ 工程复杂度。

---

### 2. Abel 是动态的，但不是动态混乱的

Python 和 JavaScript 是全局动态语言。

Abel 不同。

普通 Abel 代码仍然是结构化、可检查的。  
动态行为只通过显式边界进入：

```abel
any tuple = [[1, "hello", true]];

any object = [{"name" = "Nitori", "score" = 42}];

str name = cast(object["name"]);
int score = cast(object["score"]);
```

动态层是可见的。  
它是一条水路，不是到处漏水。

---

### 3. Abel 是 backend-first 的

Abel 不试图把所有重能力都塞进语言本体。

重活应该交给 backend：

```text
图像处理
音频处理
文件系统工具
数学引擎
native UI 工具
编译辅助工具
外部命令封装
高性能 C++ 代码
```

Abel 提供前端组合层：

```abel
fn int main() {
    any files = fs::scan("input");

    files
        |> image::resize_all(_, 1024)
        |> fs::write_all("output", _);

    return 0;
}
```

用户编写可读的流程逻辑。  
backend 处理真正的机器复杂度。

---

## Abel 编程模型

Abel 项目围绕一个简单分层展开：

```text
Backend layer：
  native 代码、外部库、重计算、系统集成。

Module layer：
  面向 Abel 的声明、稳定接口、资源、package 元数据。

Frontend layer：
  用户编写的 .abel 文件，用于组合 module 并构造实际工具。
```

Abel 的核心工作流是：

```text
1. 描述你需要的能力。
2. AI 编写或更新 backend/module。
3. Abel 暴露干净接口。
4. 用户编写 .abel 前端。
5. 运行、测试、迭代。
```

因此，Abel 不只是一门语言。  
它是一种 AI 辅助本地工程协议。

---

## Dynamic Waterworks

v1.2 引入 Abel 的动态组合层。

### Dynamic tuple

```abel
any row = [[1, "Abel", true]];
```

`[[...]]` 创建一个动态 tuple-like 值。  
它被故意设计得不同于普通静态结构。

### Dynamic string map

```abel
any meta = [{"name" = "Abel", "version" = 12, "ok" = true}];
```

`[{"key" = value}]` 创建一个动态 string-keyed map-like 值。

这个语法是有意设计的。  
它绕开 parser 歧义，并明确标记动态边界。

### Pipe with hole

```abel
any result =
    meta
    |> _["version"]
    |> _ + 1;
```

`_` 表示在管道中流动的当前值。

这让 Abel 能进行紧凑的数据转换，同时不会把整门语言变成动态脚本泥潭。

### `do` expression

```abel
any projected = meta |> do {
    int version = cast<int>(_["version"]);
    return [{"next" = version + 1, "raw" = _}];
};
```

`do` 是立即执行的表达式块，有自己的局部作用域。
当它位于 pipe RHS 时，可以直接使用当前 `_` pipe context，因此复杂水路可以显式展开，而不需要新增 pipe operator。

---

## 设计哲学

Abel 遵循几条硬规则。

### 保持静态核心小而清楚

核心语言应该保持可理解。

Abel 不应该变成一个缩小版 C++ template 系统。

### 把复杂度放到 module 后面

如果一个功能需要复杂 runtime、native 库、大型依赖或高性能逻辑，它通常应该位于 backend/module 边界之后。

### 让动态行为可见

动态数据很有用。  
隐藏的动态行为很危险。

Abel 使用 `any`、动态 literal、indexing、casting 和 pipe 语法，让动态流动显式出现。

### 面向人类 + AI 协作优化

Abel 代码应该容易被人类阅读，也容易被 AI agent 安全修改。

这意味着：

```text
小语法表面
清晰 module 边界
稳定 package 布局
强诊断
显式 backend 接口
可测试行为
```

---

## Abel 适合什么

Abel 适合：

```text
本地自动化
个人工程工具
native backend 前端
文件处理
媒体处理
编译辅助
项目脚手架
资源流水线
AI 生成模块
C++/Qt backend 编排
小到中型本地应用
```

一个好的 Abel 项目应该是这样：

```text
C++ 做重活。
AI 写桥接。
Abel 暴露接口。
用户写前端。
```

---

## Abel 不是什么

Abel 不是：

```text
C++ 替代品
Rust 替代品
Python 生态替代品
浏览器前端语言
大型企业级语言
纯学术语言
template 元编程 playground
最大化静态类型理论实验
```

Abel 的强项不是最大语言能力。

Abel 的强项是受控组合。

---

## 示例

一个小型 Abel 前端可能长这样：

```abel
fn int main() {
    any config = [{
        "input" = "assets",
        "output" = "dist",
        "size" = 1024
    }];

    fs::scan(config["input"])
        |> image::resize_all(_, cast(config["size"]))
        |> fs::write_all(cast(config["output"]), _);

    return 0;
}
```

用户编写 workflow。  
backend module 提供 `fs` 和 `image` 能力。

---

## CLI

Abel 通过 `abel` 命令提供本地项目工作流。

常用命令包括：

```text
abel init
abel check
abel run
abel test
abel build
abel add
abel remove
abel update
abel package
abel resources
abel version
```

CLI 支持完整本地工程闭环：

```text
创建项目
添加 module
检查前端代码
运行程序
测试行为
构建 package/backend artifact
检查资源
```

---

## 从源码构建

需求：

```text
C++23 compiler
CMake
Qt6
```

典型构建方式：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

运行：

```bash
./build/abel version
```

---

## Abel 愿景

传统编程问的是：

```text
人类如何写完所有代码？
```

Abel 问的是：

```text
人类和 AI 应该如何分配工程复杂度？
```

答案是：

```text
AI 构造能力。
Abel 暴露干净接口。
人类组合这些接口。
```

这使 Abel 特别适合 AI 时代的个人工程。

它足够小，可以理解。  
足够 native，可以做事。  
足够动态，可以组合。  
足够结构化，可以维护。  
足够简单，可以让 AI 稳定协作。

---

## License

See the repository license file.
