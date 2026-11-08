// Guest (iOS) path <-> host (Windows) path mapping.
#pragma once
#include "common.h"

namespace vfs {

// Guest-visible locations
constexpr const char* kBundlePath = "/var/containers/Bundle/Application/7A1B2C3D-0000-4000-8000-000000016200/minecraftpe.app";
constexpr const char* kHomePath = "/var/mobile/Containers/Data/Application/5E6F7A8B-0000-4000-8000-000000016200";

void init(const std::string& host_bundle_dir, const std::string& host_data_dir);

// Returns host path (UTF-16) for a guest path. Relative paths are resolved
// against the guest cwd.
std::wstring to_host(const char* guest_path);
// Converts a host path back to the guest view (best effort).
std::string to_guest(const std::wstring& host_path);

std::string cwd();
bool set_cwd(const char* guest_path);

const std::string& host_bundle_dir();
const std::string& host_data_dir();

// The app bundle is read-only (as on iOS)
bool is_readonly(const char* guest_path);

// Special device files
bool is_dev_random(const char* guest_path);
bool is_dev_null(const char* guest_path);

}  // namespace vfs
