# Abel

Abel is an experimental programming language runtime built with Qt 6 and C++23.

Current implementation scope:

- lexer, parser, AST, basic name/type checking, and tree-run interpreter;
- C/C++-style value model with storage, lvalues/prvalues, pointers, mutable `T&`, read-only `const T&`, structs, and vector values;
- `any`, `any...`, lambda/function values, control flow, and core builtins such as `build_string`, `print`, vector/string methods, char helpers, any inspection helpers, math helpers, and first-slice file/path helpers;
- backend blocks, resource-node JSON, `QPluginLoader` loading, and Qt/C++ plugin dispatch through `libabelcore.so`;
- static definite-return checking for functions, methods, and lambdas, so non-void callables missing a return are rejected by `abel check` before `abel run`;
- runtime diagnostics with Abel stack frames, `file:line:column`, source-line excerpts, and caret lines for the primary error and stack call sites; runtime conversion diagnostics now point at the argument, return expression, assignment RHS, or backend call that caused the conversion instead of falling back to declarations;
- `std.io` / `std.path` / `std.env` first slices through builtins `scan`, `read_text`, `write_text`, `append_text`, `read_lines`, `write_lines`, `path_exists`, `path_is_file`, `path_is_dir`, `copy_file`, `move_path`, `remove_path`, `path_join`, `path_dirname`, `path_basename`, `path_ext`, `path_absolute`, `path_clean`, `mkdirs`, `current_dir`, `env_exists`, and `env_get`, with TypeChecker and Interpreter sharing the same signatures including pipe-shaped calls;
- `std.char` / `std.any` first slices through builtins `char_code`, `char_from_code`, `char_is_digit`, `char_is_letter`, `char_is_alnum`, `char_is_space`, `char_is_upper`, `char_is_lower`, `char_upper`, `char_lower`, `char_to_str`, `any_type`, `any_is`, and typed `any_is_*` predicates; these are inspection/conversion helpers only, while `cast<T>(any)` remains the typed extraction mechanism;
- `std.debug` first slice through builtins `debug_break()` and `debug_assert(bool, any...)`, plus `std.test` first-slice builtins `test_assert`, `test_eq`, `test_ne`, and `test_close`, all reporting through the same runtime diagnostic/stack/source-location path;
- module/use/export syntax first slice and package multi-file entry: project `abel check/run/build` now parses root-package `src/**/*.abel`, also consumes dependency package non-entry `src/**/*.abel` library sources, keeps the root manifest entry file last, typechecks/runs the merged program while preserving per-file source spans, enforces explicit `use` for cross-module lookup, supports `export use` re-export propagation, supports module-qualified and import-alias-qualified function/struct/backend lookup, and rejects root-package access to non-exported dependency functions/structs/backends;
- v1 package skeleton with `abel init [project-dir]`, `abel.package.json`, `abel add/remove/update/build`, `abel package publish` into a local registry, local registry index/list/check commands, local path dependencies, local registry dependencies that pick the highest satisfying SemVer version into `.abel/cache/packages`, `file://` registry URI mirror cache under `.abel/cache/registries`, `abel.lock.json`, package graph consumption, same-package-name conflict diagnostics, CMake backend artifact auto-build metadata, and project-local backend artifact cache under `.abel/cache/backend` with `.abel-cache.json` sidecar validation;
- installable Abel SDK first slice: headers, `libabelcore.so`, `abel` CLI, `AbelConfig.cmake`, `AbelTargets.cmake`, external backend fixture coverage for `find_package(Abel REQUIRED)`, and a backend binder matrix for common scalar/vector types plus `AbelRuntimeContext&` diagnostics and `bindVariadic` / `AbelVariadicArgs` for Abel `any...`;
- CLI commands: `abel init`, `abel add`, `abel remove`, `abel update`, `abel build`, `abel test`, `abel check`, `abel run`, `abel package check`, `abel package publish`, `abel package registry index/check/list`, `abel resources check`, and `abel run --resource`.

Abel still does **not** implement split/JIT, a large VM, HTTP/network registry downloads, a full registry SemVer solver, full versioned/ABI cache invalidation, a stable cross-Qt/cross-compiler ABI, a manifest/hash audit system, or a context exporter. `const T&` currently means a read-only lvalue reference first slice; it does not yet implement prvalue lifetime extension or the full `const T*` / `T* const` matrix. `module` / `use` are now enforced for same-package cross-module lookup; dependency `export` is enforced for cross-package top-level functions/structs/backends; `export use some.module;` re-exports an imported module through the current module; module-qualified lookup and `use some.module as Alias;` work for functions, struct types/constructors, and backend calls. A complete public/private module system and full import/export surface remain v1 follow-up work. Local path and local registry dependencies now support SemVer requirement checks and reject a graph that resolves the same package name to different versions/sources; registry support is currently local directory publish/index/cache plus `file://` URI mirror-cache first slice, and backend plugin auto-build currently exists only as a first CMake-based `backendArtifacts[].build` slice.

Local registry layout:

```text
registry/
  .abel-registry.json
  dep/
    1.0.0/abel.package.json
    1.2.0/abel.package.json
```

Add a registry dependency:

```bash
build/abel package publish <dep-project-dir> ../registry
build/abel package registry check ../registry
build/abel package registry list ../registry
build/abel add registry dep '^1.0.0' ../registry <project-dir>
# equivalent URI form:
build/abel add registry dep '^1.0.0' file:///absolute/path/to/registry <project-dir>
```

`abel package publish <project-dir> <registry-dir>` copies a package to `<registry>/<name>/<version>` while skipping the package's `.abel` cache and refreshes `<registry>/.abel-registry.json`. Existing versions are rejected unless `--overwrite` is passed. `abel package registry index <registry-dir>` rebuilds the index from disk, `abel package registry check <registry-dir>` verifies the index is current, and `abel package registry list <registry-dir>` prints package/version/path rows. When `.abel-registry.json` exists, registry dependency resolution verifies and consumes that index; a stale or malformed index is rejected instead of being silently bypassed. If no index exists yet, Abel still falls back to scanning the local registry directory. `file://` registry URIs are mirrored into `<project-dir>/.abel/cache/registries/...` before normal registry resolution. `abel update/build` copies the selected version into `<project-dir>/.abel/cache/packages/dep/<version>` and records the cached path in `abel.lock.json`.

Resource JSON `qtVersion`, `kit`, `platform`, `compiler`, `compilerVersion`, `cxxStandard`, and `abelAbi` are now load-time gates: `abel resources check` validates JSON shape only, while `abel run --resource` / package backend loading rejects resources whose compatibility strings do not match the Abel runtime. Package `backendArtifacts` fill these fields from the current runtime by default, so normal users should not hand-write low-level ResourceNode JSON.

`abel build <project-dir>` copies root/dependency backend artifacts into `.abel/cache/backend/...` and writes a neighboring `<plugin>.abel-cache.json` sidecar. `abel run <project-dir>` only prefers the cached plugin when that sidecar still matches the current source artifact path, size, mtime, ResourceNode fields, platform/compiler/C++/Abel ABI strings, and symbol list; if metadata is missing or stale, it falls back to the source artifact path until `abel build` refreshes the cache.

`abel test <project-dir>` runs every `tests/**/*.abel` file in a package project. Each test is checked with the same package graph as `abel check`, gets root/dependency library sources, uses the test file as its own entry `main`, auto-loads package backend artifacts, and passes only when it exits with code `0`. If a test file declares `fn void setup()` or `fn void teardown()`, `abel test` calls them before and after `main`; `teardown` still runs after a failing `main`. Use `--filter <substring>` to run only tests whose relative path matches, `--expect-fail <substring>` to track known failing tests by relative path, `--report-json <file>` to write a machine-readable report with total/passed/failed/xfail/xpass counts plus per-test status, phase, exit code, and diagnostics, or `--report-junit <file>` to write a CI-friendly JUnit XML report. Test files can use `test_assert(cond, any...)`, `test_eq(actual, expected, any...)`, and `test_ne(actual, expected, any...)`; assertion failures produce Abel source spans and stack traces.

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
build/abel test examples/project
build/abel package check examples/project_backend
build/abel build examples/project_backend
build/abel run examples/project_backend
build/abel resources check plugins/examples/math_backend/resource.json
build/abel run --resource plugins/examples/math_backend/resource.json examples/smoke/backend.abel
```

## License

This repository is public for visibility and timestamped disclosure, but it is **not open source**.

All source code, documentation, examples, tests, designs, names, and related materials are proprietary and all rights are reserved. See `LICENSE`.
