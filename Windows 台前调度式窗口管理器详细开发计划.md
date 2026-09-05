# Windows 11 台前调度式窗口管理器详细开发计划

文档版本：1.6
依据：需求说明 1.6、概要设计 1.6
当前目标版本：0.1.0-rc15

## 1. 交付目标与工作规则

交付物是一个默认实际生效的 Windows 11 托盘程序：新激活窗口可靠下居中，后台普通窗口优先在左上/右上露出标题栏与侧边，纯位置无解时才进行最深局部 Z-order 调整。

开发遵循以下强制规则：

1. 每个功能点使用独立 Git 提交，不混入用户文件或无关改动。
2. 每个功能提交同时加入或修改对应自动化测试和明确用例。
3. 每次提交前在 Debug、Release 下分别完成 Configure、Build 和全量 CTest。
4. 默认 `dry_run=false`；DryRun 仅作为托盘中的诊断选项。
5. 任何失败路径不应用部分窗口移动或层级计划。
6. 旧设置模型和旧求解规则直接删除，不维护读取、保存或迁移兼容层。

## 2. 技术基线

- 语言：C++20。
- 构建：CMake Presets + Ninja + Visual Studio 2022 Build Tools。
- 平台：Windows 11，Per-Monitor DPI Awareness V2，当前交互用户进程。
- UI：原生 Win32 隐藏消息窗口、托盘菜单、模态设置对话框和 owner-draw 预览。
- 事件：`SetWinEventHook` 的 out-of-context 回调、有界队列、合并和周期 reconciliation。
- 几何：物理像素半开矩形；配置以 DIP 保存，按目标窗口 DPI 转换。
- 测试：C++ 单元/属性/集成测试，PowerShell 真实进程、文档和发布包测试。
- 支持范围：同一显示器上 1–20 个普通非 topmost 顶级窗口。

默认平衡配置：

| 边 | 最小长度 | 最大长度 | 动态比例 | 深度 |
| --- | ---: | ---: | ---: | ---: |
| 顶部 | 120 DIP | 240 DIP | 25% | 32 DIP 回退值 |
| 左侧 | 120 DIP | 240 DIP | 25% | 40 DIP |
| 右侧 | 160 DIP | 300 DIP | 30% | 64 DIP |
| 底部 | 180 DIP | 360 DIP | 35% | 64 DIP |

其他默认值：事件合并 12 ms、reconciliation 250 ms、每批最多 32 次移动、512 个求解状态、16 ms 求解时间、20 个受管理窗口、连续失败阈值 3、最小屏上宽高 100 DIP。

## 3. 模块与代码边界

```text
src/
  app/
    app_lifecycle.*           托盘进程、热重建和诊断
    settings.*                新设置模型、预设和持久化
    settings_menu.*           强类型托盘命令与依赖归一化
    settings_dialog.*         中文可视化设置界面
  events/
    event_queue.*             有界队列
    event_coalescer.*         批次合并和 reconciliation
  geometry/
    rect.* / region.*         半开矩形与区域运算
    interaction_zones.*       分边检测矩形和命中采样
  solver/
    visibility_analyzer.*     动态可辨识区域与目标判定
    candidate_generator.*     四边及左上/右上通道候选
    candidate_ranker.*        硬约束与稳定字典序
    layout_solver.*           全局和增量完整布局求解
    z_order_solver.*          top-prefix/bottom-first fallback
  window/
    window_snapshot.*         Win32 快照值对象
    window_classifier.*       保守管理范围
    mvp_coordinator.*         事务、放置、降级和原子应用
    move_applier.*            位置与层级执行、后验验证
  platform/win32/
    window_provider.*         枚举、DWM、DPI、标题栏和 Z-order
    win_event_hook.*          Hook 线程与消息循环
    tray_controller.*         中文托盘资源和命令分发
tests/
  *_tests.cpp                 单元、属性和组件集成测试
  tray_settings_integration_tests.ps1
  documentation_tests.ps1
  release_bundle_tests.ps1
```

依赖方向固定为：`geometry/solver` 不依赖 Win32；`window` 通过接口组合 provider、solver 和 applier；`app/platform` 负责具体 Win32 生命周期。纯求解测试不得创建真实桌面窗口。

## 4. 已完成基础里程碑

### M0–M3：进程、事件与窗口快照

- 建立 CMake/C++20、Debug/Release、应用清单、日志和版本基线。
- 实现单实例托盘进程、暂停/恢复、紧急停用和配置持久化。
- 实现 WinEvent Hook、有界事件队列、事件合并、内部移动令牌和周期 reconciliation。
- 实现窗口枚举、WindowKey 代数、DWM visual frame、DPI、工作区、虚拟桌面和瞬时 Z-order。
- 测试覆盖 HWND 复用、事件风暴、对象过滤、队列溢出和不完整快照。

### M4–M9：几何、求解、真实应用与发布基线

- 实现 Rect、Region、DIP 换算、可见区域减法和命中归属。
- 实现候选生成、硬约束、确定性排序、有界状态搜索和布局哈希。
- 实现不激活目标的真实移动、DryRun 隔离、后验捕获、失败熔断和恢复。
- 完成 2–20 窗口链式布局、随机属性、长时稳定和候选发布包。
- 测试覆盖尺寸不变、活动窗口保护、越界拒绝、应用拒绝和原子失败。

### M10：受控 Z-order fallback

- 位置方案明确无解后才枚举层级候选。
- 使用 top-prefix 保护最长上层前缀，在同一开放边界内 bottom-first 枚举。
- 目标只提升到所需局部边界，不默认到活动窗口正下方。
- 每个新顺序重新运行完整位置求解，并验证活动窗口和 topmost 不变量。

### M11：托盘参数控制面

- 为全部设置建立强类型字段、常用值、自定义值和命令编码。
- 设置改变后停止协调器、原子保存并热重建，无需手工编辑配置或重启程序。
- 真实进程测试验证托盘命令、日志、配置落盘、重建存活和退出。

### M12–M15：布局体验与设置可视化

- 候选中心距离按工作区宽高归一化，降低宽屏长轴偏差。
- 激活放置只发生在新的前台切换，拖动抢占并保留用户最终位置。
- 独立计算相邻边，扣除共享角部；默认目标必须是真正的两边组合。
- 托盘和对话框中文化，连续数值支持自定义输入，设置预览展示 DIP 效果。
- 增量 fallback 每步重新扫描完整布局，修复 A/B/C 多层遮挡而非只处理最上层。

## 5. M16：可辨识度算法整体替换

目标：按“容易辨认和选择窗口”重建布局模型，删除统一边缘参数和旧的方向/放置规则。

### M16-01 分边动态可辨识区域

实现内容：

1. 新增 `EdgeAffordanceRule` 与 `PixelEdgeAffordance`，四边独立保存最小长度、最大长度、动态比例和深度。
2. 实际长度按窗口边长比例计算后 clamp，并按目标 DPI 转换。
3. WindowProvider 捕获标准窗口实际 `titleBarHeight`；顶部优先使用该值。
4. `VisibilityGoal` 固定为 `TopAndSide` 与 `AnyRecognizableEdge` 两级。
5. 共享角部不能同时满足顶部和侧边，单边顺序固定为顶部、左侧、右侧、底部。

自动化用例：

- 100%、150%、200% DPI 的四边独立换算；
- 小窗/大窗动态长度上下限；
- 标准标题栏与无边框回退；
- 单一角部失败、独立顶部加侧边成功；
- 右/底默认区域大于顶部。

完成提交：`15e7429 solver: model edge-specific window affordances`。

### M16-02 新活动窗口靠下居中

实现内容：

1. 对非活动到活动的窗口水平居中并底部对齐当前 `rcWork`。
2. 根据 visual frame 与 placement frame 差值生成实际坐标。
3. 超高窗口顶部对齐，保证标题栏仍在工作区内。
4. 重复前台事件、当前已活动窗口的移动和拖动事务不生成自动放置。
5. 放置与后台修复共用完整计划和移动预算。

自动化用例：正常窗口、超高窗口、不同工作区原点、visual/placement 偏移、重复激活、拖动抢占、开关关闭、移动预算和 DryRun。

完成提交：`45db15e layout: bottom-align newly activated windows`。

### M16-03 左上/右上通道均衡

实现内容：

1. 候选生成器显式产生 `TopLeftChannel` 与 `TopRightChannel` 双轴候选。
2. 排序时先比较可辨识等级，再比较左右通道负载差。
3. 同负载时按窗口 Z-order 确定性交替，避免全部窗口聚集到同一侧。
4. 中心距离仍参与软排序，但不能压过顶部加侧边。

自动化用例：完全覆盖的左右双通道候选、A/B/C 堆叠、三至多个后台窗口通道数量差、单侧受工作区限制时的硬约束优先。

完成提交：`f94b87d solver: balance top-left and top-right channels`。

### M16-04 新设置模型与中文界面

实现内容：

1. 删除旧设置字段、配置解析、菜单入口、诊断名和兼容读取。
2. 增加 Compact、Balanced、Prominent、Custom 四种可辨识预设。
3. 中文托盘和可视化设置界面直接编辑四边独立规则。
4. 任一分边规则被修改后标记 Custom；同边最小/最大冲突自动归一化。
5. 示例配置、发布脚本、用户指南和诊断字段切换到新模型。

自动化用例：默认平衡值、三个预设、旧键忽略、新键 round-trip、菜单中文与自定义值、设置对话框预览和真实进程热重建。

完成提交：`835d319 ui: replace legacy edge settings with affordance presets`。

### M16-05 设计文档替换与回归约束

实现内容：

1. 需求说明和概要设计只描述新算法。
2. 详细计划、TODO、用户指南和验收场景与当前字段及诊断名一致。
3. 文档自动化测试要求新结构、默认值、通道、放置和 fallback 术语存在。
4. 文档自动化测试拒绝旧配置键和旧诊断字段重新出现。

自动化用例：运行 `documentation_tests.ps1` 并由 CTest 在 Debug/Release 中执行。

交付门槛：所有文档与代码一致，Debug/Release 全量 CTest 通过后独立提交。

## 6. M17：RC15 发布

### M17-01 版本与发布资料

1. 将应用版本提升到 `0.1.0-rc15`。
2. README 发布命令、TODO 和发布元数据使用同一版本。
3. Debug/Release 重新 Configure、Build 和执行全量 CTest。
4. 独立提交版本变更，不混入算法或文档替换提交。

### M17-02 发布包验证

1. 使用 `scripts/stage_release.ps1` 从 Release 构建生成版本化目录、ZIP、`current.json` 和回滚记录。
2. 解压 ZIP 并只对包内实际 EXE 执行托盘设置集成测试。
3. 验证默认实际应用、中文设置、图标、功能示意图、示例配置和版本文件。
4. 计算 ZIP SHA-256，记录 commit 与测试结果。

发布产物不进入功能提交；若脚本或资源需修复，必须另建带测试的提交并重新执行全流程。

## 7. 测试矩阵

| 编号 | 场景 | 期望结果 | 主要测试 |
| --- | --- | --- | --- |
| T01 | 标准标题栏窗口 | 顶部使用实际标题栏高度 | window_provider / candidate |
| T02 | 无边框窗口 | 顶部使用回退深度 | candidate |
| T03 | 完全覆盖两窗口 | 生成顶部加左或右侧的双轴候选 | candidate / layout_solver |
| T04 | A/B/C 多层遮挡 | 最终全部后台窗口合格 | layout_solver / coordinator |
| T05 | 多窗口双通道 | 左右数量差不超过 1 | layout_solver |
| T06 | 单侧空间不足 | 硬约束优先，可使用另一通道或降级 | layout_solver |
| T07 | 新前台切换 | 活动窗口水平居中、底部对齐一次 | coordinator |
| T08 | 已活动窗口拖动 | 松手后保持用户位置 | coordinator / WinEvent |
| T09 | 放置开关关闭 | 活动窗口不动，后台仍修复 | coordinator / tray |
| T10 | 宽屏与竖屏 | 同屏幕占比偏移具有等价中心代价 | candidate_ranker |
| T11 | TopAndSide 无解 | 完整证明后降级到单边 | coordinator |
| T12 | 纯位置有解 | Z-order 完全不变 | z_order_solver / coordinator |
| T13 | 纯位置明确无解 | 最深局部层级先尝试 | z_order_solver |
| T14 | 求解超时 | 返回 Timeout，不进入层级 fallback | layout_solver / coordinator |
| T15 | 应用期间窗口变化 | 整批验证失败并 reconciliation | move_applier / coordinator |
| T16 | 新设置 round-trip | 仅新键保存，预设和自定义值一致 | core / tray / integration |
| T17 | 发布包首次运行 | `dry_run=false` 且资源完整 | release_bundle |

## 8. 属性不变量

随机或组合测试必须持续验证：

1. 任何成功结果都不改变窗口宽高。
2. 活动窗口不被后台位置求解器移动，也不参与 Z-order 重排。
3. 所有受管理窗口在同一最终快照中达到本轮可辨识目标。
4. 顶部加侧边必须由两块独立区域满足。
5. 原本合法的更上层窗口不会因候选应用而变成违规。
6. 纯位置成功时 Z-order 与初始快照完全一致。
7. fallback 不跨越活动窗口、topmost 边界或受保护上层前缀。
8. 相同输入、设置和预算生成相同结果。
9. Timeout、复杂度超限和无解互不混淆。
10. 应用后实际状态与模拟最终快照一致，否则整批失败。

## 9. 提交前验证流程

每个提交依次执行：

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure

cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
```

随后执行 `git diff --check`，确认用户未跟踪文件和无关工作区改动未被加入暂存区，再创建独立提交。若任一配置、构建或测试失败，不得提交。

## 10. 发布验收标准

- 默认首次启动会实际管理窗口，不要求编辑配置文件。
- 中文托盘可切换运行状态、放置开关、预设和全部连续参数。
- 可视化设置界面能解释四边参数、DIP 换算并预览实际差异。
- 新活动窗口只在状态转换时靠下居中；用户移动不被抢回。
- 后台窗口优先使用均衡的左上/右上标题栏通道，必要时才降级。
- 多层遮挡时全部受管理窗口共同合格。
- Z-order 仅在完整位置无解时最深局部调整，最近使用的上层窗口优先保持不变。
- Debug/Release 全量测试、发布包测试与真实进程托盘测试全部通过。
- ZIP 的版本、commit、资源清单和 SHA-256 可追溯。
