# Windows Stage Manager 用户指南

## 首次启动

1. 解压版本化发布包并启动 `stage_manager.exe`。
2. 程序常驻系统托盘；默认 `dry_run=false`，前台切换时会居中新活动窗口，拖动结束后会自动整理符合条件的被遮挡窗口。
3. 打开 `%LOCALAPPDATA%\WindowsStageManager\logs\manager.log`，可确认 `application_start` 中的实际 `dry_run` 值。

## 托盘参数设置

右键托盘图标并打开 `Settings`。所有可调运行参数均在多级菜单中，菜单标题显示当前值，当前选项带勾选。选择新值后程序会取消当前事务、重建内部窗口管理器、立即保存并继续运行，不需要手动编辑配置文件，也不需要重启托盘程序。

可调项目包括运行模式、首选/最低暴露边缘数、暴露边缘长度与深度、修复目标长度、最小屏上宽度/高度、事件合并窗口、reconciliation 间隔、每批最大移动数、最大求解状态数/时间、最大管理窗口数和连续失败阈值。若设置存在依赖，程序自动保持 `minimum_exposed_edges <= preferred_exposed_edges` 和 `min_exposed_edge_dip <= repair_target_edge_dip`。

参数仍持久化到 `%LOCALAPPDATA%\WindowsStageManager\settings.ini`，该文件用于下次启动恢复和故障诊断，正常操作无需打开。首次运行没有配置文件时使用以下关键默认值：

```ini
enabled=true
dry_run=false
preferred_exposed_edges=2
minimum_exposed_edges=1
max_managed_windows=20
max_consecutive_failures=3
```

程序开箱即可工作且不改变窗口尺寸。普通窗口发生“非活动 -> 活动”切换时，新活动窗口的视觉框会一次性移动到当前显示器工作区中央；同一窗口已经活动后，重复前台事件不会再次居中，用户手动拖动或调整后的最终位置也不会被居中覆盖。默认先尝试让每个受影响窗口保留两个独立的合格边缘，只有该目标无解时才降级为一个边缘。满足硬约束的修复候选优先选择窗口中心距工作区中心最近的位置，不再固定偏向左上、右下或其他边缘组合。

协调器会先完成所有纯位置求解；只有两边缘和单边缘的位置方案都明确无解时，才启用 Z-order fallback。fallback 采用 top-prefix（上层前缀保护）优先、bottom-first（同边界内最底层目标优先）的策略：先只允许最深层的局部顺序变化，若无解才逐层向上扩大可变化区间；在同一个插入边界内，从最底层目标窗口开始尝试。目标只会提升到当前求解所需的最深安全层级，不再默认提升到活动窗口正下方。深层局部重排能形成完整合法布局时，不会触及更上层窗口；活动窗口不会被重排，也不会被候选窗口越过。日志中的 `activation_centering_used=true` 表示本批实际采用了激活居中，`required_exposed_edges` 是本批实际采用的边缘目标，`edge_goal_degraded=true` 表示边缘数降级，`z_order_fallback_used=true` 表示使用了该回退；`planned_reorders` 和 `applied_reorders` 分别记录计划和已验证的重排数。

拖动结束和前台窗口切换都会触发布局检查。非活动窗口通过普通点击、任务栏或键盘切换成为前台时会居中；如果该次激活同时进入拖动事务，则以用户拖动后的最终位置为准，不执行居中。若希望先观察计划或排查问题，从 `Settings > Run mode` 选择 `Preview only (DryRun)`；DryRun 会生成移动和重排计划，但不会调用 Win32 修改窗口。恢复实际管理时选择 `Apply window changes`。

## 日常操作与故障处理

- 托盘菜单可暂停/恢复管理、修改参数或退出程序。
- `Ctrl+Alt+F12` 会立即停用自动管理并取消当前移动事务。
- 连续 API 或快照故障达到阈值后，程序会自动停用；检查日志、从托盘切换到 DryRun 后再重新启用。
- 显示器、DPI、工作区变化以及队列溢出会触发全量快照重建。
- `Unsatisfiable` 表示激活居中、位置修复与安全 Z-order fallback 的组合没有完整满足硬约束的方案；程序不会应用部分计划、强行越界或让其他窗口跨越活动窗口。

## 回滚

发布目录中的 `current.json` 指向当前 ZIP，`rollback.json` 指向上一个已提升 ZIP。回滚时：退出程序，备份日志和配置，解压 `rollback.json` 指定的版本到新的目录；启动后立即从托盘切换到 DryRun 验证。不要覆盖正在运行的可执行文件。
