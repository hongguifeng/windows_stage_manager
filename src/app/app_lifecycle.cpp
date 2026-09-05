#include "app/app_lifecycle.h"

#ifdef _WIN32

#include "diagnostics/logger.h"
#include "app/version.h"
#include "window/reconcile_scheduler.h"

#include <algorithm>
#include <chrono>
#include <string>
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

std::uint64_t steady_now_ms() noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
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
    if (!tray_.initialize(message_window_, enabled_.load())) {
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
    instance_mutex_ = CreateMutexW(nullptr, TRUE, kMutexName);
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
        update_runtime_status(static_cast<window::MvpBatchStatus>(w_param));
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
    switch (action) {
    case TrayAction::ToggleEnabled:
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
    case TrayAction::Exit:
        request_exit();
        return;
    case TrayAction::None:
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
        observation.droppedEvents = dropped_delta;
        observation.durationUs = static_cast<std::uint64_t>(batch_duration.count());
        observation.queueDepth = queue_depth;
        observation.reconciliation = result.events.requiresFullReconcile;
        observation.dryRun = result.status == window::MvpBatchStatus::DryRun;
        observation.solverFailure = result.status == window::MvpBatchStatus::Unsatisfiable;
        observation.apiFailure = result.status == window::MvpBatchStatus::ApiError ||
            result.reason == window::MvpSuspendReason::SnapshotUnavailable;
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
        const auto managed_windows = std::to_string(result.managedWindowCount);
        const auto moved_windows = std::to_string(result.movedWindowCount);
        const auto solver_states = std::to_string(result.solve.statesVisited);
        const auto planned_moves = std::to_string(result.solve.moves.size());
        const auto applied_moves = std::to_string(result.apply.appliedMoves.size());
        const auto duration_us = std::to_string(observation.durationUs);
        const auto queue_depth_text = std::to_string(queue_depth);
        const auto total_batches = std::to_string(metrics.batches);
        const auto total_dropped = std::to_string(metrics.droppedEvents);
        diagnostics::Logger::instance().log(
            diagnostics::LogLevel::Debug,
            "batch_complete",
            {{"status", status_name(result.status)},
             {"transaction_id", transaction_id},
             {"layout_generation", layout_generation},
             {"input_events", input_count},
             {"coalesced_locations", coalesced_count},
             {"managed_windows", managed_windows},
             {"moved_windows", moved_windows},
             {"solver_states", solver_states},
             {"planned_moves", planned_moves},
             {"applied_moves", applied_moves},
             {"duration_us", duration_us},
             {"queue_depth", queue_depth_text},
             {"total_batches", total_batches},
             {"total_dropped_events", total_dropped}});
        if (message_window_ != nullptr) {
            const auto reported_status = health_action == window::HealthAction::DisableAutomation
                ? window::MvpBatchStatus::ApiError
                : result.status;
            PostMessageW(message_window_,
                         kCoordinatorStatusMessage,
                         static_cast<WPARAM>(reported_status),
                         0);
        }
    }
}

void AppLifecycle::update_runtime_status(window::MvpBatchStatus status)
{
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
