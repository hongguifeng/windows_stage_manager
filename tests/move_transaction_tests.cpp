#include "window/move_transaction.h"

#include <cstdio>
#include <limits>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

int move_transaction_tests()
{
    using stage_manager::window::MoveFailureTracker;
    using stage_manager::window::MoveTransactionGuard;
    using stage_manager::window::WindowEvent;
    using stage_manager::window::WindowEventType;
    using stage_manager::window::WindowKey;

    MoveTransactionGuard guard;
    CHECK(!guard.allows(1, 1));
    guard.activate(10, 2);
    CHECK(guard.allows(10, 2));
    CHECK(!guard.allows(10, 3));
    guard.observe(WindowEvent{WindowEventType::LocationChange, 1, 0, 0, 0});
    CHECK(guard.allows(10, 2));
    guard.observe(WindowEvent{WindowEventType::MoveSizeStart, 1, 0, 0, 0});
    CHECK(!guard.allows(10, 2));
    guard.activate(11, 1);
    CHECK(guard.allows(11, 1));
    CHECK(!guard.allows(10, 2));
    guard.cancel();
    CHECK(!guard.allows(11, 1));

    const WindowKey first{1, 100, 1000};
    MoveFailureTracker failures;
    const auto first_failure = failures.record_failure(10, first);
    CHECK(first_failure.count == 1);
    CHECK(first_failure.nonCooperative);
    CHECK(failures.failure_count(10, first) == 1);
    CHECK(failures.is_non_cooperative(10, first));
    CHECK(failures.record_failure(10, first).count == 2);
    CHECK(failures.failure_count(10, first) == 2);

    const WindowKey reused{1, 100, 1001};
    CHECK(failures.record_failure(10, reused).count == 1);
    CHECK(failures.failure_count(10, first) == 0);
    CHECK(failures.failure_count(10, reused) == 1);
    CHECK(failures.record_failure(11, reused).count == 1);
    CHECK(failures.failure_count(10, reused) == 0);
    failures.clear();
    CHECK(failures.failure_count(11, reused) == 0);
    return 0;
}
