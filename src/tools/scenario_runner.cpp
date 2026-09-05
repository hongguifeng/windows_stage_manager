#include "solver/layout_solver.h"
#include "solver/visibility_analyzer.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::uint32_t iterations = 100;
    std::uint64_t seed = 0x9'01'20'26ULL;
};

bool parse_unsigned(std::string_view text, std::uint64_t& value)
{
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool parse_options(int argc, char** argv, Options& options)
{
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (index + 1 >= argc) {
            return false;
        }
        std::uint64_t value = 0;
        if (!parse_unsigned(argv[++index], value)) {
            return false;
        }
        if (argument == "--iterations" && value >= 1 && value <= 100'000) {
            options.iterations = static_cast<std::uint32_t>(value);
        } else if (argument == "--seed") {
            options.seed = value;
        } else {
            return false;
        }
    }
    return true;
}

stage_manager::solver::LayoutWindow make_window(
    std::uintptr_t hwnd,
    stage_manager::geometry::Rect rectangle,
    std::int32_t z_index)
{
    stage_manager::solver::LayoutWindow window;
    window.key = {hwnd, static_cast<std::uint32_t>(hwnd), hwnd};
    window.placementRect = rectangle;
    window.visualRect = rectangle;
    window.workArea = {0, 0, 1920, 1080};
    window.lastStableRect = rectangle;
    window.monitor = 1;
    window.zIndex = z_index;
    window.managed = true;
    window.movable = true;
    window.visible = true;
    window.blocksVisibility = true;
    window.currentDesktop = true;
    return window;
}

stage_manager::solver::LayoutSnapshot make_scenario(std::uint32_t window_count,
                                                     std::uint32_t sample,
                                                     std::mt19937_64& random)
{
    std::uniform_int_distribution<std::int64_t> jitter(-12, 12);
    stage_manager::solver::LayoutSnapshot snapshot;
    snapshot.version = sample + 1;
    snapshot.windows.reserve(window_count);
    for (std::uint32_t index = 0; index < window_count; ++index) {
        const auto column = static_cast<std::int64_t>(index % 5);
        const auto row = static_cast<std::int64_t>(index / 5);
        auto rectangle = stage_manager::geometry::Rect{
            20 + column * 360 + jitter(random),
            20 + row * 250 + jitter(random),
            320 + column * 360 + jitter(random),
            240 + row * 250 + jitter(random),
        };
        rectangle.right = rectangle.left + 300;
        rectangle.bottom = rectangle.top + 220;
        if (index != 0 && (index + sample) % 4 == 0) {
            rectangle = snapshot.windows[index - 1].placementRect;
        }
        snapshot.windows.push_back(make_window(
            1000 + sample * 20 + index, rectangle,
            static_cast<std::int32_t>(index)));
    }
    return snapshot;
}

stage_manager::solver::SolverPolicy benchmark_policy()
{
    stage_manager::solver::SolverPolicy policy;
    const stage_manager::solver::EdgeAffordanceRule edge{48, 48, 24, 100};
    policy.ranking.visibility.top = edge;
    policy.ranking.visibility.left = edge;
    policy.ranking.visibility.right = edge;
    policy.ranking.visibility.bottom = edge;
    policy.ranking.visibility.maximumRegionRectangles = 256;
    policy.ranking.minimumOnscreenWidth = 100;
    policy.ranking.minimumOnscreenHeight = 100;
    policy.ranking.activeWindowIndex = 0;
    policy.limits.maximumMoves = 32;
    policy.limits.maximumStates = 128;
    policy.limits.maximumElapsedMs = 1'000;
    policy.limits.maximumCandidatesPerViolation = 128;
    policy.repairTargetLength = 64;
    return policy;
}

std::uint64_t percentile(std::vector<std::uint64_t> samples, std::uint32_t percentage)
{
    std::sort(samples.begin(), samples.end());
    const auto rank = (samples.size() * percentage + 99) / 100;
    return samples[std::max<std::size_t>(rank, 1) - 1];
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        std::fprintf(stderr, "usage: scenario_runner [--iterations N] [--seed N]\n");
        return 2;
    }

    const auto policy = benchmark_policy();
    std::printf("seed,windows,samples,p50_us,p95_us,max_us,states,moves,solved,no_violation,unsatisfiable,timeout,complex\n");
    for (const std::uint32_t window_count : {2u, 5u, 10u, 20u}) {
        std::mt19937_64 random(options.seed ^ window_count);
        std::vector<std::uint64_t> durations;
        std::uint64_t states = 0;
        std::uint64_t moves = 0;
        std::uint32_t solved = 0;
        std::uint32_t no_violation = 0;
        std::uint32_t unsatisfiable = 0;
        std::uint32_t timeout = 0;
        std::uint32_t complex = 0;
        for (std::uint32_t sample = 0; sample < options.iterations; ++sample) {
            const auto layout = make_scenario(window_count, sample, random);
            const auto started = std::chrono::steady_clock::now();
            const auto result = stage_manager::solver::solve_layout(layout, policy);
            const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started);
            durations.push_back(static_cast<std::uint64_t>(duration.count()));
            states += result.statesVisited;
            moves += result.moves.size();
            if (result.statesVisited > policy.limits.maximumStates ||
                result.moves.size() > policy.limits.maximumMoves) {
                return 1;
            }
            switch (result.status) {
            case stage_manager::solver::SolveStatus::Solved:
                ++solved;
                if (!stage_manager::solver::scan_visibility_violations(
                         result.finalSnapshot, policy.ranking.visibility).violations.empty()) {
                    return 1;
                }
                break;
            case stage_manager::solver::SolveStatus::NoViolation:
                ++no_violation;
                break;
            case stage_manager::solver::SolveStatus::Unsatisfiable:
                ++unsatisfiable;
                break;
            case stage_manager::solver::SolveStatus::Timeout:
                ++timeout;
                break;
            case stage_manager::solver::SolveStatus::GeometryTooComplex:
                ++complex;
                break;
            case stage_manager::solver::SolveStatus::InvalidSnapshot:
                return 1;
            }
        }
        std::printf("%llu,%u,%u,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u\n",
                    static_cast<unsigned long long>(options.seed),
                    window_count,
                    options.iterations,
                    static_cast<unsigned long long>(percentile(durations, 50)),
                    static_cast<unsigned long long>(percentile(durations, 95)),
                    static_cast<unsigned long long>(*std::max_element(
                        durations.begin(), durations.end())),
                    static_cast<unsigned long long>(states),
                    static_cast<unsigned long long>(moves),
                    solved,
                    no_violation,
                    unsatisfiable,
                    timeout,
                    complex);
    }
    return 0;
}
