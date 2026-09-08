#include "app/app_lifecycle.h"

#ifdef _WIN32

#include "app/resource.h"
#include "app/settings_dialog.h"
#include "diagnostics/logger.h"
#include "app/version.h"
#include "window/reconcile_scheduler.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace stage_manager::app {
namespace {

std::string_view status_name(window::MvpBatchStatus status) noexcept
{
    switch (status) {
    case window::MvpBatchStatus::Disabled:
        return "disabled";
    case window::MvpBatchStatus::Idle:
        return "idle";
    case window::MvpBatchStatus::Dragging:
        return "dragging";
    case window::MvpBatchStatus::DryRun:
        return "dry_run";
    case window::MvpBatchStatus::Applied:
        return "applied";
    case window::MvpBatchStatus::PartiallySolved:
        return "partially_solved";
    case window::MvpBatchStatus::Unsatisfiable:
        return "unsatisfiable";
    case window::MvpBatchStatus::Suspended:
        return "suspended";
    case window::MvpBatchStatus::ApiError:
        return "api_error";
    case window::MvpBatchStatus::Rebuilding:
        return "rebuilding";
    }
    return "unknown";
}

std::string_view solve_status_name(solver::SolveStatus status) noexcept
{
    switch (status) {
    case solver::SolveStatus::Solved:
        return "solved";
    case solver::SolveStatus::PartiallySolved:
        return "partially_solved";
    case solver::SolveStatus::NoViolation:
        return "no_violation";
    case solver::SolveStatus::Unsatisfiable:
        return "unsatisfiable";
    case solver::SolveStatus::InvalidSnapshot:
        return "invalid_snapshot";
    case solver::SolveStatus::GeometryTooComplex:
        return "geometry_too_complex";
    case solver::SolveStatus::Timeout:
        return "timeout";
    }
    return "unknown";
}

LayoutFailureReason layout_failure_reason(solver::SolveStatus status) noexcept
{
    switch (status) {
    case solver::SolveStatus::Unsatisfiable:
        return LayoutFailureReason::NoFeasibleLayout;
    case solver::SolveStatus::Timeout:
        return LayoutFailureReason::SearchLimitReached;
    case solver::SolveStatus::GeometryTooComplex:
        return LayoutFailureReason::GeometryTooComplex;
    case solver::SolveStatus::InvalidSnapshot:
        return LayoutFailureReason::InvalidLayoutInput;
    case solver::SolveStatus::Solved:
    case solver::SolveStatus::PartiallySolved:
    case solver::SolveStatus::NoViolation:
        return LayoutFailureReason::Unknown;
    }
    return LayoutFailureReason::Unknown;
}

std::string_view layout_failure_reason_name(LayoutFailureReason reason) noexcept
{
    switch (reason) {
    case LayoutFailureReason::NoFeasibleLayout:
        return "no_feasible_layout";
    case LayoutFailureReason::SearchLimitReached:
        return "search_limit_reached";
    case LayoutFailureReason::GeometryTooComplex:
        return "geometry_too_complex";
    case LayoutFailureReason::InvalidLayoutInput:
        return "invalid_layout_input";
    case LayoutFailureReason::Unknown:
        return "unknown";
    }
    return "unknown";
}

std::uint64_t steady_now_ms() noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::wstring single_instance_mutex_name(std::wstring_view default_name)
{
    constexpr wchar_t kTestInstanceVariable[] =
        L"WINDOWS_STAGE_MANAGER_TEST_INSTANCE_ID";
    wchar_t suffix[65]{};
    const auto length = GetEnvironmentVariableW(
        kTestInstanceVariable, suffix, static_cast<DWORD>(std::size(suffix)));
    if (length == 0 || length >= std::size(suffix) ||
        !std::all_of(suffix, suffix + length, [](wchar_t character) {
            return std::iswalnum(character) != 0 || character == L'-' ||
                character == L'_';
        })) {
        return std::wstring(default_name);
    }
    return std::wstring(default_name) + L"." + suffix;
}

} // namespace

AppLifecycle::~AppLifecycle()
{
    stop_window_manager();
    unregister_emergency_hotkey();
    tray_.shutdown();
    destroy_message_window();
    if (instance_ != nullptr) {
        UnregisterClassW(kMessageWindowClass, instance_);
    }
    if (instance_mutex_ != nullptr) {
        CloseHandle(instance_mutex_);
    }
    diagnostics::Logger::instance().shutdown();
}

int AppLifecycle::run(HINSTANCE instance, int)
{
    instance_ = instance;
    if (!acquire_single_instance() || !register_window_class() || !create_message_window()) {
        return 1;
    }
    settings_path_ = default_settings_path();
    settings_ = load_settings(settings_path_);
    health_monitor_.set_failure_threshold(settings_.maxConsecutiveFailures);
    enabled_.store(settings_.enabled);
    dry_run_.store(settings_.dryRun);
    diagnostics::Logger::instance().initialize(default_log_path());
    diagnostics::Logger::instance().log(
        diagnostics::LogLevel::Info,
        "application_start",
        {{"version", kVersion}, {"dry_run", dry_run_.load() ? "true" : "false"}});
    if (!tray_.initialize(message_window_, settings_)) {
        return 1;
    }
    register_emergency_hotkey();
    if (!start_window_manager()) {
        tray_.set_status(TrayStatus::ApiError);
        diagnostics::Logger::instance().log(
            diagnostics::LogLevel::Error, "window_manager_start_failed");
    }

    MSG message{};
    while (true) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            return 1;
        }
        if (result == 0) {
            return static_cast<int>(message.wParam);
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool AppLifecycle::enabled() const noexcept
{
    return enabled_.load();
}

bool AppLifecycle::dry_run() const noexcept
{
    return dry_run_.load();
}

std::uint64_t AppLifecycle::environment_generation() const noexcept
{
    return environment_generation_;
}

bool AppLifecycle::acquire_single_instance()
{
    const auto mutex_name = single_instance_mutex_name(kMutexName);
    instance_mutex_ = CreateMutexW(nullptr, TRUE, mutex_name.c_str());
    if (instance_mutex_ == nullptr) {
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(instance_mutex_);
        instance_mutex_ = nullptr;
        return false;
    }
    return true;
}

bool AppLifecycle::register_window_class()
{
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance_;
    window_class.lpfnWndProc = &AppLifecycle::window_proc;
    window_class.lpszClassName = kMessageWindowClass;
    window_class.hIcon = static_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_STAGE_MANAGER),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_SHARED));
    window_class.hIconSm = static_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_STAGE_MANAGER),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_SHARED));

    if (window_class.hIcon == nullptr || window_class.hIconSm == nullptr) {
        return false;
    }

    if (RegisterClassExW(&window_class) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool AppLifecycle::create_message_window()
{
    message_window_ = CreateWindowExW(
        0,
        kMessageWindowClass,
        L"WindowsStageManager",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        instance_,
        this);
    return message_window_ != nullptr;
}

void AppLifecycle::destroy_message_window()
{
    if (message_window_ != nullptr) {
        DestroyWindow(message_window_);
        message_window_ = nullptr;
    }
}

LRESULT CALLBACK AppLifecycle::window_proc(
    HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    auto* lifecycle = reinterpret_cast<AppLifecycle*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create_struct = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        lifecycle = static_cast<AppLifecycle*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(lifecycle));
    }
    if (lifecycle != nullptr) {
        return lifecycle->handle_message(window, message, w_param, l_param);
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT AppLifecycle::handle_message(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    switch (message) {
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (w_param != FALSE) {
            request_exit();
        }
        return 0;
    case TrayController::kTrayCallbackMessage:
        handle_tray_action(tray_.handle_callback(l_param));
        return 0;
    case WM_COMMAND:
        handle_tray_action(tray_.handle_command(w_param));
        return 0;
    case WM_HOTKEY:
        if (w_param == 1) {
            enabled_.store(false);
            if (move_guard_ != nullptr) {
                move_guard_->cancel();
            }
            settings_.enabled = false;
            tray_.set_enabled(false);
            persist_settings();
        }
        return 0;
    case kCoordinatorStatusMessage:
        update_runtime_status(
            static_cast<window::MvpBatchStatus>(w_param),
            static_cast<LayoutFailureReason>(l_param & 0xff),
            (l_param & 0x100) != 0);
        return 0;
    case kSafetyTripMessage:
        tray_.set_enabled(false);
        tray_.set_status(TrayStatus::ApiError);
        return 0;
    case WM_DISPLAYCHANGE:
        mark_environment_changed("display_change");
        return 0;
    case WM_SETTINGCHANGE:
        mark_environment_changed("setting_change");
        return 0;
    case WM_DPICHANGED:
        mark_environment_changed("dpi_change");
        return 0;
    case WM_CLOSE:
        request_exit();
        return 0;
    case WM_DESTROY:
        stop_window_manager();
        unregister_emergency_hotkey();
        tray_.shutdown();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, w_param, l_param);
    }
}

void AppLifecycle::request_exit()
{
    if (message_window_ != nullptr) {
        DestroyWindow(message_window_);
        message_window_ = nullptr;
    }
}

void AppLifecycle::handle_tray_action(TrayAction action)
{
    switch (action.type) {
    case TrayActionType::ToggleEnabled:
        enabled_.store(!enabled_.load());
        if (enabled_.load()) {
            health_monitor_.reset();
        }
        if (!enabled_.load() && move_guard_ != nullptr) {
            move_guard_->cancel();
        }
        settings_.enabled = enabled_.load();
        tray_.set_enabled(enabled_.load());
        persist_settings();
        return;
    case TrayActionType::RequestCustomSetting: {
        if (!action.setting.has_value()) {
            return;
        }
        const auto value = prompt_custom_setting_value(
            message_window_, action.setting->field, action.setting->value,
            settings_.uiLanguage);
        if (value) {
            diagnostics::Logger::instance().log(
                diagnostics::LogLevel::Info,
                "custom_setting_submitted",
                {{"field", setting_field_name(action.setting->field)},
                 {"value", std::to_string(*value)}});
            handle_tray_action({
                TrayActionType::ApplyCustomSetting,
                SettingSelection{action.setting->field, *value}});
        } else {
            diagnostics::Logger::instance().log(
                diagnostics::LogLevel::Info,
                "custom_setting_cancelled",
                {{"field", setting_field_name(action.setting->field)}});
        }
        return;
    }
    case TrayActionType::ApplySetting:
    case TrayActionType::ApplyCustomSetting: {
        if (!action.setting.has_value()) {
            return;
        }
        if (current_setting_value(settings_, action.setting->field) == action.setting->value) {
            return;
        }
        tray_.set_status(TrayStatus::Rebuilding);
        if (move_guard_ != nullptr) {
            move_guard_->cancel();
        }
        stop_window_manager();
        const bool applied = action.type == TrayActionType::ApplyCustomSetting
            ? apply_custom_setting_selection(settings_, *action.setting)
            : apply_setting_selection(settings_, *action.setting);
        if (!applied) {
            start_window_manager();
            tray_.set_status(enabled_.load() ? TrayStatus::Running : TrayStatus::Paused);
            return;
        }
        dry_run_.store(settings_.dryRun);
        health_monitor_.set_failure_threshold(settings_.maxConsecutiveFailures);
        health_monitor_.reset();
        persist_settings();
        tray_.set_settings(settings_);
        diagnostics::Logger::instance().log(
            diagnostics::LogLevel::Info,
            "setting_changed",
            {{"field", setting_field_name(action.setting->field)},
             {"value", std::to_string(action.setting->value)}});
        if (!start_window_manager()) {
            tray_.set_status(TrayStatus::ApiError);
            diagnostics::Logger::instance().log(
                diagnostics::LogLevel::Error, "window_manager_restart_failed");
            return;
        }
        tray_.set_status(enabled_.load() ? TrayStatus::Running : TrayStatus::Paused);
        return;
    }
    case TrayActionType::OpenSettings: {
        const auto updated = prompt_settings_dialog(message_window_, settings_);
        if (!updated || *updated == settings_) {
            return;
        }
        tray_.set_status(TrayStatus::Rebuilding);
        if (move_guard_ != nullptr) {
            move_guard_->cancel();
        }
        stop_window_manager();
        settings_ = *updated;
        enabled_.store(settings_.enabled);
        dry_run_.store(settings_.dryRun);
        health_monitor_.set_failure_threshold(settings_.maxConsecutiveFailures);
        health_monitor_.reset();
        persist_settings();
        tray_.set_settings(settings_);
        tray_.set_enabled(enabled_.load());
        diagnostics::Logger::instance().log(
            diagnostics::LogLevel::Info, "settings_dialog_applied");
        if (!start_window_manager()) {
            tray_.set_status(TrayStatus::ApiError);
            diagnostics::Logger::instance().log(
                diagnostics::LogLevel::Error, "window_manager_restart_failed");
            return;
        }
        tray_.set_status(enabled_.load() ? TrayStatus::Running : TrayStatus::Paused);
        return;
    }
    case TrayActionType::Exit:
        request_exit();
        return;
    case TrayActionType::None:
        return;
    }
}

bool AppLifecycle::start_window_manager()
{
    event_queue_ = std::make_unique<window::EventQueue>(4096);
    raw_provider_ = std::make_unique<platform::win32::Win32WindowProvider>();
    identities_ = std::make_unique<window::WindowIdentityTracker>();
    provider_ = std::make_unique<window::TrackingWindowProvider>(*raw_provider_, *identities_);
    internal_moves_ = std::make_unique<window::InternalMoveTracker>();
    move_guard_ = std::make_unique<window::MoveTransactionGuard>();
    move_failures_ = std::make_unique<window::MoveFailureTracker>();
    mover_ = std::make_unique<platform::win32::Win32WindowMover>();
    move_applier_ = std::make_unique<window::VerifiedMoveApplier>(
        *mover_, *provider_, *internal_moves_, move_guard_.get(), move_failures_.get());
    coordinator_ = std::make_unique<window::MvpCoordinator>(
        *provider_,
        *move_applier_,
        *move_guard_,
        *internal_moves_,
        window::ConservativeWindowClassifier(settings_),
        settings_);
    event_hook_ = std::make_unique<platform::win32::WinEventHook>(*event_queue_);
    if (!event_hook_->start()) {
        stop_window_manager();
        return false;
    }
    coordinator_stop_.store(false);
    coordinator_thread_ = std::thread(&AppLifecycle::coordinator_loop, this);
    return true;
}

void AppLifecycle::stop_window_manager()
{
    coordinator_stop_.store(true);
    if (event_queue_ != nullptr) {
        event_queue_->close();
    }
    if (event_hook_ != nullptr) {
        event_hook_->stop();
    }
    if (coordinator_thread_.joinable()) {
        coordinator_thread_.join();
    }
    if (event_queue_ != nullptr) {
        const auto metrics = runtime_metrics_.snapshot();
        const auto batches = std::to_string(metrics.batches);
        const auto input_events = std::to_string(metrics.inputEvents);
        const auto dropped_events = std::to_string(metrics.droppedEvents);
        const auto solver_states = std::to_string(metrics.solverStates);
        const auto applied_moves = std::to_string(metrics.appliedMoves);
        const auto api_failures = std::to_string(metrics.apiFailures);
        const auto maximum_queue_depth = std::to_string(metrics.maximumQueueDepth);
        const auto maximum_batch_us = std::to_string(metrics.maximumBatchDurationUs);
        diagnostics::Logger::instance().log(
            diagnostics::LogLevel::Info,
            "runtime_summary",
            {{"batches", batches},
             {"input_events", input_events},
             {"dropped_events", dropped_events},
             {"solver_states", solver_states},
             {"applied_moves", applied_moves},
             {"api_failures", api_failures},
             {"maximum_queue_depth", maximum_queue_depth},
             {"maximum_batch_us", maximum_batch_us}});
    }
    coordinator_.reset();
    move_applier_.reset();
    mover_.reset();
    move_failures_.reset();
    move_guard_.reset();
    internal_moves_.reset();
    provider_.reset();
    identities_.reset();
    raw_provider_.reset();
    event_hook_.reset();
    event_queue_.reset();
}

void AppLifecycle::coordinator_loop()
{
    using namespace std::chrono_literals;
    std::uint64_t observed_drops = 0;
    window::ReconcileScheduler reconcile_scheduler(settings_.reconcileIntervalMs);
    reconcile_scheduler.reset(steady_now_ms());
    while (!coordinator_stop_.load()) {
        window::WindowEvent first;
        const bool received_event = event_queue_->wait_pop(first, 50ms);
        std::vector<window::WindowEvent> events;
        if (received_event) {
            events.push_back(first);
            const auto coalesce_window = std::min(settings_.eventCoalesceWindowMs, 100u);
            std::this_thread::sleep_for(std::chrono::milliseconds(coalesce_window));
        }
        const auto queue_depth = events.size() + event_queue_->size();
        auto remaining = event_queue_->drain();
        events.insert(events.end(), remaining.begin(), remaining.end());
        const auto now_ms = steady_now_ms();
        if (reconcile_scheduler.due(now_ms)) {
            events.push_back(reconcile_scheduler.poll(now_ms));
        }
        if (events.empty()) {
            continue;
        }
        const auto dropped = event_queue_->dropped_count();
        std::uint64_t dropped_delta = 0;
        if (dropped != observed_drops) {
            dropped_delta = dropped >= observed_drops ? dropped - observed_drops : dropped;
            observed_drops = dropped;
            events.push_back({window::WindowEventType::HookError, 0, 0, 0, 0});
        }
        const auto batch_started = std::chrono::steady_clock::now();
        const auto result = coordinator_->process(
            events, enabled_.load(), dry_run_.load());
        const auto batch_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - batch_started);
        diagnostics::BatchObservation observation;
        observation.inputEvents = result.events.inputCount;
        observation.coalescedLocationEvents = result.events.coalescedLocationCount;
        observation.solverStates = result.solve.statesVisited;
        observation.plannedMoves = result.solve.moves.size();
        observation.appliedMoves = result.apply.appliedMoves.size();
        observation.plannedReorders = 0;
        observation.appliedReorders = result.apply.appliedReorders.size();
        observation.droppedEvents = dropped_delta;
        observation.durationUs = static_cast<std::uint64_t>(batch_duration.count());
        observation.queueDepth = queue_depth;
        observation.reconciliation = result.events.requiresFullReconcile;
        observation.dryRun = result.status == window::MvpBatchStatus::DryRun;
        observation.solverFailure = result.status == window::MvpBatchStatus::Unsatisfiable;
        observation.apiFailure = result.status == window::MvpBatchStatus::ApiError ||
            result.reason == window::MvpSuspendReason::SnapshotUnavailable;
        observation.partialLayout = result.partialLayoutUsed;
        runtime_metrics_.record(observation);
        const auto health_action = health_monitor_.observe(result.status, result.reason);
        if (health_action == window::HealthAction::DisableAutomation &&
            enabled_.exchange(false)) {
            if (move_guard_ != nullptr) {
                move_guard_->cancel();
            }
            const auto failures = std::to_string(
                health_monitor_.state().consecutiveFailures);
            diagnostics::Logger::instance().log(
                diagnostics::LogLevel::Error,
                "automation_disabled_after_failures",
                {{"consecutive_failures", failures}});
            if (message_window_ != nullptr) {
                PostMessageW(message_window_, kSafetyTripMessage, 0, 0);
            }
        }

        const auto metrics = runtime_metrics_.snapshot();
        const auto transaction_id = std::to_string(result.transactionId);
        const auto layout_generation = std::to_string(result.layoutGeneration);
        const auto input_count = std::to_string(result.events.inputCount);
        const auto coalesced_count = std::to_string(result.events.coalescedLocationCount);
        const auto snapshot_windows = std::to_string(result.snapshotWindowCount);
        const auto solver_windows = std::to_string(result.solverWindowCount);
        const auto managed_windows = std::to_string(result.managedWindowCount);
        const auto blocking_windows = std::to_string(result.blockingWindowCount);
        const auto moved_windows = std::to_string(result.movedWindowCount);
        const auto solver_states = std::to_string(result.solve.statesVisited);
        const auto solver_elapsed_ms = std::to_string(result.solve.elapsedMs);
        const auto retry_delay_ms = std::to_string(result.backgroundRetryDelayMs);
        const auto solve_status = result.solveAttempted
            ? solve_status_name(result.solve.status)
            : std::string_view{"not_run"};
        const auto planned_moves = std::to_string(result.solve.moves.size());
        const auto applied_moves = std::to_string(result.apply.appliedMoves.size());
        const auto apply_status = std::to_string(static_cast<unsigned>(result.apply.status));
        const auto apply_error = std::to_string(result.apply.lastError);
        const auto failed_move_index = std::to_string(result.apply.failedMoveIndex);
        const auto apply_snapshot_status = std::to_string(static_cast<unsigned>(result.apply.finalSnapshot.status));
        const auto apply_snapshot_error = std::to_string(result.apply.finalSnapshot.lastError);
        const auto planned_reorders = std::string{"0"};
        const auto applied_reorders = std::to_string(result.apply.appliedReorders.size());
        const auto fallback_used = result.fallbackUsed ? std::string_view{"true"}
                                                       : std::string_view{"false"};
        const auto affordance_goal = result.affordanceGoal == solver::VisibilityGoal::TitleBarLeftHalf
            ? std::string_view{"title_bar_left_half"}
            : result.affordanceGoal ==
                solver::VisibilityGoal::TopAndSide
            ? std::string_view{"top_and_side"}
            : std::string_view{"any_edge"};
        const auto affordance_goal_degraded = result.affordanceGoalDegraded
            ? std::string_view{"true"}
            : std::string_view{"false"};
        const auto partial_layout_used = result.partialLayoutUsed
            ? std::string_view{"true"}
            : std::string_view{"false"};
        const auto remaining_violations = std::to_string(result.solve.violations.size());
        const auto activation_placement_used = result.activationPlacementUsed
            ? std::string_view{"true"}
            : std::string_view{"false"};
        const auto activation_layout_suppressed = result.activationLayoutSuppressed
            ? std::string_view{"true"}
            : std::string_view{"false"};
        const auto duration_us = std::to_string(observation.durationUs);
        const auto queue_depth_text = std::to_string(queue_depth);
        const auto total_batches = std::to_string(metrics.batches);
        const auto total_dropped = std::to_string(metrics.droppedEvents);
        diagnostics::Logger::instance().log(
            diagnostics::LogLevel::Debug,
            "batch_complete",
            {{"status", status_name(result.status)},
             {"reason", window::suspend_reason_name(result.reason)},
             {"transaction_id", transaction_id},
             {"layout_generation", layout_generation},
             {"input_events", input_count},
             {"coalesced_locations", coalesced_count},
             {"snapshot_windows", snapshot_windows},
             {"solver_windows", solver_windows},
             {"managed_windows", managed_windows},
             {"blocking_windows", blocking_windows},
             {"moved_windows", moved_windows},
             {"solve_status", solve_status},
             {"solver_states", solver_states},
             {"solver_elapsed_ms", solver_elapsed_ms},
             {"background_retry_used", result.backgroundRetryUsed ? "true" : "false"},
             {"background_retry_pending", result.backgroundRetryPending ? "true" : "false"},
             {"background_retry_delay_ms", retry_delay_ms},
             {"planned_moves", planned_moves},
             {"applied_moves", applied_moves},
             {"apply_status_code", apply_status},
             {"apply_last_error", apply_error},
             {"failed_move_index", failed_move_index},
             {"apply_snapshot_status_code", apply_snapshot_status},
             {"apply_snapshot_last_error", apply_snapshot_error},
             {"planned_reorders", planned_reorders},
             {"applied_reorders", applied_reorders},
             {"fallback_used", fallback_used},
             {"affordance_goal", affordance_goal},
             {"affordance_goal_degraded", affordance_goal_degraded},
             {"partial_layout_used", partial_layout_used},
             {"remaining_violation_count", remaining_violations},
              {"activation_placement_used", activation_placement_used},
              {"activation_layout_suppressed", activation_layout_suppressed},
             {"duration_us", duration_us},
             {"queue_depth", queue_depth_text},
             {"total_batches", total_batches},
             {"total_dropped_events", total_dropped}});
        // Planned geometry, not proof that native applications accepted it.
        // No application titles or paths: HWND + Z-order make later reports
        // of a recent window moving far away diagnosable from the log.
        const auto& planned = result.solve.finalSnapshot.windows;
        const auto anchor = std::find_if(planned.begin(), planned.end(), [&](const auto& item) {
            return result.activeWindow && item.key == *result.activeWindow;
        });
        if (result.solveAttempted && anchor != planned.end()) {
            const auto point = [](std::int64_t x, std::int64_t y) {
                return std::to_string(x) + "," + std::to_string(y);
            };
            const auto active_hwnd = std::to_string(anchor->key.hwnd);
            const auto active_title = point(anchor->visualRect.left, anchor->visualRect.top);
            for (const auto& item : planned) {
                if (!item.managed || item.key == anchor->key) continue;
                const auto move = std::find_if(result.solve.moves.begin(), result.solve.moves.end(),
                    [&](const auto& candidate) { return candidate.window == item.key; });
                const auto from_left = item.visualRect.left +
                    (move == result.solve.moves.end() ? 0 : move->from.left - move->to.left);
                const auto from_top = item.visualRect.top +
                    (move == result.solve.moves.end() ? 0 : move->from.top - move->to.top);
                const auto hwnd = std::to_string(item.key.hwnd);
                const auto z_index = std::to_string(item.zIndex);
                const auto before = point(from_left, from_top);
                const auto after = point(item.visualRect.left, item.visualRect.top);
                diagnostics::Logger::instance().log(diagnostics::LogLevel::Debug, "background_title_plan",
                    {{"transaction_id", transaction_id}, {"active_hwnd", active_hwnd},
                     {"hwnd", hwnd}, {"z_index", z_index}, {"active_title_planned", active_title},
                     {"title_before", before}, {"title_planned", after}});
            }
        }
        if (message_window_ != nullptr) {
            const auto reported_status = health_action == window::HealthAction::DisableAutomation
                ? window::MvpBatchStatus::ApiError
                : result.status;
            PostMessageW(message_window_,
                         kCoordinatorStatusMessage,
                         static_cast<WPARAM>(reported_status),
                         static_cast<LPARAM>(layout_failure_reason(result.solve.status)) |
                             ((reported_status == window::MvpBatchStatus::Applied ||
                               (result.solveAttempted && result.solve.status == solver::SolveStatus::NoViolation &&
                                reported_status == window::MvpBatchStatus::Idle)) ? 0x100 : 0));
        }
    }
}

void AppLifecycle::update_runtime_status(
    window::MvpBatchStatus status, LayoutFailureReason failure_reason, bool layout_recovered)
{
    const bool entered_unsatisfiable = layout_failure_episode_.observe(
        status == window::MvpBatchStatus::Unsatisfiable,
        layout_recovered || status == window::MvpBatchStatus::Disabled);
    if (entered_unsatisfiable &&
        tray_.show_layout_failure_notification(steady_now_ms(), failure_reason)) {
        diagnostics::Logger::instance().log(
            diagnostics::LogLevel::Info,
            "layout_failure_notification_shown",
            {{"failure_reason", layout_failure_reason_name(failure_reason)}});
    }
    switch (status) {
    case window::MvpBatchStatus::Disabled:
        tray_.set_status(TrayStatus::Paused);
        return;
    case window::MvpBatchStatus::Unsatisfiable:
    case window::MvpBatchStatus::Suspended:
        tray_.set_status(TrayStatus::Unsatisfiable);
        return;
    case window::MvpBatchStatus::ApiError:
        tray_.set_status(TrayStatus::ApiError);
        return;
    case window::MvpBatchStatus::Rebuilding:
        tray_.set_status(TrayStatus::Rebuilding);
        return;
    case window::MvpBatchStatus::Idle:
    case window::MvpBatchStatus::Dragging:
    case window::MvpBatchStatus::DryRun:
    case window::MvpBatchStatus::Applied:
    case window::MvpBatchStatus::PartiallySolved:
        tray_.set_status(enabled_.load() ? TrayStatus::Running : TrayStatus::Paused);
        return;
    }
}

void AppLifecycle::register_emergency_hotkey()
{
    emergency_hotkey_registered_ = RegisterHotKey(
        message_window_, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12) != FALSE;
}

void AppLifecycle::unregister_emergency_hotkey()
{
    if (emergency_hotkey_registered_ && message_window_ != nullptr) {
        UnregisterHotKey(message_window_, 1);
    }
    emergency_hotkey_registered_ = false;
}

void AppLifecycle::mark_environment_changed(const char* reason)
{
    ++environment_generation_;
    diagnostics::Logger::instance().log(
        diagnostics::LogLevel::Debug,
        "environment_changed",
        {{"generation", std::to_string(environment_generation_)}, {"reason", reason}});
    if (event_queue_ != nullptr) {
        event_queue_->try_push({window::WindowEventType::Reconcile,
                                0,
                                0,
                                steady_now_ms(),
                                environment_generation_});
    }
}

void AppLifecycle::persist_settings()
{
    if (!settings_path_.empty() && !save_settings(settings_, settings_path_)) {
        diagnostics::Logger::instance().log(
            diagnostics::LogLevel::Warning, "settings_save_failed", {{"path", settings_path_.string()}});
    }
}

} // namespace stage_manager::app

#endif
