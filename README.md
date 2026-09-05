# Windows 11 Stage Manager-style Window Manager

[English](README.en.md) | **简体中文**

一个住在系统托盘里的小工具，帮你自动整理桌面上的普通窗口：你点击的后台窗口激活后会放到你选定的位置，其余被遮挡的窗口则被排成"标题栏阶梯"，保证每扇窗口始终保留两条可点击的边。它**只移动窗口**，从不改变窗口大小或窗口层级（Z-order）。

![流程示意：点击后台窗口后，新激活窗口自动放到所选位置（默认水平居中、竖直靠下），被遮挡窗口获得两条独立可点击边缘](assets/feature-overview.svg)

## 快速开始

1. 获取发布 ZIP `WindowsStageManager-<version>.zip`（构建方法见文末），解压后直接运行 `stage_manager.exe`。
2. 程序常驻系统托盘，开箱默认：
   - `place_activated_window=true`：窗口由"未活动"变为"活动"时，自动放到"水平居中、竖直靠下"的位置（默认 `activation_horizontal_alignment=1`、`activation_vertical_alignment=2`，可在托盘改为其他 3×3 位置）；
   - `affordance_preset=1`（平衡）：被遮挡窗口修复为两条边可见；
   - `dry_run=false`：方案真正执行（改为 DryRun 则只预览、不动窗口）。
3. 任何时候觉得行为不合适，按 `Ctrl+Alt+F12` 立即停用（紧急停用），之后在托盘里选"启用管理"即可恢复。

## 它为你做什么

### 新激活窗口放到所选位置

- 窗口由"未活动"变为"活动"（鼠标点击、任务栏、Alt+Tab 等均可）时，把它的可视矩形放到你选择的位置：水平可选靠左、居中、靠右，竖直可选靠上、居中、靠下，默认**水平居中 + 竖直靠下**；窗口比工作区还高时保持标题栏贴住工作区顶部。
- 已经活动的窗口不会再次被放置；你把它拖到哪里，它就停在哪里。
- 右键点击只用于激活窗口或打开上下文菜单，菜单弹出、关闭并返回原窗口的整个过程都不触发自动窗口移动；另一个普通窗口正常激活后，自动布局照常恢复。
- 不想让程序动刚激活的窗口？托盘"快速参数设置"里关掉"自动放置新激活窗口"即可，可再随时打开。

### 修复被遮挡的窗口

- 每次激活切换或拖动落位后，程序为**同一显示屏**上至多 20 个普通窗口生成一份批量方案：一次求解、一次验证、一次应用。
- 优先让每扇窗口露出"顶边 + 侧边"（两条独立可点击的边），排成左上/右上交替的标题栏阶梯；无解时按 单边（top > left > right > bottom）→ 部分布局 逐级降级。
- 部分布局中优先保证最近使用的上层窗口，它绝不为其他窗口让位；先保住它、再逐步改善更下方的窗口。实在没有安全方案时程序不做任何改动，并弹出一条"窗口布局暂时无解"通知（同一状态不会反复弹，重试有频率限制，窗口状态变化后自动重算）。

### 安全边界

- 不调整窗口尺寸或 Z-order；每次批量移动都带位置/尺寸后校验。
- 连续多次（默认 3 次）快照或窗口 API 失败后，熔断自动停用程序，避免基于过期数据乱动窗口。
- 你手动拖动完成的窗口，其最终位置优先于任何自动放置。

## 托盘与设置

- 右键托盘图标：**暂停/启用管理**、**打开可视化设置…**、**快速参数设置**、**退出**。
- "快速参数设置"包含：运行模式（应用窗口调整 / 仅预览（DryRun））、自动放置新激活窗口（关闭/开启）、新激活窗口水平位置（靠左/水平居中/靠右）、竖直位置（靠上/垂直居中/靠下）、可辨识度预设（紧凑/平衡/醒目），以及顶/左/右/bottom 四边的最小长度、最大长度、侧边深度、动态占比共 16 个连续值（都有"自定义…"入口，菜单位实时显示当前值）。
- "打开可视化设置…"提供带中文名称、单位和范围的参数编辑，并可用画布实时查看每扇窗口保留的可点击边缘；"保存并应用"后立即生效，无需重启程序。
- 配置与日志自动保存在 `%LOCALAPPDATA%\WindowsStageManager`（`settings.ini`、`logs\manager.log`），日常使用无需手工编辑。DryRun 模式下可在日志的 `batch_complete` 行看到本次规划的移动（`planned_moves`）与使用的降级级别（`affordance_goal`、`affordance_goal_degraded`）。

## 构建与测试（开发者）

需要 CMake 3.25+ / Ninja / VS2022 C++ / Windows 11 SDK：

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

Release 构建改用 `windows-release` 预设；每个提交都必须在 Debug 与 Release 下通过 Configure / Build / CTest。

## 发布暂存

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\stage_release.ps1 `
  -BuildDirectory build\release -OutputDirectory artifacts\release `
  -Version 0.1.0-rc22 -Commit (git rev-parse --short HEAD)
```

生成带版本号的 ZIP 与 `manifest.json`，更新 `current.json`，并把上一个版本留作 `rollback.json` 以便回滚。

## 文档

- [软件功能说明（当前实现的唯一基准）](docs/SOFTWARE_FEATURES.md)
- [软件设计文档（架构、算法与接手指南）](docs/SOFTWARE_DESIGN.md)

上述两份文档随当前代码维护：功能边界以《软件功能说明》为准，内部实现与扩展方法以《软件设计文档》为准。
