# Abel 中文教程：学习 Abel 与搭建 Abel 工程

> 状态：面向 Abel v0。  
> 目标：让人类能读懂 Abel、运行 Abel 程序、搭建一个小工程，并理解如何把 Qt/C++ 插件接到 Abel backend。

Abel v0 不是“更安全的 C++”，也不是 Python/JS 风格动态语言。Abel 的核心定位是：

```text
C/C++ 值模型
+ Qt 字符串/字符
+ vector<T> 内建容器
+ lambda / any / variadic
+ backend block / Qt plugin
+ tree-run interpreter
```

它保留 C/C++ 的复杂度和风险：指针可能为空，引用可能悬挂，vector 扩容后旧引用/指针可能失效，越界不承诺兜底。学习 Abel 时不要把它当成“去指针化脚本语言”，要把它当成一个正在成型的 C/C++ 能力面语言核心。

---

## 1. 先搭环境

Abel v0 固定使用 Qt 6.11.1 + C++23 + CMake + Ninja。

本仓库当前锁定的工具链：

```text
Qt kit:        /home/tnuzy/Qt/6.11.1/gcc_64
qt-cmake:      /home/tnuzy/Qt/6.11.1/gcc_64/bin/qt-cmake
CMake:         /home/tnuzy/Qt/Tools/CMake/bin/cmake
Ninja:         /home/tnuzy/Qt/Tools/Ninja/ninja
C++ compiler:  /usr/bin/g++
C++ standard:  C++23
```

配置和构建：

```bash
/home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64 \
  -DCMAKE_C_COMPILER=/usr/bin/gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_CXX_STANDARD=23

/home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
```

测试时请限制 4GB 虚拟内存，避免测试失控拖死系统：

```bash
/bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
```

河童式验算：先确认 CLI 活着。

```bash
build/abel version
build/abel check examples/smoke/hello.abel
build/abel run examples/smoke/hello.abel
```

---

## 2. Abel 工程结构

当前仓库关键结构：

```text
Abel/
  AGENTS.md                         # Abel v0 的唯一工程规格与 Agent 操作手册
  README.md                         # 公开仓库说明
  LICENSE                           # proprietary / all rights reserved
  CMakeLists.txt

  src/
    abelcore/
      lexer.*                       # 词法
      parser.*                      # 语法
      ast.*                         # AST
      type.*                        # 类型表示
      typechecker.*                 # 静态检查
      value.* runtime.*             # 值、存储、location、frame
      interpreter.*                 # tree-run interpreter
      builtin_registry.*            # 内建函数/方法注册
      backend_* resource_node.*     # backend/plugin/resource node

    abelcli/
      main.cpp                      # abel CLI

  plugins/examples/math_backend/    # Qt backend plugin 示例
  examples/smoke/                   # Abel smoke 程序
  tests/                            # QTest
```

学习顺序建议：

```text
1. examples/smoke/*.abel
2. tests/interpreter/test_interpreter.cpp
3. tests/typechecker/test_typechecker.cpp
4. src/abelcore/type.h / value.h / runtime.h
5. src/abelcore/interpreter.cpp
6. src/abelcore/builtin_registry.cpp
7. src/abelcore/backend_registry.cpp / resource_node.cpp
8. plugins/examples/math_backend/math_backend.cpp
```

不要一上来读完整 parser。先看“语言能跑什么”，再看“解释器如何表达存储模型”。

---

## 3. 第一个 Abel 程序

创建文件，例如 `examples/smoke/hello.abel`：

```abel
fn int main() {
    return 0;
}
```

检查：

```bash
build/abel check examples/smoke/hello.abel
```

运行：

```bash
build/abel run examples/smoke/hello.abel
echo $?
```

入口规则：

- `fn int main()`：返回值作为进程退出码；
- `fn void main()`：退出码为 0；
- v0 先按单文件入口理解。

注意：当前 lexer 尚未把注释作为语法特性记录在教程示例中，所以教程里的 Abel 代码块不依赖 `//` 或 `/* */` 注释。

---

## 4. 基础类型

Abel v0 支持：

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

常用别名：

```text
int    -> i32
long   -> i64
ll     -> i64
double -> f64
```

Qt 映射：

```text
char = QChar
str  = QString
```

简单例子：

```abel
fn int main() {
    int x = 40;
    long y = 2;
    bool ok = true;
    str s = build_string("answer=", x + y);
    println(s);
    return 0;
}
```

---

## 5. C/C++ 值模型：变量、引用、指针

Abel 的机关在这里：变量不是 JS/Python 对象引用，变量拥有对象存储。

```text
普通赋值复制值。
T& 是别名，必须初始化，不可重绑。
T* 保存地址值。
&x 取 lvalue 地址。
*p 解引用指针，得到 lvalue。
函数参数默认按值传递。
要修改调用方对象，用 T& 或 T*。
```

引用：

```abel
fn int main() {
    int x = 0;
    int& r = x;
    r = 5;
    return x;
}
```

运行结果应返回 `5`。

指针：

```abel
fn int main() {
    int x = 1;
    int* p = &x;
    *p = *p + 9;
    return x;
}
```

运行结果应返回 `10`。

引用参数：

```abel
fn void inc(int& x) {
    x = x + 1;
}

fn int main() {
    int v = 4;
    inc(v);
    return v;
}
```

运行结果应返回 `5`。

学习检查：

- `int& r;` 必须报错，因为引用必须初始化；
- `int& r = 1;` 必须报错，因为非常量引用不能绑定 prvalue；
- `int x = y;` 是复制，不是共享对象。

---

## 6. vector<T>

Abel 使用内建 `vector<T>`，不是旧式数组语法。

```abel
fn int main() {
    vector<int> xs = {1, 2, 3};
    xs[1] = 10;
    return xs[1];
}
```

`vector<T>` 是值类型：

```abel
fn int main() {
    vector<int> a = {1, 2};
    vector<int> b = a;
    b[0] = 99;
    return a[0];
}
```

这里返回 `1`，因为赋值复制 vector。

要修改调用方 vector：

```abel
fn void mutate(vector<int>& xs) {
    xs.push(20);
    xs[0] = 10;
}

fn int main() {
    vector<int> xs = {1, 2, 3};
    mutate(xs);
    return xs[0];
}
```

内建方法：

```text
xs.len()
xs.empty()
xs.push(x)
xs.pop()
xs.clear()
xs.reserve(n)
xs.resize(n)
xs.front()
xs.back()
```

`front()` / `back()` 返回 lvalue，可赋值：

```abel
fn int main() {
    vector<int> xs = {1, 2};
    xs.front() = 7;
    xs.back() = 9;
    return xs[0] + xs[1];
}
```

---

## 7. 控制流

条件必须是 `bool`，不要写 C 风格 `if (1)`。

```abel
fn int main() {
    int x = 0;

    repeat(3) {
        x = x + 2;
    }

    while (x < 10) {
        x = x + 1;
    }

    if (x == 10) {
        return x;
    } elseif (x > 10) {
        return 1;
    } else {
        return 0;
    }
}
```

`repeat(n)` 在 v0 中对负数执行 0 次。

C-style `for`：

```abel
fn int main() {
    int sum = 0;
    for (int i = 0; i < 3; i = i + 1) {
        sum = sum + i;
    }
    return sum;
}
```

range-for：

```abel
fn int main() {
    vector<int> xs = {1, 2, 3};

    for (x in xs) {
        x = x + 10;
    }

    return xs[2];
}
```

这里 `x` 是 vector 元素的引用，返回 `13`。

---

## 8. struct 与方法

Abel struct 是值类型。

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

运行结果应返回 `2`。

当前 v0 边界：

- struct 成员默认 public；
- v0 禁止引用字段；
- v0 支持字段、构造、方法、`this`、基础 const 方法；
- private/public、高级生命周期和完整 const receiver 以后再扩。

---

## 9. lambda 与 func 类型

函数值类型：

```abel
func int(int, int) f;
```

lambda：

```abel
fn int main() {
    int x = 1;
    int y = 2;

    func void() g = lambda [&] void() {
        x = x + 9;
    };

    func int() h = lambda [x, &y] int() {
        y = y + 1;
        return x + y;
    };

    g();
    return h();
}
```

捕获规则：

```text
[=]      按值捕获用到的外部变量
[&]      按引用捕获用到的外部变量
[x]      按值捕获 x
[&x]     按引用捕获 x
[x, &y]  混合捕获
```

引用捕获不做生命周期兜底。像 C++ 一样，别把短命对象的引用拿到长命闭包里乱用。

---

## 10. any、cast、variadic、字符串构建

`any` 是显式动态边界，不是把 Abel 变成动态语言。

```abel
fn int first(any... args) {
    return cast<int>(args[0]);
}

fn int main() {
    any x = 7;
    int y = cast<int>(x);
    return y + first(3);
}
```

规则：

- `any` 可以装任意 Abel 值；
- 从 `any` 取出必须 `cast<T>(x)`；
- 类型不匹配是 runtime error；
- `any...` 最多一个，必须是最后一个参数。

字符串构建：

```abel
fn int main() {
    int old = 18;
    str school = "Hakurei";
    str s = build_string("My Old is ", old, " , My School is ", school);
    println(s);
    return 0;
}
```

给 struct 定义 `to_str(T)` 后可参与 `build_string`：

```abel
struct Student {
    str name;
    int age;
}

fn str to_str(Student s) {
    return build_string(s.name, "(", s.age, ")");
}

fn int main() {
    Student s = Student("Aya", 16);
    println(build_string("student=", s));
    return 0;
}
```

没有 `to_str(Student)` 时，`build_string(s)` 应被静态拒绝。

---

## 11. 字符串和字符 vector

`str` 和 `vector<char>` 不做隐式转换。

```abel
fn int main() {
    vector<char> cs = str_to_chars("ab");
    cs[1] = 'z';
    str s = chars_to_str(cs);
    println(s);
    return cs.len();
}
```

---

## 12. operator

已支持常用 operator：

```text
+ - * / %
== != < <= > >=
&& || !
& 取地址
* 解引用
. 字段/方法
-> 指针字段
= 赋值
```

Abel v0 额外支持：

```text
a ** b       幂
a %% b       欧几里得模
a <? b       min
a >? b       max
x |> f       f(x)
x |> f(a)    f(x, a)
```

例子：

```abel
fn int add(int a, int b) {
    return a + b;
}

fn int main() {
    int piped = 4 |> add(5);
    return (2 ** 3) + (-5 %% 3) + (piped >? 4);
}
```

---

## 13. backend：Abel 调 Qt/C++ 插件

Abel 用 `backend` 声明外部能力系统：

```abel
backend MathSystem {
    fn int fast_add(int a, int b);
    fn void sort(vector<int>& xs);
}

fn int main() {
    vector<int> xs = {3, 1, 2};
    MathSystem::sort(xs);
    return MathSystem::fast_add(xs[0], xs[2]);
}
```

这里 Abel 只声明签名；真正实现来自 Qt plugin。

检查 resource：

```bash
build/abel resources check plugins/examples/math_backend/resource.json
```

运行 backend 示例：

```bash
build/abel run --resource plugins/examples/math_backend/resource.json examples/smoke/backend.abel
echo $?
```

当前示例中：

- C++ 插件会排序 `{3,1,2}` 为 `{1,2,3}`；
- `fast_add(xs[0], xs[2])` 返回 `4`；
- 所以进程退出码是 `4`。

---

## 14. 写一个 Qt backend plugin

插件开发者不应该手写大量 `AbelValue` 拆箱逻辑。v0 提供 `AbelBackendPluginBase` / binder。

示例：`plugins/examples/math_backend/math_backend.cpp`

```cpp
#include "abelcore/backend_plugin_base.h"

#include <algorithm>

class MathBackend final : public abel::AbelBackendPluginBase {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IAbelBackend_iid)
    Q_INTERFACES(abel::IAbelBackend)

public:
    MathBackend()
    {
        bind(QStringLiteral("MathSystem.fast_add"), [](int a, int b) {
            return a + b;
        });
        bind(QStringLiteral("MathSystem.sort"), [](std::vector<int>& xs) {
            std::sort(xs.begin(), xs.end());
        });
    }

    QString backendId() const override
    {
        return QStringLiteral("MathSystem");
    }
};

#include "math_backend.moc"
```

对应 resource node：

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
  ],
  "state": "unloaded",
  "lastError": ""
}
```

新增 backend 的基本步骤：

```text
1. 在 Abel 源码里写 backend Block，锁定函数签名。
2. 在 C++ plugin 里继承 AbelBackendPluginBase。
3. 用 bind("Backend.symbol", lambda) 注册实现。
4. 在 CMakeLists.txt 加 MODULE library，链接 abelcore 和 Qt6::Core。
5. 写 resource.json，列出 path、iid、backendId、symbols。
6. build。
7. 运行 build/abel resources check resource.json。
8. 运行 build/abel run --resource resource.json your_file.abel。
9. 给 resource / backend / interpreter 增加 QTest。
```

v0 binder 当前覆盖的是核心通路需要的类型矩阵，不是完整 C++ 类型宇宙。扩类型前先看 `src/abelcore/backend_binder.h`。

---

## 15. 搭建自己的 Abel 小工程

推荐最小目录：

```text
my_abel_project/
  src/
    main.abel
  resources/
    math.resource.json
  plugins/
    libmath_backend.so
```

但 v0 CLI 还没有完整包管理和模块系统，所以最稳妥的方式是：把 Abel 仓库作为工具链项目，先用单文件 `.abel` 练习。

示例 `src/main.abel`：

```abel
struct Student {
    str name;
    int age;
}

fn str to_str(Student s) {
    return build_string(s.name, "(", s.age, ")");
}

fn int main() {
    vector<int> xs = {3, 1, 2};
    for (x in xs) {
        x = x + 10;
    }

    Student s = Student("Aya", xs[0]);
    println(build_string("student=", s));
    return xs.len();
}
```

检查和运行：

```bash
/path/to/Abel/build/abel check src/main.abel
/path/to/Abel/build/abel run src/main.abel
```

如果你需要 backend：

```bash
/path/to/Abel/build/abel run --resource resources/math.resource.json src/main.abel
```

---

## 16. 如何继续开发 Abel 本体

先读 `AGENTS.md`。它是本仓库唯一工程规格。

开发纪律：

```text
1. 每轮从 clean tree 开始。
2. 修改必须走显式 patch。
3. 大块推进，不为细枝末节无限验证。
4. 但提交前至少做相关 build/check/test。
5. test 必须用 4GB 内存上限。
6. 更新 AGENTS.md 末尾强制区。
7. 一个逻辑任务一个 commit。
```

推荐大块：

```text
scan
完整 const 指针/引用矩阵
backend binder 类型矩阵
struct private/public 与高级项
用户自定义 operator
模块/use/export
```

每做一块，都按这条闭环：

```text
语法节点
→ parser
→ typechecker
→ runtime/interpreter
→ builtin/backend registry 如果需要
→ CLI 如果需要
→ QTest
→ smoke
→ AGENTS.md 进度
→ commit
```

---

## 17. 常见坑

### 17.1 把 Abel 当脚本语言

错误直觉：

```text
变量赋值只是共享对象。
引用会自动安全。
越界会被 runtime 兜底。
any 会自动转换。
```

正确模型：

```text
变量有存储。
赋值复制。
引用/指针是 C/C++ 能力面。
any 必须显式 cast。
```

### 17.2 条件写 int

Abel 条件要 bool：

```abel
if (x != 0) {
    return 1;
}
```

不要写：

```abel
if (x) {
    return 1;
}
```

### 17.3 backend 声明和插件 symbol 不一致

三处必须咬合：

```text
Abel backend block: MathSystem::fast_add
C++ plugin bind:    MathSystem.fast_add
resource symbols:   MathSystem.fast_add
```

任一处错，ResourceNode/BackendRegistry 应给 E06xx 类诊断。

### 17.4 修改 parser 后不加错误恢复测试

parser 曾经因为错误恢复不前进导致内存爆炸。以后动 parser，要特别检查：

```text
遇到错误是否消费 token？
循环是否保证前进？
静态/backend 调用语法是否消费完整？
```

测试仍然要卡 4GB。

---

## 18. 学习路线

最短路线：

```text
1. 跑 hello.abel。
2. 写 int / bool / str / build_string。
3. 写引用和指针例子。
4. 写 vector 值复制和 vector<T>& 修改。
5. 写 for / range-for。
6. 写 struct Counter。
7. 写 lambda 捕获。
8. 写 any... + cast。
9. 跑 backend smoke。
10. 改 MathBackend 加一个新函数。
```

掌握标准：

```text
你能解释 Abel 的变量存储模型。
你能判断一个表达式是 lvalue 还是 prvalue。
你能说明什么时候需要 T& / T*。
你能写一个 vector<T>& 函数并预测是否写回。
你能给 struct 写 to_str 并接入 build_string。
你能让 Abel backend block 调到 Qt plugin。
你能新增一个 builtin 或 backend symbol，并知道 parser/typechecker/interpreter 哪层该不该动。
```

到这里，你就不是“会跑 Abel”，而是开始能维护 Abel 这台机器了。
