#pragma once

#include "window/window_snapshot.h"

#include "app/settings.h"

#include <cstdint>
#include <span>
#include <vector>

namespace stage_manager::window {

enum class WindowDisposition : std::uint8_t {
    Managed,
    Unmanaged,
};

enum class UnmanagedReason : std::uint8_t {
    None,
    InvalidIdentity,
    NotRootWindow,
    ApiQueryFailed,
    NotCurrentDesktop,
    Invisible,
    Minimized,
    Maximized,
    Topmost,
    ToolWindow,
    NoActivate,
    OwnedWindow,
    SystemUi,
    TooSmall,
};

struct ClassificationResult {
    WindowDisposition disposition = WindowDisposition::Unmanaged;
    UnmanagedReason reason = UnmanagedReason::InvalidIdentity;

    constexpr bool managed() const noexcept
    {
        return disposition == WindowDisposition::Managed;
    }
};

class ConservativeWindowClassifier final {
public:
    explicit ConservativeWindowClassifier(app::Settings settings = {});

    ClassificationResult classify(const WindowSnapshot& snapshot,
                                  std::span<const WindowSnapshot> all_windows = {}) const;
    std::vector<ClassificationResult> classify_batch(
        std::span<const WindowSnapshot> snapshots) const;
    void annotate(WindowSnapshotBatch& batch) const;

private:
    app::Settings settings_;
};

} // namespace stage_manager::window
