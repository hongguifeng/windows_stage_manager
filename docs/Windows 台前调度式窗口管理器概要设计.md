# Windows 11 台前调度式窗口管理器概要设计

文档版本：1.6
对应需求：`Windows 台前调度式窗口管理器需求说明.md`
设计状态：当前实现基线

## 1. 设计目标

程序是当前交互用户会话中的 Windows 11 托盘进程。它在不改变窗口尺寸、激活状态和 topmost 属性的前提下，为后台普通窗口保留容易辨认和点击的区域。

设计遵循以下原则：

1. 标题栏是首要识别线索，顶部区域优先使用窗口的实际标题栏高度。
2. 后台窗口首选同时露出顶部与一条侧边，并在左上、右上两条通道之间均衡分配。
3. 新活动窗口只在非活动到活动的切换时自动放置，水平居中且底部对齐；用户随后手动移动的位置具有最高优先级。
4. 全部受管理窗口必须在同一个最终快照中合格，不能只修复最上层或第一个违规窗口。
5. 位置方案优先于 Z-order 方案；层级调整从最深、最局部的位置开始，尽量保护最近使用的上层窗口。
6. 求解、应用和验证是原子事务。无完整合法解、超时或平台状态不确定时不应用部分计划。

## 2. 总体架构

```text
WinEvent Hook / 定时 reconciliation
              |
              v
      有界事件队列与合并器
              |
              v
  WindowProvider -> WindowClassifier
              |
              v
       不可变 LayoutSnapshot
              |
              v
 激活放置 -> 位置求解 -> Z-order fallback
              |
              v
 MoveApplier -> 重新捕获 -> 后验验证
              |
              v
      日志、健康状态与托盘 UI
```

### 2.1 线程与所有权

- Hook 线程安装 `SetWinEventHook(WINEVENT_OUTOFCONTEXT)` 并运行消息循环，回调只过滤事件并写入有界队列。
- Coordinator 线程合并事件、捕获快照、执行纯内存求解并驱动应用事务，是运行状态和稳定布局的唯一写入者。
- 托盘与设置窗口运行在 UI 消息线程。设置保存时先停止并 join 协调器，再持久化新快照并热重建运行组件。
- 求解器不直接调用 Win32 API；应用器不自行修改计划，只执行并验证已求得的完整方案。

### 2.2 事件批次

事件合并窗口默认为 12 ms。同一 HWND 的位置变化只保留最新记录，生命周期和前台切换事件优先。队列溢出、钩子恢复、显示器/DPI 变化或周期 reconciliation 都会触发完整重新枚举。

```text
合并事件
  -> 判断前台切换或拖动事务
  -> 捕获完整窗口快照
  -> 可选生成活动窗口放置
  -> 尝试 TopAndSide 位置求解
  -> 必要时尝试 AnyRecognizableEdge 位置求解
  -> 仅在明确 Unsatisfiable 时尝试 Z-order fallback
  -> 原子应用
  -> 重新捕获并验证
```

`Timeout`、`GeometryTooComplex`、`InvalidSnapshot` 和 Win32 查询错误与 `Unsatisfiable` 分离，前三类结果不能触发“位置无解”的层级调整。

## 3. 核心数据模型

### 3.1 坐标与窗口快照

所有几何使用物理屏幕像素和半开矩形 `[left, right) × [top, bottom)`。进程清单声明 Per-Monitor DPI Awareness V2；设置以 DIP 持久化，在目标窗口 DPI 上向上取整为像素。

```text
WindowKey {
    hwnd
    processId
    instanceGeneration
}

WindowSnapshot {
    key, rootHwnd, ownerHwnd, className
    placementRect, visualRect, workArea
    monitor, dpi, titleBarHeight
    style, exStyle, zIndex
    visible, iconic, zoomed, topmost, cloaked
    currentDesktop, managed, zOrderKnown
}
```

`placementRect` 用于 `SetWindowPos`，`visualRect` 优先取 DWM 扩展框并用于可见性判断。两者的偏移在生成移动计划时保持一致。`instanceGeneration` 防止 HWND 销毁后复用造成误操作；`zIndex` 仅对当前完整快照有效。

### 3.2 分边可辨识规则

```text
EdgeAffordanceRule {
    minimumLengthDip
    maximumLengthDip
    depthDip
    lengthPercent
}

VisibilityRequirements {
    top, left, right, bottom: EdgeAffordanceRule
    maximumRegionRectangles
    goal: TopAndSide | AnyRecognizableEdge
}

PixelEdgeAffordance {
    length
    depth
}
```

每条边的像素长度独立计算：

```text
lengthDip = clamp(edgeLengthDip * lengthPercent / 100,
                  minimumLengthDip,
                  maximumLengthDip)
lengthPx  = min(edgeLengthPx, scaleDipCeil(lengthDip, dpi))
depthPx   = min(perpendicularLengthPx, scaleDipCeil(depthDip, dpi))
```

顶部是例外：`titleBarHeight > 0` 时，顶部 `depthPx` 采用实际标题栏高度；只有取不到可靠值时才使用顶部规则的 `depthDip`。

默认平衡规则为：

| 边 | 最小长度 | 最大长度 | 比例 | 深度 |
| --- | ---: | ---: | ---: | ---: |
| 顶部 | 120 DIP | 240 DIP | 25% | 32 DIP 回退值 |
| 左侧 | 120 DIP | 240 DIP | 25% | 40 DIP |
| 右侧 | 160 DIP | 300 DIP | 30% | 64 DIP |
| 底部 | 180 DIP | 360 DIP | 35% | 64 DIP |

### 3.3 求解结果

```text
SolveStatus = Solved | NoViolation | Unsatisfiable |
              InvalidSnapshot | GeometryTooComplex | Timeout

SolveResult {
    status
    moves
    violations
    candidateRejections
    finalSnapshot, finalState
    statesVisited, elapsedMs
}
```

位置计划和可选层级计划共同构成批次结果。任何一个计划超出预算或后验验证失败，整批都失败。

## 4. 窗口发现、分类与标题栏高度

`WindowProvider` 用 `EnumWindows` 获取候选，并在同一次捕获中查询可见性、窗口样式、owner/root、DWM visual frame、显示器工作区、DPI、虚拟桌面和当前 Z-order。

标准标题栏高度按以下顺序求取：

1. 对带 `WS_CAPTION` 的窗口，比较客户区原点与 DWM 视觉框顶部的垂直差。
2. 若差值无效，则使用当前窗口 DPI 对应的系统 frame/caption 指标组合。
3. 无标题栏、自绘标题栏或查询失败时记为 0，由几何层使用顶部回退深度。

分类器默认只管理当前会话、当前虚拟桌面、活动窗口所在显示器上的普通矩形顶级窗口。最小化、最大化、全屏、cloaked、tool/no-activate/topmost、Shell、菜单、提示、通知、owned 窗口和查询不完整窗口被保守排除。未管理但可见且位于目标之上的普通窗口仍可作为遮挡物。

## 5. 可辨识度分析

### 5.1 可见区域

对目标窗口按 Z-order 收集所有更高且 `blocksVisibility=true` 的矩形。几何引擎从目标视觉框与工作区交集开始，依次减去这些遮挡矩形，得到有界矩形 Region；矩形数超过上限返回 `GeometryTooComplex`。

每条边的检测区域由 `PixelEdgeAffordance` 生成。只有一个连续、未遮挡、在工作区内的矩形同时达到该边长度和深度时，该边才合格。候选应用前后的代表点还要通过根窗口命中归属检查。

### 5.2 顶部加侧边

`VisibilityGoal::TopAndSide` 仅在以下任一组合成立时满足：

- `topLeft`：顶部与左侧各自拥有独立合格区域；
- `topRight`：顶部与右侧各自拥有独立合格区域。

检测时将共享角部从顶部带和侧边带中分别限制到各自通道，避免同一块可见角落被重复计算。`VisibilityGoal::AnyRecognizableEdge` 则接受任意一条完整合格边。

单边可辨识顺序为顶部、左侧、右侧、底部；这是人体识别难度的模型，不是强制移动方向。

## 6. 活动窗口放置

只在记录的活动 HWND 发生变化、目标属于受管理普通窗口、同批没有进入拖动事务且 `placeActivatedWindow=true` 时生成一次放置：

```text
visualLeft = workArea.left + (workArea.width - visualRect.width) / 2
visualTop  = workArea.bottom - visualRect.height
```

再用 visual frame 与 placement frame 的偏移换算成 `SetWindowPos` 坐标。若窗口高于工作区，视觉框顶部改为与工作区顶部对齐。活动放置先写入模拟快照，随后与所有后台窗口修复共同求解，并占用批次移动预算。

相同 HWND 的重复前台事件不创建新放置。`MOVESIZESTART` 可抢占自动事务；`MOVESIZEEND` 后以用户最终位置作为固定活动位置重新求解。关闭开关只取消活动窗口放置，不取消后台窗口修复。

## 7. 位置候选与排序

### 7.1 候选生成

对每个违规窗口，候选生成器组合以下偏移：

- 当前坐标；
- 各遮挡窗口四条边外侧，偏移量使用目标窗口对应边的实际深度；
- 工作区边界；
- X/Y 轴偏移的合法笛卡尔组合；
- 显式 `TopLeftChannel`：目标顶部位于遮挡物上方且左侧位于其左方；
- 显式 `TopRightChannel`：目标顶部位于遮挡物上方且右侧位于其右方。

候选去重并受每个违规项的数量上限约束。候选只改变位置，不改变尺寸或 Z-order。

### 7.2 硬约束

候选进入排序前必须全部满足：

1. 目标不是活动窗口，仍可移动且身份有效。
2. placement frame 与 visual frame 使用同一位移，窗口尺寸不变。
3. 屏上宽高不小于配置值，显示器和工作区约束有效。
4. 当前目标达到本轮 `VisibilityGoal`。
5. 所有其他受管理窗口在模拟最终快照中仍达到同一目标。
6. 普通位置轮次不改变 Z-order、topmost、owner 或激活状态。

### 7.3 字典序代价

`CandidateCost` 按以下字段稳定排序：

1. `visibilityPreference`：TopLeft、TopRight、TopOnly、LeftOnly、RightOnly、BottomOnly、Unrecognized；
2. `channelImbalance`：应用候选后的左、右通道数量差；
3. `channelAlternationPenalty`：负载相同时按目标 Z-order 确定性交替；
4. `centerDistance`：横纵偏移分别按工作区宽高归一化后的中心距离；
5. `movedWindowCount`、本次 Manhattan 距离、距稳定位置距离、边界距离和方向切换惩罚；
6. WindowKey 与坐标平局键。

可辨识等级高于中心距离，因此算法不会为了靠近中心而选择更难辨认的单边方案。中心距离归一化避免宽屏的长轴产生不成比例的空白。通道负载只在几何允许时均衡，不覆盖硬约束。

## 8. 完整布局求解

求解器先运行有界全局搜索。每个状态扫描所有受管理窗口的违规项，按 Z-order 修复首个违规项，并将通过硬约束的候选加入待搜索状态；布局哈希防止重复和振荡。

若全局搜索在预算内没有直接完成，增量完整布局求解器按 Z-order 从上到下修复。每次选择候选后都在更新后的完整阻挡关系上重新扫描全部窗口，直到所有窗口合格。它不能把“单个目标已合格”当作成功。

求解顺序为：

1. `TopAndSide` 全局位置搜索；
2. `TopAndSide` 增量完整布局修复；
3. 明确无解后，`AnyRecognizableEdge` 全局位置搜索；
4. `AnyRecognizableEdge` 增量完整布局修复；
5. 两个目标都明确 `Unsatisfiable` 后，才进入 Z-order fallback。

达到状态数、候选数或时间上限返回各自的非无解状态，不应用已生成的局部 moves。

## 9. Z-order fallback

fallback 候选只包含当前布局里的非活动、受管理、非 topmost、非 owned 窗口。活动窗口及其他窗口不能越过 topmost/普通窗口边界。

算法使用两层保护：

1. `top-prefix`：从最长的上层不变前缀开始，只开放最深的插入边界；无解时才逐层缩短受保护前缀。
2. `bottom-first`：在同一开放边界内，从当前最底层候选向上枚举。

候选窗口只提升到当前开放边界的下一层，不直接提升到活动窗口正下方。每一种候选顺序都重新运行完整位置求解并验证全部窗口；首个完整合法解立即返回。这样最深窗口的局部变化可以解决时，最近使用的上层窗口顺序完全不变。

## 10. 应用、验证与故障安全

位置移动通过 `SetWindowPos`/`BeginDeferWindowPos` 执行，并使用不激活、不改变 owner 和不改变尺寸的标志。层级计划逐项使用显式 insert-after 关系。应用前再次检查 WindowKey、monitor、topmost 和事务代数。

每个自动移动登记内部令牌；匹配的 `LOCATIONCHANGE` 只完成令牌，不被解释为用户拖动。任何偏离预期的外部变化会丢弃旧计划并触发重新捕获。

应用后重新获取：

- placement/visual rect 与尺寸；
- 活动窗口、monitor、DPI、topmost 和 Z-order；
- 全部受管理窗口的分边可辨识结果。

任一字段不符即返回验证失败，不接受部分成功，并进入 reconciliation。连续失败达到阈值后熔断暂停；`Ctrl+Alt+F12` 可随时紧急停用。`dry_run=true` 只输出计划，默认配置为 `false`。

## 11. 设置与托盘控制面

`Settings` 直接保存当前生效快照：

- 启用状态、DryRun、`placeActivatedWindow`；
- `affordancePreset`：Compact、Balanced、Prominent、Custom；
- 顶/左/右/底各自的最小长度、最大长度、比例和深度；
- 最小屏上宽高、事件/刷新间隔、移动/状态/时间/窗口/失败上限。

中文托盘提供运行开关、快速参数、可视化设置和退出。连续数值显示常用值并提供“自定义…”。修改任一分边值后预设变为 Custom；最小/最大长度冲突时同边联动归一化。

`SettingsDialog` 维护独立 draft。预览按当前显示器 DPI 绘制四条不同大小的可辨识区域，显示 DIP、像素换算、含义与效果；取消不改变运行状态，保存时一次性校验并持久化。

热更新顺序为：取消活动事务，停止并 join 协调器，校验与保存设置，重建 provider/hook/coordinator，恢复原运行或暂停状态。当前版本只接受新设置模型，不读取或迁移旧配置键。

## 12. 可观测性与测试

批次日志至少记录事务 ID、活动窗口、`activation_placement_used`、`affordance_goal`、`affordance_goal_degraded`、违规窗口、候选拒绝、fallback、位置/层级计划、应用结果、状态数和耗时。日志不记录窗口内容或敏感标题。

自动化测试分为：

- 几何/属性测试：分边 DIP 换算、标题栏高度、角部扣除、Region 上限和随机矩形不变量；
- 求解器测试：左右通道、A/B/C 全窗口修复、宽屏归一化、超时与无解分离；
- Z-order 测试：纯位置优先、top-prefix、bottom-first、不可越过活动/topmost；
- 协调器测试：非活动到活动放置、重复事件、拖动抢占、原子移动预算和降级诊断；
- Win32/托盘集成测试：真实标题栏、设置保存与热重建、DryRun 隔离、应用后重新捕获；
- 文档与发布包测试：默认值、中文操作入口、资源、版本一致性和旧设置模型清除。

每个提交必须包含对应自动化用例，并在 Debug、Release 下分别完成 Configure、Build 和全量 CTest。
