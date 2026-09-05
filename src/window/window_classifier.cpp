#include "window/window_classifier.h"

#include "geometry/dpi.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>

namespace stage_manager::window {
namespace {

ClassificationResult unmanaged(UnmanagedReason reason)
{
    return ClassificationResult{WindowDisposition::Unmanaged, reason};
}

bool is_system_ui(std::wstring_view class_name)
{
    constexpr std::wstring_view kSystemClasses[] = {
        L"#32768",
        L"#32769",
        L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd",
        L"WorkerW",
        L"Progman",
        L"NotifyIconOverflowWindow",
        L"Windows.UI.Core.CoreWindow",
        L"ApplicationFrameWindow",
        L"XamlExplorerHostIslandWindow",
    };
    return std::find(std::begin(kSystemClasses), std::end(kSystemClasses), class_name) !=
        std::end(kSystemClasses);
}

bool is_substantive_owned_window(const WindowSnapshot& candidate,
                                 const app::Settings& settings)
{
    if (!candidate.visible || !candidate.currentDesktop || candidate.iconic || candidate.cloaked ||
        !candidate.placementRect.valid()) {
        return false;
    }

    const auto width = static_cast<std::int64_t>(candidate.placementRect.right) -
        candidate.placementRect.left;
    const auto height = static_cast<std::int64_t>(candidate.placementRect.bottom) -
        candidate.placementRect.top;
    return width >= geometry::scale_dip_ceil(settings.minOnscreenWidthDip, candidate.dpi) &&
        height >= geometry::scale_dip_ceil(settings.minOnscreenHeightDip, candidate.dpi);
}

bool has_substantive_owned_window(const WindowSnapshot& snapshot,
                                  std::span<const WindowSnapshot> all_windows,
                                  const app::Settings& settings)
{
    return std::any_of(
        all_windows.begin(), all_windows.end(), [&snapshot, &settings](const auto& candidate) {
            return candidate.ownerHwnd == snapshot.key.hwnd &&
                is_substantive_owned_window(candidate, settings);
        });
}

} // namespace

ConservativeWindowClassifier::ConservativeWindowClassifier(app::Settings settings)
    : settings_(std::move(settings))
{
}

ClassificationResult ConservativeWindowClassifier::classify(
    const WindowSnapshot& snapshot, std::span<const WindowSnapshot> all_windows) const
{
    if (snapshot.key.hwnd == 0 || snapshot.key.processId == 0 ||
        snapshot.key.instanceGeneration == 0) {
        return unmanaged(UnmanagedReason::InvalidIdentity);
    }
    if (snapshot.rootHwnd != snapshot.key.hwnd) {
        return unmanaged(UnmanagedReason::NotRootWindow);
    }
    if (snapshot.queryFailures != 0) {
        return unmanaged(UnmanagedReason::ApiQueryFailed);
    }
    if (!snapshot.currentDesktop) {
        return unmanaged(UnmanagedReason::NotCurrentDesktop);
    }
    if (!snapshot.visible) {
        return unmanaged(UnmanagedReason::Invisible);
    }
    if (snapshot.iconic) {
        return unmanaged(UnmanagedReason::Minimized);
    }
    if (snapshot.zoomed) {
        return unmanaged(UnmanagedReason::Maximized);
    }
    if (snapshot.topmost) {
        return unmanaged(UnmanagedReason::Topmost);
    }

#ifdef _WIN32
    if ((snapshot.exStyle & WS_EX_TOOLWINDOW) != 0) {
        return unmanaged(UnmanagedReason::ToolWindow);
    }
    if ((snapshot.exStyle & WS_EX_NOACTIVATE) != 0) {
        return unmanaged(UnmanagedReason::NoActivate);
    }
#endif

    if (snapshot.ownerHwnd != 0 ||
        has_substantive_owned_window(snapshot, all_windows, settings_)) {
        return unmanaged(UnmanagedReason::OwnedWindow);
    }
    if (is_system_ui(snapshot.className)) {
        return unmanaged(UnmanagedReason::SystemUi);
    }

    const auto width = static_cast<std::int64_t>(snapshot.placementRect.right) -
        snapshot.placementRect.left;
    const auto height = static_cast<std::int64_t>(snapshot.placementRect.bottom) -
        snapshot.placementRect.top;
    if (!snapshot.placementRect.valid() ||
        width < geometry::scale_dip_ceil(settings_.minOnscreenWidthDip, snapshot.dpi) ||
        height < geometry::scale_dip_ceil(settings_.minOnscreenHeightDip, snapshot.dpi)) {
        return unmanaged(UnmanagedReason::TooSmall);
    }

    return ClassificationResult{WindowDisposition::Managed, UnmanagedReason::None};
}

std::vector<ClassificationResult> ConservativeWindowClassifier::classify_batch(
    std::span<const WindowSnapshot> snapshots) const
{
    std::vector<ClassificationResult> results;
    results.reserve(snapshots.size());
    for (const auto& snapshot : snapshots) {
        results.push_back(classify(snapshot, snapshots));
    }
    return results;
}

void ConservativeWindowClassifier::annotate(WindowSnapshotBatch& batch) const
{
    const auto results = classify_batch(batch.windows);
    for (std::size_t index = 0; index < batch.windows.size(); ++index) {
        batch.windows[index].managed = results[index].managed();
    }
}

} // namespace stage_manager::window
