#include "window/snapshot_report.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

int main()
{
    using stage_manager::window::ClassificationResult;
    using stage_manager::window::SnapshotRefreshReason;
    using stage_manager::window::SnapshotStatus;
    using stage_manager::window::UnmanagedReason;
    using stage_manager::window::WindowDisposition;
    using stage_manager::window::WindowSnapshot;
    using stage_manager::window::WindowSnapshotBatch;

    WindowSnapshot window;
    window.key = {0x123, 42, 7};
    window.rootHwnd = 0x123;
    window.className = L"Test\"Class\\\u7a97";
    window.placementRect = {-20, 10, 300, 210};
    window.visualRect = {-12, 18, 292, 202};
    window.workArea = {-1920, 0, 0, 1080};
    window.monitor = 0x456;
    window.dpi = 144;
    window.zIndex = 3;
    window.zOrderKnown = true;
    window.currentDesktop = true;
    window.visible = true;

    WindowSnapshotBatch batch;
    batch.version = 9;
    batch.reason = SnapshotRefreshReason::Manual;
    batch.status = SnapshotStatus::Ok;
    batch.complete = true;
    batch.windows.push_back(window);

    const std::vector<ClassificationResult> classifications = {
        {WindowDisposition::Unmanaged, UnmanagedReason::TooSmall},
    };
    std::ostringstream output;
    stage_manager::window::write_snapshot_report(output, batch, classifications);
    const auto report = output.str();

    CHECK(report.find("\"version\":9") != std::string::npos);
    CHECK(report.find("\"reason\":\"manual\"") != std::string::npos);
    CHECK(report.find("\"hwnd\":\"0x123\"") != std::string::npos);
    CHECK(report.find("\"processId\":42") != std::string::npos);
    CHECK(report.find("\"instanceGeneration\":7") != std::string::npos);
    CHECK(report.find("\"className\":\"Test\\\"Class\\\\\\u7a97\"") != std::string::npos);
    CHECK(report.find("\"placementRect\":[-20,10,300,210]") != std::string::npos);
    CHECK(report.find("\"dpi\":144") != std::string::npos);
    CHECK(report.find("\"zIndex\":3") != std::string::npos);
    CHECK(report.find("\"currentDesktop\":true") != std::string::npos);
    CHECK(report.find("\"classification\":\"too_small\"") != std::string::npos);
    CHECK(report.find("\"managed\":false") != std::string::npos);
    return 0;
}
