# Windows 台前调度式窗口管理器 MVP 验收测试

## 自动化用例

| 编号 | 场景 | 自动化覆盖 |
| --- | --- | --- |
| MVP-A01 | 完全覆盖时生成非活动窗口移动计划 | `stage_manager_mvp_coordinator` |
| MVP-A02 | 配置 DryRun 时不调用移动后端 | `stage_manager_mvp_coordinator` |
| MVP-A03 | 显式 Apply 后复核位置、尺寸、Z-order 和激活状态 | `stage_manager_win32_window_mover` |
| MVP-A04 | 管理器内部位置事件不产生递归移动 | `stage_manager_mvp_coordinator` |
| MVP-A05 | 活动窗口跨显示器后暂停 | `stage_manager_mvp_coordinator` |
| MVP-A06 | 最大化窗口不进入自动移动 | `stage_manager_mvp_coordinator` |
| MVP-A07 | 应用拒绝位置时停止重试 | `stage_manager_move_applier`、`stage_manager_win32_window_mover` |
| MVP-A08 | 窗口销毁、身份重建和新事务抢占 | `stage_manager_move_applier`、`stage_manager_mvp_coordinator` |
| MVP-A09 | 快捷禁用路径不捕获或移动窗口 | `stage_manager_mvp_coordinator`、`stage_manager_tray` |
| MVP-A10 | 位置无解后按底层优先生成安全 Z-order fallback | `stage_manager_z_order_solver`、`stage_manager_mvp_coordinator` |
| MVP-A11 | 重排后复核焦点、位置、尺寸、topmost 和最终 Z-order | `stage_manager_move_applier`、`stage_manager_win32_window_mover` |
| MVP-A12 | 托盘参数菜单命令、当前值勾选和全部字段预设 | `stage_manager_tray`、`stage_manager_core` |
| MVP-A13 | 真实进程热切换 DryRun、落盘、日志、重建后存活和退出 | `stage_manager_tray_settings_integration` |
| MVP-A14 | 非活动窗口切换为活动时按当前显示器工作区居中，重复前台事件不重移 | `stage_manager_mvp_coordinator` |
| MVP-A15 | 激活时直接拖动及已活动窗口后续拖动均保持用户位置 | `stage_manager_mvp_coordinator` |
| MVP-A16 | 激活居中与被遮挡窗口修复、Z-order fallback 和移动上限组成完整计划 | `stage_manager_mvp_coordinator` |

## 手工验收步骤

1. 使用默认 `dry_run=false` 启动程序，打开两个普通非管理员测试窗口，点击后方窗口使其从非活动切换为活动；确认它一次性移动到当前显示器工作区中央，被遮挡窗口按需修复，所有窗口尺寸不变。随后手动拖动当前活动窗口，确认松手后保持在用户放置的位置且不再居中。
2. 从托盘 `Settings > Run mode` 选择 `Preview only (DryRun)`，无需重启地重复上述操作；确认日志有计划但窗口位置不变化。随后选择 `Apply window changes`，确认恢复真实管理。
3. 将活动窗口拖过显示器边界；确认本批次暂停，不继续推动其他窗口。
4. 分别使用最大化窗口、置顶窗口、菜单和工具窗口遮挡；确认这些窗口不会作为移动目标。
5. 移动后立即按 `Ctrl+Alt+F12`；确认管理器进入暂停状态，后续拖动不再产生计划。
6. 从托盘依次修改运行模式、边缘数、边缘长度、求解上限和失败阈值，确认当前项勾选且无需重启即可生效；退出并再次启动后应保持用户明确设置的值。
7. 构造所有位置候选均无解、但交换较深层窗口即可求解的四窗口场景；确认最近使用的上层窗口 `zIndex` 不变，目标没有被直接提升到活动窗口下方，活动窗口仍在其上方，重排窗口的位置和所有窗口尺寸不变，并在日志中看到 `z_order_fallback_used=true`。
8. 重复点击已处于活动状态的窗口，并在活动状态下多次拖动；确认日志中不出现新的 `activation_centering_used=true`，窗口停留在每次用户放置的位置。

真实移动测试只应使用专门创建、可丢弃内容的窗口。发生 API 错误、无解、队列重建或位置回弹时，应先恢复 DryRun，再保存诊断日志。
