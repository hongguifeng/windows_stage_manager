#include "window/event_queue.h"

#include <algorithm>

namespace stage_manager::window {

EventQueue::EventQueue(std::size_t capacity)
    : capacity_(std::max<std::size_t>(capacity, 1))
{
}

bool EventQueue::try_push(WindowEvent event)
{
    {
        std::scoped_lock lock(mutex_);
        if (closed_ || queue_.size() >= capacity_) {
            ++dropped_count_;
            return false;
        }
        queue_.push_back(event);
    }
    condition_.notify_one();
    return true;
}

std::optional<WindowEvent> EventQueue::try_pop()
{
    std::scoped_lock lock(mutex_);
    if (queue_.empty()) {
        return std::nullopt;
    }
    WindowEvent event = queue_.front();
    queue_.pop_front();
    return event;
}

bool EventQueue::wait_pop(WindowEvent& event, std::chrono::milliseconds timeout)
{
    std::unique_lock lock(mutex_);
    const bool signalled = condition_.wait_for(lock, timeout, [this] {
        return closed_ || !queue_.empty();
    });
    if (!signalled || queue_.empty()) {
        return false;
    }
    event = queue_.front();
    queue_.pop_front();
    return true;
}

std::vector<WindowEvent> EventQueue::drain(std::size_t max_events)
{
    std::scoped_lock lock(mutex_);
    const std::size_t count = max_events == 0
        ? queue_.size()
        : std::min(max_events, queue_.size());
    std::vector<WindowEvent> events;
    events.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        events.push_back(queue_.front());
        queue_.pop_front();
    }
    return events;
}

void EventQueue::close()
{
    {
        std::scoped_lock lock(mutex_);
        closed_ = true;
    }
    condition_.notify_all();
}

bool EventQueue::closed() const
{
    std::scoped_lock lock(mutex_);
    return closed_;
}

std::size_t EventQueue::size() const
{
    std::scoped_lock lock(mutex_);
    return queue_.size();
}

std::uint64_t EventQueue::dropped_count() const
{
    std::scoped_lock lock(mutex_);
    return dropped_count_;
}

} // namespace stage_manager::window

