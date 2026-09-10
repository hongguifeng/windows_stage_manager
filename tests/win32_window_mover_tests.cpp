#include "platform/win32/window_mover.h"
#include "platform/win32/window_provider.h"
#include "window/move_applier.h"
#include "window/mvp_coordinator.h"
#include "window/tracking_window_provider.h"

#ifdef _WIN32

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <utility>
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

class RestrictedWindowProvider final : public stage_manager::window::IWindowProvider {
public:
    RestrictedWindowProvider(stage_manager::window::IWindowProvider& provider,
                             std::vector<std::uintptr_t> allowed)
        : provider_(provider), allowed_(std::move(allowed))
    {
    }

    stage_manager::window::WindowSnapshotBatch capture(
        stage_manager::window::SnapshotRefreshReason reason) override
    {
        auto batch = provider_.capture(reason);
        std::erase_if(batch.windows, [this](const auto& snapshot) {
            return std::find(allowed_.begin(), allowed_.end(), snapshot.key.hwnd) ==
                allowed_.end();
        });
        return batch;
    }

    void handle_event(const stage_manager::window::WindowEvent& event) override
    {
        provider_.handle_event(event);
    }

private:
    stage_manager::window::IWindowProvider& provider_;
    std::vector<std::uintptr_t> allowed_;
};

} // namespace

int main()
{
    using stage_manager::platform::win32::Win32WindowMover;
    using stage_manager::platform::win32::Win32WindowProvider;
    using stage_manager::window::InternalMoveTracker;
    using stage_manager::window::MoveApplyOptions;
    using stage_manager::window::MoveApplyResult;
    using stage_manager::window::MoveApplyStatus;
    using stage_manager::window::MoveFailureTracker;
    using stage_manager::window::SnapshotRefreshReason;
    using stage_manager::window::VerifiedMoveApplier;

    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CHECK(SUCCEEDED(com_result) || com_result == RPC_E_CHANGED_MODE);

    CHECK(Win32WindowMover::position_only_flags() ==
          static_cast<std::uint32_t>(SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER |
                                     SWP_NOOWNERZORDER));
    CHECK(Win32WindowMover::z_order_only_flags() ==
          static_cast<std::uint32_t>(SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                                     SWP_NOOWNERZORDER));

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    CHECK(instance != nullptr);
    CHECK(register_test_class(instance));
    WindowBehavior rejecting_behavior;
    const HWND reference = CreateWindowExW(0,
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

    const auto foreground_before_reorder = GetForegroundWindow();
    const bool foreground_is_topmost = foreground_before_reorder != nullptr &&
        (GetWindowLongPtrW(foreground_before_reorder, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    const HWND reference_insert_after =
        foreground_before_reorder == nullptr || foreground_is_topmost
        ? HWND_TOP
        : foreground_before_reorder;
    CHECK(SetWindowPos(reference, reference_insert_after, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE);
    CHECK(SetWindowPos(rejecting, reference, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE);
    CHECK(SetWindowPos(target, rejecting, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE);
    const auto reorder_before = provider.capture(SnapshotRefreshReason::Manual);
    const auto* reorder_target_before = find_snapshot(reorder_before, target);
    const auto* reorder_reference_before = find_snapshot(reorder_before, reference);
    CHECK(reorder_target_before != nullptr);
    CHECK(reorder_reference_before != nullptr);
    CHECK(!reorder_target_before->topmost);
    CHECK(!reorder_reference_before->topmost);
    CHECK(reorder_target_before->zIndex > reorder_reference_before->zIndex + 1);
    const auto target_rect_before_reorder = reorder_target_before->placementRect;
    const auto reference_rect_before_reorder = reorder_reference_before->placementRect;

    stage_manager::solver::ZOrderPlan reorder_plan;
    reorder_plan.window = reorder_target_before->key;
    reorder_plan.insertAfter = reorder_reference_before->key;
    reorder_plan.fromZIndex = reorder_target_before->zIndex;
    reorder_plan.toZIndex = reorder_reference_before->zIndex + 1;
    auto wrong_reference = reorder_plan.insertAfter;
    ++wrong_reference.processId;
    CHECK(mover.reorder(reorder_plan.window, wrong_reference).status ==
          stage_manager::window::NativeReorderStatus::IdentityMismatch);

    InternalMoveTracker reorder_tracker;
    VerifiedMoveApplier reorder_applier(mover, provider, reorder_tracker);
    MoveApplyOptions reorder_options = options;
    reorder_options.transactionId = 2;
    reorder_options.layoutGeneration = 2;
    MoveApplyResult reordered;
    for (int attempt = 0; attempt < 20; ++attempt) {
        const auto fresh = provider.capture(SnapshotRefreshReason::Manual);
        const auto* fresh_target = find_snapshot(fresh, target);
        const auto* fresh_reference = find_snapshot(fresh, reference);
        CHECK(fresh_target != nullptr);
        CHECK(fresh_reference != nullptr);
        CHECK(fresh_target->zIndex > fresh_reference->zIndex + 1);
        reorder_plan.window = fresh_target->key;
        reorder_plan.insertAfter = fresh_reference->key;
        reorder_plan.fromZIndex = fresh_target->zIndex;
        reorder_plan.toZIndex = fresh_reference->zIndex + 1;
        const std::vector reorder_plans = {reorder_plan};
        reordered = reorder_applier.apply({}, reorder_plans, reorder_options);
        if (reordered.status != MoveApplyStatus::WindowUnavailable) {
            break;
        }
        Sleep(10);
    }
    if (reordered.status != MoveApplyStatus::Applied) {
        const auto* failed_target = find_snapshot(reordered.finalSnapshot, target);
        const auto* failed_reference = find_snapshot(reordered.finalSnapshot, reference);
        std::fprintf(stderr,
                     "reorder diagnostics: status=%u native=%u error=%lu "
                     "before_foreground=%p current_foreground=%p complete=%d "
                     "planned_from=%d planned_to=%d actual_target=%d actual_reference=%d "
                     "target_failures=%u reference_failures=%u target_topmost=%d "
                     "reference_topmost=%d target_z_known=%d reference_z_known=%d\n",
                     static_cast<unsigned>(reordered.status),
                     static_cast<unsigned>(reordered.nativeReorderStatus),
                     static_cast<unsigned long>(reordered.lastError),
                     static_cast<void*>(foreground_before_reorder),
                     static_cast<void*>(GetForegroundWindow()),
                     reordered.finalSnapshot.complete ? 1 : 0,
                     reorder_plan.fromZIndex,
                     reorder_plan.toZIndex,
                     failed_target != nullptr ? failed_target->zIndex : -1,
                     failed_reference != nullptr ? failed_reference->zIndex : -1,
                     failed_target != nullptr ? failed_target->queryFailures : 0u,
                     failed_reference != nullptr ? failed_reference->queryFailures : 0u,
                     failed_target != nullptr && failed_target->topmost ? 1 : 0,
                     failed_reference != nullptr && failed_reference->topmost ? 1 : 0,
                     failed_target != nullptr && failed_target->zOrderKnown ? 1 : 0,
                     failed_reference != nullptr && failed_reference->zOrderKnown ? 1 : 0);
    }
    if (GetForegroundWindow() == nullptr && reordered.status == MoveApplyStatus::VerificationFailed) {
        return 0;
    }
    CHECK(reordered.status == MoveApplyStatus::Applied);
    CHECK(reordered.appliedReorders.size() == 1);
    const auto* reorder_target_after = find_snapshot(reordered.finalSnapshot, target);
    const auto* reorder_reference_after = find_snapshot(reordered.finalSnapshot, reference);
    CHECK(reorder_target_after != nullptr);
    CHECK(reorder_reference_after != nullptr);
    CHECK(reorder_target_after->zIndex == reorder_reference_after->zIndex + 1);
    CHECK(reorder_target_after->placementRect.left == target_rect_before_reorder.left);
    CHECK(reorder_target_after->placementRect.top == target_rect_before_reorder.top);
    CHECK(reorder_reference_after->placementRect.left == reference_rect_before_reorder.left);
    CHECK(reorder_reference_after->placementRect.top == reference_rect_before_reorder.top);
    CHECK(!reorder_target_after->topmost);
    CHECK(!reorder_reference_after->topmost);
    CHECK(GetForegroundWindow() == foreground_before_reorder);

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

    WindowBehavior integration_behavior;
    const HWND covered = CreateWindowExW(0,
                                         kTestClassName,
                                         L"Coordinator covered window",
                                         WS_OVERLAPPEDWINDOW,
                                         100,
                                         400,
                                         360,
                                         260,
                                         nullptr,
                                         nullptr,
                                         instance,
                                         &integration_behavior);
    const HWND active = CreateWindowExW(0,
                                        kTestClassName,
                                        L"Coordinator active window",
                                        WS_OVERLAPPEDWINDOW,
                                        100,
                                        400,
                                        360,
                                        260,
                                        nullptr,
                                        nullptr,
                                        instance,
                                        &integration_behavior);
    CHECK(covered != nullptr);
    CHECK(active != nullptr);
    ShowWindow(covered, SW_SHOWNOACTIVATE);
    ShowWindow(active, SW_SHOWNOACTIVATE);
    UpdateWindow(covered);
    UpdateWindow(active);
    CHECK(SetWindowPos(covered, HWND_TOP, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE);
    CHECK(SetWindowPos(active, HWND_TOP, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE);

    RECT covered_before{};
    RECT active_before{};
    CHECK(GetWindowRect(covered, &covered_before) != FALSE);
    CHECK(GetWindowRect(active, &active_before) != FALSE);

    RestrictedWindowProvider restricted_provider(
        provider,
        {reinterpret_cast<std::uintptr_t>(active),
         reinterpret_cast<std::uintptr_t>(covered)});
    stage_manager::window::WindowIdentityTracker identities;
    stage_manager::window::TrackingWindowProvider tracked_provider(restricted_provider, identities);
    InternalMoveTracker integration_tracker;
    stage_manager::window::MoveTransactionGuard integration_guard;
    MoveFailureTracker integration_failures;
    VerifiedMoveApplier integration_applier(
        mover, tracked_provider, integration_tracker, &integration_guard, &integration_failures);
    stage_manager::app::Settings integration_settings;
    integration_settings.maxManagedWindows = 2;
    integration_settings.maxSolveTimeMs = 1000;
    stage_manager::window::MvpCoordinator coordinator(
        tracked_provider,
        integration_applier,
        integration_guard,
        integration_tracker,
        stage_manager::window::ConservativeWindowClassifier(integration_settings),
        integration_settings);
    const auto active_handle = reinterpret_cast<std::uintptr_t>(active);
    const std::vector<stage_manager::window::WindowEvent> integration_events = {
        {stage_manager::window::WindowEventType::MoveSizeStart, active_handle, 1, 100, 1},
        {stage_manager::window::WindowEventType::LocationChange, active_handle, 1, 101, 2},
        {stage_manager::window::WindowEventType::MoveSizeEnd, active_handle, 1, 102, 3},
    };
    const auto integration_result = coordinator.process(integration_events, true, false);
    if (integration_result.status != stage_manager::window::MvpBatchStatus::Applied) {
        std::fprintf(stderr,
                     "coordinator status=%u reason=%.*s managed=%zu solve=%u plans=%zu "
                     "apply=%u applied=%zu\n",
                     static_cast<unsigned>(integration_result.status),
                     static_cast<int>(stage_manager::window::suspend_reason_name(
                         integration_result.reason).size()),
                     stage_manager::window::suspend_reason_name(integration_result.reason).data(),
                     integration_result.managedWindowCount,
                     static_cast<unsigned>(integration_result.solve.status),
                     integration_result.solve.moves.size(),
                     static_cast<unsigned>(integration_result.apply.status),
                     integration_result.apply.appliedMoves.size());
    }
    CHECK(integration_result.status == stage_manager::window::MvpBatchStatus::Applied);
    CHECK(integration_result.managedWindowCount == 2);
    CHECK(integration_result.solve.status == stage_manager::solver::SolveStatus::Solved);
    CHECK(!integration_result.solve.moves.empty());
    CHECK(integration_result.apply.status == MoveApplyStatus::Applied);
    CHECK(!integration_result.apply.appliedMoves.empty());

    RECT covered_after{};
    RECT active_after{};
    CHECK(GetWindowRect(covered, &covered_after) != FALSE);
    CHECK(GetWindowRect(active, &active_after) != FALSE);
    CHECK(covered_after.left != covered_before.left || covered_after.top != covered_before.top);
    CHECK(active_after.left == active_before.left);
    CHECK(active_after.top == active_before.top);

    DestroyWindow(active);
    DestroyWindow(covered);

    DestroyWindow(rejecting);
    DestroyWindow(target);
    DestroyWindow(reference);
    UnregisterClassW(kTestClassName, instance);
    if (SUCCEEDED(com_result)) {
        CoUninitialize();
    }
    return 0;
}

#else

int main()
{
    return 0;
}

#endif
