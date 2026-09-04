#include "platform/win32/win_event_hook.h"

#ifdef _WIN32

#include <array>
#include <optional>

namespace stage_manager::platform::win32 {

std::mutex WinEventHook::registry_mutex_;
std::unordered_map<HWINEVENTHOOK, WinEventHook*> WinEventHook::registry_;

WinEventHook::WinEventHook(window::EventQueue& queue)
    : queue_(&queue)
{
}

WinEventHook::~WinEventHook()
{
    stop();
}

bool WinEventHook::start()
{
    std::unique_lock lock(state_mutex_);
    if (thread_.joinable() || running_) {
        return running_;
    }

    started_ = false;
    start_success_ = false;
    thread_ = std::thread(&WinEventHook::run, this);
    state_condition_.wait(lock, [this] { return started_; });
    const bool success = start_success_;
    lock.unlock();

    if (!success && thread_.joinable()) {
        thread_.join();
    }
    return success;
}

void WinEventHook::stop()
{
    DWORD thread_id = 0;
    {
        std::scoped_lock lock(state_mutex_);
        if (!thread_.joinable()) {
            return;
        }
        thread_id = thread_id_;
    }

    if (thread_id != 0) {
        PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool WinEventHook::running() const
{
    std::scoped_lock lock(state_mutex_);
    return running_;
}

void WinEventHook::run()
{
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    {
        std::scoped_lock lock(state_mutex_);
        thread_id_ = GetCurrentThreadId();
        start_success_ = install_hooks();
        started_ = true;
        running_ = start_success_;
    }
    state_condition_.notify_all();

    if (!start_success_) {
        return;
    }

    while (true) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    uninstall_hooks();
    {
        std::scoped_lock lock(state_mutex_);
        running_ = false;
        thread_id_ = 0;
    }
}

bool WinEventHook::install_hooks()
{
    constexpr std::array<std::pair<DWORD, DWORD>, 6> ranges = {{
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY},
        {EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW},
        {EVENT_OBJECT_HIDE, EVENT_OBJECT_HIDE},
        {EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE},
    }};

    for (const auto [minimum, maximum] : ranges) {
        const auto hook = SetWinEventHook(
            minimum,
            maximum,
            nullptr,
            &WinEventHook::event_proc,
            0,
            0,
            WINEVENT_OUTOFCONTEXT);
        if (hook == nullptr) {
            uninstall_hooks();
            return false;
        }
        {
            std::scoped_lock lock(registry_mutex_);
            registry_.emplace(hook, this);
        }
        hooks_.push_back(hook);
    }
    return true;
}

void WinEventHook::uninstall_hooks()
{
    for (const auto hook : hooks_) {
        {
            std::scoped_lock lock(registry_mutex_);
            registry_.erase(hook);
        }
        UnhookWinEvent(hook);
    }
    hooks_.clear();
}

void CALLBACK WinEventHook::event_proc(
    HWINEVENTHOOK hook,
    DWORD event,
    HWND hwnd,
    LONG id_object,
    LONG id_child,
    DWORD event_thread,
    DWORD)
{
    WinEventHook* owner = nullptr;
    {
        std::scoped_lock lock(registry_mutex_);
        const auto iterator = registry_.find(hook);
        if (iterator != registry_.end()) {
            owner = iterator->second;
        }
    }
    if (owner != nullptr) {
        owner->accept_event(event, hwnd, id_object, id_child, event_thread);
    }
}

void WinEventHook::accept_event(
    DWORD event, HWND hwnd, LONG id_object, LONG id_child, DWORD event_thread)
{
    const auto mapped_event = map_event(event);
    if (!mapped_event.has_value() || hwnd == nullptr) {
        return;
    }

    if (is_object_event(event)) {
        if (id_object != OBJID_WINDOW || id_child != CHILDID_SELF) {
            return;
        }
        if (event != EVENT_OBJECT_DESTROY &&
            GetAncestor(hwnd, GA_ROOT) != hwnd) {
            return;
        }
    }

    if (event != EVENT_OBJECT_DESTROY && !IsWindow(hwnd)) {
        return;
    }

    window::WindowEvent window_event;
    window_event.type = *mapped_event;
    window_event.hwnd = reinterpret_cast<std::uintptr_t>(hwnd);
    window_event.eventThreadId = event_thread;
    window_event.timestampMs = GetTickCount64();
    window_event.sequence = sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
    queue_->try_push(window_event);
}

bool WinEventHook::is_object_event(DWORD event)
{
    switch (event) {
    case EVENT_OBJECT_DESTROY:
    case EVENT_OBJECT_SHOW:
    case EVENT_OBJECT_HIDE:
    case EVENT_OBJECT_LOCATIONCHANGE:
        return true;
    default:
        return false;
    }
}

std::optional<window::WindowEventType> WinEventHook::map_event(DWORD event)
{
    switch (event) {
    case EVENT_SYSTEM_MOVESIZESTART:
        return window::WindowEventType::MoveSizeStart;
    case EVENT_SYSTEM_MOVESIZEEND:
        return window::WindowEventType::MoveSizeEnd;
    case EVENT_SYSTEM_FOREGROUND:
        return window::WindowEventType::Foreground;
    case EVENT_OBJECT_LOCATIONCHANGE:
        return window::WindowEventType::LocationChange;
    case EVENT_OBJECT_SHOW:
        return window::WindowEventType::Show;
    case EVENT_OBJECT_HIDE:
        return window::WindowEventType::Hide;
    case EVENT_OBJECT_DESTROY:
        return window::WindowEventType::Destroy;
    default:
        return std::nullopt;
    }
}

} // namespace stage_manager::platform::win32

#endif

