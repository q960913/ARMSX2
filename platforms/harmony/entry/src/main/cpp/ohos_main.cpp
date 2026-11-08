// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// libarmsx2.so: the NAPI module the ArkTS app imports, and the native side of
// its XComponent (surface + touch). Everything else is in ohos_host.cpp.
//
// ArkTS API (see types/libarmsx2/index.d.ts):
//   init(resourceManager, filesDir): string   '' on success, else the error
//   hasJit(): boolean
//   padInput(port, input, value): void        input = PadDualshock2::Inputs index
//   openPauseMenu(): void
//   setForeground(foreground): void
//   refreshGameList(): void
//   bootFile(path): void
//   getBiosDir(): string / getGamesDir(): string

#include "ohos_host.h"

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <hilog/log.h>
#include <napi/native_api.h>
#include <rawfile/raw_file_manager.h>

#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>
#include <unistd.h>

static constexpr unsigned int LOG_DOMAIN_ARMSX2 = 0x3200;
static constexpr const char* LOG_TAG_ARMSX2 = "ARMSX2";

//////////////////////////////////////////////////////////////////////////
// stdout/stderr -> hilog, so the core's console output shows in DevEco / hdc hilog.
//////////////////////////////////////////////////////////////////////////

static void* LogPipeThread(void* arg)
{
	const int fd = static_cast<int>(reinterpret_cast<intptr_t>(arg));
	char buf[1024];
	std::string line;
	for (;;)
	{
		const ssize_t n = read(fd, buf, sizeof(buf));
		if (n <= 0)
			break;
		for (ssize_t i = 0; i < n; i++)
		{
			if (buf[i] == '\n')
			{
				OH_LOG_Print(LOG_APP, LOG_INFO, LOG_DOMAIN_ARMSX2, LOG_TAG_ARMSX2, "%{public}s", line.c_str());
				line.clear();
			}
			else
			{
				line.push_back(buf[i]);
			}
		}
	}
	return nullptr;
}

static void RedirectStdioToHilog()
{
	static bool redirected = false;
	if (redirected)
		return;
	redirected = true;

	int fds[2];
	if (pipe(fds) != 0)
		return;
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	dup2(fds[1], STDOUT_FILENO);
	dup2(fds[1], STDERR_FILENO);
	close(fds[1]);

	pthread_t thread;
	if (pthread_create(&thread, nullptr, &LogPipeThread, reinterpret_cast<void*>(static_cast<intptr_t>(fds[0]))) == 0)
		pthread_detach(thread);
}

//////////////////////////////////////////////////////////////////////////
// XComponent
//////////////////////////////////////////////////////////////////////////

static void UpdateSurface(OH_NativeXComponent* component, void* window)
{
	uint64_t width = 0, height = 0;
	OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);
	OhosHost::SetSurface(static_cast<NativeWindow*>(window), static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

static void OnSurfaceCreated(OH_NativeXComponent* component, void* window)
{
	UpdateSurface(component, window);
}

static void OnSurfaceChanged(OH_NativeXComponent* component, void* window)
{
	UpdateSurface(component, window);
}

static void OnSurfaceDestroyed(OH_NativeXComponent* component, void* window)
{
	OhosHost::SetSurface(nullptr, 0, 0);
}

static void DispatchTouchEvent(OH_NativeXComponent* component, void* window)
{
	OH_NativeXComponent_TouchEvent event;
	if (OH_NativeXComponent_GetTouchEvent(component, window, &event) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS)
		return;

	// One pointer is enough for the menus; the on-screen pad is an ArkUI layer above
	// the surface, so its touches never land here.
	int down;
	switch (event.type)
	{
		case OH_NATIVEXCOMPONENT_DOWN: down = 1; break;
		case OH_NATIVEXCOMPONENT_UP:
		case OH_NATIVEXCOMPONENT_CANCEL: down = 0; break;
		default: down = -1; break;
	}
	OhosHost::PointerEvent(event.x, event.y, down);
}

static OH_NativeXComponent_Callback s_xcomponent_callbacks = {
	&OnSurfaceCreated,
	&OnSurfaceChanged,
	&OnSurfaceDestroyed,
	&DispatchTouchEvent,
};

static void RegisterXComponent(napi_env env, napi_value exports)
{
	napi_value xcomponent_obj = nullptr;
	if (napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &xcomponent_obj) != napi_ok || !xcomponent_obj)
		return;

	OH_NativeXComponent* component = nullptr;
	if (napi_unwrap(env, xcomponent_obj, reinterpret_cast<void**>(&component)) != napi_ok || !component)
		return;

	OH_NativeXComponent_RegisterCallback(component, &s_xcomponent_callbacks);
}

//////////////////////////////////////////////////////////////////////////
// NAPI helpers
//////////////////////////////////////////////////////////////////////////

static std::string GetString(napi_env env, napi_value value)
{
	size_t len = 0;
	if (napi_get_value_string_utf8(env, value, nullptr, 0, &len) != napi_ok)
		return {};
	std::string ret(len, '\0');
	napi_get_value_string_utf8(env, value, ret.data(), len + 1, &len);
	return ret;
}

static napi_value MakeString(napi_env env, const std::string& str)
{
	napi_value ret;
	napi_create_string_utf8(env, str.c_str(), str.size(), &ret);
	return ret;
}

static napi_value MakeUndefined(napi_env env)
{
	napi_value ret;
	napi_get_undefined(env, &ret);
	return ret;
}

template <size_t N>
static bool GetArgs(napi_env env, napi_callback_info info, napi_value (&args)[N])
{
	size_t argc = N;
	return napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) == napi_ok && argc >= N;
}

//////////////////////////////////////////////////////////////////////////
// Exported functions
//////////////////////////////////////////////////////////////////////////

static napi_value Init(napi_env env, napi_callback_info info)
{
	napi_value args[2];
	if (!GetArgs(env, info, args))
		return MakeString(env, "init(resourceManager, filesDir) expects two arguments.");

	RedirectStdioToHilog();

	NativeResourceManager* mgr = OH_ResourceManager_InitNativeResourceManager(env, args[0]);
	const std::string files_dir = GetString(env, args[1]);
	if (!mgr || files_dir.empty())
		return MakeString(env, "Invalid resource manager or files directory.");

	std::string error;
	const bool ok = OhosHost::SyncResources(mgr, files_dir, &error) && OhosHost::Start(files_dir, &error);
	OH_ResourceManager_ReleaseNativeResourceManager(mgr);
	return MakeString(env, ok ? std::string() : error);
}

static napi_value HasJit(napi_env env, napi_callback_info info)
{
	napi_value ret;
	napi_get_boolean(env, OhosHost::HasJit(), &ret);
	return ret;
}

static napi_value PadInput(napi_env env, napi_callback_info info)
{
	napi_value args[3];
	if (!GetArgs(env, info, args))
		return MakeUndefined(env);

	uint32_t port = 0, input = 0;
	double value = 0.0;
	napi_get_value_uint32(env, args[0], &port);
	napi_get_value_uint32(env, args[1], &input);
	napi_get_value_double(env, args[2], &value);
	OhosHost::PadInput(port, input, static_cast<float>(value));
	return MakeUndefined(env);
}

static napi_value OpenPauseMenu(napi_env env, napi_callback_info info)
{
	OhosHost::OpenPauseMenu();
	return MakeUndefined(env);
}

static napi_value SetForeground(napi_env env, napi_callback_info info)
{
	napi_value args[1];
	if (!GetArgs(env, info, args))
		return MakeUndefined(env);
	bool foreground = true;
	napi_get_value_bool(env, args[0], &foreground);
	OhosHost::SetAppForeground(foreground);
	return MakeUndefined(env);
}

static napi_value RefreshGameList(napi_env env, napi_callback_info info)
{
	OhosHost::RefreshGameList();
	return MakeUndefined(env);
}

static napi_value BootFile(napi_env env, napi_callback_info info)
{
	napi_value args[1];
	if (GetArgs(env, info, args))
		OhosHost::BootFile(GetString(env, args[0]));
	return MakeUndefined(env);
}

static napi_value GetBiosDir(napi_env env, napi_callback_info info)
{
	return MakeString(env, OhosHost::GetBiosDirectory());
}

static napi_value GetGamesDir(napi_env env, napi_callback_info info)
{
	return MakeString(env, OhosHost::GetGamesDirectory());
}

//////////////////////////////////////////////////////////////////////////
// Module registration
//////////////////////////////////////////////////////////////////////////

EXTERN_C_START
static napi_value ModuleInit(napi_env env, napi_value exports)
{
	const napi_property_descriptor desc[] = {
		{"init", nullptr, Init, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"hasJit", nullptr, HasJit, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"padInput", nullptr, PadInput, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"openPauseMenu", nullptr, OpenPauseMenu, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"setForeground", nullptr, SetForeground, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"refreshGameList", nullptr, RefreshGameList, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"bootFile", nullptr, BootFile, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"getBiosDir", nullptr, GetBiosDir, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"getGamesDir", nullptr, GetGamesDir, nullptr, nullptr, nullptr, napi_default, nullptr},
	};
	napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);

	// The same module is also loaded for the XComponent (libraryname: 'armsx2'); that
	// load carries the component in its exports.
	RegisterXComponent(env, exports);
	return exports;
}
EXTERN_C_END

static napi_module s_armsx2_module = {
	.nm_version = 1,
	.nm_flags = 0,
	.nm_filename = nullptr,
	.nm_register_func = ModuleInit,
	.nm_modname = "armsx2",
	.nm_priv = nullptr,
	.reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterArmsx2Module()
{
	napi_module_register(&s_armsx2_module);
}
