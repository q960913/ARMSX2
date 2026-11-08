// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// HarmonyOS frontend host: PCSX2's Host:: interface, settings, and the CPU thread.
//
// Modelled on pcsx2-sdl/Main.cpp. The UI is PCSX2's FullscreenUI (Big Picture),
// drawn into the ArkUI XComponent surface by the Vulkan renderer, so the ArkTS
// side only has to supply a surface, touches and pad input (ohos_main.cpp).

#include "ohos_host.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <native_window/external_window.h>
#include <pthread.h>

#include "fmt/format.h"

#include "common/Console.h"
#include "common/CrashHandler.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/HTTPDownloader.h"
#include "common/Path.h"
#include "common/ProgressCallback.h"
#include "common/SmallString.h"
#include "common/StringUtil.h"
#include "common/Threading.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Achievements.h"
#include "pcsx2/Config.h"
#include "pcsx2/GS.h"
#include "pcsx2/GameList.h"
#include "pcsx2/Host.h"
#include "pcsx2/Host/AudioStream.h"
#include "pcsx2/INISettingsInterface.h"
#include "pcsx2/ImGui/FullscreenUI.h"
#include "pcsx2/ImGui/ImGuiFullscreen.h"
#include "pcsx2/IopThread/OwnerAsserts.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/Memory.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/SIO/Pad/PadDualshock2.h"
#include "pcsx2/VMManager.h"

namespace
{
	std::unique_ptr<INISettingsInterface> s_base_settings;
	std::unique_ptr<INISettingsInterface> s_secrets_settings;

	std::thread s_cpu_thread;
	std::atomic<std::thread::id> s_cpu_thread_id{};
	std::atomic<bool> s_shutdown_requested{false};

	// Host::RunOnCPUThread queue, drained by Host::PumpMessagesOnCPUThread while a
	// VM runs and by the idle loop while FullscreenUI is up without one.
	std::mutex s_cpu_queue_lock;
	std::deque<std::function<void()>> s_cpu_queue;
	std::condition_variable s_cpu_queue_cv;

	// The XComponent surface. The UI thread swaps it; the GS thread takes its own
	// reference in AcquireRenderWindow so a surface the UI drops stays valid until
	// the GS thread has moved off it.
	std::mutex s_window_lock;
	OHNativeWindow* s_window = nullptr;
	OHNativeWindow* s_acquired_window = nullptr;
	u32 s_window_width = 0;
	u32 s_window_height = 0;

	// Pad input arrives on the ArkUI thread and goes straight to the pad state, as on
	// Android; this keeps it off a VM that is being torn down.
	std::mutex s_pad_lock;

	std::thread s_gamelist_thread;

	bool s_was_running_before_background = false;

	std::string s_files_dir;
} // namespace

static void DrainCPUThreadQueue()
{
	for (;;)
	{
		std::function<void()> fn;
		{
			std::lock_guard lock(s_cpu_queue_lock);
			if (s_cpu_queue.empty())
				return;
			fn = std::move(s_cpu_queue.front());
			s_cpu_queue.pop_front();
		}
		fn();
	}
}

static void StopGameListRefreshThread()
{
	if (s_gamelist_thread.joinable())
		s_gamelist_thread.join();
}

//////////////////////////////////////////////////////////////////////////
// Settings
//////////////////////////////////////////////////////////////////////////

static bool InitializeConfig(const std::string& files_dir, std::string* error)
{
	// Everything lives in the app sandbox: <files>/resources (copied from the HAP),
	// <files>/inis, <files>/bios, <files>/games, memcards, states, ...
	EmuFolders::AppRoot = files_dir;
	EmuFolders::DataRoot = files_dir;
	EmuFolders::Settings = Path::Combine(files_dir, "inis");
	if (!EmuFolders::SetResourcesDirectory())
	{
		*error = "The resources directory is missing.";
		return false;
	}
	FileSystem::EnsureDirectoryExists(EmuFolders::Settings.c_str(), false);
	FileSystem::EnsureDirectoryExists(Path::Combine(files_dir, "games").c_str(), false);
	FileSystem::EnsureDirectoryExists(Path::Combine(files_dir, "bios").c_str(), false);

	CrashHandler::SetWriteDirectory(EmuFolders::DataRoot);

	const char* hw_check_error = nullptr;
	if (!VMManager::PerformEarlyHardwareChecks(&hw_check_error))
	{
		*error = fmt::format("Hardware check failed: {}", hw_check_error ? hw_check_error : "unknown");
		return false;
	}

	const std::string ini_path = Path::Combine(EmuFolders::Settings, "PCSX2.ini");
	const bool ini_exists = FileSystem::FileExists(ini_path.c_str());
	s_base_settings = std::make_unique<INISettingsInterface>(ini_path);
	Host::Internal::SetBaseSettingsLayer(s_base_settings.get());
	if (!ini_exists || !s_base_settings->Load() || !VMManager::Internal::CheckSettingsVersion())
	{
		Console.WriteLnFmt("Initialising fresh config at {}.", ini_path);
		VMManager::SetDefaultSettings(*s_base_settings, true, true, true, true, true);
	}
	VMManager::Internal::MigrateSettings(*s_base_settings);

	const std::string secrets_path = Path::Combine(EmuFolders::Settings, "secrets.ini");
	s_secrets_settings = std::make_unique<INISettingsInterface>(secrets_path);
	Host::Internal::SetSecretsSettingsLayer(s_secrets_settings.get());
	if (FileSystem::FileExists(secrets_path.c_str()))
		s_secrets_settings->Load();

	{
		auto lock = Host::GetSettingsLock();
		SettingsInterface& si = *s_base_settings;

		// Fixed for this platform: Vulkan is the only renderer built, OHAudio the
		// only working audio output, and there are no SDL/XInput devices to poll.
		si.SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(GSRendererType::VK));
		si.SetStringValue("SPU2/Output", "Backend", AudioStream::GetBackendName(AudioBackend::OHAudio));
		si.SetBoolValue("InputSources", "SDL", false);
		si.SetBoolValue("InputSources", "XInput", false);
		si.SetBoolValue("EmuCore/GS", "FullscreenMode", true);

		// First-run defaults that suit a phone/tablet; left alone once the user changes them.
		if (!si.ContainsValue("EmuCore", "EnableFastBoot"))
			si.SetBoolValue("EmuCore", "EnableFastBoot", true);
		if (!si.ContainsValue("EmuCore/GS", "VsyncEnable"))
			si.SetBoolValue("EmuCore/GS", "VsyncEnable", false);

		// The folder the app imports games into is always scanned.
		const std::string games_dir = Path::Combine(files_dir, "games");
		std::vector<std::string> dirs = si.GetStringList("GameList", "RecursivePaths");
		if (std::find(dirs.begin(), dirs.end(), games_dir) == dirs.end())
			si.AddToStringList("GameList", "RecursivePaths", games_dir.c_str());
	}

	Error save_error;
	if (!s_base_settings->Save(&save_error))
		Console.ErrorFmt("Failed to save config: {}", save_error.GetDescription());

	VMManager::Internal::LoadStartupSettings();
	return true;
}

//////////////////////////////////////////////////////////////////////////
// CPU thread
//////////////////////////////////////////////////////////////////////////

static void CPUThreadMain()
{
	Threading::SetNameOfCurrentThread("CPU Thread");
	s_cpu_thread_id.store(std::this_thread::get_id(), std::memory_order_release);

	if (!VMManager::Internal::CPUThreadInitialize())
	{
		Console.Error("CPU thread init failed.");
		VMManager::Internal::CPUThreadShutdown();
		return;
	}

	VMManager::ApplySettings();
	VMManager::ReloadInputBindings(true);

	// Straight into Big Picture: it is this frontend's whole UI.
	ImGuiManager::InitializeFullscreenUI();
	if (!MTGS::WaitForOpen())
	{
		Console.Error("Failed to open MTGS for FullscreenUI.");
		VMManager::Internal::CPUThreadShutdown();
		return;
	}
	MTGS::SetRunIdle(true);

	while (!s_shutdown_requested.load(std::memory_order_acquire))
	{
		DrainCPUThreadQueue();

		switch (VMManager::GetState())
		{
			case VMState::Initializing:
				continue;

			case VMState::Running:
				VMManager::Execute();
				continue;

			case VMState::Resetting:
				VMManager::Reset();
				continue;

			case VMState::Stopping:
				VMManager::Shutdown(false);
				continue;

			case VMState::Paused:
			case VMState::Shutdown:
			default:
			{
				// Menus only: pump input for them, then wait for work or a state change.
				VMManager::IdlePollUpdate();
				std::unique_lock lock(s_cpu_queue_lock);
				s_cpu_queue_cv.wait_for(lock, std::chrono::milliseconds(16), []() { return !s_cpu_queue.empty(); });
				continue;
			}
		}
	}

	if (VMManager::HasValidVM())
		VMManager::Shutdown(false);
	if (MTGS::IsOpen())
	{
		MTGS::SetRunIdle(false);
		MTGS::WaitForClose();
	}

	StopGameListRefreshThread();
	VMManager::Internal::CPUThreadShutdown();
	s_cpu_thread_id.store(std::thread::id{}, std::memory_order_release);
}

//////////////////////////////////////////////////////////////////////////
// OhosHost (called by the NAPI layer)
//////////////////////////////////////////////////////////////////////////

bool OhosHost::Start(const std::string& files_dir, std::string* error)
{
	if (s_cpu_thread.joinable())
		return true;

	s_files_dir = files_dir;
	CrashHandler::Install();
	Log::SetConsoleOutputLevel(LOGLEVEL_INFO);
	Log::SetFileOutputLevel(LOGLEVEL_INFO, Path::Combine(files_dir, "emulog.txt"));

	if (!InitializeConfig(files_dir, error))
		return false;

	SysMemory::ReserveMemory();

	s_shutdown_requested.store(false, std::memory_order_release);
	s_cpu_thread = std::thread(&CPUThreadMain);
	return true;
}

void OhosHost::Stop()
{
	if (!s_cpu_thread.joinable())
		return;

	s_shutdown_requested.store(true, std::memory_order_release);
	if (VMManager::HasValidVM())
		VMManager::SetState(VMState::Stopping);
	s_cpu_queue_cv.notify_all();
	s_cpu_thread.join();
}

bool OhosHost::HasJit()
{
	return SysMemory::HasCodeMemory();
}

void OhosHost::SetSurface(NativeWindow* window, uint32_t width, uint32_t height)
{
	{
		std::lock_guard lock(s_window_lock);
		if (s_window != window)
		{
			if (s_window)
				OH_NativeWindow_NativeObjectUnreference(s_window);
			s_window = window;
			if (s_window)
				OH_NativeWindow_NativeObjectReference(s_window);
		}
		if (width > 0 && height > 0)
		{
			s_window_width = width;
			s_window_height = height;
		}
	}

	// The ring buffer is the CPU thread's to write, so the swap chain update hops there.
	// With the surface gone, AcquireRenderWindow reports Surfaceless and the GS stops
	// presenting into it.
	Host::RunOnCPUThread([]() {
		if (MTGS::IsOpen())
			MTGS::UpdateDisplayWindow();
	});
}

void OhosHost::PointerEvent(float x, float y, int down)
{
	Host::RunOnCPUThread([x, y, down]() {
		InputManager::UpdatePointerAbsolutePosition(0, x, y);
		if (down >= 0)
			InputManager::InvokeEvents(InputManager::MakePointerButtonKey(0, 0), static_cast<float>(down));
	});
}

static GenericInputBinding PadInputToGeneric(u32 input)
{
	switch (input)
	{
		case PadDualshock2::Inputs::PAD_UP: return GenericInputBinding::DPadUp;
		case PadDualshock2::Inputs::PAD_RIGHT: return GenericInputBinding::DPadRight;
		case PadDualshock2::Inputs::PAD_DOWN: return GenericInputBinding::DPadDown;
		case PadDualshock2::Inputs::PAD_LEFT: return GenericInputBinding::DPadLeft;
		case PadDualshock2::Inputs::PAD_TRIANGLE: return GenericInputBinding::Triangle;
		case PadDualshock2::Inputs::PAD_CIRCLE: return GenericInputBinding::Circle;
		case PadDualshock2::Inputs::PAD_CROSS: return GenericInputBinding::Cross;
		case PadDualshock2::Inputs::PAD_SQUARE: return GenericInputBinding::Square;
		case PadDualshock2::Inputs::PAD_SELECT: return GenericInputBinding::Select;
		case PadDualshock2::Inputs::PAD_START: return GenericInputBinding::Start;
		case PadDualshock2::Inputs::PAD_L1: return GenericInputBinding::L1;
		case PadDualshock2::Inputs::PAD_L2: return GenericInputBinding::L2;
		case PadDualshock2::Inputs::PAD_R1: return GenericInputBinding::R1;
		case PadDualshock2::Inputs::PAD_R2: return GenericInputBinding::R2;
		case PadDualshock2::Inputs::PAD_L3: return GenericInputBinding::L3;
		case PadDualshock2::Inputs::PAD_R3: return GenericInputBinding::R3;
		case PadDualshock2::Inputs::PAD_L_UP: return GenericInputBinding::LeftStickUp;
		case PadDualshock2::Inputs::PAD_L_RIGHT: return GenericInputBinding::LeftStickRight;
		case PadDualshock2::Inputs::PAD_L_DOWN: return GenericInputBinding::LeftStickDown;
		case PadDualshock2::Inputs::PAD_L_LEFT: return GenericInputBinding::LeftStickLeft;
		default: return GenericInputBinding::Unknown;
	}
}

void OhosHost::PadInput(uint32_t port, uint32_t input, float value)
{
	if (input >= PadDualshock2::Inputs::LENGTH || port >= Pad::NUM_CONTROLLER_PORTS)
		return;

	// While a menu is open the pad drives it, like a controller does in Big Picture.
	// FullscreenUI's state belongs to the CPU/GS threads, so ask from there.
	const GenericInputBinding generic = PadInputToGeneric(input);
	Host::RunOnCPUThread([port, input, value, generic]() {
		if (generic != GenericInputBinding::Unknown && FullscreenUI::HasActiveWindow())
		{
			ImGuiManager::ProcessGenericInputEvent(generic, InputLayout::Playstation, value, port);
			return;
		}

		std::lock_guard lock(s_pad_lock);
		if (VMManager::HasValidVM())
			Pad::SetControllerState(port, input, value);
	});
}

void OhosHost::OpenPauseMenu()
{
	Host::RunOnCPUThread([]() {
		if (VMManager::HasValidVM())
			FullscreenUI::OpenPauseMenu();
	});
}

void OhosHost::SetAppForeground(bool foreground)
{
	Host::RunOnCPUThread([foreground]() {
		if (!VMManager::HasValidVM())
			return;
		if (!foreground)
		{
			s_was_running_before_background = (VMManager::GetState() == VMState::Running);
			if (s_was_running_before_background)
				VMManager::SetPaused(true);
		}
		else if (s_was_running_before_background)
		{
			s_was_running_before_background = false;
			VMManager::SetPaused(false);
		}
	});
}

void OhosHost::RefreshGameList()
{
	Host::RefreshGameListAsync(false);
}

void OhosHost::BootFile(const std::string& path)
{
	Host::RunOnCPUThread([path]() {
		if (VMManager::HasValidVM())
			return;
		VMBootParameters params;
		params.filename = path;
		Error error;
		if (VMManager::Initialize(params, &error) != VMBootResult::StartupSuccess)
		{
			Host::ReportErrorAsync("Boot failed", error.GetDescription());
			return;
		}
		VMManager::SetState(VMState::Running);
	});
}

std::string OhosHost::GetBiosDirectory()
{
	return EmuFolders::Bios.empty() ? Path::Combine(s_files_dir, "bios") : EmuFolders::Bios;
}

std::string OhosHost::GetGamesDirectory()
{
	return Path::Combine(s_files_dir, "games");
}

//////////////////////////////////////////////////////////////////////////
// Host:: interface
//////////////////////////////////////////////////////////////////////////

void Host::CommitBaseSettingChanges()
{
	if (!s_base_settings)
		return;
	Error err;
	if (!s_base_settings->Save(&err))
		Console.ErrorFmt("Failed to save settings: {}", err.GetDescription());
}

void Host::LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock)
{
}

void Host::CheckForSettingsChanges(const Pcsx2Config& old_config)
{
}

bool Host::RequestResetSettings(bool folders, bool core, bool controllers, bool hotkeys, bool ui)
{
	{
		auto lock = Host::GetSettingsLock();
		VMManager::SetDefaultSettings(*s_base_settings, folders, core, controllers, hotkeys, ui);
	}
	Host::CommitBaseSettingChanges();
	return true;
}

void Host::SetDefaultUISettings(SettingsInterface& si)
{
	si.SetBoolValue("UI", "StartBigPictureMode", true);
}

bool Host::LocaleCircleConfirm()
{
	return false;
}

std::unique_ptr<ProgressCallback> Host::CreateHostProgressCallback()
{
	return ProgressCallback::CreateNullProgressCallback();
}

void Host::ReportInfoAsync(const std::string_view title, const std::string_view message)
{
	if (!title.empty() && !message.empty())
		INFO_LOG("{}: {}", title, message);
	else if (!message.empty())
		INFO_LOG("{}", message);
}

void Host::ReportErrorAsync(const std::string_view title, const std::string_view message)
{
	if (!title.empty() && !message.empty())
		ERROR_LOG("{}: {}", title, message);
	else if (!message.empty())
		ERROR_LOG("{}", message);

	// Also on screen: with no desktop dialogs, the OSD is the only place a user sees it.
	if (!message.empty())
	{
		Host::AddOSDMessage(fmt::format("{}{}{}", title, title.empty() ? "" : ": ", message),
			Host::OSD_ERROR_DURATION);
	}
}

void Host::OpenURL(const std::string_view url)
{
}

bool Host::CopyTextToClipboard(const std::string_view text)
{
	return false;
}

std::string Host::GetTextFromClipboard()
{
	return std::string();
}

void Host::BeginTextInput()
{
}

void Host::EndTextInput()
{
}

std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
	return Host::AcquireRenderWindow(false);
}

void Host::OnInputDeviceConnected(const std::string_view identifier, const std::string_view device_name)
{
}

void Host::OnInputDeviceDisconnected(const InputBindingKey key, const std::string_view identifier)
{
}

void Host::SetMouseMode(bool relative_mode, bool hide_cursor)
{
}

void Host::SetMouseLock(bool state)
{
}

std::optional<WindowInfo> Host::AcquireRenderWindow(bool recreate_window)
{
	std::lock_guard lock(s_window_lock);

	if (s_acquired_window)
	{
		OH_NativeWindow_NativeObjectUnreference(s_acquired_window);
		s_acquired_window = nullptr;
	}

	WindowInfo wi;
	if (!s_window)
	{
		// No surface (app in the background, or not created yet). Surfaceless makes the
		// renderer skip the swap chain until SetSurface() brings one back.
		wi.type = WindowInfo::Type::Surfaceless;
		return wi;
	}

	OH_NativeWindow_NativeObjectReference(s_window);
	s_acquired_window = s_window;

	wi.type = WindowInfo::Type::OHOS;
	wi.window_handle = s_window;
	wi.surface_width = s_window_width;
	wi.surface_height = s_window_height;
	// FullscreenUI lays out for 1280x720 and scales from the surface size itself;
	// this only scales the OSD, as Android does from the long side.
	wi.surface_scale = static_cast<float>(std::max(s_window_width, s_window_height)) / 1280.0f;
	if (wi.surface_scale <= 0.0f)
		wi.surface_scale = 1.0f;
	return wi;
}

void Host::ReleaseRenderWindow()
{
	std::lock_guard lock(s_window_lock);
	if (s_acquired_window)
	{
		OH_NativeWindow_NativeObjectUnreference(s_acquired_window);
		s_acquired_window = nullptr;
	}
}

void Host::BeginPresentFrame()
{
}

void Host::RequestResizeHostDisplay(s32 width, s32 height)
{
}

void Host::OnVMStarting()
{
}

void Host::OnVMStarted()
{
	if (!SysMemory::HasCodeMemory())
	{
		Host::AddOSDMessage("JIT unavailable: running on the interpreters (very slow). "
							"This device or build lacks the writable code memory permission.",
			Host::OSD_WARNING_DURATION);
	}
}

void Host::OnVMDestroyed()
{
}

void Host::OnVMPaused()
{
}

void Host::OnVMResumed()
{
}

void Host::OnGameChanged(const std::string& title, const std::string& elf_override, const std::string& disc_path,
	const std::string& disc_serial, u32 disc_crc, u32 current_crc)
{
	if (!title.empty())
		INFO_LOG("Game changed: {} (serial {}, CRC {:08X})", title, disc_serial, current_crc);
}

void Host::OnPerformanceMetricsUpdated()
{
}

void Host::OnSaveStateLoading(const std::string_view filename)
{
}

void Host::OnSaveStateLoaded(const std::string_view filename, bool was_successful)
{
}

void Host::OnSaveStateSaved(const std::string_view filename)
{
}

void Host::PumpMessagesOnCPUThread()
{
	if (s_shutdown_requested.load(std::memory_order_acquire) && VMManager::HasValidVM())
		VMManager::SetState(VMState::Stopping);

	DrainCPUThreadQueue();
}

void Host::RunOnCPUThread(std::function<void()> function, bool block)
{
	BLOCKING_HOST_CALL_ASSERT(block);

	if (s_cpu_thread_id.load(std::memory_order_acquire) == std::this_thread::get_id())
	{
		function();
		return;
	}

	if (block)
	{
		std::mutex done_lock;
		std::condition_variable done_cv;
		bool done = false;
		auto wrapped = [&function, &done_lock, &done_cv, &done]() {
			function();
			std::lock_guard lk(done_lock);
			done = true;
			done_cv.notify_all();
		};
		{
			std::lock_guard lock(s_cpu_queue_lock);
			s_cpu_queue.emplace_back(std::move(wrapped));
		}
		s_cpu_queue_cv.notify_all();
		std::unique_lock lk(done_lock);
		done_cv.wait(lk, [&done]() { return done; });
		return;
	}

	{
		std::lock_guard lock(s_cpu_queue_lock);
		s_cpu_queue.emplace_back(std::move(function));
	}
	s_cpu_queue_cv.notify_all();
}

void Host::RefreshGameListAsync(bool invalidate_cache)
{
	StopGameListRefreshThread();
	s_gamelist_thread = std::thread([invalidate_cache]() {
		Threading::SetNameOfCurrentThread("GameList Refresh");
		GameList::Refresh(invalidate_cache, false, nullptr);
	});
}

void Host::CancelGameListRefresh()
{
	StopGameListRefreshThread();
}

bool Host::IsFullscreen()
{
	return true;
}

void Host::SetFullscreen(bool enabled)
{
}

void Host::RequestExitApplication(bool allow_confirm)
{
	// The app's lifetime belongs to the system on HarmonyOS; just leave the game.
	if (VMManager::HasValidVM())
		VMManager::SetState(VMState::Stopping);
}

void Host::RequestExitBigPicture()
{
	Host::RequestExitApplication(false);
}

void Host::RequestVMShutdown(bool allow_confirm, bool allow_save_state, bool default_save_state)
{
	if (VMManager::HasValidVM())
		VMManager::SetState(VMState::Stopping);
}

void Host::OnAchievementsLoginSuccess(const char* username, u32 points, u32 sc_points, u32 unread_messages)
{
}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason reason)
{
}

void Host::OnAchievementsHardcoreModeChanged(bool enabled)
{
}

bool Host::HasNativeAchievementNotifications()
{
	return false;
}

void Host::OnAchievementNotification(const char*, float, const char*, const char*, const char*)
{
}

void Host::OnAchievementsRefreshed()
{
}

void Host::OnCoverDownloaderOpenRequested()
{
}

void Host::OnCreateMemoryCardOpenRequested()
{
}

bool Host::InBatchMode()
{
	return false;
}

bool Host::InNoGUIMode()
{
	return false;
}

bool Host::ShouldPreferHostFileSelector()
{
	return false;
}

void Host::OpenHostFileSelectorAsync(std::string_view title, bool select_directory, FileSelectorCallback callback,
	FileSelectorFilters filters, std::string_view initial_directory)
{
	// Files come in through the app's import buttons (system document picker), so
	// FullscreenUI's own browser covers the rest.
	callback(std::string());
}

int Host::LocaleSensitiveCompare(std::string_view lhs, std::string_view rhs)
{
	const int res = std::strncmp(lhs.data(), rhs.data(), std::min(lhs.size(), rhs.size()));
	if (res != 0)
		return res;
	return lhs.size() > rhs.size() ? 1 : (lhs.size() < rhs.size() ? -1 : 0);
}

s32 Host::Internal::GetTranslatedStringImpl(
	const std::string_view context, const std::string_view msg, char* tbuf, size_t tbuf_space)
{
	if (msg.size() > tbuf_space)
		return -1;
	if (msg.empty())
		return 0;

	std::memcpy(tbuf, msg.data(), msg.size());
	return static_cast<s32>(msg.size());
}

std::string Host::TranslatePluralToString(const char* context, const char* msg, const char* disambiguation, int count)
{
	TinyString count_str = TinyString::from_format("{}", count);

	std::string ret(msg);
	for (;;)
	{
		const std::string::size_type pos = ret.find("%n");
		if (pos == std::string::npos)
			break;
		ret.replace(pos, 2, count_str.view());
	}
	return ret;
}

std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(const std::string_view str)
{
	return std::nullopt;
}

std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(u32 code)
{
	return std::nullopt;
}

const char* InputManager::ConvertHostKeyboardCodeToIcon(u32 code)
{
	return nullptr;
}

BEGIN_HOTKEY_LIST(g_host_hotkeys)
END_HOTKEY_LIST()

//////////////////////////////////////////////////////////////////////////
// Platform pieces the core leaves to the frontend
//////////////////////////////////////////////////////////////////////////

// No HTTP client yet (achievement login, cover downloads). Callers treat null as
// "downloads unavailable".
std::unique_ptr<HTTPDownloader> HTTPDownloader::Create(std::string user_agent)
{
	return nullptr;
}

bool Common::PlaySoundAsync(const char* path)
{
	return false;
}

// The OHOS libc declares pthread_setcanceltype() (and PTHREAD_CANCEL_ASYNCHRONOUS)
// but does not export it, and SDL's thread setup calls it. Nothing here relies on
// asynchronous cancellation, so report the default type. Weak, so a libc that does
// export it wins.
extern "C" __attribute__((weak)) int pthread_setcanceltype(int type, int* oldtype)
{
	if (oldtype)
		*oldtype = PTHREAD_CANCEL_DEFERRED;
	return 0;
}
