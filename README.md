# Windows 11 台前调度式窗口管理器

这是一个 Windows 11 后台窗口管理器：普通窗口从非活动切换为活动时，可按开关和用户选择的位置自动放置；水平可选靠左、居中、靠右，竖直可选靠上、居中、靠下，默认仍是水平居中、竖直靠下。窗口已经活动后，用户手动拖到哪里就保持在哪里。随后程序检查同一显示器上最多 20 个普通窗口，优先把被遮挡窗口排成左上、右上两条均衡的标题栏阶梯；确实无法同时露出顶部和侧边时才降级为单边。顶部按实际标题栏高度判断，左边次之，较难辨认的右边和底边要求更大的可点击区域。程序只调整窗口位置，不调整窗口尺寸或 Z-order；完整布局无解时优先保证最近使用的上层窗口可辨认，再依次尝试改善更下层窗口。

![点击后方窗口后，活动窗口居中并为其他窗口保留两条独立可点击边缘的流程示意图](assets/feature-overview.svg)

> 开箱默认值：`dry_run=false`、`place_activated_window=true`、`activation_horizontal_alignment=1`（居中）、`activation_vertical_alignment=2`（靠下）、`affordance_preset=1`（平衡）。若不希望自动放置，可从中文托盘菜单关闭“自动放置新激活窗口”；若只想观察计划，在“快速参数设置 > 运行模式”中选择“仅预览（DryRun）”。

## 构建与测试

需要 CMake 3.25+、Ninja、Visual Studio 2022 C++ 工具链和 Windows 11 SDK：

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

发布构建将 `windows-debug` 替换为 `windows-release`。所有提交要求 Debug 和 Release 均通过 Configure、Build 和 CTest。

## 运行

启动 `build\release\stage_manager.exe`。中文托盘菜单可暂停或恢复管理；“打开可视化设置…”可分别选择新激活窗口的水平、竖直位置，并在预览中实时查看组合效果；同一窗口中还提供紧凑、平衡、醒目三种可辨识度预设，以及顶、左、右、底的动态长度上下限、比例和深度。“快速参数设置”也提供相同的位置选项、预设和连续数值的“自定义…”入口。紧急停用快捷键为 `Ctrl+Alt+F12`。持久化配置与日志默认位于 `%LOCALAPPDATA%\WindowsStageManager`，正常使用不需要手动编辑 `settings.ini`。

如需先验证规则，可从托盘启用 DryRun 并结合日志检查计划。真实应用仍保留紧急停用、故障熔断、用户拖动位置保护以及位置/尺寸/Z-order 后验验证。

## 发布暂存

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\stage_release.ps1 `
  -BuildDirectory build\release -OutputDirectory artifacts\release `
  -Version 0.1.0-rc16 -Commit (git rev-parse --short HEAD)
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
