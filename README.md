# Abel

Abel is an experimental programming language runtime built with Qt 6 and C++23.

Current implementation scope:

- lexer, parser, AST, basic name/type checking, and tree-run interpreter;
- C/C++-style value model with storage, lvalues/prvalues, pointers, references, structs, and vector values;
- `any`, `any...`, lambda/function values, control flow, and core builtins such as `build_string`, `print`, and vector methods;
- backend blocks, resource-node JSON, `QPluginLoader` loading, and Qt/C++ plugin dispatch through `libabelcore.so`;
- v1 package skeleton with `abel init [project-dir]`, `abel.package.json`, `abel add/remove/update/build`, local path dependencies, `abel.lock.json`, package graph consumption, CMake backend artifact auto-build metadata, and project-local backend artifact cache under `.abel/cache/backend` with `.abel-cache.json` sidecar validation;
- installable Abel SDK first slice: headers, `libabelcore.so`, `abel` CLI, `AbelConfig.cmake`, `AbelTargets.cmake`, external backend fixture coverage for `find_package(Abel REQUIRED)`, and a backend binder matrix for common scalar/vector types plus `AbelRuntimeContext&` diagnostics and `bindVariadic` / `AbelVariadicArgs` for Abel `any...`;
- CLI commands: `abel init`, `abel add`, `abel remove`, `abel update`, `abel build`, `abel check`, `abel run`, `abel package check`, `abel resources check`, and `abel run --resource`.

Abel still does **not** implement split/JIT, a large VM, registry downloads, semantic-version solving, full versioned/ABI cache invalidation, a stable cross-Qt/cross-compiler ABI, a manifest/hash audit system, or a context exporter. Backend plugin auto-build currently exists only as a first CMake-based `backendArtifacts[].build` slice.

Resource JSON `qtVersion` and `kit` are now load-time gates: `abel resources check` validates JSON shape only, while `abel run --resource` / package backend loading rejects resources whose Qt version or kit does not match the Abel runtime.

`abel build <project-dir>` copies root/dependency backend artifacts into `.abel/cache/backend/...` and writes a neighboring `<plugin>.abel-cache.json` sidecar. `abel run <project-dir>` only prefers the cached plugin when that sidecar still matches the current source artifact path, size, mtime, ResourceNode fields, and symbol list; if metadata is missing or stale, it falls back to the source artifact path until `abel build` refreshes the cache.

## Build

This repository is currently pinned to the local Qt/GCC toolchain described in `AGENTS.md`:

```bash
/home/tnuzy/Qt/Tools/CMake/bin/cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=/home/tnuzy/Qt/6.11.1/gcc_64 \
  -DCMAKE_C_COMPILER=/usr/bin/gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_CXX_STANDARD=23

/home/tnuzy/Qt/Tools/CMake/bin/cmake --build build
```

## Install / SDK

Install into a local prefix:

```bash
/home/tnuzy/Qt/Tools/CMake/bin/cmake --install build --prefix build/abel-sdk
```

External Qt backend plugins can then use:

```cmake
find_package(Abel REQUIRED)

add_library(my_backend MODULE my_backend.cpp)
target_link_libraries(my_backend PRIVATE Abel::abelcore)
```

The installed SDK is still tied to the same Qt kit/compiler ABI used to build Abel.

## Test

Use the 4GB memory cap when running tests:

```bash
/bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
```

## Smoke examples

```bash
build/abel check examples/smoke/hello.abel
build/abel run examples/smoke/hello.abel
build/abel init build/abel_init_smoke/project
build/abel build build/abel_init_smoke/project
build/abel package check examples/project
build/abel check examples/project
build/abel run examples/project
build/abel package check examples/project_backend
build/abel build examples/project_backend
build/abel run examples/project_backend
build/abel resources check plugins/examples/math_backend/resource.json
build/abel run --resource plugins/examples/math_backend/resource.json examples/smoke/backend.abel
```

## License

This repository is public for visibility and timestamped disclosure, but it is **not open source**.

All source code, documentation, examples, tests, designs, names, and related materials are proprietary and all rights are reserved. See `LICENSE`.
