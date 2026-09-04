#include "geometry/dpi.h"

#include <cstdint>
#include <limits>

namespace stage_manager::geometry {
namespace {

std::int64_t multiply_divide_nearest(
    std::int64_t value, std::uint32_t numerator, std::uint32_t denominator) noexcept
{
    if (denominator == 0) {
        return 0;
    }
    if (numerator == 0) {
        return 0;
    }
    const bool negative = value < 0;
    const auto magnitude = negative
        ? static_cast<std::uint64_t>(-(value + 1)) + 1
        : static_cast<std::uint64_t>(value);
    if (magnitude > std::numeric_limits<std::uint64_t>::max() / numerator) {
        return negative ? std::numeric_limits<std::int64_t>::min()
                        : std::numeric_limits<std::int64_t>::max();
    }
    const auto product = magnitude * numerator;
    const auto quotient = product / denominator;
    const auto remainder = product % denominator;
    const auto scaled = quotient + (remainder >= (denominator + 1) / 2 ? 1u : 0u);
    if (negative) {
        const auto minimum_magnitude = static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()) + 1;
        return scaled >= minimum_magnitude ? std::numeric_limits<std::int64_t>::min()
                                           : -static_cast<std::int64_t>(scaled);
    }
    return scaled > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(scaled);
}

} // namespace

std::int64_t scale_dip_ceil(std::uint32_t dip, std::uint32_t dpi) noexcept
{
    if (dpi == 0) {
        return 0;
    }
    const auto product = static_cast<std::uint64_t>(dip) * dpi;
    return static_cast<std::int64_t>((product + kDefaultDpi - 1) / kDefaultDpi);
}

std::int64_t scale_dip_nearest(std::int64_t dip, std::uint32_t dpi) noexcept
{
    return multiply_divide_nearest(dip, dpi, kDefaultDpi);
}

std::int64_t pixels_to_dip_nearest(std::int64_t pixels, std::uint32_t dpi) noexcept
{
    return multiply_divide_nearest(pixels, kDefaultDpi, dpi);
}

} // namespace stage_manager::geometry
