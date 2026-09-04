#include "window/internal_move_tracker.h"

namespace stage_manager::window {

InternalMoveToken InternalMoveTracker::begin(
    std::uintptr_t hwnd, std::uint64_t transaction_id)
{
    InternalMoveToken token;
    token.hwnd = hwnd;
    token.generation = ++next_generation_;
    token.transactionId = transaction_id;
    active_[hwnd] = token;
    return token;
}

bool InternalMoveTracker::matches(const WindowEvent& event) const
{
    if (event.type != WindowEventType::LocationChange) {
        return false;
    }
    return active_.find(event.hwnd) != active_.end();
}

bool InternalMoveTracker::complete(const InternalMoveToken& token)
{
    const auto iterator = active_.find(token.hwnd);
    if (iterator == active_.end() || iterator->second.generation != token.generation) {
        return false;
    }
    active_.erase(iterator);
    return true;
}

void InternalMoveTracker::cancel(std::uintptr_t hwnd)
{
    active_.erase(hwnd);
}

void InternalMoveTracker::clear()
{
    active_.clear();
}

} // namespace stage_manager::window
