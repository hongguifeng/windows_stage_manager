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
| MVP-A14 | 非活动窗口切换为活动时在当前显示器水平居中并底部对齐，重复前台事件不重移 | `stage_manager_mvp_coordinator` |
| MVP-A15 | 激活时直接拖动及已活动窗口后续拖动均保持用户位置 | `stage_manager_mvp_coordinator` |
| MVP-A16 | 激活放置与被遮挡窗口修复、Z-order fallback 和移动上限组成完整计划 | `stage_manager_mvp_coordinator` |
| MVP-A17 | 单一外露窄带不能用两个角满足两边缘目标，完整覆盖修复必须双轴移动 | `stage_manager_candidate`、`stage_manager_mvp_coordinator` |
| MVP-A18 | 放置开关关闭后活动窗口保持原位，但被遮挡窗口仍正常修复 | `stage_manager_core`、`stage_manager_mvp_coordinator`、`stage_manager_tray` |
| MVP-A19 | 中文托盘提供预设和自定义数值，真实对话框输入后落盘、记录日志并热重建 | `stage_manager_tray`、`stage_manager_core`、`stage_manager_tray_settings_integration` |
| MVP-A20 | 四边按独立动态规则换算，顶部优先使用真实标题栏高度 | `stage_manager_candidate`、`stage_manager_window_provider` |
| MVP-A21 | 三个以上后台窗口优先形成左右均衡的顶部加侧边通道 | `stage_manager_layout_solver` |
| MVP-A22 | A/B/C 多层遮挡的最终快照中全部受管理窗口合格 | `stage_manager_layout_solver`、`stage_manager_mvp_coordinator` |
| MVP-A23 | 宽屏与竖屏的中心距离按工作区纵横轴独立归一化 | `stage_manager_candidate_ranker` |

## 手工验收步骤

1. 使用默认 `dry_run=false` 启动程序，打开两个普通非管理员测试窗口，点击后方窗口使其从非活动切换为活动；确认它一次性水平居中并与当前显示器工作区底部对齐，被遮挡窗口按需修复，所有窗口尺寸不变。随后手动拖动当前活动窗口，确认松手后保持在用户放置的位置且不再自动放置。
2. 从托盘“参数设置 > 运行模式”选择“仅预览（DryRun）”，无需重启地重复上述操作；确认日志有计划但窗口位置不变化。随后选择“应用窗口调整”，确认恢复真实管理。
3. 将活动窗口拖过显示器边界；确认本批次暂停，不继续推动其他窗口。
4. 分别使用最大化窗口、置顶窗口、菜单和工具窗口遮挡；确认这些窗口不会作为移动目标。
5. 移动后立即按 `Ctrl+Alt+F12`；确认管理器进入暂停状态，后续拖动不再产生计划。
6. 从中文托盘依次修改运行模式、新激活窗口放置开关、可辨识度预设、四边各自的长度/比例/深度、求解上限和失败阈值，确认当前项勾选且无需重启即可生效；对连续数值和最大管理窗口数选择“自定义…”，分别验证合法输入会保存、越界或非整数输入会显示错误且不会关闭对话框；退出并再次启动后应保持用户明确设置的值。
7. 构造所有位置候选均无解、但交换较深层窗口即可求解的四窗口场景；确认最近使用的上层窗口 `zIndex` 不变，目标没有被直接提升到活动窗口下方，活动窗口仍在其上方，重排窗口的位置和所有窗口尺寸不变，并在日志中看到 `z_order_fallback_used=true`。
8. 重复点击已处于活动状态的窗口，并在活动状态下多次拖动；确认日志中不出现新的 `activation_placement_used=true`，窗口停留在每次用户放置的位置。
9. 关闭“新激活窗口靠下居中”，点击后方窗口使其成为活动窗口；确认该窗口保持原位，而原前台窗口被遮挡时仍会移动。重新开启后再切换到另一窗口，确认仅这次新的激活发生靠下居中。
10. 用一个同尺寸前台窗口完全覆盖后台窗口；确认后台窗口的修复位置同时改变 X 和 Y，从横向和纵向各露出一段扣除角部后仍合格的区域。若只横向露出一条竖带，不得把带状区域的两个角报告为两条边缘。
11. 堆叠一个活动窗口和至少三个后台窗口，依次切换活动窗口；确认所有被覆盖窗口都被处理，左上和右上标题栏通道的窗口数量差不超过 1。只有工作区几何确实限制一侧时才允许偏向另一侧。
12. 打开可视化设置窗口，在平衡预设下比较顶部、左侧、右侧和底部预览；确认顶部使用标题栏高度，右侧和底部展示的可辨识区域明显大于顶部。修改任一分边值后预设应显示为“自定义”。

真实移动测试只应使用专门创建、可丢弃内容的窗口。发生 API 错误、无解、队列重建或位置回弹时，应先恢复 DryRun，再保存诊断日志。
