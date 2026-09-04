#pragma once

#ifdef _WIN32

#include "window/event_queue.h"

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <unordered_map>
#include <vector>

namespace stage_manager::platform::win32 {

class WinEventHook final {
public:
    explicit WinEventHook(window::EventQueue& queue);
    ~WinEventHook();

    WinEventHook(const WinEventHook&) = delete;
    WinEventHook& operator=(const WinEventHook&) = delete;

    bool start();
    void stop();
    bool running() const;

private:
    static void CALLBACK event_proc(
        HWINEVENTHOOK hook,
        DWORD event,
        HWND hwnd,
        LONG id_object,
        LONG id_child,
        DWORD event_thread,
        DWORD event_time);

    void run();
    bool install_hooks();
    void uninstall_hooks();
    void accept_event(
        DWORD event, HWND hwnd, LONG id_object, LONG id_child, DWORD event_thread);

    static bool is_object_event(DWORD event);
    static std::optional<window::WindowEventType> map_event(DWORD event);

    window::EventQueue* queue_;
    std::thread thread_;
    mutable std::mutex state_mutex_;
    std::condition_variable state_condition_;
    DWORD thread_id_ = 0;
    bool started_ = false;
    bool start_success_ = false;
    bool running_ = false;
    std::vector<HWINEVENTHOOK> hooks_;
    std::atomic<std::uint64_t> sequence_{0};

    static std::mutex registry_mutex_;
    static std::unordered_map<HWINEVENTHOOK, WinEventHook*> registry_;
};

} // namespace stage_manager::platform::win32

#endif
