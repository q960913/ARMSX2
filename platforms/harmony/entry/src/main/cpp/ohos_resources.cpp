// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Unpacks the HAP's rawfile/resources tree (GameDB, shaders, fonts, icons: a copy
// of bin/resources made at build time by entry/hvigorfile.ts) into the app's files
// directory, where the core reads it with plain file I/O.
//
// The copy is redone only when the stamp file the build writes differs from the
// one already unpacked, so a normal launch costs one small read.

#include "ohos_host.h"

#include <rawfile/raw_dir.h>
#include <rawfile/raw_file.h>
#include <rawfile/raw_file_manager.h>

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#include "fmt/format.h"

#include <cstdio>
#include <optional>
#include <vector>

static constexpr const char* RAW_ROOT = "resources";
static constexpr const char* STAMP_NAME = ".armsx2-resources";

static std::optional<std::string> ReadRawFileToString(const NativeResourceManager* mgr, const std::string& name)
{
	RawFile* const file = OH_ResourceManager_OpenRawFile(mgr, name.c_str());
	if (!file)
		return std::nullopt;

	std::string data;
	data.resize(static_cast<size_t>(std::max<long>(OH_ResourceManager_GetRawFileSize(file), 0)));
	const int read = data.empty() ? 0 : OH_ResourceManager_ReadRawFile(file, data.data(), data.size());
	OH_ResourceManager_CloseRawFile(file);
	if (read < 0)
		return std::nullopt;
	data.resize(static_cast<size_t>(read));
	return data;
}

static bool CopyRawFile(const NativeResourceManager* mgr, const std::string& raw_path, const std::string& dest_path,
	std::string* error)
{
	RawFile* const file = OH_ResourceManager_OpenRawFile(mgr, raw_path.c_str());
	if (!file)
	{
		*error = fmt::format("Cannot open bundled file {}", raw_path);
		return false;
	}

	std::FILE* out = FileSystem::OpenCFile(dest_path.c_str(), "wb");
	if (!out)
	{
		OH_ResourceManager_CloseRawFile(file);
		*error = fmt::format("Cannot write {}", dest_path);
		return false;
	}

	std::vector<char> buf(256 * 1024);
	bool ok = true;
	for (;;)
	{
		const int n = OH_ResourceManager_ReadRawFile(file, buf.data(), buf.size());
		if (n < 0)
		{
			ok = false;
			break;
		}
		if (n == 0)
			break;
		if (std::fwrite(buf.data(), 1, static_cast<size_t>(n), out) != static_cast<size_t>(n))
		{
			ok = false;
			break;
		}
	}

	std::fclose(out);
	OH_ResourceManager_CloseRawFile(file);
	if (!ok)
		*error = fmt::format("Failed copying {}", raw_path);
	return ok;
}

static bool CopyRawDir(const NativeResourceManager* mgr, const std::string& raw_dir, const std::string& dest_dir,
	std::string* error)
{
	if (!FileSystem::EnsureDirectoryExists(dest_dir.c_str(), true))
	{
		*error = fmt::format("Cannot create {}", dest_dir);
		return false;
	}

	RawDir* const dir = OH_ResourceManager_OpenRawDir(mgr, raw_dir.c_str());
	if (!dir)
	{
		*error = fmt::format("Bundled directory {} is missing", raw_dir);
		return false;
	}

	std::vector<std::string> names;
	const int count = OH_ResourceManager_GetRawFileCount(dir);
	for (int i = 0; i < count; i++)
	{
		if (const char* name = OH_ResourceManager_GetRawFileName(dir, i))
			names.emplace_back(name);
	}
	OH_ResourceManager_CloseRawDir(dir);

	for (const std::string& name : names)
	{
		if (name == STAMP_NAME)
			continue;

		const std::string raw_path = raw_dir + "/" + name;
		const std::string dest_path = Path::Combine(dest_dir, name);
		const bool ok = OH_ResourceManager_IsRawDir(mgr, raw_path.c_str()) ?
							CopyRawDir(mgr, raw_path, dest_path, error) :
							CopyRawFile(mgr, raw_path, dest_path, error);
		if (!ok)
			return false;
	}
	return true;
}

bool OhosHost::SyncResources(const NativeResourceManager* mgr, const std::string& files_dir, std::string* error)
{
	const std::string dest = Path::Combine(files_dir, RAW_ROOT);
	const std::string dest_stamp = Path::Combine(dest, STAMP_NAME);

	const std::optional<std::string> bundled_stamp = ReadRawFileToString(mgr, fmt::format("{}/{}", RAW_ROOT, STAMP_NAME));
	if (!bundled_stamp.has_value())
	{
		*error = "The app was built without its resources (rawfile/resources). "
				 "Build it with hvigor, which copies bin/resources in.";
		return false;
	}

	const std::optional<std::string> unpacked_stamp = FileSystem::ReadFileToString(dest_stamp.c_str());
	if (unpacked_stamp.has_value() && unpacked_stamp.value() == bundled_stamp.value())
		return true;

	Console.WriteLnFmt("Unpacking resources to {}", dest);
	if (FileSystem::DirectoryExists(dest.c_str()))
		FileSystem::RecursiveDeleteDirectory(dest.c_str());

	if (!CopyRawDir(mgr, RAW_ROOT, dest, error))
		return false;

	// Written last, so an interrupted copy is redone on the next launch.
	if (!FileSystem::WriteStringToFile(dest_stamp.c_str(), bundled_stamp.value()))
	{
		*error = fmt::format("Cannot write {}", dest_stamp);
		return false;
	}
	return true;
}
