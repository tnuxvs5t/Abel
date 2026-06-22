cmake_minimum_required(VERSION 3.30)

project(Abel
    VERSION 0.0.1
    DESCRIPTION "Abel v0 language core and Qt backend runtime"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Test)

add_library(abelcore SHARED
    src/abelcore/source_span.h
    src/abelcore/diagnostic.h
    src/abelcore/abel_version.h
    src/abelcore/abel_version.cpp
    src/abelcore/token.h
    src/abelcore/lexer.h
    src/abelcore/lexer.cpp
    src/abelcore/ast.h
    src/abelcore/ast.cpp
    src/abelcore/parser.h
    src/abelcore/parser.cpp
    src/abelcore/type.h
    src/abelcore/type.cpp
    src/abelcore/value.h
    src/abelcore/value.cpp
    src/abelcore/runtime.h
    src/abelcore/runtime.cpp
    src/abelcore/backend_interface.h
    src/abelcore/backend_binder.h
    src/abelcore/backend_plugin_base.h
    src/abelcore/backend_registry.h
    src/abelcore/backend_registry.cpp
    src/abelcore/resource_node.h
    src/abelcore/resource_node.cpp
    src/abelcore/package_manifest.h
    src/abelcore/package_manifest.cpp
    src/abelcore/builtin_registry.h
    src/abelcore/builtin_registry.cpp
    src/abelcore/typechecker.h
    src/abelcore/typechecker.cpp
    src/abelcore/interpreter.h
    src/abelcore/interpreter.cpp
)

target_include_directories(abelcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(abelcore
    PUBLIC
        Qt6::Core
)

target_compile_definitions(abelcore
    PRIVATE
        ABELCORE_LIBRARY
)

add_executable(abel
    src/abelcli/main.cpp
)

target_link_libraries(abel
    PRIVATE
        abelcore
        Qt6::Core
)

add_library(math_backend MODULE
    plugins/examples/math_backend/math_backend.cpp
)

target_link_libraries(math_backend
    PRIVATE
        abelcore
        Qt6::Core
)

set_target_properties(math_backend PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    OUTPUT_NAME math_backend
)

enable_testing()

add_executable(abel_smoke_tests
    tests/smoke/test_smoke.cpp
)

target_link_libraries(abel_smoke_tests
    PRIVATE
        abelcore
        Qt6::Core
        Qt6::Test
)

add_test(NAME abel_smoke_tests COMMAND abel_smoke_tests)

add_executable(abel_parser_tests
    tests/parser/test_parser.cpp
)

target_link_libraries(abel_parser_tests
    PRIVATE
        abelcore
        Qt6::Core
        Qt6::Test
)

add_test(NAME abel_parser_tests COMMAND abel_parser_tests)

add_executable(abel_interpreter_tests
    tests/interpreter/test_interpreter.cpp
)

target_link_libraries(abel_interpreter_tests
    PRIVATE
        abelcore
        Qt6::Core
        Qt6::Test
)

add_test(NAME abel_interpreter_tests COMMAND abel_interpreter_tests)

add_executable(abel_builtin_tests
    tests/builtin/test_builtin_registry.cpp
)

target_link_libraries(abel_builtin_tests
    PRIVATE
        abelcore
        Qt6::Core
        Qt6::Test
)

add_test(NAME abel_builtin_tests COMMAND abel_builtin_tests)

add_executable(abel_typechecker_tests
    tests/typechecker/test_typechecker.cpp
)

target_link_libraries(abel_typechecker_tests
    PRIVATE
        abelcore
        Qt6::Core
        Qt6::Test
)

add_test(NAME abel_typechecker_tests COMMAND abel_typechecker_tests)

add_executable(abel_backend_resource_tests
    tests/backend/test_backend_resource.cpp
)

target_link_libraries(abel_backend_resource_tests
    PRIVATE
        abelcore
        Qt6::Core
        Qt6::Test
)

add_dependencies(abel_backend_resource_tests math_backend)

add_test(NAME abel_backend_resource_tests COMMAND abel_backend_resource_tests)

add_executable(abel_package_manifest_tests
    tests/package/test_package_manifest.cpp
)

target_link_libraries(abel_package_manifest_tests
    PRIVATE
        abelcore
        Qt6::Core
        Qt6::Test
)

add_test(NAME abel_package_manifest_tests COMMAND abel_package_manifest_tests)
