#include "platform/win32/window_mover.h"
#include "platform/win32/window_provider.h"
#include "window/move_applier.h"

#ifdef _WIN32

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

namespace {

constexpr wchar_t kTestClassName[] = L"WindowsStageManager.WindowMoverTestWindow";

struct WindowBehavior {
    bool rejectMoves = false;
};

LRESULT CALLBACK test_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    auto* behavior = reinterpret_cast<WindowBehavior*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        behavior = static_cast<WindowBehavior*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(behavior));
    }
    if (message == WM_WINDOWPOSCHANGING && behavior != nullptr && behavior->rejectMoves) {
        auto* position = reinterpret_cast<WINDOWPOS*>(l_param);
        position->flags |= SWP_NOMOVE;
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

bool register_test_class(HINSTANCE instance)
{
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = &test_window_proc;
    window_class.lpszClassName = kTestClassName;
    if (RegisterClassExW(&window_class) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

const stage_manager::window::WindowSnapshot* find_snapshot(
    const stage_manager::window::WindowSnapshotBatch& batch, HWND window)
{
    const auto native = reinterpret_cast<std::uintptr_t>(window);
    const auto iterator = std::find_if(batch.windows.begin(), batch.windows.end(),
                                       [native](const auto& snapshot) {
                                           return snapshot.key.hwnd == native;
                                       });
    return iterator == batch.windows.end() ? nullptr : &*iterator;
}

stage_manager::geometry::Rect to_rect(const stage_manager::window::PixelRect& rectangle)
{
    return {rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
}

} // namespace

int main()
{
    using stage_manager::platform::win32::Win32WindowMover;
    using stage_manager::platform::win32::Win32WindowProvider;
    using stage_manager::window::InternalMoveTracker;
    using stage_manager::window::MoveApplyOptions;
    using stage_manager::window::MoveApplyStatus;
    using stage_manager::window::MoveFailureTracker;
    using stage_manager::window::SnapshotRefreshReason;
    using stage_manager::window::VerifiedMoveApplier;

    CHECK(Win32WindowMover::position_only_flags() ==
          static_cast<std::uint32_t>(SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER |
                                     SWP_NOOWNERZORDER));

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    CHECK(instance != nullptr);
    CHECK(register_test_class(instance));
    WindowBehavior rejecting_behavior;
    const HWND reference = CreateWindowExW(WS_EX_NOACTIVATE,
                                           kTestClassName,
                                           L"Window mover reference",
                                           WS_OVERLAPPEDWINDOW,
                                           420,
                                           100,
                                           260,
                                           180,
                                           nullptr,
                                           nullptr,
                                           instance,
                                           nullptr);
    const HWND target = CreateWindowExW(WS_EX_NOACTIVATE,
                                        kTestClassName,
                                        L"Window mover target",
                                        WS_OVERLAPPEDWINDOW,
                                        100,
                                        100,
                                        260,
                                        180,
                                        nullptr,
                                        nullptr,
                                        instance,
                                        nullptr);
    const HWND rejecting = CreateWindowExW(WS_EX_NOACTIVATE,
                                           kTestClassName,
                                           L"Window mover rejecting",
                                           WS_OVERLAPPEDWINDOW,
                                           740,
                                           100,
                                           260,
                                           180,
                                           nullptr,
                                           nullptr,
                                           instance,
                                           &rejecting_behavior);
    CHECK(reference != nullptr);
    CHECK(target != nullptr);
    CHECK(rejecting != nullptr);
    ShowWindow(reference, SW_SHOWNOACTIVATE);
    ShowWindow(target, SW_SHOWNOACTIVATE);
    ShowWindow(rejecting, SW_SHOWNOACTIVATE);
    UpdateWindow(reference);
    UpdateWindow(target);
    UpdateWindow(rejecting);

    Win32WindowProvider provider;
    const auto before = provider.capture(SnapshotRefreshReason::Manual);
    const auto* before_target = find_snapshot(before, target);
    const auto* before_reference = find_snapshot(before, reference);
    CHECK(before.complete);
    CHECK(before_target != nullptr);
    CHECK(before_reference != nullptr);
    const bool target_above_reference = before_target->zIndex < before_reference->zIndex;

    stage_manager::solver::MovePlan plan;
    plan.window = before_target->key;
    plan.from = to_rect(before_target->placementRect);
    plan.to = {plan.from.left + 40,
               plan.from.top + 30,
               plan.from.right + 40,
               plan.from.bottom + 30};

    Win32WindowMover mover;
    const auto invalid_window = mover.move({}, plan.to);
    CHECK(invalid_window.status ==
          stage_manager::window::NativeMoveStatus::InvalidWindow);
    auto wrong_identity = plan.window;
    ++wrong_identity.processId;
    const auto identity_mismatch = mover.move(wrong_identity, plan.to);
    CHECK(identity_mismatch.status ==
          stage_manager::window::NativeMoveStatus::IdentityMismatch);

    InternalMoveTracker tracker;
    VerifiedMoveApplier applier(mover, provider, tracker);
    MoveApplyOptions options;
    options.dryRun = false;
    options.transactionId = 1;
    options.layoutGeneration = 1;
    options.positionTolerance = 0;
    const std::vector plans = {plan};
    const auto result = applier.apply(plans, options);
    CHECK(result.status == MoveApplyStatus::Applied);
    CHECK(result.appliedMoves.size() == 1);
    const auto* after_target = find_snapshot(result.finalSnapshot, target);
    const auto* after_reference = find_snapshot(result.finalSnapshot, reference);
    CHECK(after_target != nullptr);
    CHECK(after_reference != nullptr);
    CHECK(after_target->placementRect.left == plan.to.left);
    CHECK(after_target->placementRect.top == plan.to.top);
    CHECK(after_target->placementRect.right - after_target->placementRect.left ==
          plan.from.width());
    CHECK(after_target->placementRect.bottom - after_target->placementRect.top ==
          plan.from.height());
    CHECK((after_target->zIndex < after_reference->zIndex) == target_above_reference);
    CHECK(GetForegroundWindow() != target);
    CHECK(tracker.find(reinterpret_cast<std::uintptr_t>(target)).has_value());

    const auto reject_before = provider.capture(SnapshotRefreshReason::Manual);
    const auto* rejecting_snapshot = find_snapshot(reject_before, rejecting);
    CHECK(rejecting_snapshot != nullptr);
    stage_manager::solver::MovePlan rejected_plan;
    rejected_plan.window = rejecting_snapshot->key;
    rejected_plan.from = to_rect(rejecting_snapshot->placementRect);
    rejected_plan.to = {rejected_plan.from.left + 40,
                        rejected_plan.from.top + 30,
                        rejected_plan.from.right + 40,
                        rejected_plan.from.bottom + 30};
    rejecting_behavior.rejectMoves = true;
    InternalMoveTracker rejecting_tracker;
    MoveFailureTracker failures;
    VerifiedMoveApplier rejecting_applier(
        mover, provider, rejecting_tracker, nullptr, &failures);
    const std::vector rejected_plans = {rejected_plan};
    const auto rejected = rejecting_applier.apply(rejected_plans, options);
    CHECK(rejected.status == MoveApplyStatus::MoveRejected);
    CHECK(rejected.requiresReconcile);
    CHECK(rejected.windowFailureCount == 1);
    CHECK(rejected.transactionNonCooperative);
    CHECK(failures.is_non_cooperative(options.transactionId, rejected_plan.window));

    DestroyWindow(rejecting);
    DestroyWindow(target);
    DestroyWindow(reference);
    UnregisterClassW(kTestClassName, instance);
    return 0;
}

#else

int main()
{
    return 0;
}

#endif
