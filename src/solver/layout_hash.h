#pragma once

#include "solver/layout_snapshot.h"

#include <cstdint>

namespace stage_manager::solver {

struct LayoutHash {
    std::uint64_t first = 0;
    std::uint64_t second = 0;

    constexpr bool operator==(const LayoutHash&) const noexcept = default;
};

LayoutHash hash_layout(const LayoutSnapshot& snapshot) noexcept;

} // namespace stage_manager::solver
