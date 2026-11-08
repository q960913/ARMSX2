// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// HarmonyOS frontend: what the NAPI layer (ohos_main.cpp) asks of the host
// (ohos_host.cpp). The host implements PCSX2's Host:: interface and owns the
// emulator's CPU thread, modelled on the pcsx2-sdl frontend; the UI is PCSX2's
// own FullscreenUI (Big Picture), drawn into the XComponent surface.

#pragma once

#include <cstdint>
#include <string>

struct NativeResourceManager;
struct NativeWindow;

namespace OhosHost
{
	/// Copies the bundled resources (rawfile/resources) into <files_dir>/resources
	/// when they are missing or from another build. Defined in ohos_resources.cpp.
	bool SyncResources(const NativeResourceManager* mgr, const std::string& files_dir, std::string* error);

	/// Loads settings rooted at files_dir and starts the CPU thread in FullscreenUI.
	bool Start(const std::string& files_dir, std::string* error);

	/// Stops the VM and joins the CPU thread.
	void Stop();

	/// True when the kernel gave us writable+executable memory, i.e. the JIT runs.
	/// Only meaningful once a game has booted (the code area is mapped at startup).
	bool HasJit();

	/// The XComponent surface. window may be null when it goes away.
	void SetSurface(NativeWindow* window, uint32_t width, uint32_t height);

	/// Touch on the surface, in surface pixels. down: 1 press, 0 release, -1 move.
	void PointerEvent(float x, float y, int down);

	/// A DualShock 2 input (PadDualshock2::Inputs index) from the on-screen pad or a
	/// physical controller. value 0..1.
	void PadInput(uint32_t port, uint32_t input, float value);

	/// Opens the in-game pause menu (or does nothing outside a game).
	void OpenPauseMenu();

	/// App moved to the background / foreground.
	void SetAppForeground(bool foreground);

	/// Rescans the game folders (after the user imported files).
	void RefreshGameList();

	/// Boots a disc image directly.
	void BootFile(const std::string& path);

	/// Directory the app imports BIOS images and games into.
	std::string GetBiosDirectory();
	std::string GetGamesDirectory();
} // namespace OhosHost
