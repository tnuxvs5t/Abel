# Abel

[English](README.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja.md)

**Abel is an AI-assisted native engineering language.**

AI builds backend-powered modules.  
Users compose them through clean C-like `.abel` frontends.

Abel is not trying to replace C++, Python, Lua, or Bash directly.  
It is designed for a different programming model:

```text
C++ / native backend  →  Abel module interface  →  user-written .abel frontend
```

The goal is simple:

> Let AI handle engineering complexity, while humans write small, readable, composable Abel programs.

---

## Status

**Current version: Abel v1.2**

v1.2 is fully implemented and released.  
The internal full test suite passes.

v1.2 introduces Abel's **Dynamic Waterworks** layer:

```abel
any tuple = [[1, "text", true]];

any object = [{"name" = "Abel", "version" = 12, "released" = true}];

any next = object["version"] |> _ + 1;
```

This gives Abel dynamic composition ability without polluting the static core type system.

---

## What Abel Is

Abel is a compact C-like language for local native engineering.

It is designed around five ideas:

```text
1. C-like readability
2. Small static core
3. Explicit dynamic boundary
4. Backend-first extensibility
5. AI-assisted module engineering
```

A typical Abel project is not written entirely by hand.

Instead:

```text
AI:
  - writes native backends
  - binds external libraries
  - generates Abel modules
  - maintains package/resource metadata
  - writes tests
  - handles repetitive engineering work

User:
  - reads module interfaces
  - writes .abel frontend logic
  - composes backend capabilities
  - builds local tools quickly
```

Abel turns AI-generated backend code into human-readable, human-controllable project interfaces.

---

## Why Abel?

### 1. Abel keeps C/C++ structure, but removes daily friction

C++ is powerful, but heavy for small local tools, glue logic, automation scripts, and project frontends.

Abel keeps the familiar C-like mental model:

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

But Abel avoids forcing every small program into full C++ project complexity.

---

### 2. Abel is dynamic, but not dynamically chaotic

Python and JavaScript are dynamic everywhere.

Abel is different.

Normal Abel code is still structured and checkable.  
Dynamic behavior enters through explicit boundaries:

```abel
any tuple = [[1, "hello", true]];

any object = [{"name" = "Nitori", "score" = 42}];

str name = cast(object["name"]);
int score = cast(object["score"]);
```

The dynamic layer is visible.  
It is a waterway, not a leak.

---

### 3. Abel is backend-first

Abel does not try to implement every heavy capability inside the language itself.

Heavy work belongs in backends:

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

Abel provides the frontend layer:

```abel
fn int main() {
    any files = fs::scan("input");

    files
        |> image::resize_all(_, 1024)
        |> fs::write_all("output", _);

    return 0;
}
```

The user writes readable composition logic.  
The backend handles the machinery.

---

## The Abel Programming Model

Abel projects are built around a simple separation:

```text
Backend layer:
  Native code, external libraries, heavy computation, system integration.

Module layer:
  Abel-facing declarations, stable interfaces, resources, package metadata.

Frontend layer:
  User-written .abel files that compose modules into actual tools.
```

This is the core Abel workflow:

```text
1. Describe the capability you need.
2. AI writes or updates the backend/module.
3. Abel exposes a clean interface.
4. You write the .abel frontend.
5. Run, test, refine.
```

Abel is therefore not just a language.  
It is a protocol for AI-assisted local engineering.

---

## Dynamic Waterworks

v1.2 introduces Abel's dynamic composition layer.

### Dynamic tuple

```abel
any row = [[1, "Abel", true]];
```

`[[...]]` creates a dynamic tuple-like value.  
It is intentionally distinct from normal static structures.

### Dynamic string map

```abel
any meta = [{"name" = "Abel", "version" = 12, "ok" = true}];
```

`[{"key" = value}]` creates a dynamic string-keyed map-like value.

This syntax is intentional.  
It avoids parser ambiguity and marks a clear dynamic boundary.

### Pipe with hole

```abel
any result =
    meta
    |> _["version"]
    |> _ + 1;
```

`_` represents the value flowing through the pipe.

This enables compact data transformation without turning the whole language into a dynamic scripting mess.

---

## Design Philosophy

Abel follows several hard design rules.

### Keep the static core small

The core language should stay understandable.

Abel should not become a miniature C++ template system.

### Put complexity behind modules

If a feature requires complicated runtime machinery, native libraries, large dependencies, or high-performance logic, it should usually live behind a backend/module boundary.

### Make dynamic behavior visible

Dynamic data is useful.  
Hidden dynamic behavior is dangerous.

Abel uses `any`, dynamic literals, indexing, casting, and pipe syntax to make dynamic flow explicit.

### Optimize for human + AI collaboration

Abel code should be easy for humans to read and easy for AI agents to modify safely.

This means:

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

Abel is designed for:

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

A good Abel project feels like this:

```text
C++ does the heavy work.
AI writes the bridge.
Abel exposes the interface.
The user writes the frontend.
```

---

## What Abel Is Not

Abel is not designed to be:

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

Abel's strength is not maximal language power.

Its strength is controlled composition.

---

## Example

A small Abel frontend may look like this:

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

The user writes the workflow.  
The backend modules provide `fs` and `image`.

---

## CLI

Abel provides a local project workflow through the `abel` command.

Common commands include:

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

The CLI is designed to support a complete local engineering loop:

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

## Project Structure

A typical Abel repository may contain:

```text
src/
  Abel core implementation

examples/
  Example Abel programs and projects

tests/
  Parser, typechecker, interpreter, backend, package, CLI, and SDK tests

plugins/
  Example backend plugins

abel.package.json
  Package metadata

abel.lock.json
  Locked dependency state
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
