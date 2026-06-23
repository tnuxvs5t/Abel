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

### 14.1 Abel SDK 当前实际范围

先把机关量清楚：当前 Abel 已有 **安装版 SDK 第一片**。它不是最终稳定 ABI 发行包，但已经能让外部 backend 工程用 CMake 正常消费：

```bash
/home/tnuzy/Qt/Tools/CMake/bin/cmake --install /path/to/Abel/build --prefix "$ABEL_PREFIX"
```

安装后目录大致是：

```text
$ABEL_PREFIX/
  bin/
    abel
  lib/
    libabelcore.so
    cmake/Abel/
      AbelConfig.cmake
      AbelConfigVersion.cmake
      AbelTargets.cmake
  include/
    abelcore/
      backend_interface.h
      backend_binder.h
      backend_plugin_base.h
      type.h
      value.h
      runtime.h
      ...
```

外部 backend CMake 可以写：

```cmake
find_package(Abel REQUIRED)
target_link_libraries(my_backend PRIVATE Abel::abelcore)
```

`AbelConfig.cmake` 会引入：

```text
Abel::abelcore
Abel::abel
Abel_INCLUDE_DIR
Abel_CORE_TARGET
Abel_CLI_TARGET
```

仍然必须注意 ABI：

```text
Qt6::Core
C++23
和 Abel 本体相同的 Qt kit / compiler / ABI
不承诺跨 Qt 版本 ABI
不承诺跨编译器 ABI
```

如果外部工程找不到 `AbelConfig.cmake`，配置时显式给：

```bash
-DAbel_DIR="$ABEL_PREFIX/lib/cmake/Abel"
```

如果运行时找不到 `libabelcore.so`，优先检查 plugin 的 RPATH。临时兜底可以用：

```bash
LD_LIBRARY_PATH="$ABEL_PREFIX/lib:$LD_LIBRARY_PATH" \
  $ABEL_BIN run .
```

旧的 build-tree SDK 做法（手写 `$ABEL_ROOT/src` 和 `$ABEL_BUILD/libabelcore.so`）只作为没有安装 SDK 时的临时 fallback，不再是推荐路径。

#### v1 binder 常用类型矩阵

当前 `AbelBackendBinder` 可以稳定用于这些 C++ lambda 参数：

```text
Abel bool          -> C++ bool
Abel int/i32       -> C++ int
Abel long/ll/i64   -> C++ qint64
Abel double/f64    -> C++ double
Abel char          -> C++ QChar 或 char
Abel str           -> C++ QString
Abel any           -> C++ abel::AbelValue
Abel vector<T>     -> C++ std::vector<T>
Abel vector<T>&    -> C++ std::vector<T>&，并在调用结束后写回 Abel vector
AbelRuntimeContext& 可作为最后一个 C++ lambda 参数，用于自定义结构化诊断
Abel any...        -> C++ bindVariadic + abel::AbelVariadicArgs 或 std::vector<abel::AbelValue>
```

其中 `vector<T>` 的 T 覆盖常用标量：

```text
bool / int / qint64 / double / QChar / char / QString / abel::AbelValue
```

当前直接返回值建议使用：

```text
void
bool
int
qint64
double
QChar
char
QString
abel::AbelValue
std::vector<bool/int/qint64/double/QChar/char/QString/AbelValue>
```

注意：

```text
str 对应 C++ QString。
char 对应 C++ QChar；C++ char 走 Latin-1 映射。
vector<T> 可以直接作为参数或返回值；vector<T>& 调用结束后写回 Abel vector。
除了 vector<T>&，不要假设普通 T& 参数会写回 Abel。
AbelRuntimeContext& 必须放在 lambda 最后一个参数；放在中间不是 Abel 调用签名的一部分，不支持。
变长 backend 不用写普通 bind；使用 bindVariadic("Backend.symbol", lambda)。lambda 只能接一个 payload 参数，外加可选的最后一个 AbelRuntimeContext&。
当前不支持任意 struct/class 自动拆装箱，不支持任意 pointer/reference 矩阵。
```

变长参数示例：

```cpp
bindVariadic(QStringLiteral("MathSystem.join_debug"),
             [](abel::AbelVariadicArgs args) {
                 return args.buildString();
             });

bindVariadic(QStringLiteral("MathSystem.count_variadic"),
             [](std::vector<abel::AbelValue> args) {
                 return static_cast<int>(args.size());
             });
```

Abel 侧声明：

```abel
backend MathSystem {
    fn str join_debug(any... args);
    fn int count_variadic(any... args);
}
```

例如：

```abel
backend ExampleSystem {
    fn bool accept_text(str name, str content);
    fn void notify(str message);
}
```

可对应：

```cpp
bind(QStringLiteral("ExampleSystem.accept_text"), [](QString name, QString content) {
    return !name.isEmpty() && !content.isEmpty();
});

bind(QStringLiteral("ExampleSystem.notify"), [](QString message) {
    Q_UNUSED(message);
});
```

`void` 返回可以表达“只执行不返回值”的能力；如果调用方需要知道失败原因或状态，v0 更推荐返回 `bool` / `int` / `str` 等显式结果。

### 14.2 从 0 搭建外部 backend 工程（MathSystem 示例）

这一节是操作闭环，不只是概念。

假设你有：

```text
Abel SDK： /home/you/abel-sdk
Abel CLI： /home/you/abel-sdk/bin/abel
用户工程：/home/you/my_abel_project
```

先约定路径：

```bash
export ABEL_PREFIX=/home/you/abel-sdk
export ABEL_BIN=/home/you/abel-sdk/bin/abel
export QT_PREFIX=/home/tnuzy/Qt/6.11.1/gcc_64
export CMAKE=/home/tnuzy/Qt/Tools/CMake/bin/cmake
```

用户工程目录：

```text
my_abel_project/
  src/
    main.abel
  backend/
    CMakeLists.txt
    math_backend.cpp
  resources/
    math_backend.json
  build/
    backend/                 # CMake 生成
```

#### 第一步：写 Abel 侧声明和调用

`src/main.abel`：

```abel
backend MathSystem {
    fn int fast_add(int a, int b);
    fn void sort(vector<int>& xs);
}

fn int main() {
    vector<int> xs = {3, 1, 2};
    MathSystem::sort(xs);
    int ans = MathSystem::fast_add(xs[0], xs[2]);
    println(build_string("backend answer=", ans));
    return ans;
}
```

先静态检查 Abel 代码：

```bash
$ABEL_BIN check src/main.abel
```

此时直接 run 会失败，因为 backend 还没绑定，这是正常的。

#### 第二步：写 C++ plugin

`backend/math_backend.cpp`：

```cpp
#include "abelcore/backend_plugin_base.h"

#include <QString>

#include <algorithm>
#include <vector>

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

当前 binder 常用可用类型：

```text
void
bool
int
qint64
double
QChar
char
QString
abel::AbelValue
std::vector<bool/int/qint64/double/QChar/char/QString/AbelValue>
std::vector<T>&
AbelRuntimeContext&  // 仅最后一个参数
```

`std::vector<T>&` 会在 plugin 返回后写回 Abel 的 `vector<T>`。普通 `T&` 不承诺写回。

#### 第三步：写 backend/CMakeLists.txt

`backend/CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.30)

project(MyAbelMathBackend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core)
find_package(Abel REQUIRED)

add_library(math_backend MODULE
    math_backend.cpp
)

target_link_libraries(math_backend
    PRIVATE
        Abel::abelcore
)

set_target_properties(math_backend PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    OUTPUT_NAME math_backend
    BUILD_RPATH "$<TARGET_FILE_DIR:Abel::abelcore>"
)
```

这个 CMake 做了三件事：

```text
1. 找 Qt6::Core。
2. 通过 AbelConfig.cmake 引入 Abel::abelcore。
3. 把 plugin 编译到 build/backend/plugins/libmath_backend.so。
```

#### 第四步：配置和编译 plugin

在用户工程根目录运行：

```bash
$CMAKE -S backend -B build/backend -G Ninja \
  -DAbel_DIR="$ABEL_PREFIX/lib/cmake/Abel" \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_CXX_STANDARD=23

$CMAKE --build build/backend
```

编译成功后应出现：

```text
build/backend/plugins/libmath_backend.so
```

#### 第五步：写 ResourceNode JSON

推荐方式：`path` 写绝对路径，避免相对路径误解。

`resources/math_backend.json`：

```json
{
  "id": "math.backend",
  "kind": "qt_plugin",
  "path": "/home/you/my_abel_project/build/backend/plugins/libmath_backend.so",
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

`qtVersion` 和 `kit` 是加载期兼容门禁：`resources check` 不会按当前机器拒绝外来版本/kit，它只检查 JSON 形状；真正 `run --resource` 或 package 自动加载时，Abel 会在 `QPluginLoader` 前拒绝和当前 Abel runtime Qt version / kit 不一致的资源。

注意：如果 `path` 写相对路径，例如：

```json
"path": "plugins/libmath_backend.so"
```

那么底层 `--resource` 路径会把它解析为相对 `$ABEL_BIN` 所在目录，也就是通常的：

```text
/home/you/abel-sdk/bin/plugins/libmath_backend.so
```

不是相对用户工程目录。所以有两种安全做法：

```text
方案 A：resource JSON 里写 plugin 绝对路径。
方案 B：更推荐 package backendArtifacts，让 path 相对 package 根目录，再由 abel build 复制到 .abel/cache/backend。
```

#### 第六步：检查 resource 和运行

`resources check` 只检查 JSON 结构和字段，不会真的加载 `.so`，也不会因为 `qtVersion` / `kit` 和当前 runtime 不一致而失败：

```bash
$ABEL_BIN resources check resources/math_backend.json
```

真正加载 plugin 的命令是；这一步才会检查 Qt version / kit、IID、backendId、symbols 和签名：

```bash
$ABEL_BIN run --resource resources/math_backend.json src/main.abel
echo $?
```

期望输出类似：

```text
backend answer=4
```

退出码应为：

```text
4
```

#### 第七步：backend 排错顺序

如果失败，按这个顺序查：

```text
1. $ABEL_BIN check src/main.abel 是否通过。
2. build/backend/plugins/libmath_backend.so 是否存在。
3. resource JSON 的 path 是否是绝对路径，或 package backendArtifacts path 是否相对 package 根目录正确。
4. resource JSON 的 iid 是否是 org.abel.IAbelBackend/1.0。
5. backendId 是否三处一致：MathSystem。
6. symbol 是否三处一致：MathSystem.fast_add / MathSystem.sort。
7. Abel backend block 的签名是否和 C++ lambda 签名一致。
8. 若提示找不到 libabelcore.so，优先检查 plugin BUILD_RPATH，临时设置 LD_LIBRARY_PATH=$ABEL_PREFIX/lib。
```

`LD_LIBRARY_PATH` 兜底运行方式：

```bash
LD_LIBRARY_PATH="$ABEL_PREFIX/lib:$LD_LIBRARY_PATH" \
  $ABEL_BIN run --resource resources/math_backend.json src/main.abel
```

新增 backend 的基本步骤：

```text
1. 在 Abel 源码里写 backend Block，锁定函数签名。
2. 在 C++ plugin 里继承 AbelBackendPluginBase。
3. 用 bind("Backend.symbol", lambda) 注册实现。
4. 用 backend/CMakeLists.txt 链接 Abel::abelcore。
5. 编译出 libxxx_backend.so。
6. resource.json 写绝对 path，或在 abel.package.json 里写 backendArtifacts 并运行 abel build。
7. 运行 $ABEL_BIN resources check resource.json。
8. 运行 $ABEL_BIN run --resource resource.json your_file.abel。
9. 若这是 Abel 本体新增能力，再给 resource / backend / interpreter 增加 QTest。
```

v1 binder 当前覆盖常用标量/vector/诊断通道，不是完整 C++ 类型宇宙。扩类型前先看 `src/abelcore/backend_binder.h`。

---

## 15. 搭建自己的 Abel 小工程

推荐最小目录：

```text
my_abel_project/
  abel.package.json
  src/
    main.abel
```

当前 v1 包管理已经支持项目级入口、本地 path 依赖、以及本地 registry 目录依赖第一闭环：`abel init [project-dir]` 可以生成最小工程，`abel.package.json` 描述包名、版本、入口文件，`abel add/remove/update` 可以操作依赖并生成 `abel.lock.json`，path/registry dependency 已支持 SemVer 版本要求检查，registry dependency 会把选中的版本复制到 `.abel/cache/packages`，`abel build` 会执行项目级预构建检查、按 `backendArtifacts[].build` 自动构建 CMake backend plugin，并把 backend artifact 复制进根项目缓存，`abel check/run <project-dir>` 会读取 package graph。

从空目录创建：

```bash
ABEL=/path/to/Abel/build/abel
$ABEL init my_abel_project
cd my_abel_project
```

最小 `abel.package.json`：

```json
{
  "name": "my-abel-project",
  "version": "0.1.0",
  "entry": "src/main.abel"
}
```

检查和运行：

```bash
$ABEL package check .
$ABEL update .
$ABEL build .
$ABEL check .
$ABEL run .
```

本地 path 依赖操作：

```bash
$ABEL add path ../some_dep .
$ABEL update .
$ABEL remove some_dep .
```

`abel add path` 会读取依赖项目的 `abel.package.json`，自动推断依赖名，把依赖写入当前项目 manifest，并刷新 `abel.lock.json`。`abel remove` 按依赖名删除 manifest 中的依赖，并刷新 lockfile。这样普通用户不需要手动编辑 dependencies JSON。

本地 registry 依赖操作：

```bash
$ABEL add registry dep '^1.0.0' ../registry .
$ABEL update .
```

当前 registry 是本地目录，不是远程服务。布局约定：

```text
registry/
  dep/
    1.0.0/
      abel.package.json
      src/main.abel
    1.2.0/
      abel.package.json
      src/main.abel
```

`abel add registry dep '^1.0.0' ../registry .` 会写入：

```json
{
  "dependencies": [
    {"name": "dep", "kind": "registry", "registry": "../registry", "version": "^1.0.0"}
  ]
}
```

`abel update/build` 会在 registry 中选择满足 requirement 的最高 SemVer 版本，例如 `1.2.0`，复制到：

```text
.abel/cache/packages/dep/1.2.0
```

lockfile 会记录实际解析版本、声明要求、registry source 与缓存后的 `resolvedPath`。后续 `abel check/run <project-dir>` 从 lockfile 的 cached `resolvedPath` 消费依赖图。

如果手动写依赖，当前 path/registry dependency 的 `version` 字段是版本要求，不是锁定结果。支持的第一片 SemVer 语法：

```json
{
  "dependencies": [
    {"name": "some-dep", "kind": "path", "path": "../some_dep", "version": "^0.2.0"},
    {"name": "other", "kind": "path", "path": "../other", "version": ">=1.1.0 <2.0.0"},
    {"name": "dep", "kind": "registry", "registry": "../registry", "version": "^1.0.0"}
  ]
}
```

规则：

```text
包自身 version 必须是 major.minor.patch。
空 version 或 "*" 表示任意版本。
"1.2.3" / "=1.2.3" 表示精确版本。
">=1.2.0 <2.0.0" 表示多个比较条件同时满足。
"^0.2.0" / "~0.2.0" 支持第一片 npm 风格范围。
abel update/build 会把实际解析到的包版本和 versionRequirement 写入 lockfile。
之后 manifest 的 version requirement 改了，旧 lockfile 会被视为 stale。
```

`abel build` 当前是项目级构建门面：刷新/校验 lockfile，检查入口 Abel 源码；若 backend artifact 带 `build` 字段，则先执行 CMake 配置/构建；最后把根包与依赖包声明的 backend artifact 复制到根项目：

```text
.abel/cache/backend/<package>/<backendId>/<plugin-file>
```

它还不是完整 native/backend artifact 生态；目前自动构建只支持 CMake build spec，没有 ABI hash、semver 或远程 registry 级缓存失效策略。当前缓存策略是：`abel build` 每次先构建需要构建的 backend，再覆盖复制到项目缓存，并在同目录写入 `<plugin>.abel-cache.json` 元数据 sidecar。若 `backendArtifacts` 未显式写 `qtVersion` / `kit`，Abel 会用当前 runtime 的 Qt version / kit 生成内部 ResourceNode；加载时仍会拒绝声明值和当前 runtime 不一致的资源。

`abel run <project-dir>` 会读取 package graph；如果依赖包在 `backendArtifacts` 声明了 Qt plugin，根项目只要通过 `abel add path` 依赖它，运行时也会自动加载这个依赖 backend。若已经运行过 `abel build`，`run` 只会在缓存 `.so` 存在且 sidecar 元数据仍匹配当前源 artifact 的路径、大小、mtime、ResourceNode 字段与 symbols 时，优先加载 `.abel/cache/backend/...` 下的缓存 artifact；若缓存不存在、元数据缺失或已经失效，则回退到依赖包声明的源 artifact 路径，直到重新运行 `abel build` 刷新缓存。lockfile 若已经过期，`abel check/run` 会提示先运行 `abel update` 或 `abel build`，避免悄悄使用旧依赖图。

注意：这仍只是 v1 包管理引擎的早期闭环。path dependency 与本地 registry dependency 已有 SemVer requirement 检查和本地 package cache，但远程 registry 下载、完整 resolver 冲突求解、网络 download cache、安装版 SDK 成熟化、ABI 校验与完整版本化缓存失效仍是后续 v1 工作。

若项目需要 C++ backend，当前可以在根包或依赖包描述里写 `backendArtifacts`。如果 plugin 已经存在，`abel build` 会直接复制；如果写了 `build` 字段，`abel build` 会先调用 CMake 构建 plugin。运行时 `abel run <project-dir>` 自动加载 plugin，不用在普通运行命令里手动传 `--resource`：

```json
{
  "name": "my-abel-project",
  "version": "0.1.0",
  "entry": "src/main.abel",
  "backendArtifacts": [
    {
      "backendId": "MathSystem",
      "path": "build/backend/plugins/libmath_backend.so",
      "symbols": ["fast_add", "sort"],
      "build": {
        "system": "cmake",
        "source": "backend",
        "buildDir": "build/backend",
        "generator": "Ninja",
        "configureArgs": [
          "-DAbel_DIR=/path/to/abel-sdk/lib/cmake/Abel",
          "-DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/gcc_64",
          "-DCMAKE_CXX_STANDARD=23"
        ]
      }
    }
  ]
}
```

`path` 相对声明它的 package 根目录解析，并且应该指向 CMake 构建完成后的 `.so`。`build.source` 与 `build.buildDir` 也相对 package 根目录解析。`abel build` 会先构建，再把 `path` 指向的产物复制到根项目 `.abel/cache/backend/...`，并写 `<plugin>.abel-cache.json`；`abel run` 在 sidecar 匹配时优先加载缓存，否则回退源 artifact。缓存 metadata 会记录 ResourceNode 的 kind/iid/qtVersion/kit/symbols；加载期再做当前 runtime Qt version / kit 门禁。这仍是过渡形态；v1 complete 的目标是由包管理引擎生成和维护 backend artifact/resource 信息。

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
$ABEL check .
$ABEL run .
```

如果你需要 backend：

```bash
$ABEL run .
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

### 17.3 非 void 函数漏写 return

`fn int`、返回 `str` 的函数、返回非 void 的方法和 lambda 都必须保证所有可静态确认的路径返回值。当前 TypeChecker 已做保守 definite-return 检查：

```abel
fn int bad() {
    int x = 1;
}
```

`abel check` 会直接报：

```text
function 'bad' may end without returning int
```

允许：

```abel
fn int ok(bool b) {
    if (b) {
        return 1;
    } else {
        return 2;
    }
}
```

如果函数体已经有根因错误，例如调用未知函数，TypeChecker 不再额外追加“可能缺 return”的噪音，避免一个根因污染成诊断瀑布。运行期仍保留 ended-without-return 防线，但正常 `abel run` 会先执行同一套 check。

### 17.4 backend 声明和插件 symbol 不一致

三处必须咬合：

```text
Abel backend block: MathSystem::fast_add
C++ plugin bind:    MathSystem.fast_add
resource symbols:   MathSystem.fast_add
```

任一处错，ResourceNode/BackendRegistry 应给 E06xx 类诊断。

### 17.5 运行期错误怎么看

Abel 的运行期诊断已经包含 Abel 调用栈、源码位置、源码行 excerpt 和 caret。先看 primary error，再沿 `stack:` 往下看调用链：

```text
E0517: division by zero at src/main.abel:3:24
                return 1 / 0;
                       ^^^^^
stack:
  at fn inner (src/main.abel:7:24)
                return inner();
                       ^^^^^^^
```

排错顺序：

```text
1. primary error 的源码行：真正崩的位置。
2. stack 第一帧：谁直接触发它。
3. 后续帧：从 main/backend/lambda/method 往内的调用路径。
4. 若是 backend error，优先看 Abel 调用点和 backend block 签名是否匹配。
```

运行期转换错误也应看 primary 行。比如实参类型、返回值类型、赋值 RHS 或 backend 实参在运行期不匹配时，诊断应指向调用实参或 `return expr` 那一行，而不是函数声明里的参数位置：

```text
return take("bad");
       ^^^^^^^^^^^
```

当前 excerpt 来自 lexer 保存的单行源码；多文件/module source map 仍是 v1 后续大块。

当前 `std.debug` 第一片以 builtin 形式提供：

```abel
debug_break();                         // 直接产生 debug breakpoint 诊断
debug_assert(x > 0, "x must > 0");      // 条件 false 时产生断言诊断
debug_assert(ok, "id=", id, ", bad");   // message 使用 build_string 同一套 stringify
```

规则：

```text
debug_break() 无参数，返回 void，但运行到这里会报告 E0596 并终止当前 run。
debug_assert(cond, any... message) 要求 cond 是 bool。
cond 为 true 时继续执行。
cond 为 false 时报告 E0598，message 会拼进诊断。
诊断同样带 primary 源码行、caret 和 Abel 调用栈。
```

### 17.5 修改 parser 后不加错误恢复测试

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
