#pragma once

#include "window/window_event.h"
#include "window/window_snapshot.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace stage_manager::window {

class IMoveApplyGuard {
public:
    virtual ~IMoveApplyGuard() = default;
    virtual bool allows(std::uint64_t transaction_id,
                        std::uint64_t layout_generation) const = 0;
};

class MoveTransactionGuard final : public IMoveApplyGuard {
public:
    void activate(std::uint64_t transaction_id, std::uint64_t layout_generation);
    void observe(const WindowEvent& event);
    void cancel();
    bool allows(std::uint64_t transaction_id,
                std::uint64_t layout_generation) const override;

private:
    mutable std::mutex mutex_;
    std::uint64_t transaction_id_ = 0;
    std::uint64_t layout_generation_ = 0;
    bool cancelled_ = true;
};

struct WindowMoveFailure {
    WindowKey window;
    std::uint32_t count = 0;
    bool nonCooperative = false;
};

class MoveFailureTracker final {
public:
    WindowMoveFailure record_failure(std::uint64_t transaction_id,
                                     const WindowKey& window);
    std::uint32_t failure_count(std::uint64_t transaction_id,
                                const WindowKey& window) const;
    bool is_non_cooperative(std::uint64_t transaction_id,
                            const WindowKey& window) const;
    void clear();

private:
    std::uint64_t transaction_id_ = 0;
    std::unordered_map<NativeWindowHandle, WindowMoveFailure> failures_;
};

} // namespace stage_manager::window
