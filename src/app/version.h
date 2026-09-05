#pragma once

#include <string_view>

namespace stage_manager::app {

inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 0;
inline constexpr std::string_view kVersion = "0.1.0-rc6";
inline constexpr std::string_view kProductName = "WindowsStageManager";

} // namespace stage_manager::app
