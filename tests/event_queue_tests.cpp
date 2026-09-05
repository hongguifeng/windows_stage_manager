#include "window/event_queue.h"

#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <unordered_set>
#include <vector>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

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

    constexpr std::size_t kCapacity = 128;
    constexpr std::uint64_t kProducerCount = 4;
    constexpr std::uint64_t kEventsPerProducer = 10'000;
    EventQueue storm_queue(kCapacity);
    std::atomic<std::uint64_t> accepted = 0;
    std::vector<std::thread> producers;
    for (std::uint64_t producer = 0; producer < kProducerCount; ++producer) {
        producers.emplace_back([producer, &accepted, &storm_queue] {
            for (std::uint64_t index = 0; index < kEventsPerProducer; ++index) {
                const auto sequence = producer * kEventsPerProducer + index + 1;
                if (storm_queue.try_push({WindowEventType::LocationChange,
                                          static_cast<std::uintptr_t>(producer + 1),
                                          static_cast<std::uint32_t>(producer + 1),
                                          sequence,
                                          sequence})) {
                    accepted.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }

    CHECK(storm_queue.size() == kCapacity);
    CHECK(accepted.load() == kCapacity);
    CHECK(storm_queue.dropped_count() ==
          kProducerCount * kEventsPerProducer - kCapacity);
    const auto retained = storm_queue.drain();
    CHECK(retained.size() == kCapacity);
    CHECK(storm_queue.size() == 0);
    std::unordered_set<std::uint64_t> retained_sequences;
    for (const auto& event : retained) {
        CHECK(retained_sequences.insert(event.sequence).second);
    }

    return 0;
}
