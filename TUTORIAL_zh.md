# Abel 中文教程：学习语言与搭建工程

> 这份教程面向当前 Abel 主线：语言核心、标准库切片、backend/SDK、包管理和测试入口已经形成闭环，但仍处于快速演进阶段。

---

## 1. Abel 的定位

Abel 不是 Python/JS 式动态脚本，也不是 Rust 式安全语言。它的机关是：

```text
C/C++ 值模型
+ Qt 字符串/字符
+ vector<T> 内建容器
+ struct / lambda / any / any...
+ builtin 标准库切片
+ backend block 调 Qt/C++ plugin
+ package project / lockfile / local registry
+ tree-run interpreter
```

学习 Abel 时要记住：

- 变量拥有对象存储。
- 普通赋值复制值。
- `T&` 是别名，必须初始化，不可重绑。
- `T*` 是地址值，空指针风险不由语言兜底。
- `vector` 扩容后旧引用/指针是否失效，按 C++ 风险模型理解。
- TypeChecker 和 Interpreter 正在持续收敛语义一致性；遇到 check/run 分裂要当作编译器问题修。

---

## 2. 构建 Abel 本体

固定工具链：

```text
Qt:      /home/tnuzy/Qt/6.11.1/gcc_64
CMake:   /home/tnuzy/Qt/Tools/CMake/bin/cmake
Ninja:   /home/tnuzy/Qt/Tools/Ninja/ninja
GCC/G++: /usr/bin/gcc /usr/bin/g++
C++:     C++23
```

配置与构建：

```bash
/home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64 \
  -DCMAKE_C_COMPILER=/usr/bin/gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_CXX_STANDARD=23

/home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
```

测试必须限制 4GB 虚拟内存：

```bash
/bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
```

快速验算：

```bash
build/abel version
build/abel check examples/smoke/hello.abel
build/abel run examples/smoke/hello.abel
```

---

## 3. 从 0 搭建 Abel 用户工程

优先让 CLI 生成骨架：

```bash
build/abel init my_project
cd my_project
../build/abel package check .
../build/abel check .
../build/abel run .
```

典型结构：

```text
my_project/
  abel.package.json
  README.md
  .gitignore
  src/
    main.abel
  tests/
    smoke.abel
```

最小 `abel.package.json`：

```json
{
  "name": "my-project",
  "version": "0.1.0",
  "entry": "src/main.abel"
}
```

最小 `src/main.abel`：

```abel
fn int main() {
    println("hello from Abel");
    return 0;
}
```

入口规则：

- `fn int main()`：返回值作为进程退出码。
- `fn void main()`：退出码为 0。
- 项目输入会合并 `src/**/*.abel`，入口文件最后加载。

---

## 4. 基础类型

Abel 当前常用类型：

```text
void bool
i8 i16 i32 i64
u8 u16 u32 u64
f64
char str any
vector<T>
T* T& const T const T&
func R(A, B)
```

别名：

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

例子：

```abel
fn int main() {
    int x = 40;
    long y = 2;
    str s = build_string("answer=", x + y);
    println(s);
    return 0;
}
```

---

## 5. 值、引用、指针

引用写回：

```abel
fn void inc(int& x) {
    x = x + 1;
}

fn int main() {
    int a = 4;
    inc(a);
    return a;
}
```

指针写回：

```abel
fn void inc_ptr(int* p) {
    *p = *p + 1;
}

fn int main() {
    int a = 4;
    inc_ptr(&a);
    return a;
}
```

只读引用：

```abel
fn int read(const int& x) {
    return x;
}
```

当前边界：

- `T&` 需要 lvalue。
- `const T&` 当前仍按只读 lvalue 引用理解；不承诺 C++ 那种完整临时生命周期延长。
- `const T*` / `T* const` 的完整矩阵仍在推进中。

---

## 6. vector

```abel
fn int main() {
    vector<int> xs = {1, 2, 3};
    xs.push(4);
    xs[1] = 10;
    return xs.len() + xs.back();
}
```

常用方法：

```text
len empty push pop clear reserve resize front back
insert erase find contains count extend slice
sort reverse unique binary_search lower_bound upper_bound
```

`vector<struct>.resize(n)` 会走元素默认构造；若元素不可默认构造，TypeChecker 应提前诊断。

临时 vector 的只读方法可用：

```abel
fn int main() {
    return str_to_chars("abc").len();
}
```

---

## 7. struct、构造、方法和重载

```abel
struct Box {
    int x;

    init(int v) {
        x = v;
    }

    init(str s) {
        x = s.len();
    }

    fn int get() {
        return x;
    }

    fn int get(int add) {
        return x + add;
    }
}

fn int main() {
    Box a = Box(5);
    Box b = Box("abcd");
    return a.get() + b.get(3);
}
```

当前支持：

- 多个 `init(...)` 构造 overload。
- 同名方法 overload。
- `public:` / `private:` 成员标签。
- `const fn` 方法只读 receiver。
- 无显式 `init` 时保留 positional field construction。
- 零参 `init()` 作为默认构造入口。

当前不承诺：

- friend/protected/nested type。
- 默认参数。
- 模板方法/模板构造。
- 返回类型 overload。
- 完整 C++ overload ranking。

---

## 8. 函数、lambda、operator

普通函数 overload：

```abel
fn int pick(int x) { return x + 10; }
fn int pick(str s) { return s.len(); }

fn int main() {
    return pick(1) + pick("abc");
}
```

lambda：

```abel
fn int main() {
    int x = 1;
    func int() f = lambda [&] int() {
        x = x + 1;
        return x;
    };
    return f();
}
```

用户二元 operator 第一片：

```abel
struct Point { int x; int y; }

fn Point operator +(Point a, Point b) {
    return Point(a.x + b.x, a.y + b.y);
}
```

当前不支持 `operator()` / `operator[]` / `operator<>`。

---

## 9. any 与字符串化

`any` 是显式动态边界：

```abel
any x = 123;
int y = cast<int>(x);
```

字符串拼接：

```abel
fn int main() {
    int age = 18;
    str school = "Hakurei";
    println("age=", age, ", school=", school);
    return 0;
}
```

给 struct 自定义字符串化：

```abel
struct Student {
    str name;
    int age;
}

fn str to_str(Student s) {
    return build_string(s.name, "(", s.age, ")");
}
```

---

## 10. 标准库切片

### 字符串

```text
str.len empty contains find substr slice replace
starts_with ends_with trim lower upper split
str.join(vector<str>)
str.parse_int parse_long parse_double parse_bool
```

### 数学

```text
abs sqrt floor ceil round trunc pow
min max clamp
sin cos tan asin acos atan atan2
exp log log10 gcd lcm
```

### 文件、路径、环境

```text
read_text write_text append_text
read_lines write_lines
path_exists path_is_file path_is_dir
copy_file move_path remove_path mkdirs
path_join path_dirname path_basename path_ext
path_absolute path_clean current_dir
env_exists env_get
```

### char / any

```text
char_code char_from_code
char_is_digit char_is_letter char_is_alnum char_is_space
char_is_upper char_is_lower char_upper char_lower char_to_str

any_type any_is
any_is_bool any_is_int any_is_double any_is_char any_is_str
any_is_vector any_is_pointer
```

---

## 11. 测试 Abel 项目

项目测试文件放在：

```text
tests/**/*.abel
```

例子：

```abel
fn int main() {
    test_eq(1 + 2, 3, "math works");
    return 0;
}
```

运行：

```bash
abel test .
abel test --filter smoke .
abel test --expect-fail known_bug .
abel test --report-json report.json .
abel test --report-junit report.xml .
```

fixture：

```abel
fn void setup() {
    println("before");
}

fn void teardown() {
    println("after");
}
```

---

## 12. 包管理

本地 path dependency：

```bash
abel add path ../dep .
abel update .
abel check .
abel run .
```

本地 registry：

```bash
abel package publish ../dep ../registry
abel package registry index ../registry
abel package registry check ../registry
abel package registry list ../registry
abel add registry dep '^1.0.0' ../registry .
```

`file://` registry：

```bash
abel add registry dep '^1.0.0' file:///absolute/path/to/registry .
```

当前包管理能力：

- `abel.lock.json` 锁定解析结果。
- `.abel/cache/packages` 缓存 registry package。
- `.abel/cache/registries` 镜像 `file://` registry。
- 同 package name 被解析到不同 version/source/path 会报 conflict。
- dependency package 的非 entry `src/**/*.abel` 会并入根项目。
- 跨包访问依赖顶层 `fn/struct/backend` 要求目标 `export`。

当前仍不是：

- HTTP/network registry。
- 完整 SemVer solver。
- 成熟网络下载缓存。
- 完整版本化 ABI cache invalidation。

---

## 13. module / use / export

```abel
module app.main;
use app.math;

fn int main() {
    return add(1, 2);
}
```

re-export：

```abel
module app.api;
export use app.math;
```

限定调用：

```abel
use app.math as M;

fn int main() {
    return M::add(1, 2);
}
```

当前支持：

- 同包跨模块访问必须 `use`。
- 跨包访问依赖包符号要求目标 `export`。
- `export use` 会传播真实 module import。
- alias 可用于函数、struct 类型/构造、backend 调用。

当前边界：

- alias 不随 re-export 传播。
- 还没有完整 hide/rename/per-symbol public surface。

---

## 14. backend / SDK

Abel 侧声明：

```abel
backend MathSystem {
    fn int fast_add(int a, int b);
}

fn int main() {
    return MathSystem::fast_add(1, 2);
}
```

C++ plugin 侧理想形态：

```cpp
#include <abelcore/backend_plugin_base.h>

class MathBackend final : public abel::AbelBackendPluginBase {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IAbelBackend_iid)
    Q_INTERFACES(abel::IAbelBackend)

public:
    MathBackend() {
        bind("MathSystem.fast_add", [](int a, int b) {
            return a + b;
        });
    }

    QString backendId() const override {
        return QStringLiteral("MathSystem");
    }
};
```

安装 SDK：

```bash
cmake --install build --prefix build/abel-sdk
```

CMake：

```cmake
find_package(Abel REQUIRED)

add_library(math_backend MODULE math_backend.cpp)
target_link_libraries(math_backend PRIVATE Abel::abelcore)
```

backend artifact 推荐写进 package manifest，让 `abel build/run` 自动构建、缓存和加载，不要让普通用户手写 resource JSON。

专家 ResourceNode JSON 用于调试：

```json
{
  "id": "math.backend",
  "kind": "qt_plugin",
  "path": "plugins/libmath_backend.so",
  "iid": "org.abel.IAbelBackend/1.0",
  "backendId": "MathSystem",
  "qtVersion": "6.11.1",
  "kit": "gcc_64",
  "platform": "linux",
  "compiler": "gcc",
  "compilerVersion": "14.2.0",
  "cxxStandard": "23",
  "abelAbi": "abel-core-1",
  "symbols": ["MathSystem.fast_add"]
}
```

ResourceNode 的兼容字段在加载时门禁。`resources check` 只检查 JSON 形状。

---

## 15. 调试顺序

Abel 运行期诊断包含：

- primary `file:line:column`
- source excerpt
- caret
- Abel stack frames

排错顺序：

```text
1. 先看 primary message 和 caret。
2. 再沿 stack 从最内层调用点往外看。
3. 类型问题优先确认 check/run 是否一致。
4. backend 问题同时检查 Abel 调用点、backend block 声明、resource/backendId/symbol、Qt kit/ABI。
5. 包管理问题先看 lockfile stale、registry index stale、同名 package conflict。
```

---

## 16. 阅读 Abel 本体源码

推荐顺序：

```text
examples/smoke/*.abel
tests/interpreter/test_interpreter.cpp
tests/typechecker/test_typechecker.cpp
src/abelcore/type.h
src/abelcore/value.h
src/abelcore/runtime.h
src/abelcore/typechecker.cpp
src/abelcore/interpreter.cpp
src/abelcore/builtin_registry.cpp
src/abelcore/backend_*.h/cpp
src/abelcli/main.cpp
```

不要先读完整 parser；先看可运行行为，再看类型和值模型。

---

## 17. tight v1 边界

v1 complete 的目标不是把所有想象中的语言功能都做完，而是形成一个本地可用、语义闭合、诊断可靠、工程闭环的 Abel。

v1 最小完备闭环：

```text
承诺语法都有 TypeChecker + Interpreter 行为。
check/run 对类型、引用、receiver、overload、backend call 保持一致。
template 做最简无约束形态；不做 template+interface 约束系统。
标准库覆盖本地程序常用能力。
backend/SDK 支持本地 Qt/C++ plugin 的稳定 ABI 窗口。
包管理覆盖本地 path、local/file registry、lock/cache/conflict/backend artifact。
诊断能给出源码位置、excerpt/caret 和 Abel 调用栈。
文档命令能按当前 CLI 跑通。
```

v1 不进入这些低边际无底洞：

```text
HTTP/network registry、全球包索引、签名发布
完整 C++ 模板/overload/ADL/default args 复刻
template+interface/require 约束系统
跨 Qt/跨编译器稳定 ABI
Rust 式生命周期证明或 GC
JIT/split/IDE/debugger UI/DAP
regex/locale/streaming IO/GUI 标准库
```

template 在 v1 可以做最简无约束形态，目标是让常用泛型函数/类型可用；interface/require 不与 template 绑定进 v1。未进入 v1 的语法入口要有稳定 reserved/not implemented 诊断，而不是 parser-only 幻影功能。
