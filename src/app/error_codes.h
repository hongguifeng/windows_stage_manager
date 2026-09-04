#pragma once

#include <cstdint>
#include <string_view>

namespace stage_manager::app {

enum class ErrorCode : std::uint16_t {
    Ok = 0,
    InvalidArgument,
    IoError,
    QueueFull,
    HookInstallFailed,
    WindowNotFound,
    MoveFailed,
    StaleSnapshot,
    Unsatisfiable,
    Timeout,
    GeometryTooComplex,
    PermissionDenied,
};

constexpr std::string_view to_string(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::Ok:
        return "ok";
    case ErrorCode::InvalidArgument:
        return "invalid_argument";
    case ErrorCode::IoError:
        return "io_error";
    case ErrorCode::QueueFull:
        return "queue_full";
    case ErrorCode::HookInstallFailed:
        return "hook_install_failed";
    case ErrorCode::WindowNotFound:
        return "window_not_found";
    case ErrorCode::MoveFailed:
        return "move_failed";
    case ErrorCode::StaleSnapshot:
        return "stale_snapshot";
    case ErrorCode::Unsatisfiable:
        return "unsatisfiable";
    case ErrorCode::Timeout:
        return "timeout";
    case ErrorCode::GeometryTooComplex:
        return "geometry_too_complex";
    case ErrorCode::PermissionDenied:
        return "permission_denied";
    }
    return "unknown";
}

} // namespace stage_manager::app

