cmake_minimum_required(VERSION 3.20)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
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

# Fetch Luau for in-process Luau VM interaction and TDD
set(LUAU_BUILD_CLI OFF CACHE BOOL "Do not build Luau CLI" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "Do not build Luau tests" FORCE)
FetchContent_Declare(
    luau
    GIT_REPOSITORY https://github.com/luau-lang/luau.git
    GIT_TAG 0.650
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(luau)

enable_testing()

include_directories(
    src
    ${openxr_SOURCE_DIR}/include
    ${luau_SOURCE_DIR}/VM/include
    ${luau_SOURCE_DIR}/Compiler/include
    ${luau_SOURCE_DIR}/Common/include
)

# Core source files
set(CORE_SOURCES
    src/core/logger.cpp
    src/core/logger.h
    src/core/detour.cpp
    src/core/detour.h
    src/luau/vr_bridge.cpp
    src/luau/vr_bridge.h
    src/luau/luau_interceptor.cpp
    src/luau/luau_interceptor.h
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
    Luau.VM
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
    tests/test_detour.cpp
    tests/test_vr_bridge.cpp
    tests/test_luau_interceptor.cpp
    tests/test_companion_mod.cpp
)

target_link_libraries(fs25_vr_tests PRIVATE
    GTest::gtest_main
    openxr_loader
    Luau.VM
    Luau.Compiler
    dxgi.lib
    d3d12.lib
)

target_compile_definitions(fs25_vr_tests PRIVATE
    XR_USE_GRAPHICS_API_D3D12
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

include(GoogleTest)
gtest_discover_tests(fs25_vr_tests)
