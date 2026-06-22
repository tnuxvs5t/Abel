# Abel

Abel is an experimental v0 programming language runtime built with Qt 6 and C++23.

Current v0 scope:

- lexer, parser, AST, basic name/type checking, and tree-run interpreter;
- C/C++-style value model with storage, lvalues/prvalues, pointers, references, structs, and vector values;
- `any`, `any...`, lambda/function values, control flow, and core builtins such as `build_string`, `print`, and vector methods;
- backend blocks, resource-node JSON, `QPluginLoader` loading, and Qt/C++ plugin dispatch through `libabelcore.so`;
- CLI commands: `abel check`, `abel run`, `abel resources check`, and `abel run --resource`.

Abel v0 intentionally does **not** implement split/JIT, a large VM, a package manager, a manifest/hash system, or a context exporter.

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

## Test

Use the 4GB memory cap when running tests:

```bash
/bin/bash -lc 'ulimit -v 4194304; /home/tnuzy/Qt/Tools/CMake/bin/ctest --test-dir build --output-on-failure -j1'
```

## Smoke examples

```bash
build/abel check examples/smoke/hello.abel
build/abel run examples/smoke/hello.abel
build/abel resources check plugins/examples/math_backend/resource.json
build/abel run --resource plugins/examples/math_backend/resource.json examples/smoke/backend.abel
```

## License

This repository is public for visibility and timestamped disclosure, but it is **not open source**.

All source code, documentation, examples, tests, designs, names, and related materials are proprietary and all rights are reserved. See `LICENSE`.
