# Windows 11 Stage Manager-style Window Manager

**English** | [简体中文](README.md)

A system-tray utility that automatically organizes ordinary desktop windows. When you activate a background window, the app places it at your chosen position and arranges covered windows into a title-bar staircase so that every window retains two clickable edges. It **only moves windows** and never changes their size or Z-order.

![After a background window is activated, it moves to the selected position (horizontally centered and bottom-aligned by default), while covered windows retain two independent clickable edges](assets/feature-overview.svg)

## Quick start

1. Download the release archive `WindowsStageManager-<version>.zip` (or build it using the instructions below), extract it, and run `stage_manager.exe`.
2. The application stays in the system tray. Its defaults are:
   - `place_activated_window=true`: when a window changes from inactive to active, place it at the horizontally centered and bottom-aligned position (`activation_horizontal_alignment=1` and `activation_vertical_alignment=2`; any of the nine positions can be selected from the tray);
   - `affordance_preset=1` (Balanced): rearrange covered windows so two edges remain visible;
   - `dry_run=false`: apply the calculated moves (DryRun calculates and logs them without moving windows).
3. Press `Ctrl+Alt+F12` at any time to disable management immediately. Select **Enable management** from the tray menu to resume.

## What it does

### Place newly activated windows

- When an inactive window becomes active through a mouse click, the taskbar, Alt+Tab, or another normal activation path, its visible rectangle is moved to the configured position. Horizontal alignment can be Left, Center, or Right; vertical alignment can be Top, Center, or Bottom. The default is **Center + Bottom**. If the window is taller than the work area, its title bar stays aligned with the top of the work area.
- A window that is already active is not placed again. If you drag it, it remains where you leave it.
- A right click that activates a window or opens its context menu does not trigger automatic window movement, including while the menu opens, closes, and returns focus to the original window. Normal layout resumes after another ordinary window is activated.
- To keep newly activated windows in place, turn off **Place newly activated windows** under **Quick settings** in the tray menu. It can be turned back on at any time.

### Keep covered windows recognizable

- After an activation change or the end of a drag, the app creates one batch plan for up to 20 ordinary windows on the **same monitor**, validates it, and then applies it.
- It first tries to expose a top edge plus one side edge—two independent clickable edges—forming alternating top-left and top-right title-bar lanes. If no full solution exists, it degrades through one edge (`top > left > right > bottom`) and then a partial layout.
- Partial layouts protect higher, more recently used windows first; those windows do not move aside for lower windows. When there is no safe solution, no move is applied and a short **No window layout is currently available** notification appears. Repeated notifications are throttled, and the layout is retried after window state changes.

### Safety boundaries

- Window size and Z-order are never changed. Every batch verifies position and size after each move.
- After repeated snapshot or Window API failures (three by default), a circuit breaker disables management to avoid moving windows from stale state.
- The final position of a window you manually dragged takes precedence over automatic placement.

## Tray and settings

- Right-click the tray icon for **Pause/Enable management**, **Open visual settings...**, **Quick settings**, and **Exit**. Use **Interface language** under Quick settings to switch between Simplified Chinese and English; the choice takes effect immediately and is saved automatically.
- **Quick settings** contains the run mode (Apply window moves / Preview only (DryRun)), newly activated window placement, horizontal and vertical placement, recognizability presets (Compact/Balanced/Prominent), and 16 continuous values for the minimum length, maximum length, depth, and dynamic percentage of the top, left, right, and bottom edges. Continuous values include a **Custom...** option and show the current value in the menu.
- **Open visual settings...** provides localized names, units, valid ranges, and a live canvas preview of the clickable edges retained for each window. **Save and apply** takes effect immediately without restarting the application.
- Configuration and logs are stored automatically under `%LOCALAPPDATA%\WindowsStageManager` (`settings.ini` and `logs\manager.log`). Manual editing is not required. In DryRun mode, `batch_complete` log entries report planned moves (`planned_moves`) and the selected degradation level (`affordance_goal` and `affordance_goal_degraded`).

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
  -Version 0.1.0-rc22 -Commit (git rev-parse --short HEAD)
```

This produces a versioned ZIP archive and `manifest.json`, updates `current.json`, and retains the previous release as `rollback.json`.

## Documentation

- [Software features (authoritative description of the current implementation)](docs/SOFTWARE_FEATURES.md)
- [Software design (architecture, algorithms, and handoff guide)](docs/SOFTWARE_DESIGN.md)

These two documents are maintained with the code. `SOFTWARE_FEATURES.md` defines the current functional boundary, and `SOFTWARE_DESIGN.md` describes the implementation and extension points. They are currently available in Simplified Chinese.
