cmake_minimum_required(VERSION 3.20)
project(FarmingSimVr LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure MSVC uses standard-conforming flags and UTF-8
if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
endif()

include(FetchContent)

# Fetch OpenXR-SDK
set(DYNAMIC_LOADER OFF CACHE BOOL "Build static OpenXR loader" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Do not build OpenXR tests" FORCE)

FetchContent_Declare(
    OpenXR
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG release-1.1.63
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(OpenXR)

# Fetch GoogleTest for TDD
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

include_directories(
    src
    ${openxr_SOURCE_DIR}/include
)

# Core source files
set(CORE_SOURCES
    src/core/logger.cpp
    src/core/logger.h
    src/openxr/openxr_dispatch.h
    src/openxr/openxr_manager.cpp
    src/openxr/openxr_manager.h
    src/proxy/system_dxgi.h
    src/proxy/system_dxgi.cpp
    src/proxy/dxgi_proxy.cpp
    src/proxy/dxgi_proxy.h
)

# DXGI Proxy DLL target
add_library(dxgi SHARED
    ${CORE_SOURCES}
    src/dllmain.cpp
    src/proxy/dxgi_exports.def
)

target_link_libraries(dxgi PRIVATE
    openxr_loader
    d3d12.lib
)

target_compile_definitions(dxgi PRIVATE
    XR_USE_GRAPHICS_API_D3D12
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# Test executable target
add_executable(fs25_vr_tests
    ${CORE_SOURCES}
    tests/test_main.cpp
    tests/test_logger.cpp
    tests/test_openxr_manager.cpp
    tests/test_dxgi_proxy.cpp
)

target_link_libraries(fs25_vr_tests PRIVATE
    GTest::gtest_main
    openxr_loader
    dxgi.lib
    d3d12.lib
)

target_compile_definitions(fs25_vr_tests PRIVATE
    XR_USE_GRAPHICS_API_D3D12
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

include(GoogleTest)
gtest_discover_tests(fs25_vr_tests)
