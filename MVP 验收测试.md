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

## 手工验收步骤

1. 使用默认 `dry_run=false` 启动程序，打开两个普通非管理员测试窗口，使活动窗口覆盖另一个窗口；确认优先只移动非活动窗口，窗口尺寸和前台窗口不变。
2. 将测试配置改为 `dry_run=true` 并重启，重复上述操作；确认日志有计划但窗口位置不变化。
3. 将活动窗口拖过显示器边界；确认本批次暂停，不继续推动其他窗口。
4. 分别使用最大化窗口、置顶窗口、菜单和工具窗口遮挡；确认这些窗口不会作为移动目标。
5. 移动后立即按 `Ctrl+Alt+F12`；确认管理器进入暂停状态，后续拖动不再产生计划。
6. 修改 `dry_run` 并退出，确认配置被保存；再次启动后应保持用户明确设置的模式。
7. 构造所有位置候选均无解、但交换较深层窗口即可求解的四窗口场景；确认最近使用的上层窗口 `zIndex` 不变，目标没有被直接提升到活动窗口下方，活动窗口仍在其上方，所有窗口位置和尺寸不变，并在日志中看到 `z_order_fallback_used=true`。

真实移动测试只应使用专门创建、可丢弃内容的窗口。发生 API 错误、无解、队列重建或位置回弹时，应先恢复 DryRun，再保存诊断日志。
