#include "window/snapshot_report.h"

#include <cstdint>
#include <iomanip>
#include <ostream>
#include <string_view>

namespace stage_manager::window {
namespace {

std::string_view to_string(SnapshotRefreshReason reason) noexcept
{
    switch (reason) {
    case SnapshotRefreshReason::Initial:
        return "initial";
    case SnapshotRefreshReason::Event:
        return "event";
    case SnapshotRefreshReason::Reconcile:
        return "reconcile";
    case SnapshotRefreshReason::EnvironmentChange:
        return "environment_change";
    case SnapshotRefreshReason::HookRecovery:
        return "hook_recovery";
    case SnapshotRefreshReason::Manual:
        return "manual";
    }
    return "unknown";
}

std::string_view to_string(SnapshotStatus status) noexcept
{
    switch (status) {
    case SnapshotStatus::Ok:
        return "ok";
    case SnapshotStatus::EnumerationFailed:
        return "enumeration_failed";
    case SnapshotStatus::Stale:
        return "stale";
    }
    return "unknown";
}

std::string_view to_string(UnmanagedReason reason) noexcept
{
    switch (reason) {
    case UnmanagedReason::None:
        return "none";
    case UnmanagedReason::InvalidIdentity:
        return "invalid_identity";
    case UnmanagedReason::NotRootWindow:
        return "not_root_window";
    case UnmanagedReason::ApiQueryFailed:
        return "api_query_failed";
    case UnmanagedReason::NotCurrentDesktop:
        return "not_current_desktop";
    case UnmanagedReason::Invisible:
        return "invisible";
    case UnmanagedReason::Minimized:
        return "minimized";
    case UnmanagedReason::Maximized:
        return "maximized";
    case UnmanagedReason::Topmost:
        return "topmost";
    case UnmanagedReason::ToolWindow:
        return "tool_window";
    case UnmanagedReason::NoActivate:
        return "no_activate";
    case UnmanagedReason::OwnedWindow:
        return "owned_window";
    case UnmanagedReason::SystemUi:
        return "system_ui";
    case UnmanagedReason::TooSmall:
        return "too_small";
    }
    return "unknown";
}

void write_json_wstring(std::ostream& output, std::wstring_view value)
{
    constexpr char kHexDigits[] = "0123456789abcdef";
    output << '"';
    for (const wchar_t character : value) {
        const auto code_unit = static_cast<std::uint32_t>(character);
        if (code_unit == '"') {
            output << "\\\"";
        } else if (code_unit == '\\') {
            output << "\\\\";
        } else if (code_unit >= 0x20 && code_unit <= 0x7e) {
            output << static_cast<char>(code_unit);
        } else {
            output << "\\u";
            for (int shift = 12; shift >= 0; shift -= 4) {
                output << kHexDigits[(code_unit >> shift) & 0x0f];
            }
        }
    }
    output << '"';
}

void write_rect(std::ostream& output, const PixelRect& rect)
{
    output << '[' << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom
           << ']';
}

void write_handle(std::ostream& output, NativeWindowHandle handle)
{
    const auto flags = output.flags();
    output << "\"0x" << std::hex << handle << '"';
    output.flags(flags);
}

} // namespace

void write_snapshot_report(std::ostream& output,
                           const WindowSnapshotBatch& batch,
                           std::span<const ClassificationResult> classifications)
{
    output << "{\n  \"version\":" << batch.version << ",\n"
           << "  \"reason\":\"" << to_string(batch.reason) << "\",\n"
           << "  \"status\":\"" << to_string(batch.status) << "\",\n"
           << "  \"complete\":" << (batch.complete ? "true" : "false") << ",\n"
           << "  \"lastError\":" << batch.lastError << ",\n"
           << "  \"windows\":[\n";

    for (std::size_t index = 0; index < batch.windows.size(); ++index) {
        const auto& snapshot = batch.windows[index];
        const ClassificationResult classification = index < classifications.size()
            ? classifications[index]
            : ClassificationResult{};

        output << "    {\"hwnd\":";
        write_handle(output, snapshot.key.hwnd);
        output << ",\"processId\":" << snapshot.key.processId
               << ",\"instanceGeneration\":" << snapshot.key.instanceGeneration
               << ",\"rootHwnd\":";
        write_handle(output, snapshot.rootHwnd);
        output << ",\"ownerHwnd\":";
        write_handle(output, snapshot.ownerHwnd);
        output << ",\"className\":";
        write_json_wstring(output, snapshot.className);
        output << ",\"style\":" << snapshot.style << ",\"exStyle\":" << snapshot.exStyle
               << ",\"placementRect\":";
        write_rect(output, snapshot.placementRect);
        output << ",\"visualRect\":";
        write_rect(output, snapshot.visualRect);
        output << ",\"workArea\":";
        write_rect(output, snapshot.workArea);
        output << ",\"monitor\":";
        write_handle(output, snapshot.monitor);
        output << ",\"titleBarHeight\":" << snapshot.titleBarHeight
               << ",\"titleBarHeightSource\":\"" << title_bar_source_name(snapshot.titleBarHeightSource) << "\"";
        output << ",\"dpi\":" << snapshot.dpi << ",\"zIndex\":" << snapshot.zIndex
               << ",\"zOrderKnown\":" << (snapshot.zOrderKnown ? "true" : "false")
               << ",\"currentDesktop\":" << (snapshot.currentDesktop ? "true" : "false")
               << ",\"queryFailures\":" << snapshot.queryFailures
               << ",\"visible\":" << (snapshot.visible ? "true" : "false")
               << ",\"iconic\":" << (snapshot.iconic ? "true" : "false")
               << ",\"zoomed\":" << (snapshot.zoomed ? "true" : "false")
               << ",\"topmost\":" << (snapshot.topmost ? "true" : "false")
               << ",\"cloaked\":" << (snapshot.cloaked ? "true" : "false")
               << ",\"managed\":" << (classification.managed() ? "true" : "false")
               << ",\"classification\":\"" << to_string(classification.reason) << "\"}"
               << (index + 1 == batch.windows.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

} // namespace stage_manager::window
