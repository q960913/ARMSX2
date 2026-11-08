// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Force-included into every C++ file of the HarmonyOS build (see CMakeLists.txt).
//
// The HarmonyOS NDK ships clang/libc++ 15, a little older than the toolchains the
// core is written against. This fills the few library gaps the core runs into, so
// the shared sources need no HarmonyOS-specific edits for them.

#pragma once

#ifdef __cplusplus

#include <cstddef>
#include <memory>
#include <type_traits>

// C++20 only: some third-party code in the build is compiled as C++17.
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 160000 && __cplusplus >= 202002L
namespace std
{
	// libc++ 16.
	template <class T>
		requires(!std::is_array_v<T>)
	inline std::unique_ptr<T> make_unique_for_overwrite()
	{
		return std::unique_ptr<T>(new T);
	}

	template <class T>
		requires(std::is_unbounded_array_v<T>)
	inline std::unique_ptr<T> make_unique_for_overwrite(std::size_t n)
	{
		return std::unique_ptr<T>(new std::remove_extent_t<T>[n]);
	}
} // namespace std
#endif

#endif // __cplusplus
