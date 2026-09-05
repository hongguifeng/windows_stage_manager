#include "solver/layout_hash.h"

#include <cstdint>

namespace stage_manager::solver {
namespace {

void append_byte(std::uint64_t& hash, std::uint8_t value, std::uint64_t prime) noexcept
{
    hash ^= value;
    hash *= prime;
}

template <typename Value>
void append_value(std::uint64_t& hash, Value value, std::uint64_t prime) noexcept
{
    const auto unsigned_value = static_cast<std::uint64_t>(value);
    for (unsigned shift = 0; shift < 64; shift += 8) {
        append_byte(hash, static_cast<std::uint8_t>(unsigned_value >> shift), prime);
    }
}

void append_rect(std::uint64_t& hash,
                 const geometry::Rect& rectangle,
                 std::uint64_t prime) noexcept
{
    append_value(hash, rectangle.left, prime);
    append_value(hash, rectangle.top, prime);
    append_value(hash, rectangle.right, prime);
    append_value(hash, rectangle.bottom, prime);
}

} // namespace

LayoutHash hash_layout(const LayoutSnapshot& snapshot) noexcept
{
    constexpr std::uint64_t kFirstOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kFirstPrime = 1099511628211ULL;
    constexpr std::uint64_t kSecondOffset = 7809847782465536322ULL;
    constexpr std::uint64_t kSecondPrime = 14029467366897019727ULL;

    LayoutHash result{kFirstOffset, kSecondOffset};
    append_value(result.first, snapshot.windows.size(), kFirstPrime);
    append_value(result.second, snapshot.windows.size(), kSecondPrime);
    for (const auto& window : snapshot.windows) {
        append_value(result.first, window.key.hwnd, kFirstPrime);
        append_value(result.first, window.key.processId, kFirstPrime);
        append_value(result.first, window.key.instanceGeneration, kFirstPrime);
        append_rect(result.first, window.placementRect, kFirstPrime);
        append_rect(result.first, window.visualRect, kFirstPrime);
        append_value(result.first, window.zIndex, kFirstPrime);
        append_value(result.first, window.topmost, kFirstPrime);

        append_value(result.second, window.key.hwnd, kSecondPrime);
        append_value(result.second, window.key.processId, kSecondPrime);
        append_value(result.second, window.key.instanceGeneration, kSecondPrime);
        append_rect(result.second, window.placementRect, kSecondPrime);
        append_rect(result.second, window.visualRect, kSecondPrime);
        append_value(result.second, window.zIndex, kSecondPrime);
        append_value(result.second, window.topmost, kSecondPrime);
    }
    return result;
}

} // namespace stage_manager::solver
