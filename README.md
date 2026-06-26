# Abel

Abel is an experimental programming language and runtime built with **Qt 6 + C++23**.

It is public for visibility and timestamped disclosure, but it is **not open source**. See `LICENSE`.

## What Abel is

Abel is designed around:

- C/C++-style value semantics: storage, lvalues/prvalues, pointers, references, structs, and vectors.
- A tree-run interpreter with a static type checker.
- Qt-native `str` / `char` values (`QString` / `QChar`).
- Builtin standard-library slices for strings, vectors, math, file/path/env, char/any, debug, and tests.
- v1.1-a structured calls: named/default arguments, pipe holes, and limited spread into `any...`, normalized back into statically checked calls.
- `backend` blocks that call Qt/C++ plugins through `libabelcore.so`.
- v1.1-b SDK helpers that let backend plugins carry complex objects behind ordinary Abel handles/`any` boundaries.
- Package projects with `abel.package.json`, local dependencies, local registries, lockfiles, backend artifact caching, and project tests.

Abel intentionally does **not** pretend to be a safe scripting language. Pointer/null/reference/container invalidation risks follow the C/C++ capability model unless a specific diagnostic is implemented.

Abel also intentionally does **not** turn v1.1 into a dynamic object language: there is no built-in `map`/`dict`/`object` type, object literal, dynamic field access, or dynamic backend invoke in the core language. Put complex state in backend-backed libraries and expose it through ordinary modules, structs, templates, methods, `long` handles, and `any` escape hatches.

## Current command surface

```bash
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
abel package registry index <registry-dir>
abel package registry check <registry-dir>
abel package registry list <registry-dir>
abel resources check <resource.json>
```

Extra runtime flags:

```bash
abel run --resource <resource.json> <file-or-project>
abel test --filter <substring> <project-dir>
abel test --expect-fail <substring> <project-dir>
abel test --report-json <report.json> <project-dir>
abel test --report-junit <report.xml> <project-dir>
```

## Build this repository

This checkout is pinned to the local Qt/GCC kit recorded in `AGENTS.md`.

```bash
/home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64 \
  -DCMAKE_C_COMPILER=/usr/bin/gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_CXX_STANDARD=23

/home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
```

Run tests with a 4GB virtual-memory cap:

```bash
/bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
```

## Install the SDK

```bash
/home/tnuzy/Qt/Tools/CMake/bin/cmake --install build --prefix build/abel-sdk
```

External backend plugins can then use:

```cmake
find_package(Abel REQUIRED)

add_library(my_backend MODULE my_backend.cpp)
target_link_libraries(my_backend PRIVATE Abel::abelcore)
```

The SDK is tied to the same Qt kit, compiler, C++ standard, and Abel ABI metadata as the runtime that loads the plugin.

## Minimal Abel project

```bash
build/abel init build/my_project
build/abel check build/my_project
build/abel run build/my_project
```

Typical project:

```text
my_project/
  abel.package.json
  src/
    main.abel
  tests/
    smoke.abel
```

Minimal program:

```abel
fn int main() {
    println("hello from Abel");
    return 0;
}
```

Structured call example:

```abel
fn int scale(int x, int by = 2) {
    return x * by;
}

struct Point {
    int x;
    int y;

    init(int x, int y = 0) {
        this.x = x;
        this.y = y;
    }
}

fn str report(any... xs) {
    return build_string(...xs);
}

fn int main() {
    vector<any> tail = {" units", true};
    int value = 3 |> scale(x: _, by: 4);
    Point p = Point(x: value);
    str text = report("value=", p.x, ...tail);
    return text.len() + p.y;
}
```

Structured calls are syntax sugar over statically checked calls:

- `inc(1)` may use declaration defaults, while `inc(x: 1, by: 2)` is normalized to positional order before overload/type checking.
- `lhs |> f(_)`, `lhs |> f(1, _)`, `lhs |> _.method()`, and read-only multi-hole uses reuse one evaluated LHS; mutable/ref multi-hole writes are rejected.
- `...xs` only expands `vector<any>` or `any...` into an `any...` tail. It does not fill fixed parameters.
- Function value calls and builtin methods remain positional-only ABI boundaries; builtin `build_string`/`print`/`println` accept limited spread.

Backend complexity pattern:

```abel
backend StoreRuntime {
    fn long create();
    fn void set(long h, any key, any value);
    fn any get(long h, any key);
}

template <type V>
struct Store {
    long h;

    init() {
        h = StoreRuntime::create();
    }

    fn void set(str key, V value) {
        StoreRuntime::set(h, key, value);
    }

    const fn V get(str key) {
        return cast<V>(StoreRuntime::get(h, key));
    }
}
```

The Abel core only sees a normal `backend` block, `long`, `any`, and a template `struct`. A C++ backend may use SDK helpers such as `abel::AbelValueKey` and `abel::AbelBackendHandleStore<T>` internally.

## Smoke commands

```bash
build/abel check examples/smoke/hello.abel
build/abel run examples/smoke/hello.abel

build/abel package check examples/project
build/abel check examples/project
build/abel run examples/project
build/abel test examples/project

build/abel build examples/project_backend
build/abel run examples/project_backend

build/abel resources check plugins/examples/math_backend/resource.json
build/abel run --resource plugins/examples/math_backend/resource.json examples/smoke/backend.abel
```

## Main documents

- `AGENTS.md` — current development manual and agent operating contract for this repository.
- `TUTORIAL_zh.md` — Chinese tutorial for learning Abel and building Abel projects.
- `CODEX.md` — prompt file for using Codex inside a new Abel user project.

## License

This repository is public but proprietary. All rights are reserved unless a separate written license says otherwise.
