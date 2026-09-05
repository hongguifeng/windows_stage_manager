# Windows 11 台前调度式窗口管理器

这是一个 Windows 11 后台窗口管理器：用户结束拖动或点击切换前台窗口后，它检查同一显示器上的 2–20 个普通窗口，优先为被遮挡窗口保留两个可点击边缘，确实无解时降级为一个。程序不会调整窗口尺寸或活动窗口；只有所有位置方案都无解时，才会从最底层非活动窗口开始尝试安全的 Z-order 调整。

> 开箱默认值：`dry_run=false`。启动后会在拖动结束时自动移动符合条件的非活动窗口。若只想观察计划，请先在配置中设置 `dry_run=true`。

## 构建与测试

需要 CMake 3.25+、Ninja、Visual Studio 2022 C++ 工具链和 Windows 11 SDK：

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

发布构建将 `windows-debug` 替换为 `windows-release`。所有提交要求 Debug 和 Release 均通过 Configure、Build 和 CTest。

## 运行

启动 `build\release\stage_manager.exe`。托盘菜单可暂停或恢复管理；紧急停用快捷键为 `Ctrl+Alt+F12`。配置与日志默认位于 `%LOCALAPPDATA%\WindowsStageManager`。

如需先验证规则，可启用 DryRun 并结合日志检查计划。真实应用仍保留紧急停用、故障熔断、活动窗口保护以及位置/尺寸/Z-order 后验验证。

## 发布暂存

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\stage_release.ps1 `
  -BuildDirectory build\release -OutputDirectory artifacts\release `
  -Version 0.1.0-rc8 -Commit (git rev-parse --short HEAD)
```

脚本生成版本化 ZIP、`current.json`，并在提升下一版本时把上一版本记录保存为 `rollback.json`。

## 文档

- [用户指南](USER_GUIDE.md)
- [已知限制](KNOWN_LIMITATIONS.md)
- [MVP 验收测试](MVP%20验收测试.md)
- [需求说明](Windows%20台前调度式窗口管理器需求说明.md)
- [概要设计](Windows%20台前调度式窗口管理器概要设计.md)
- [详细开发计划](Windows%20台前调度式窗口管理器详细开发计划.md)
- [开发 TODO](TODO.md)
