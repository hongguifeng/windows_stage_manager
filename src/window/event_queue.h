#pragma once

#include "window/window_event.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace stage_manager::window {

class EventQueue final {
public:
    explicit EventQueue(std::size_t capacity);

    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;

    bool try_push(WindowEvent event);
    std::optional<WindowEvent> try_pop();
    bool wait_pop(WindowEvent& event, std::chrono::milliseconds timeout);
    std::vector<WindowEvent> drain(std::size_t max_events = 0);

    void close();
    bool closed() const;
    std::size_t size() const;
    std::uint64_t dropped_count() const;

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<WindowEvent> queue_;
    bool closed_ = false;
    std::uint64_t dropped_count_ = 0;
};

} // namespace stage_manager::window

