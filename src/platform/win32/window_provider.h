#pragma once

#ifdef _WIN32

#include "window/window_provider.h"

namespace stage_manager::platform::win32 {

class Win32WindowProvider final : public window::IWindowProvider {
public:
    Win32WindowProvider() = default;

    Win32WindowProvider(const Win32WindowProvider&) = delete;
    Win32WindowProvider& operator=(const Win32WindowProvider&) = delete;

    window::WindowSnapshotBatch capture(window::SnapshotRefreshReason reason) override;

private:
    std::uint64_t nextVersion_ = 0;
};

} // namespace stage_manager::platform::win32

#endif
