# Windows 11 台前调度式窗口管理器

这是一个 Windows 11 后台窗口管理器：用户结束拖动后，它检查同一显示器上的 2–20 个普通窗口，并为被遮挡窗口保留可点击的交互边缘。程序不会调整窗口尺寸、Z-order 或活动窗口。

> 安全默认值：`dry_run=true`。默认只计算和记录移动计划，不会移动真实窗口。只有用户明确把配置改为 `dry_run=false` 后才会调用窗口移动 API。

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

首次试用请保持 DryRun，结合日志确认候选窗口与计划正确。真实移动仅应用于可丢弃内容的测试窗口。

## 发布暂存

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\stage_release.ps1 `
  -BuildDirectory build\release -OutputDirectory artifacts\release `
  -Version 0.1.0-rc1 -Commit (git rev-parse --short HEAD)
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
