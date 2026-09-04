#include "window/event_queue.h"

#include <cassert>
#include <chrono>

int main()
{
    using namespace std::chrono_literals;
    using stage_manager::window::EventQueue;
    using stage_manager::window::WindowEvent;
    using stage_manager::window::WindowEventType;

    EventQueue queue(2);
    assert(queue.try_push({WindowEventType::Show, 1, 10, 100, 1}));
    assert(queue.try_push({WindowEventType::Hide, 2, 10, 101, 2}));
    assert(!queue.try_push({WindowEventType::Destroy, 3, 10, 102, 3}));
    assert(queue.dropped_count() == 1);

    const auto first = queue.try_pop();
    assert(first.has_value());
    assert(first->type == WindowEventType::Show);
    assert(first->hwnd == 1);

    WindowEvent waited{};
    assert(queue.wait_pop(waited, 10ms));
    assert(waited.type == WindowEventType::Hide);
    assert(queue.size() == 0);

    queue.close();
    assert(queue.closed());
    assert(!queue.try_push({WindowEventType::Reconcile, 0, 0, 0, 4}));
    assert(queue.dropped_count() == 2);
    assert(!queue.wait_pop(waited, 1ms));

    return 0;
}

