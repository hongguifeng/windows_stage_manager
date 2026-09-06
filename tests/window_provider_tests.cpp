#include "platform/win32/window_provider.h"

#ifdef _WIN32

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace {

constexpr wchar_t kTestClassName[] = L"WindowsStageManager.WindowProviderTestWindow";

LRESULT CALLBACK test_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    return DefWindowProcW(window, message, w_param, l_param);
}

bool register_test_class(HINSTANCE instance)
{
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = &test_window_proc;
    window_class.lpszClassName = kTestClassName;
    if (RegisterClassExW(&window_class) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

const stage_manager::window::WindowSnapshot* find_snapshot(
    const stage_manager::window::WindowSnapshotBatch& batch, HWND window)
{
    const auto native_window = reinterpret_cast<std::uintptr_t>(window);
    const auto iterator = std::find_if(
        batch.windows.begin(), batch.windows.end(), [native_window](const auto& snapshot) {
            return snapshot.key.hwnd == native_window;
        });
    return iterator == batch.windows.end() ? nullptr : &*iterator;
}

} // namespace

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

int main()
{
    using stage_manager::platform::win32::Win32WindowProvider;
    using stage_manager::window::SnapshotField;
    using stage_manager::window::SnapshotRefreshReason;
    using stage_manager::window::SnapshotStatus;
    using stage_manager::window::field_bit;

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    CHECK(instance != nullptr);
    CHECK(register_test_class(instance));

    const HWND owner = CreateWindowExW(
        0,
        kTestClassName,
        L"Window provider owner",
        WS_OVERLAPPEDWINDOW,
        80,
        80,
        320,
        220,
        nullptr,
        nullptr,
        instance,
        nullptr);
    const HWND first = CreateWindowExW(
        0,
        kTestClassName,
        L"Window provider first",
        WS_OVERLAPPEDWINDOW,
        440,
        80,
        320,
        220,
        nullptr,
        nullptr,
        instance,
        nullptr);
    const HWND owned = CreateWindowExW(
        0,
        kTestClassName,
        L"Window provider owned",
        WS_POPUP | WS_CAPTION,
        120,
        130,
        180,
        120,
        owner,
        nullptr,
        instance,
        nullptr);
    CHECK(owner != nullptr);
    CHECK(first != nullptr);
    CHECK(owned != nullptr);

    ShowWindow(owner, SW_SHOWNA);
    ShowWindow(first, SW_SHOWNA);
    ShowWindow(owned, SW_SHOWNA);
    UpdateWindow(owner);
    UpdateWindow(first);
    UpdateWindow(owned);

    SetWindowPos(first, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    Win32WindowProvider provider;
    const auto initial = provider.capture(SnapshotRefreshReason::Initial);
    CHECK(initial.status == SnapshotStatus::Ok);
    CHECK(initial.complete);
    CHECK(initial.reason == SnapshotRefreshReason::Initial);
    CHECK(!initial.windows.empty());

    const auto* owner_snapshot = find_snapshot(initial, owner);
    const auto* first_snapshot = find_snapshot(initial, first);
    const auto* owned_snapshot = find_snapshot(initial, owned);
    CHECK(owner_snapshot != nullptr);
    CHECK(first_snapshot != nullptr);
    CHECK(owned_snapshot != nullptr);

    CHECK(owner_snapshot->key.processId == GetCurrentProcessId());
    CHECK(owner_snapshot->rootHwnd == owner_snapshot->key.hwnd);
    CHECK(owner_snapshot->ownerHwnd == 0);
    CHECK(owned_snapshot->ownerHwnd == reinterpret_cast<std::uintptr_t>(owner));
    CHECK(owner_snapshot->placementRect.valid());
    CHECK(owner_snapshot->placementRect.right > owner_snapshot->placementRect.left);
    CHECK(owner_snapshot->placementRect.bottom > owner_snapshot->placementRect.top);
    CHECK(owner_snapshot->visualRect.valid());
    CHECK(owner_snapshot->workArea.valid());
    CHECK(owner_snapshot->dpi > 0);
    CHECK(owner_snapshot->titleBarHeight > 0);
    CHECK(owner_snapshot->titleBarHeightSource != stage_manager::window::TitleBarHeightSource::Unknown);
    CHECK(owned_snapshot->titleBarHeight > 0);
    CHECK(owner_snapshot->zOrderKnown);
    CHECK(first_snapshot->zOrderKnown);
    CHECK(owned_snapshot->zOrderKnown);
    CHECK((owner_snapshot->queryFailures & field_bit(SnapshotField::ZOrder)) == 0);
    CHECK((owner_snapshot->queryFailures & field_bit(SnapshotField::Style)) == 0);
    CHECK((first_snapshot->queryFailures & field_bit(SnapshotField::ZOrder)) == 0);
    CHECK((owned_snapshot->queryFailures & field_bit(SnapshotField::ZOrder)) == 0);
    CHECK(owner_snapshot->currentDesktop ||
          (owner_snapshot->queryFailures & field_bit(SnapshotField::CurrentDesktop)) != 0);

    SetWindowPos(owner, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    const auto reordered = provider.capture(SnapshotRefreshReason::Event);
    CHECK(reordered.version > initial.version);
    CHECK(reordered.reason == SnapshotRefreshReason::Event);
    const auto* reordered_owner = find_snapshot(reordered, owner);
    const auto* reordered_first = find_snapshot(reordered, first);
    CHECK(reordered_owner != nullptr);
    CHECK(reordered_first != nullptr);
    CHECK(reordered_owner->zIndex < reordered_first->zIndex);

    ShowWindow(first, SW_HIDE);
    const auto hidden = provider.capture(SnapshotRefreshReason::Reconcile);
    CHECK(hidden.status == SnapshotStatus::Ok);
    CHECK(hidden.reason == SnapshotRefreshReason::Reconcile);
    const auto* hidden_first = find_snapshot(hidden, first);
    CHECK(hidden_first != nullptr);
    CHECK(!hidden_first->visible);

    DestroyWindow(owned);
    DestroyWindow(first);
    DestroyWindow(owner);
    UnregisterClassW(kTestClassName, instance);
    return 0;
}

#else

int main()
{
    return 0;
}

#endif
