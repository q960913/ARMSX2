#-------------------------------------------------------------------------------
# HarmonyOS / OpenHarmony dependency sourcing.
#
# HarmonyOS has no system packages for any of this, so everything is built from
# source. The sources are the ones the Android build already vendors under
# platforms/android/app/src/main/cpp/3rdparty (ARMSX2_VENDOR_DIR), so there is
# one copy of each library in the repository, not two.
#-------------------------------------------------------------------------------
find_package(Git)
find_package(Threads REQUIRED)

# zlib ships in the OHOS sysroot (libz.so + zlib.h).
find_package(ZLIB REQUIRED)

# add_subdirectory for a vendored library. The sources live outside this tree, so
# each needs an explicit binary directory.
macro(armsx2_vendor name)
	set(_sub "${name}")
	if(${ARGC} GREATER 1)
		set(_sub "${ARGV1}")
	endif()
	add_subdirectory("${ARMSX2_VENDOR_DIR}/${_sub}" "${CMAKE_BINARY_DIR}/3rdparty/${_sub}" EXCLUDE_FROM_ALL)
endmacro()

# zstd
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
armsx2_vendor(zstd)
if(NOT TARGET Zstd::Zstd)
	add_library(Zstd::Zstd ALIAS libzstd_static)
endif()

# freetype
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
armsx2_vendor(freetype)
if(NOT TARGET Freetype::Freetype)
	add_library(Freetype::Freetype ALIAS freetype)
endif()

armsx2_vendor(plutovg)
armsx2_vendor(plutosvg)

# SDL3: the core links it for its gamepad database and SDL input/audio sources.
# HarmonyOS is not an SDL platform, so only the parts with no OS backend are
# built. Pads reach the emulator through the NAPI bridge instead (ohos_main.cpp).
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
foreach(_sdl_sub VIDEO RENDER GPU CAMERA AUDIO HAPTIC HIDAPI SENSOR DIALOG TRAY JOYSTICK)
	set(SDL_${_sdl_sub} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(_sdl_be ALSA PULSEAUDIO PIPEWIRE JACK SNDIO OSS X11 WAYLAND KMSDRM VULKAN OPENGL OPENGLES DBUS IBUS LIBUDEV LIBURING)
	set(SDL_${_sdl_be} OFF CACHE BOOL "" FORCE)
endforeach()
armsx2_vendor(sdl3)
if(NOT TARGET SDL3::SDL3)
	add_library(SDL3::SDL3 ALIAS SDL3-static)
endif()

# libjpeg-turbo
set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
set(WITH_TURBOJPEG OFF CACHE BOOL "" FORCE)
armsx2_vendor(libjpeg-turbo)
target_include_directories(jpeg-static PUBLIC
	$<BUILD_INTERFACE:${ARMSX2_VENDOR_DIR}/libjpeg-turbo/src>
	$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/3rdparty/libjpeg-turbo>
)
if(NOT TARGET JPEG::JPEG)
	add_library(JPEG::JPEG ALIAS jpeg-static)
endif()

# libpng
set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
# libpng generates pnglibconf.h by running the preprocessor without the target
# flags, which cannot find the OHOS sysroot's per-arch headers. Use the prebuilt
# header, as libpng itself does for Android and iOS.
set(PNG_LIBCONF_HEADER "${ARMSX2_VENDOR_DIR}/libpng/pnglibconf.h.prebuilt" CACHE FILEPATH "" FORCE)
armsx2_vendor(libpng)
if(NOT TARGET PNG::PNG)
	add_library(PNG::PNG ALIAS png_static)
endif()

# libwebp
foreach(_webp ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO WEBPMUX EXTRAS)
	set(WEBP_BUILD_${_webp} OFF CACHE BOOL "" FORCE)
endforeach()
armsx2_vendor(libwebp)
if(NOT TARGET WebP::libwebp)
	add_library(WebP::libwebp ALIAS webp)
endif()

# lz4
set(LZ4_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
armsx2_vendor(lz4 lz4/build/cmake)
if(NOT TARGET LZ4::LZ4)
	add_library(LZ4::LZ4 ALIAS lz4_static)
endif()

# shaderc, for the Vulkan renderer's runtime GLSL -> SPIR-V. Its SPIRV-Tools /
# glslang sources are fetched on demand, like the Android CI does:
#   python3 platforms/android/app/src/main/cpp/3rdparty/shaderc/utils/git-sync-deps
if(USE_VULKAN)
	set(SHADERC_SKIP_INSTALL ON CACHE BOOL "" FORCE)
	set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
	set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
	set(SHADERC_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
	set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "" FORCE)
	set(SHADERC_ENABLE_WERROR_COMPILE OFF CACHE BOOL "" FORCE)
	set(SHADERC_THIRD_PARTY_ROOT_DIR "${ARMSX2_VENDOR_DIR}/shaderc/third_party" CACHE STRING "" FORCE)
	set(SHADERC_SPIRV_TOOLS_DIR "${SHADERC_THIRD_PARTY_ROOT_DIR}/spirv-tools" CACHE STRING "" FORCE)
	set(SHADERC_SPIRV_HEADERS_DIR "${SHADERC_THIRD_PARTY_ROOT_DIR}/spirv-headers" CACHE STRING "" FORCE)
	set(SHADERC_GLSLANG_DIR "${SHADERC_THIRD_PARTY_ROOT_DIR}/glslang" CACHE STRING "" FORCE)
	if(NOT EXISTS "${SHADERC_GLSLANG_DIR}/CMakeLists.txt")
		message(FATAL_ERROR "shaderc's third-party sources are missing. Run:\n"
			"  python3 platforms/android/app/src/main/cpp/3rdparty/shaderc/utils/git-sync-deps")
	endif()
	set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
	set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
	set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
	set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
	set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
	set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
	set(SPIRV_TOOLS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
	armsx2_vendor(shaderc)
	set(SHADERC_INCLUDE_DIR "${ARMSX2_VENDOR_DIR}/shaderc/libshaderc/include")
endif()

armsx2_vendor(fast_float)
armsx2_vendor(rapidyaml)
if(NOT TARGET ryml::ryml)
	add_library(ryml::ryml ALIAS pcsx2-rapidyaml)
endif()
armsx2_vendor(lzma)
armsx2_vendor(libchdr)
disable_compiler_warnings_for_target(libchdr)
armsx2_vendor(soundtouch)
armsx2_vendor(simpleini)
armsx2_vendor(imgui)
# cpuinfo does not know the "OHOS" system name and would build a stub that always
# fails to initialise. HarmonyOS exposes the same /proc and /sys CPU files as
# Linux, so build its Linux backend.
set(_armsx2_system_name "${CMAKE_SYSTEM_NAME}")
set(CMAKE_SYSTEM_NAME Linux)
armsx2_vendor(cpuinfo)
set(CMAKE_SYSTEM_NAME "${_armsx2_system_name}")
disable_compiler_warnings_for_target(cpuinfo)
armsx2_vendor(libzip)
armsx2_vendor(rcheevos)
armsx2_vendor(rapidjson)
armsx2_vendor(discord-rpc)
armsx2_vendor(freesurround)

if(USE_OPENGL)
	armsx2_vendor(glad)
endif()

if(USE_VULKAN)
	armsx2_vendor(vulkan)
endif()

# cubeb has no HarmonyOS backend; it is built only because the core's audio code
# links it. Sound goes through OHAudio (OhosAudioStream.cpp).
armsx2_vendor(cubeb)
disable_compiler_warnings_for_target(cubeb)
disable_compiler_warnings_for_target(speex)

armsx2_vendor(demangler)
armsx2_vendor(ccc)
armsx2_vendor(vixl)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFMT_USE_EXCEPTIONS=0 -DFMT_USE_RTTI=0")
armsx2_vendor(fmt)
