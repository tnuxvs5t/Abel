# Abel

Abel is an experimental programming language and runtime built with **Qt 6 + C++23**.

It is public for visibility and timestamped disclosure, but it is **not open source**. See `LICENSE`.

## What Abel is

Abel is designed around:

- C/C++-style value semantics: storage, lvalues/prvalues, pointers, references, structs, and vectors.
- A tree-run interpreter with a static type checker.
- Qt-native `str` / `char` values (`QString` / `QChar`).
- Builtin standard-library slices for strings, vectors, math, file/path/env, debug, and tests.
- `backend` blocks that call Qt/C++ plugins through `libabelcore.so`.
- Package projects with `abel.package.json`, local dependencies, local registries, lockfiles, backend artifact caching, and project tests.

Abel intentionally does **not** pretend to be a safe scripting language. Pointer/null/reference/container invalidation risks follow the C/C++ capability model unless a specific diagnostic is implemented.

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
