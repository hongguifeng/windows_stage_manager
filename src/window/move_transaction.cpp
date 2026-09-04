#include "window/move_transaction.h"

#include <limits>

namespace stage_manager::window {

void MoveTransactionGuard::activate(std::uint64_t transaction_id,
                                    std::uint64_t layout_generation)
{
    const std::scoped_lock lock(mutex_);
    transaction_id_ = transaction_id;
    layout_generation_ = layout_generation;
    cancelled_ = transaction_id == 0 || layout_generation == 0;
}

void MoveTransactionGuard::observe(const WindowEvent& event)
{
    if (event.type == WindowEventType::MoveSizeStart) {
        cancel();
    }
}

void MoveTransactionGuard::cancel()
{
    const std::scoped_lock lock(mutex_);
    cancelled_ = true;
}

bool MoveTransactionGuard::allows(std::uint64_t transaction_id,
                                  std::uint64_t layout_generation) const
{
    const std::scoped_lock lock(mutex_);
    return !cancelled_ && transaction_id_ == transaction_id &&
        layout_generation_ == layout_generation;
}

WindowMoveFailure MoveFailureTracker::record_failure(std::uint64_t transaction_id,
                                                      const WindowKey& window)
{
    if (transaction_id_ != transaction_id) {
        transaction_id_ = transaction_id;
        failures_.clear();
    }
    auto [iterator, inserted] = failures_.try_emplace(window.hwnd);
    auto& failure = iterator->second;
    if (inserted || failure.window != window) {
        failure = {};
        failure.window = window;
    }
    if (failure.count != std::numeric_limits<std::uint32_t>::max()) {
        ++failure.count;
    }
    failure.nonCooperative = true;
    return failure;
}

std::uint32_t MoveFailureTracker::failure_count(std::uint64_t transaction_id,
                                                const WindowKey& window) const
{
    if (transaction_id_ != transaction_id) {
        return 0;
    }
    const auto iterator = failures_.find(window.hwnd);
    return iterator != failures_.end() && iterator->second.window == window
        ? iterator->second.count
        : 0;
}

bool MoveFailureTracker::is_non_cooperative(std::uint64_t transaction_id,
                                            const WindowKey& window) const
{
    if (transaction_id_ != transaction_id) {
        return false;
    }
    const auto iterator = failures_.find(window.hwnd);
    return iterator != failures_.end() && iterator->second.window == window &&
        iterator->second.nonCooperative;
}

void MoveFailureTracker::clear()
{
    transaction_id_ = 0;
    failures_.clear();
}

} // namespace stage_manager::window
