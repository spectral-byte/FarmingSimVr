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

# Fetch MinHook for inline hooking (pinned release)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(minhook)

enable_testing()

# Core source files
set(CORE_SOURCES
    src/core/logger.cpp
    src/core/logger.h
    src/core/math_utils.h
    src/core/detour.cpp
    src/core/detour.h
    src/core/injector_runtime.cpp
    src/core/injector_runtime.h
    src/core/dll_lifecycle.cpp
    src/core/dll_lifecycle.h
    src/luau/vr_bridge.cpp
    src/luau/vr_bridge.h
    src/luau/luau_interceptor.cpp
    src/luau/luau_interceptor.h
    src/openxr/openxr_dispatch.h
    src/openxr/openxr_cursor_overlay.h
    src/openxr/openxr_cursor_overlay.cpp
    src/openxr/openxr_manager.cpp
    src/openxr/openxr_manager.h
    src/proxy/system_dxgi.h
    src/proxy/system_dxgi.cpp
    src/proxy/mirror_window_controller.h
    src/proxy/mirror_window_controller.cpp
    src/proxy/dxgi_proxy.cpp
    src/proxy/dxgi_proxy.h
    src/ngx/ngx_defs.h
    src/ngx/virtual_ngx_handle.h
    src/ngx/ngx_interceptor.h
    src/ngx/ngx_interceptor.cpp
)

# Core static library: compiled once, shared by the Injector DLL and the tests
add_library(fs25_vr_core STATIC ${CORE_SOURCES})

target_include_directories(fs25_vr_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(fs25_vr_core PUBLIC
    openxr_loader
    minhook
    Luau.VM
    d3d12.lib
    comctl32.lib
)

target_compile_definitions(fs25_vr_core PUBLIC
    XR_USE_GRAPHICS_API_D3D12
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# DXGI Proxy DLL target
add_library(dxgi SHARED
    src/dllmain.cpp
    src/proxy/dxgi_exports.def
)

target_link_libraries(dxgi PRIVATE fs25_vr_core)

# Stand-in nvngx.dll for tests (exports the NGX D3D12 entry points)
add_library(fake_nvngx SHARED tests/fake_nvngx.cpp)
set_target_properties(fake_nvngx PROPERTIES OUTPUT_NAME nvngx)

# Test executable target
add_executable(fs25_vr_tests
    tests/test_main.cpp
    tests/test_dll_lifecycle.cpp
    tests/test_math_utils.cpp
    tests/test_logger.cpp
    tests/test_openxr_cursor_overlay.cpp
    tests/test_openxr_manager.cpp
    tests/test_mirror_window_controller.cpp
    tests/test_dxgi_proxy.cpp
    tests/test_detour.cpp
    tests/test_vr_bridge.cpp
    tests/test_luau_interceptor.cpp
    tests/test_companion_mod.cpp
    tests/test_virtual_ngx_handle.cpp
    tests/test_dlss_evaluation.cpp
    tests/test_ngx_interceptor.cpp
    tests/luau_test_runner.cpp
    tests/test_luau_suites.cpp
)

target_link_libraries(fs25_vr_tests PRIVATE
    fs25_vr_core
    GTest::gtest
    Luau.Compiler
    dxgi.lib
)

# The binary-load test loads the freshly built dxgi.dll next to the test executable
add_dependencies(fs25_vr_tests dxgi)

target_compile_definitions(fs25_vr_tests PRIVATE
    PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    FAKE_NVNGX_PATH="$<TARGET_FILE:fake_nvngx>"
)
add_dependencies(fs25_vr_tests fake_nvngx)

include(GoogleTest)
gtest_discover_tests(fs25_vr_tests DISCOVERY_TIMEOUT 30)

add_custom_target(run_luau_tests
    COMMAND fs25_vr_tests --gtest_filter=LuauTestSuiteTest.*
    DEPENDS fs25_vr_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running standalone Luau test suites via GoogleTest runner"
)
