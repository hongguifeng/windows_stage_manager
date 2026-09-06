# Windows 11 Stage Manager-style Window Manager

**English** | [简体中文](README.zh-CN.md)

A system-tray utility that automatically organizes ordinary desktop windows. When you activate a background window, the app places it at your chosen position and arranges covered windows into a title-bar staircase to expose at least the left half of each full-height title bar, with side strips for recognition where space permits. It **only moves windows** and never changes their size or Z-order.

![After a background window is activated, it moves to the selected position (horizontally centered and bottom-aligned by default), while covered windows expose their title bars and side strips](assets/feature-overview.en.svg)

## Quick start

1. Download the release archive `WindowsStageManager-<version>.zip` (or build it using the instructions below), extract it, and run `stage_manager.exe`.
2. The application stays in the system tray. Its defaults are:
   - `place_activated_window=true`: when a window changes from inactive to active, place it at the horizontally centered and bottom-aligned position (`activation_horizontal_alignment=1` and `activation_vertical_alignment=2`; any of the nine positions can be selected from the tray);
   - `affordance_preset=1` (Balanced): protect the left half of each title bar and prefer visible side strips;
   - `dry_run=false`: apply the calculated moves (DryRun calculates and logs them without moving windows);
   - `ui_language=1`: use the English interface by default; Simplified Chinese remains available from Quick settings.
3. Press `Ctrl+Alt+F12` at any time to disable management immediately. Select **Enable management** from the tray menu to resume.

## What it does

### Place newly activated windows

- When an inactive window becomes active through a mouse click, the taskbar, Alt+Tab, or another normal activation path, its visible rectangle is moved to the configured position. Horizontal alignment can be Left, Center, or Right; vertical alignment can be Top, Center, or Bottom. The default is **Center + Bottom**. The entire placement rectangle stays inside the work area; oversized windows are not automatically placed.
- A window that is already active is not placed again. If you drag it, it remains where you leave it.
- A right click that activates a window or opens its context menu does not trigger automatic window movement, including while the menu opens, closes, and returns focus to the original window. Normal layout resumes after another ordinary window is activated.
- To keep newly activated windows in place, turn off **Place newly activated windows** under **Quick settings** in the tray menu. It can be turned back on at any time.

### Keep covered windows recognizable

- Preview 4 prioritizes the most recent background title immediately above the active title, then older titles. Distance uses title origins with one side-strip width of horizontal tolerance. Bounded anchored reconstruction can rearrange older peers while keeping the recent title nearby; a complete safe layout is never replaced by a closer but obscured one. New `background_title_plan` logs record each peer's before/planned title coordinates, not proof of actual native placement.
- After an activation change or the end of a drag, the app creates one batch plan for up to 20 ordinary windows on the **same monitor**, validates it, and then applies it.
- The main target is a continuous, full-height title-bar segment starting at the left edge and covering at least half the window width. Left-side strips help recognition; top-right branches may expose a right-side strip instead. Side or bottom edges alone do not count as success.
- A bounded search compares top-left staircases and top-right branch entries against all higher windows, preserves size and Z-order, and favors shorter title-bar click distances for more recent windows. The protected height and vertical step are at least 1.5 times the measured/system title-bar baseline, or the configured top height if larger. A quick repair pass handles hidden title prefixes, followed by joint reconstruction and displacement-chain search when needed. Wider titles share rows above the active window; narrower titles can use reserved side space. Only globally safe improvements are applied. Foreground changes may improve existing overlaps; completely separate windows stay in place. Every moved rectangle remains fully inside the work area.
- When the 20-window management limit matters, directly covered peers are selected first; repeated windows with the same process and window class (for example, several File Explorer windows) are selected before single-instance types.
- Partial layouts increase the number of recognizable title bars without hiding any previously recognizable title. Equal counts favor higher windows; distance ordering can be relaxed when space is tight. When there is no safe solution, no move is applied and a short **No window layout is currently available** notification appears. Repeated notifications are throttled, and unfinished background repairs are retried from fresh snapshots after the desktop settles (500 ms initially, backing off to 8 seconds without progress). Dragging, foreground changes, right-click suppression, and disabling management cancel old repairs.
- If a newly foreground window is temporarily absent from the first three snapshots, the activation is retained and retried on the next periodic snapshot refresh instead of being silently lost.

### Safety boundaries

- Window size and Z-order are never changed. A background window moved by the solver must remain fully inside its current monitor work area, and every batch verifies position and size after each move. A window larger than the work area is not moved merely to obtain a layout solution.
- Transient stale snapshots pause the batch and are retried without counting toward permanent disablement. After repeated actual enumeration or Window API failures (three by default), a circuit breaker disables management to avoid moving windows from stale state.
- The final position of a window you manually dragged takes precedence over automatic placement.

## Tray and settings

- Right-click the tray icon for **Pause/Enable management**, **Open visual settings...**, **Quick settings**, and **Exit**. Use **Interface language** under Quick settings to switch between Simplified Chinese and English; the choice takes effect immediately and is saved automatically.
- **Quick settings** contains the run mode (Apply window moves / Preview only (DryRun)), newly activated window placement, horizontal and vertical placement, recognizability presets (Compact/Balanced/Prominent), and 16 continuous values for the minimum length, maximum length, depth, and dynamic percentage of the top, left, right, and bottom edges. Continuous values include a **Custom...** option and show the current value in the menu.
- **Open visual settings...** provides localized names, units, valid ranges, and a live canvas preview of the clickable edges retained for each window. **Save and apply** takes effect immediately without restarting the application.
- Configuration and logs are stored automatically under `%LOCALAPPDATA%\WindowsStageManager` (`settings.ini` and `logs\manager.log`). Manual editing is not required. In DryRun mode, `batch_complete` log entries report planned moves (`planned_moves`) and the title-bar goal (`affordance_goal`). `solver_elapsed_ms` and `background_retry_*` distinguish solver time and pending repairs. Developer builds include `window_inspector --solve` for read-only planning; `--solve-top` simulates activation of the highest managed window without moving it.

## Build and test

Requires CMake 3.25+, Ninja, Visual Studio 2022 C++, and the Windows 11 SDK:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

Use the `windows-release` preset for a Release build. Every commit must pass Configure, Build, and CTest in both Debug and Release configurations.

## Stage a release

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\stage_release.ps1 `
  -BuildDirectory build\release -OutputDirectory artifacts\release `
  -Version 0.2.1 -Commit (git rev-parse --short HEAD)
```

This produces a versioned ZIP archive and `manifest.json`, updates `current.json`, and retains the previous release as `rollback.json`.

## Documentation

- [Software features (authoritative description of the current implementation)](docs/SOFTWARE_FEATURES.md)
- [Software design (architecture, algorithms, and handoff guide)](docs/SOFTWARE_DESIGN.md)

These two documents are maintained with the code. `SOFTWARE_FEATURES.md` defines the current functional boundary, and `SOFTWARE_DESIGN.md` describes the implementation and extension points. They are currently available in Simplified Chinese.
