#include "platform/win32/window_provider.h"
#include "window/snapshot_report.h"
#include "window/window_classifier.h"
#include "window/window_identity.h"
#include "window/mvp_coordinator.h"
#include "window/tracking_window_provider.h"

#ifdef _WIN32

#include <iostream>
#include <string_view>
#include <windows.h>

namespace {
// Deliberately no native mover: this diagnostic cannot move real windows.
class ReportOnlyApplier final : public stage_manager::window::IMoveApplier {
public:
    stage_manager::window::MoveApplyResult apply(
        std::span<const stage_manager::solver::MovePlan>,
        std::span<const stage_manager::solver::ZOrderPlan>,
        const stage_manager::window::MoveApplyOptions&) override
    {
        stage_manager::window::MoveApplyResult result;
        result.status = stage_manager::window::MoveApplyStatus::DryRun;
        return result;
    }
};
}

int main(int argc, char** argv)
{
    using stage_manager::platform::win32::Win32WindowProvider;
    using stage_manager::window::ConservativeWindowClassifier;
    using stage_manager::window::SnapshotRefreshReason;
    using stage_manager::window::SnapshotStatus;
    using stage_manager::window::WindowIdentityTracker;

    Win32WindowProvider provider;
    WindowIdentityTracker identities;
    ConservativeWindowClassifier classifier;

    if (argc > 1) {
        const bool simulate_top = std::string_view(argv[1]) == "--solve-top";
        if ((!simulate_top && std::string_view(argv[1]) != "--solve") || argc > 2) {
            std::cerr << "Usage: window_inspector [--solve | --solve-top]\n";
            return 2;
        }
        namespace sm = stage_manager;
        const auto settings = sm::app::load_settings(sm::app::default_settings_path());
        sm::window::TrackingWindowProvider tracked(provider, identities);
        ReportOnlyApplier applier;
        sm::window::MoveTransactionGuard guard;
        sm::window::InternalMoveTracker moves;
        sm::window::MvpCoordinator coordinator(tracked, applier, guard, moves,
            ConservativeWindowClassifier(settings), settings);
        auto active = reinterpret_cast<std::uintptr_t>(GetForegroundWindow());
        if (simulate_top) {
            auto captured = tracked.capture(SnapshotRefreshReason::Manual);
            ConservativeWindowClassifier(settings).annotate(captured);
            std::int32_t top_z = INT32_MAX;
            active = 0;
            for (const auto& item : captured.windows) {
                if (item.managed && item.zIndex < top_z) {
                    top_z = item.zIndex;
                    active = item.key.hwnd;
                }
            }
        }
        const sm::window::WindowEvent event{sm::window::WindowEventType::Foreground,
            active, 0, GetTickCount64(), 1};
        const auto result = coordinator.process(std::span{&event, 1}, true, true);
        std::optional<std::size_t> active_index;
        for (std::size_t i = 0; i < result.solve.finalSnapshot.windows.size(); ++i)
            if (result.activeWindow && result.solve.finalSnapshot.windows[i].key == *result.activeWindow)
                active_index = i;
        std::cout << "{\"dryRun\":true,\"simulatedActivation\":" << (simulate_top ? "true" : "false")
            << ",\"activeHwnd\":" << (result.activeWindow ? result.activeWindow->hwnd : 0)
            << ",\"activeIndex\":" << (active_index ? std::to_string(*active_index) : "null")
            << ",\"solveAttempted\":" << (result.solveAttempted ? "true" : "false")
            << ",\"reason\":\"" << sm::window::suspend_reason_name(result.reason)
            << "\",\"budgetMs\":" << settings.maxSolveTimeMs
            << ",\"elapsedMs\":" << result.solve.elapsedMs
            << ",\"managedWindows\":" << result.managedWindowCount
            << ",\"plannedMoves\":" << result.solve.moves.size()
            << ",\"remainingViolations\":" << result.solve.violations.size() << ",\"windows\":[";
        sm::solver::VisibilityRequirements rules;
        rules.goal = sm::solver::VisibilityGoal::TitleBarLeftHalf;
        rules.top = {settings.topMinimumLengthDip, settings.topMaximumLengthDip,
                     settings.topDepthDip, settings.topLengthPercent};
        bool first = true;
        for (std::size_t i = 0; i < result.solve.finalSnapshot.windows.size(); ++i) {
            const auto& item = result.solve.finalSnapshot.windows[i];
            if (!item.managed) continue;
            const auto exposure = sm::solver::analyze_title_bar_exposure(result.solve.finalSnapshot, i, rules);
            auto before = item.visualRect;
            for (const auto& move : result.solve.moves)
                if (move.window == item.key)
                    before = *item.visualRect.translated(move.from.left - move.to.left, move.from.top - move.to.top);
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"hwnd\":" << item.key.hwnd << ",\"zIndex\":" << item.zIndex
                << ",\"movable\":" << (item.movable ? "true" : "false")
                << ",\"beforeVisualRect\":[" << before.left << ',' << before.top
                << ',' << before.right << ',' << before.bottom << ']'
                << ",\"workArea\":[" << item.workArea.left << ',' << item.workArea.top
                << ',' << item.workArea.right << ',' << item.workArea.bottom << ']'
                << ",\"visualRect\":[" << item.visualRect.left << ',' << item.visualRect.top
                << ',' << item.visualRect.right << ',' << item.visualRect.bottom << ']'
                << ",\"placementRect\":[" << item.placementRect.left << ',' << item.placementRect.top
                << ',' << item.placementRect.right << ',' << item.placementRect.bottom << ']'
                << ",\"titleBarHeight\":" << item.titleBarHeight
                << ",\"heightSource\":\"" << sm::window::title_bar_source_name(item.titleBarHeightSource)
                << "\",\"protectedHeight\":" << exposure.protectedHeight
                << ",\"requiredWidth\":" << exposure.requiredWidth
                << ",\"visibleWidth\":" << exposure.visibleWidth << '}';
        }
        std::cout << "],\"fixedBlockers\":[";
        first = true;
        for (const auto& item : result.solve.finalSnapshot.windows) {
            if (item.managed) continue;
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"hwnd\":" << item.key.hwnd << ",\"zIndex\":" << item.zIndex
                << ",\"visualRect\":[" << item.visualRect.left << ',' << item.visualRect.top
                << ',' << item.visualRect.right << ',' << item.visualRect.bottom << ']' << '}';
        }
        std::cout << "],\"simulatedFollowups\":[";
        auto simulated = result.solve.finalSnapshot;
        sm::solver::SolverPolicy policy;
        policy.ranking.visibility = rules;
        policy.ranking.visibility.left = {settings.leftMinimumLengthDip, settings.leftMaximumLengthDip,
                                         settings.leftDepthDip, settings.leftLengthPercent};
        policy.ranking.visibility.right = {settings.rightMinimumLengthDip, settings.rightMaximumLengthDip,
                                          settings.rightDepthDip, settings.rightLengthPercent};
        policy.limits.maximumElapsedMs = settings.maxSolveTimeMs;
        policy.limits.maximumStates = settings.maxSolverStates;
        policy.limits.maximumMoves = settings.maxMovesPerBatch;
        // Hypothetical readbacks of planned positions only. Actual applications
        // may reject moves; these rounds are not evidence of live convergence.
        for (int round = 0; result.solveAttempted && active_index && round < 8; ++round) {
            if (sm::solver::scan_visibility_violations(simulated, rules).violations.empty()) break;
            const auto next = sm::solver::solve_layout_prioritized(simulated, policy, *active_index);
            if (round) std::cout << ',';
            std::cout << "{\"plannedMoves\":" << next.moves.size()
                << ",\"remainingViolations\":" << next.violations.size()
                << ",\"elapsedMs\":" << next.elapsedMs << '}';
            simulated = next.finalSnapshot;
            if (next.moves.empty()) break;
        }
        std::cout << "]}\n";
        return 0;
    }

    auto snapshot = provider.capture(SnapshotRefreshReason::Manual);
    identities.apply(snapshot);
    const auto classifications = classifier.classify_batch(snapshot.windows);
    stage_manager::window::write_snapshot_report(std::cout, snapshot, classifications);
    return snapshot.status == SnapshotStatus::Ok && snapshot.complete ? 0 : 1;
}

#else

int main()
{
    return 1;
}

#endif
