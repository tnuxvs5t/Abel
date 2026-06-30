# Abel

[English](README.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja.md) | [简体中文教程](Tutorial.zh-CN.md)

**Abel は、AI 支援型のネイティブエンジニアリング言語です。**

AI が backend 駆動の module を構築し、  
ユーザーは簡潔な C-like `.abel` frontend でそれらを組み合わせます。

Abel は C++、Python、Lua、Bash を直接置き換えることを目的としていません。  
Abel が目指しているのは、別のプログラミング分担です。

```text
C++ / native backend  →  Abel module interface  →  user-written .abel frontend
```

Abel の目標は単純です。

> AI にエンジニアリングの複雑さを処理させ、人間は短く、読みやすく、組み合わせやすい Abel プログラムを書く。

---

## Status

**Current version: Abel v1.3**

v1.3 は現在の tree で閉じています。
内部のフルテストスイートはすべて通過しています。

v1.3 には Abel の **Dynamic Waterworks** layer が含まれます。

```abel
any tuple = [[1, "text", true]];

any object = [{"name" = "Abel", "version" = 12, "released" = true}];

any next = object["version"] |> _ + 1;
```

これにより、Abel は静的コア型システムを汚染せずに、動的な合成能力を獲得します。

v1.3 には `do { ... }` expression と C-like compound assignment も含まれます。
`do` は即時実行される local expression block で、pipe RHS で複数ステップを明示したい場合に使います。

```abel
any out = req |> do {
    any body = _["body"];
    int timeout = cast<int>(body["timeout"]);
    return [{"timeout" = timeout, "body" = body}];
};
```

`do` 内の `return` は do expression の結果を返し、外側の function からは return しません。

Compound assignment により、C programmer にとって自然に書けます。

```abel
score += 3;
score <?= limit;
box += item; // operator +=(Box& box, Item item) を呼べる
```

Tuple cast is a small Dynamic Waterworks sugar for unpacking dynamic tuples without writing one cast line per element:

```abel
any tup = [[1, 2, 3.5, "skip", 9]];
int k = 0;
[[any a, i32 b, f64& m, /, k]] = tup;
```

---

## Abel とは

Abel は、ローカル native engineering のためのコンパクトな C-like 言語です。

Abel は次の五つの考え方を中心に設計されています。

```text
1. C-like readability
2. Small static core
3. Explicit dynamic boundary
4. Backend-first extensibility
5. AI-assisted module engineering
```

典型的な Abel プロジェクトは、すべてをユーザーが手書きするものではありません。

より自然な分担は次の形です。

```text
AI:
  - native backend を書く
  - 外部ライブラリを bind する
  - Abel module を生成する
  - package/resource metadata を管理する
  - tests を書く
  - 反復的な engineering work を処理する

User:
  - module interface を読む
  - .abel frontend logic を書く
  - backend capabilities を組み合わせる
  - local tools を素早く構築する
```

Abel は、AI が生成した backend code を、人間が読めて、制御できて、組み合わせられる project interface に変換します。

---

## Why Abel?

### 1. Abel は C/C++ の構造感を保ちながら、日常的な摩擦を減らします

C++ は強力ですが、小さな local tools、glue logic、automation scripts、project frontend には重すぎることがあります。

Abel は C-like なメンタルモデルを保ちます。

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

しかし Abel は、すべての小さなプログラムに完全な C++ project complexity を要求しません。

---

### 2. Abel は dynamic ですが、dynamic chaos ではありません

Python と JavaScript は全体が dynamic です。

Abel は違います。

通常の Abel code は、構造化され、check 可能です。  
Dynamic behavior は明示的な boundary から入ります。

```abel
any tuple = [[1, "hello", true]];

any object = [{"name" = "Nitori", "score" = 42}];

str name = cast<str>(object["name"]);
int score = cast<int>(object["score"]);
```

Dynamic layer は見える場所にあります。  
それは漏水ではなく、水路です。

---

### 3. Abel は backend-first です

Abel は、重い能力をすべて言語本体の中に実装しようとはしません。

重い処理は backend に置くべきです。

```text
image processing
audio processing
filesystem tools
math engines
native UI tools
compiler helpers
external command wrappers
high-performance C++ code
```

Abel は frontend layer を提供します。

```abel
fn int main() {
    any files = fs::scan("input");

    files
        |> image::resize_all(_, 1024)
        |> fs::write_all("output", _);

    return 0;
}
```

ユーザーは読みやすい composition logic を書きます。  
backend が機械的な複雑さを処理します。

---

## The Abel Programming Model

Abel project は単純な分層構造を中心に設計されています。

```text
Backend layer:
  native code, external libraries, heavy computation, system integration.

Module layer:
  Abel-facing declarations, stable interfaces, resources, package metadata.

Frontend layer:
  user-written .abel files that compose modules into actual tools.
```

Abel の基本 workflow は次の通りです。

```text
1. 必要な capability を記述する。
2. AI が backend/module を作成または更新する。
3. Abel が clean interface を公開する。
4. ユーザーが .abel frontend を書く。
5. 実行、テスト、改善する。
```

そのため、Abel は単なる言語ではありません。  
Abel は AI-assisted local engineering のための protocol です。

---

## Dynamic Waterworks

v1.3 では dynamic composition を明示的に保ちます。

### Dynamic tuple

```abel
any row = [[1, "Abel", true]];
```

`[[...]]` は dynamic tuple-like value を生成します。  
これは通常の static structure とは意図的に区別されています。

### Tuple cast

```abel
any row = [[1, "Abel", 12]];
int version = 0;
[[int id, str name, version]] = row;
```

Tuple cast is a statement-level explicit dynamic cast. Typed entries declare new variables; bare names write existing mutable variables using their current type; `/` skips one position. `T&` is accepted as a cast marker but declares a value variable, not a reference into the tuple.

### Dynamic string map

```abel
any meta = [{"name" = "Abel", "version" = 12, "ok" = true}];
```

`[{"key" = value}]` は dynamic string-keyed map-like value を生成します。

この syntax は意図的なものです。  
parser ambiguity を避け、明確な dynamic boundary を示します。

### Pipe with hole

```abel
any result =
    meta
    |> _["version"]
    |> _ + 1;
```

`_` は pipe を流れる現在の値を表します。

これにより、Abel は簡潔な data transformation を可能にしながら、言語全体を dynamic scripting の混乱に変えません。

### `do` expression

```abel
any projected = meta |> do {
    int version = cast<int>(_["version"]);
    return [{"next" = version + 1, "raw" = _}];
};
```

`do` は即時実行される expression block で、独自の local scope を持ちます。
pipe RHS では現在の `_` pipe context を使えるため、複雑な water route を新しい pipe operator なしで明示できます。

### Compound assignment

```abel
count += 1;
score >?= best;
```

Supported compound operators are `+= -= *= /= %= %%= **= <?= >?=`.
The left side must be a mutable lvalue, and the computed value is written back to the same location.
These operators can be overloaded:

```abel
fn void operator +=(Box& box, int delta) {
    box.value += delta;
}
```

Only compound assignment overloads are opened here; Abel still does not overload `=`, `&&`, `||`, `[]`, `[]=`, `|>`, or calls.

---

## Design Philosophy

Abel はいくつかの強い設計原則に従います。

### Keep the static core small

Core language は理解可能であるべきです。

Abel は miniature C++ template system になるべきではありません。

### Put complexity behind modules

複雑な runtime machinery、native libraries、大きな dependencies、high-performance logic が必要な機能は、通常 backend/module boundary の後ろに置くべきです。

### Make dynamic behavior visible

Dynamic data は有用です。  
しかし、隠れた dynamic behavior は危険です。

Abel は `any`、dynamic literals、indexing、casting、pipe syntax によって dynamic flow を明示します。

### Optimize for human + AI collaboration

Abel code は、人間が読みやすく、AI agent が安全に修正しやすいものであるべきです。

つまり：

```text
small syntax surface
clear module boundaries
stable package layout
strong diagnostics
explicit backend interfaces
testable behavior
```

---

## What Abel Is Good For

Abel は次の用途に向いています。

```text
local automation
personal engineering tools
native backend frontends
file processing
media processing
compiler helpers
project scaffolding
resource pipelines
AI-generated modules
C++/Qt backend orchestration
small-to-medium local applications
```

良い Abel project は次のように感じられるべきです。

```text
C++ does the heavy work.
AI writes the bridge.
Abel exposes the interface.
The user writes the frontend.
```

---

## What Abel Is Not

Abel は次のものではありません。

```text
a C++ replacement
a Rust replacement
a Python ecosystem replacement
a browser frontend language
a large enterprise language
a pure academic language
a template metaprogramming playground
a maximal static type theory experiment
```

Abel の強みは、最大限の言語機能ではありません。

Abel の強みは controlled composition です。

---

## Example

小さな Abel frontend は次のようになります。

```abel
fn int main() {
    any config = [{
        "input" = "assets",
        "output" = "dist",
        "size" = 1024
    }];

    fs::scan(config["input"])
        |> image::resize_all(_, cast<int>(config["size"]))
        |> fs::write_all(cast<str>(config["output"]), _);

    return 0;
}
```

ユーザーは workflow を書きます。  
backend modules が `fs` と `image` の能力を提供します。

---

## CLI

Abel は `abel` command によって local project workflow を提供します。

Common commands:

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

CLI は local engineering loop を支援します。

```text
create project
add modules
check frontend code
run programs
test behavior
build package/backend artifacts
inspect resources
```

---

## Build From Source

Requirements:

```text
C++23 compiler
CMake
Qt6
```

Typical build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Run:

```bash
./build/abel version
```

---

## The Abel Vision

Traditional programming asks:

```text
How does a human write all the code?
```

Abel asks:

```text
How should a human and AI divide engineering complexity?
```

The answer:

```text
AI builds capabilities.
Abel exposes clean interfaces.
Humans compose those interfaces.
```

This makes Abel especially suitable for personal engineering in the AI era.

It is small enough to understand.  
Native enough to be useful.  
Dynamic enough to compose.  
Structured enough to maintain.  
Simple enough for AI to work with.

---

## License

See the repository license file.
