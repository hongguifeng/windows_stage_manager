#pragma once

#ifdef _WIN32

#include "window/move_applier.h"

#include <cstdint>

namespace stage_manager::platform::win32 {

class Win32WindowMover final : public window::IWindowMover {
public:
    window::NativeMoveResult move(const window::WindowKey& window,
                                  const geometry::Rect& destination) override;

    static constexpr std::uint32_t position_only_flags() noexcept;
};

constexpr std::uint32_t Win32WindowMover::position_only_flags() noexcept
{
    return 0x0001u | 0x0004u | 0x0010u | 0x0200u;
}

} // namespace stage_manager::platform::win32

#endif
