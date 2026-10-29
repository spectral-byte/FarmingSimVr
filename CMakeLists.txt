cmake_minimum_required(VERSION 3.20)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
project(FarmingSimVr LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure MSVC uses standard-conforming flags and UTF-8
if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
endif()

# Shipped configuration: RelWithDebInfo with full inlining (/Ob2) and a PDB.
# CMake's MSVC default is /Ob1, which only inlines functions marked inline. Set before
# FetchContent so the statically linked dependencies (Luau, MinHook, OpenXR) match.
# CI and scripts/package_release.ps1 verify this value in CMakeCache.txt.
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/Zi /O2 /Ob2 /DNDEBUG" CACHE STRING "RelWithDebInfo C++ flags (full inlining)" FORCE)
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "/Zi /O2 /Ob2 /DNDEBUG" CACHE STRING "RelWithDebInfo C flags (full inlining)" FORCE)
endif()

# Static MSVC runtime (/MT): the Injector must not import VCRUNTIME140.dll, MSVCP140.dll or the
# api-ms-win-crt-* DLLs, so players need no VC++ redistributable. Set before FetchContent so the
# statically linked dependencies and the test executable use the same runtime as the Injector.
# CI verifies the built dxgi.dll with scripts/check_injector_imports.ps1.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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
# As a static library the OpenXR loader sets its own MSVC_RUNTIME_LIBRARY to the dynamic CRT, which
# overrides CMAKE_MSVC_RUNTIME_LIBRARY above; set it back to the Injector's static runtime.
if(MSVC)
    set_target_properties(openxr_loader PROPERTIES
        MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()

# Fetch GoogleTest for TDD
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
)
# GoogleTest must follow the static runtime chosen above (the test executable and the Injector agree).
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Fetch Luau for in-process Luau VM interaction and TDD
set(LUAU_BUILD_CLI OFF CACHE BOOL "Do not build Luau CLI" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "Do not build Luau tests" FORCE)
# Must match the Luau FS25 embeds; bumping it also means updating the host fingerprint (ADR-0002 amendment).
set(FS25VR_LUAU_TAG 0.662)
FetchContent_Declare(
    luau
    GIT_REPOSITORY https://github.com/luau-lang/luau.git
    GIT_TAG ${FS25VR_LUAU_TAG}
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
    src/core/thread_trace.cpp
    src/core/thread_trace.h
    src/core/perf_trace.cpp
    src/core/perf_trace.h
    src/core/math_utils.h
    src/core/raii_utils.h
    src/core/camera_records.cpp
    src/core/camera_records.h
    src/core/detour.cpp
    src/core/detour.h
    src/core/injector_runtime.cpp
    src/core/injector_runtime.h
    src/core/dll_lifecycle.cpp
    src/core/dll_lifecycle.h
    src/input/virtual_gamepad.cpp
    src/input/virtual_gamepad.h
    src/luau/vr_bridge.cpp
    src/luau/vr_bridge.h
    src/luau/luau_interceptor.cpp
    src/luau/luau_interceptor.h
    src/openxr/openxr_dispatch.h
    src/openxr/openxr_cursor_overlay.h
    src/openxr/openxr_cursor_overlay.cpp
    src/openxr/openxr_input.h
    src/openxr/openxr_input.cpp
    src/openxr/openxr_manager.cpp
    src/openxr/openxr_manager.h
    src/openxr/openxr_types.h
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
    src/ngx/mv_pass.h
    src/ngx/mv_pass.cpp
    src/ngx/mv_reprojection.h
    src/ngx/mv_reprojection.cpp
    src/ngx/near_estimator.h
    src/ngx/near_estimator.cpp
    src/fidelityfx/ffx_defs.h
    src/fidelityfx/ffx_mv_conventions.h
    src/fidelityfx/fidelityfx_interceptor.h
    src/fidelityfx/fidelityfx_interceptor.cpp
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

# Version resource of the Injector: "FS25 VR" product identity (the Installer uses it to tell our
# dxgi.dll from a foreign one) and the Companion Mod's version from modDesc.xml.
set(FS25VR_MODDESC "${CMAKE_CURRENT_SOURCE_DIR}/mods/FS25_VRCompanion/modDesc.xml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FS25VR_MODDESC}")
file(READ "${FS25VR_MODDESC}" FS25VR_MODDESC_TEXT)
if(NOT FS25VR_MODDESC_TEXT MATCHES "<version>([0-9]+(\\.[0-9]+)*)</version>")
    message(FATAL_ERROR "No numeric <version> found in ${FS25VR_MODDESC}")
endif()
set(FS25VR_VERSION "${CMAKE_MATCH_1}")
# VERSIONINFO needs exactly four numbers.
string(REPLACE "." ";" FS25VR_VERSION_PARTS "${FS25VR_VERSION}.0.0.0")
list(SUBLIST FS25VR_VERSION_PARTS 0 4 FS25VR_VERSION_PARTS)
string(REPLACE ";" "," FS25VR_VERSION_COMMA "${FS25VR_VERSION_PARTS}")
configure_file(src/dxgi.rc.in "${CMAKE_CURRENT_BINARY_DIR}/dxgi.rc" @ONLY)

# DXGI Proxy DLL target
add_library(dxgi SHARED
    src/dllmain.cpp
    src/proxy/dxgi_exports.def
    "${CMAKE_CURRENT_BINARY_DIR}/dxgi.rc"
)

target_link_libraries(dxgi PRIVATE fs25_vr_core)

# Stand-in nvngx.dll for tests (exports the NGX D3D12 entry points)
add_library(fake_nvngx SHARED tests/fake_nvngx.cpp)
set_target_properties(fake_nvngx PROPERTIES OUTPUT_NAME nvngx)

# Test executable target
add_executable(fs25_vr_tests
    tests/test_dll_lifecycle.cpp
    tests/test_math_utils.cpp
    tests/test_camera_records.cpp
    tests/test_mv_reprojection.cpp
    tests/test_near_estimator.cpp
    tests/test_logger.cpp
    tests/test_thread_trace.cpp
    tests/test_perf_trace.cpp
    tests/test_openxr_cursor_overlay.cpp
    tests/test_openxr_manager.cpp
    tests/test_openxr_eye_copy_d3d12.cpp
    tests/test_openxr_motion_controllers.cpp
    tests/test_mirror_window_controller.cpp
    tests/test_dxgi_proxy.cpp
    tests/test_process_isolation.cpp
    tests/test_detour.cpp
    tests/test_vr_bridge.cpp
    tests/test_luau_interceptor.cpp
    tests/test_companion_mod.cpp
    tests/test_motion_controllers.cpp
    tests/test_pointer_ray.cpp
    tests/test_virtual_hands.cpp
    tests/test_pointer_handover.cpp
    tests/test_haptics.cpp
    tests/test_virtual_gamepad.cpp
    tests/test_virtual_gamepad_rumble.cpp
    tests/test_xinput_hooks.cpp
    tests/test_interactive_control_auto_on.cpp
    tests/test_menu_pointer_panel.cpp
    tests/test_menu_pointer.cpp
    tests/test_pointer_hand_switch.cpp
    tests/test_virtual_gamepad_buttons.cpp
    tests/test_menu_scroll_back.cpp
    tests/test_wheel_grab_steering_wrap.cpp
    tests/test_wheel_grab.cpp
    tests/test_wheel_grab_pinned_hand.cpp
    tests/test_wheel_grab_vehicles.cpp
    tests/test_wheel_grab_pointer.cpp
    tests/test_wheel_grab_ending.cpp
    tests/test_wheel_grab_two_hands.cpp
    tests/test_virtual_gamepad_menu_use.cpp
    tests/test_virtual_gamepad_cab_use.cpp
    tests/test_virtual_gamepad_default_bindings.cpp
    tests/test_virtual_ngx_handle.cpp
    tests/test_dlss_evaluation.cpp
    tests/test_ngx_interceptor.cpp
    tests/test_fidelityfx_interceptor.cpp
    tests/test_hand_tools_lance.cpp
    tests/test_hand_tools_trigger.cpp
    tests/test_hand_tools_look_ray.cpp
    tests/test_hand_tools_flashlight.cpp
    tests/luau_test_runner.cpp
    tests/test_luau_suites.cpp
    tests/test_hand_tools_left_hand.cpp
    tests/test_hand_tools_chainsaw.cpp
    tests/test_hand_tools_haptics.cpp
    tests/test_hand_tools_wrist_roll.cpp
    tests/test_hand_tools_offset_tuning.cpp
    tests/test_hand_tools_describe_unsupported.cpp
    tests/test_hand_tools_spray_can.cpp
    tests/test_hand_tools_spray_can_feedback.cpp
    tests/test_hand_tools_draw.cpp
    tests/test_hand_tools_horse_brush.cpp
    tests/test_hand_tools_fishing_rod.cpp
    tests/test_hand_tools_fishing_rod_haptics.cpp
    tests/test_hand_tools_fishing_rod_diagnostics.cpp
    tests/test_dialog_diagnostics.cpp
    tests/test_stereo_dialogs.cpp
    tests/test_fish_catch_auto_answer.cpp
    tests/test_object_grab_aim.cpp
    tests/test_object_grab_carry.cpp
    tests/test_object_grab_diagnostics.cpp
    tests/test_object_grab_feedback.cpp
    tests/test_object_grab_game_actions.cpp
    tests/test_object_grab_grip.cpp
    tests/test_object_grab_off_hand.cpp
    tests/test_object_grab_snap.cpp
    tests/test_object_grab_throw.cpp
    tests/test_object_grab_wraps.cpp
    tests/test_animated_objects.cpp
    tests/test_animated_objects_haptics.cpp
    tests/test_animated_objects_nearer.cpp
    tests/test_animated_objects_pointer.cpp
    tests/test_animated_objects_wraps.cpp
)

target_link_libraries(fs25_vr_tests PRIVATE
    fs25_vr_core
    GTest::gtest_main
    Luau.Compiler
    dxgi.lib
    xinput.lib
)

# The binary-load test loads the freshly built dxgi.dll next to the test executable
add_dependencies(fs25_vr_tests dxgi)

target_compile_definitions(fs25_vr_tests PRIVATE
    PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    FAKE_NVNGX_PATH="$<TARGET_FILE:fake_nvngx>"
    FS25VR_LUAU_TAG="${FS25VR_LUAU_TAG}"
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
