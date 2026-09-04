# Windows 台前调度式窗口管理器 MVP 验收测试

## 自动化用例

| 编号 | 场景 | 自动化覆盖 |
| --- | --- | --- |
| MVP-A01 | 完全覆盖时生成非活动窗口移动计划 | `stage_manager_mvp_coordinator` |
| MVP-A02 | 默认 DryRun 不调用移动后端 | `stage_manager_mvp_coordinator` |
| MVP-A03 | 显式 Apply 后复核位置、尺寸、Z-order 和激活状态 | `stage_manager_win32_window_mover` |
| MVP-A04 | 管理器内部位置事件不产生递归移动 | `stage_manager_mvp_coordinator` |
| MVP-A05 | 活动窗口跨显示器后暂停 | `stage_manager_mvp_coordinator` |
| MVP-A06 | 最大化窗口不进入自动移动 | `stage_manager_mvp_coordinator` |
| MVP-A07 | 应用拒绝位置时停止重试 | `stage_manager_move_applier`、`stage_manager_win32_window_mover` |
| MVP-A08 | 窗口销毁、身份重建和新事务抢占 | `stage_manager_move_applier`、`stage_manager_mvp_coordinator` |
| MVP-A09 | 快捷禁用路径不捕获或移动窗口 | `stage_manager_mvp_coordinator`、`stage_manager_tray` |

## 手工验收步骤

1. 保持 `dry_run=true` 启动程序，打开两个普通非管理员窗口，使活动窗口覆盖另一个窗口；确认日志只有计划，没有窗口位置变化。
2. 明确将测试配置改为 `dry_run=false`，仅使用无重要内容的测试窗口重复上述操作；确认只移动非活动窗口，窗口尺寸、前台窗口和 Z-order 不变。
3. 将活动窗口拖过显示器边界；确认本批次暂停，不继续推动其他窗口。
4. 分别使用最大化窗口、置顶窗口、菜单和工具窗口遮挡；确认这些窗口不会作为移动目标。
5. 移动后立即按 `Ctrl+Alt+F12`；确认管理器进入暂停状态，后续拖动不再产生计划。
6. 恢复 `dry_run=true` 并退出，确认设置被保存；再次启动后应保持 DryRun。

真实移动测试只应使用专门创建、可丢弃内容的窗口。发生 API 错误、无解、队列重建或位置回弹时，应先恢复 DryRun，再保存诊断日志。
