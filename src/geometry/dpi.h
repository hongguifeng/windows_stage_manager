#pragma once

#include <cstdint>

namespace stage_manager::geometry {

inline constexpr std::uint32_t kDefaultDpi = 96;

std::int64_t scale_dip_ceil(std::uint32_t dip, std::uint32_t dpi) noexcept;
std::int64_t scale_dip_nearest(std::int64_t dip, std::uint32_t dpi) noexcept;
std::int64_t pixels_to_dip_nearest(std::int64_t pixels, std::uint32_t dpi) noexcept;

} // namespace stage_manager::geometry
