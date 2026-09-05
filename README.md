# Windows 11 台前调度式窗口管理器

这是一个 Windows 11 后台窗口管理器：普通窗口从非活动切换为活动时，可按开关把它的视觉框一次性移到当前显示器工作区中央；窗口已经活动后，用户手动拖到哪里就保持在哪里。随后程序检查同一显示器上最多 20 个普通窗口，优先为被遮挡窗口保留两段位于不同边缘、且不共用角部的可点击区域，确实无解时才降级为一个边缘；合法修复位置优先靠近工作区中心，不固定偏向某个角。程序不调整窗口尺寸；只有所有位置方案都无解时，才会从最深的局部层级开始尝试安全的 Z-order 调整，尽量保持最近使用的上层窗口顺序不变。

> 开箱默认值：`dry_run=false`、`center_activated_window=true`。启动后会在前台切换时居中新活动窗口，并在拖动结束时自动移动符合条件的被遮挡窗口。若不希望自动居中，可从中文托盘菜单关闭“新激活窗口居中”；若只想观察计划，在“参数设置 > 运行模式”中选择“仅预览（DryRun）”。

## 构建与测试

需要 CMake 3.25+、Ninja、Visual Studio 2022 C++ 工具链和 Windows 11 SDK：

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

发布构建将 `windows-debug` 替换为 `windows-release`。所有提交要求 Debug 和 Release 均通过 Configure、Build 和 CTest。

## 运行

启动 `build\release\stage_manager.exe`。中文托盘菜单可暂停或恢复管理；“参数设置”子菜单可直接修改全部运行参数，当前值显示在菜单标题中并被勾选。数值参数既提供常用预设，也可通过“自定义…”输入范围内的整数；选择后立即保存和生效。紧急停用快捷键为 `Ctrl+Alt+F12`。持久化配置与日志默认位于 `%LOCALAPPDATA%\WindowsStageManager`，正常使用不需要手动编辑 `settings.ini`。

如需先验证规则，可从托盘启用 DryRun 并结合日志检查计划。真实应用仍保留紧急停用、故障熔断、用户拖动位置保护以及位置/尺寸/Z-order 后验验证。

## 发布暂存

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\stage_release.ps1 `
  -BuildDirectory build\release -OutputDirectory artifacts\release `
  -Version 0.1.0-rc11 -Commit (git rev-parse --short HEAD)
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
