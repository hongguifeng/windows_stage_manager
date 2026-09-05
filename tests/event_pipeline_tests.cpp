#include "window/event_coalescer.h"
#include "window/internal_move_tracker.h"
#include "window/reconcile_scheduler.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

int main()
{
    using stage_manager::window::EventCoalescer;
    using stage_manager::window::InternalMoveTracker;
    using stage_manager::window::ReconcileScheduler;
    using stage_manager::window::WindowEvent;
    using stage_manager::window::WindowEventType;

    const std::vector<WindowEvent> input = {
        {WindowEventType::MoveSizeStart, 10, 1, 100, 1},
        {WindowEventType::LocationChange, 10, 1, 101, 2},
        {WindowEventType::LocationChange, 10, 1, 102, 3},
        {WindowEventType::Hide, 10, 1, 103, 4},
        {WindowEventType::LocationChange, 20, 1, 104, 5},
        {WindowEventType::Reconcile, 0, 0, 105, 6},
    };

    EventCoalescer coalescer;
    const auto batch = coalescer.coalesce(input);
    assert(batch.inputCount == input.size());
    assert(batch.coalescedLocationCount == 2);
    assert(batch.requiresFullReconcile);
    assert(batch.events.size() == 5);
    assert(batch.events[0].type == WindowEventType::MoveSizeStart);
    assert(batch.events[1].type == WindowEventType::LocationChange);
    assert(batch.events[1].hwnd == 10);
    assert(batch.events[1].timestampMs == 102);
    assert(batch.events[2].type == WindowEventType::Hide);
    assert(batch.events[3].type == WindowEventType::LocationChange);
    assert(batch.events[3].hwnd == 20);
    assert(batch.events[4].type == WindowEventType::Reconcile);

    coalescer.request_full_reconcile();
    const auto requested_batch = coalescer.coalesce(std::span<const WindowEvent>{});
    assert(requested_batch.requiresFullReconcile);
    const auto cleared_batch = coalescer.coalesce(std::span<const WindowEvent>{});
    assert(!cleared_batch.requiresFullReconcile);

    ReconcileScheduler scheduler(100);
    assert(scheduler.due(0));
    const auto first_reconcile = scheduler.poll(0);
    assert(first_reconcile.type == WindowEventType::Reconcile);
    assert(first_reconcile.sequence == 1);
    assert(!scheduler.due(50));
    assert(scheduler.due(100));
    const auto second_reconcile = scheduler.poll(100);
    assert(second_reconcile.sequence == 2);
    scheduler.reset(500);
    assert(!scheduler.due(550));
    assert(scheduler.due(600));

    ReconcileScheduler clamped_scheduler(0);
    assert(clamped_scheduler.due(0));
    static_cast<void>(clamped_scheduler.poll(0));
    assert(!clamped_scheduler.due(0));
    assert(clamped_scheduler.due(1));

    InternalMoveTracker tracker;
    const auto token = tracker.begin(10, 42, 7, {100, 120, 300, 320});
    assert(token.hwnd == 10);
    assert(token.transactionId == 42);
    assert(token.layoutGeneration == 7);
    assert(token.expectedPlacementRect.left == 100);
    assert(token.expectedPlacementRect.top == 120);
    assert(token.expectedPlacementRect.right == 300);
    assert(token.expectedPlacementRect.bottom == 320);
    assert(tracker.find(10).has_value());
    assert(tracker.matches({WindowEventType::LocationChange, 10, 0, 0, 0}));
    assert(!tracker.matches({WindowEventType::Hide, 10, 0, 0, 0}));
    assert(!tracker.matches({WindowEventType::LocationChange, 20, 0, 0, 0}));
    assert(tracker.complete(token));
    assert(!tracker.find(10).has_value());
    assert(!tracker.matches({WindowEventType::LocationChange, 10, 0, 0, 0}));
    assert(!tracker.complete(token));

    const auto first_token = tracker.begin(10, 100);
    const auto replacement_token = tracker.begin(10, 101);
    assert(!tracker.complete(first_token));
    assert(tracker.matches({WindowEventType::LocationChange, 10, 0, 0, 0}));
    assert(tracker.complete(replacement_token));
    tracker.begin(20, 200);
    tracker.cancel(20);
    assert(!tracker.matches({WindowEventType::LocationChange, 20, 0, 0, 0}));
    tracker.begin(30, 300);
    tracker.clear();
    assert(!tracker.matches({WindowEventType::LocationChange, 30, 0, 0, 0}));

    constexpr std::uint64_t kStormSize = 100'000;
    constexpr std::uintptr_t kStormWindows = 20;
    std::vector<WindowEvent> storm;
    storm.reserve(kStormSize + 3);
    storm.push_back({WindowEventType::MoveSizeStart, 1, 1, 1, 1});
    for (std::uint64_t index = 0; index < kStormSize; ++index) {
        storm.push_back({WindowEventType::LocationChange,
                         static_cast<std::uintptr_t>(index % kStormWindows + 1),
                         1,
                         index + 2,
                         index + 2});
    }
    storm.push_back({WindowEventType::MoveSizeEnd,
                     1,
                     1,
                     kStormSize + 2,
                     kStormSize + 2});
    storm.push_back({WindowEventType::Reconcile,
                     0,
                     0,
                     kStormSize + 3,
                     kStormSize + 3});

    EventCoalescer storm_coalescer;
    const auto storm_batch = storm_coalescer.coalesce(storm);
    CHECK(storm_batch.inputCount == storm.size());
    CHECK(storm_batch.coalescedLocationCount == kStormWindows);
    CHECK(storm_batch.events.size() == kStormWindows + 3);
    CHECK(storm_batch.requiresFullReconcile);
    CHECK(storm_batch.events.front().type == WindowEventType::MoveSizeStart);
    CHECK(storm_batch.events[kStormWindows + 1].type == WindowEventType::MoveSizeEnd);
    CHECK(storm_batch.events.back().type == WindowEventType::Reconcile);
    for (std::uintptr_t hwnd = 1; hwnd <= kStormWindows; ++hwnd) {
        const auto found = std::find_if(
            storm_batch.events.begin(), storm_batch.events.end(), [hwnd](const auto& event) {
                return event.type == WindowEventType::LocationChange && event.hwnd == hwnd;
            });
        CHECK(found != storm_batch.events.end());
        CHECK(found->sequence == kStormSize - kStormWindows + hwnd + 1);
    }

    return 0;
}
