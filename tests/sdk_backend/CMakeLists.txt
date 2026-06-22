cmake_minimum_required(VERSION 3.30)

project(AbelSdkBackendFixture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Abel REQUIRED)

add_library(sdk_fixture_backend MODULE
    sdk_fixture_backend.cpp
)

target_link_libraries(sdk_fixture_backend
    PRIVATE
        Abel::abelcore
)

set_target_properties(sdk_fixture_backend PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    OUTPUT_NAME sdk_fixture_backend
    BUILD_RPATH "$<TARGET_FILE_DIR:Abel::abelcore>"
)
