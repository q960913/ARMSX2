# ARMSX2 HarmonyOS / OpenHarmony native build.
#
# Builds the shared PCSX2 core from the repository root ({common,pcsx2}) and the
# NAPI library the ArkTS app loads (libarmsx2.so). Third-party sources are the
# ones the Android build vendors (see cmake/SearchForStuff.cmake).
#
# Driven by DevEco Studio / hvigor through entry/build-profile.json5, or by hand:
#
#   cmake -G Ninja -S platforms/harmony/entry/src/main/cpp -B build-ohos \
#     -DCMAKE_TOOLCHAIN_FILE=$OHOS_NATIVE/build/cmake/ohos.toolchain.cmake \
#     -DOHOS_ARCH=arm64-v8a -DOHOS_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-ohos --target armsx2

cmake_minimum_required(VERSION 3.16...3.28)

cmake_policy(SET CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(ARMSX2Harmony C CXX)

if(NOT OHOS)
	message(FATAL_ERROR "This is the HarmonyOS build. Configure it with the OHOS NDK toolchain "
		"(build/cmake/ohos.toolchain.cmake), or let DevEco Studio do it.")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
	message(FATAL_ERROR "ARMSX2 for HarmonyOS is arm64-v8a only (OHOS_ARCH=arm64-v8a).")
endif()

get_filename_component(ARMSX2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.." ABSOLUTE)
set(ARMSX2_VENDOR_DIR "${ARMSX2_ROOT}/platforms/android/app/src/main/cpp/3rdparty")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# A mobile frontend: no Qt, no tests, no desktop runners.
set(ENABLE_QT_UI OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_GSRUNNER OFF CACHE BOOL "" FORCE)
set(ENABLE_LIBRETRO OFF CACHE BOOL "" FORCE)

# Vulkan only for now. HarmonyOS has EGL/GLES too, but the GL backend's context
# code has no OHOS path yet, and every Kirin GPU HarmonyOS NEXT runs on has Vulkan.
set(USE_OPENGL OFF CACHE BOOL "" FORCE)
set(USE_VULKAN ON CACHE BOOL "" FORCE)

# Release builds LTO the hot part of the core, like the APK.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
	set(LTO_PCSX2_CORE ON CACHE BOOL "")
endif()

set(ARMSX2_USE_BUNDLED_SHADERC ON CACHE BOOL "" FORCE)

# The core's common/ and pcsx2/ CMakeLists guard on this sentinel.
set(TOP_CMAKE_WAS_SOURCED TRUE)

include(Pcsx2Utils)
check_no_parenthesis_in_path()
detect_operating_system()
detect_compiler()

include(BuildParameters)
include(SearchForStuff)

get_git_version_info()
write_svnrev_h()

# libc++ 15 (the HarmonyOS NDK's) keeps its std::ranges algorithms behind the
# experimental switch, and lacks a few C++20 helpers the core uses; the compat
# header supplies those. Core only: set after the third-party libraries above.
add_compile_definitions($<$<COMPILE_LANGUAGE:CXX>:_LIBCPP_ENABLE_EXPERIMENTAL>)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/ohos_libcxx_compat.h>")

add_subdirectory("${ARMSX2_ROOT}/common" "${CMAKE_BINARY_DIR}/common")
add_subdirectory("${ARMSX2_ROOT}/3rdparty/libretro" "${CMAKE_BINARY_DIR}/3rdparty/libretro")
add_subdirectory("${ARMSX2_ROOT}/pcsx2" "${CMAKE_BINARY_DIR}/pcsx2")

# shaderc is linked in and called directly (VKShaderCache.cpp), as on Android:
# there is no system shaderc to dlopen.
target_link_libraries(PCSX2_FLAGS INTERFACE shaderc)
target_compile_definitions(PCSX2_FLAGS INTERFACE ARMSX2_LINK_SHADERC)

# The NAPI module. ArkTS imports it as 'libarmsx2.so'.
add_library(armsx2 SHARED
	ohos_main.cpp
	ohos_host.cpp
	ohos_resources.cpp
)

target_include_directories(armsx2 PRIVATE
	"${ARMSX2_ROOT}"
	"${ARMSX2_ROOT}/pcsx2"
	"${ARMSX2_ROOT}/3rdparty/include"
	"${CMAKE_BINARY_DIR}/common/include"
	"${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(armsx2 PRIVATE
	PCSX2
	PCSX2_FLAGS
	common
	imgui
	shaderc
	vixl
	ace_napi.z
	ace_ndk.z
	native_window
	rawfile.z
	hilog_ndk.z
)
