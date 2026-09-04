#pragma once

#include "window/window_event.h"

#include <cstdint>
#include <unordered_map>

namespace stage_manager::window {

struct InternalMoveToken {
    std::uintptr_t hwnd = 0;
    std::uint64_t generation = 0;
    std::uint64_t transactionId = 0;
};

class InternalMoveTracker final {
public:
    InternalMoveToken begin(std::uintptr_t hwnd, std::uint64_t transaction_id);
    bool matches(const WindowEvent& event) const;
    bool complete(const InternalMoveToken& token);
    void cancel(std::uintptr_t hwnd);
    void clear();

private:
    std::uint64_t next_generation_ = 0;
    std::unordered_map<std::uintptr_t, InternalMoveToken> active_;
};

} // namespace stage_manager::window
