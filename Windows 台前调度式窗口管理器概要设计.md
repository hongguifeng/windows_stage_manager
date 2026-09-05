# Windows 11 台前调度式窗口管理器概要设计

文档版本：1.2
对应需求：`Windows 台前调度式窗口管理器需求说明.md`  
设计目标：先实现可验证、可失败、不会自触发循环的矩形窗口 MVP

## 1. 设计原则

1. 事件回调只采集事件，不在回调中移动窗口或运行求解器。
2. 求解使用一次一致的窗口快照；调用 Windows API 后重新验证实际状态。
3. 交互区是硬约束，移动距离和布局稳定性是软代价。
4. 无解时停止推挤并保留用户控制权，不用“把窗口推出屏幕”伪造成功。
5. MVP 对透明、非矩形、置顶、最大化和系统 UI 采用保守排除。

## 2. 系统架构

```text
Windows WinEvent Hook
        |
        v
HookThread / MessageLoop
        |
        v
Bounded Event Queue -----> Periodic Reconcile Timer
        |
        v
Event Coalescer
        |
        v
Window Snapshot Store <--> Window Classifier
        |
        v
Geometry Engine
        |
        v
Constraint Solver
        |
        v
Move Planner / Applier
        |
        v
Post-move Verification -> Snapshot Store / Diagnostics
```

### 2.1 进程模型

程序作为当前用户交互会话中的托盘进程运行，不使用 Windows 服务控制用户桌面。建议单进程多线程：

- `HookThread`：安装钩子并运行消息循环，只把事件写入有界队列。
- `CoordinatorThread`：合并事件、刷新快照、启动求解、管理事务状态。
- `Solver`：纯内存计算，不直接依赖异步事件。
- `Applier`：执行 `SetWindowPos`、记录内部移动令牌并触发验证。
- `Diagnostics`：记录耗时、失败和无解原因。

快照和事务状态由 `CoordinatorThread` 独占写入。其他线程只提交事件或读取不可变快照，避免在 WinEvent 回调中持有跨线程锁。

## 3. 核心数据模型

### 3.1 坐标和矩形

所有求解矩形使用屏幕物理坐标，采用半开区间 `[left, right) x [top, bottom)`，避免边界像素重复计算。窗口移动 API 使用同一屏幕坐标系。

```text
Rect {
    int left
    int top
    int right
    int bottom
}

Size {
    int width
    int height
}
```

程序清单声明 Per-Monitor DPI Awareness V2。配置值以 DIP 保存，在窗口所属显示器上转换为物理像素。窗口跨显示器时，以活动窗口当前显示器为本批次坐标参考，并暂停跨显示器自动布局。

### 3.2 窗口身份和快照

```text
WindowKey {
    HWND hwnd
    uint32 processId
    uint64 instanceGeneration
}

WindowSnapshot {
    WindowKey key
    HWND rootHwnd
    HWND ownerHwnd
    Rect placementRect       // 用于 SetWindowPos
    Rect visualRect          // 用于可见性近似
    HMONITOR monitor
    uint32 dpi
    uint32 style
    uint32 exStyle
    bool visible
    bool iconic
    bool zoomed
    bool topmost
    bool cloaked
    bool currentDesktop
    bool managed
    int zIndex                 // 只在本次快照有效
    Rect interactionZones[4]
    Rect lastStableRect
    uint64 lastAppliedGeneration
}
```

`instanceGeneration` 在发现新 HWND 或收到销毁事件后递增。任何 API 调用前都必须验证 `IsWindow`、进程 ID 和生成号仍匹配。

### 3.3 事务和求解结果

```text
DragTransaction {
    uint64 id
    WindowKey activeWindow
    Rect startRect
    Rect lastObservedRect
    bool active
    Direction preferredEdge
    Set<WindowKey> movedWindows
    Set<LayoutHash> seenStates
}

MovePlan {
    WindowKey window
    Rect from
    Rect to
    Cost cost
}

SolveResult {
    Status status       // Solved, NoViolation, Unsatisfiable, Stale, Timeout
    List<MovePlan> moves
    List<Violation> violations
    LayoutHash finalState
    Duration elapsed
}
```

## 4. Windows 事件管线

### 4.1 钩子安装

使用 `SetWinEventHook` 的 `WINEVENT_OUTOFCONTEXT` 模式，安装在线程消息循环中。建议分别监听系统移动/调整大小事件和对象生命周期/位置事件，而不是依赖一个过宽的事件范围。

回调只执行以下操作：

1. 检查 `hwnd` 是否有效。
2. 对 `EVENT_OBJECT_*` 要求 `idObject == OBJID_WINDOW` 且 `idChild == CHILDID_SELF`。
3. 将事件类型、HWND、线程 ID、时间戳写入有界队列。
4. 立即返回，不调用 `SetWindowPos`、`EnumWindows` 或求解器。

### 4.2 事件合并

`CoordinatorThread` 以 8 至 16 ms 的合并窗口消费队列：同一 HWND 的多个 `LOCATIONCHANGE` 只保留最新事件，生命周期事件优先。队列溢出、事件顺序异常或钩子重建后执行完整重新枚举。

事件批次处理流程：

```text
consume events
    -> identify active transaction
    -> enumerate/refresh affected windows
    -> build immutable LayoutSnapshot
    -> solve in memory
    -> apply bounded MovePlan
    -> refresh actual rectangles
    -> verify constraints or enter Unsatisfiable
```

### 4.3 防止自触发

每次自动移动使用 `layoutGeneration` 和按 HWND 记录的期望矩形。收到位置事件时：

- 若与当前内部移动令牌匹配，只更新实际矩形，不新建用户事务。
- 若窗口偏离期望矩形，视为应用或用户的外部修改，丢弃旧计划并重新快照。
- `MOVESIZESTART` 优先于旧的内部事件；用户操作可以打断自动布局。

必须设置钩子注销、线程退出和窗口销毁的生命周期，避免回调访问已释放状态。

## 5. 窗口发现和分类

### 5.1 发现流程

使用 `EnumWindows` 获取顶级窗口候选，并在同一快照中读取：

```text
IsWindow
GetWindowThreadProcessId
GetAncestor(GA_ROOT)
GetWindow(GW_OWNER)
GetWindowLongPtr(GWL_STYLE/GWL_EXSTYLE)
IsWindowVisible / IsIconic / IsZoomed
MonitorFromWindow / GetMonitorInfo
GetDpiForWindow
DwmGetWindowAttribute(DWMWA_CLOAKED)
```

Z-order 不保存为跨事件持久化的整数。每次构造快照时，从 `GetTopWindow(nullptr)` 开始，使用 `GetWindow(GW_HWNDNEXT)` 遍历顶级窗口，并同时记录 topmost/普通窗口分界；遍历结果只在当前快照内用于排序。若遍历过程中窗口销毁或顺序变化，放弃本次快照并重新枚举。

当前虚拟桌面判断使用 `IVirtualDesktopManager::IsWindowOnCurrentVirtualDesktop`。如果该能力不可用，MVP 应禁用跨虚拟桌面的自动移动并记录限制。

### 5.2 分类规则

分类器采用“保守允许”：所有排除条件均在管理前检查，无法确定时返回 `Unmanaged`。需要维护可配置的类名/进程黑名单，但黑名单不能替代基础样式和生命周期检查。

拥有者/被拥有窗口、模态对话框和窗口菜单默认作为一个不可拆分组或整体不管理，避免单独移动破坏应用交互。

### 5.3 阻挡窗口

可见性计算的阻挡集合应包含当前桌面上位于目标窗口上方的可见顶级窗口，包括不可移动的未管理窗口。对无法确定透明度的窗口按不透明矩形保守处理。

置顶窗口位于普通窗口之前。不得用一个跨事件持久化的整数表示 Z-order；`zIndex` 只描述本次快照，并在每次求解前重新建立。

## 6. 可见性和几何引擎

### 6.1 可见框

`placementRect` 使用 `GetWindowRect`，用于 API 移动。`visualRect` 优先使用 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)`，用于 MVP 的视觉近似。两者不能混用而不做坐标归一化。

MVP 明确采用“窗口是矩形且不透明”的保守模型。分层窗口、窗口区域和每像素透明度不在本阶段精确计算；分类器可直接排除这类窗口。

### 6.2 交互区

对每个窗口生成四个候选边缘区。以窗口视觉框为例：

```text
leftZone   = left   edge, length L, depth D
rightZone  = right  edge, length L, depth D
topZone    = top    edge, length L, depth D
bottomZone = bottom edge, length L, depth D
```

`L` 和 `D` 为当前 DPI 下的 `RepairTargetEdge` 和 `MinExposedDepth`。若窗口尺寸不足，窗口在分类阶段被排除。

对目标窗口按当前 Z-order 从上到下计算：

```text
exposed(zone) = zone - union(visualRect of higher blockers)
```

只有当 `exposed(zone)` 中存在满足最小长度和深度的矩形，且代表点经 `WindowFromPoint` 归属目标根窗口时，该边才合格。区域可以用非重叠矩形列表实现；列表碎片超过上限时返回 `GeometryTooComplex`，由求解器进入无解/暂停，而不是近似为“可见”。

### 6.3 工作区和边界

使用 `GetMonitorInfo` 的 `rcWork` 作为工作区。候选窗口必须至少保留 `MinOnscreenWidth` 和 `MinOnscreenHeight` 的可见框在工作区内。工作区约束是硬约束，不用一个任意的大代价替代。

## 7. 约束求解器

### 7.1 约束优先级

硬约束按以下顺序判断：

1. 活动窗口位置不被计划修改。
2. 候选窗口仍属于允许的显示器、虚拟桌面和工作区范围。
3. 候选窗口尺寸不变，保留最小屏上区域。
4. 每个受管理窗口优先有两个不同边缘的交互区达到 `RepairTargetEdge`；首选目标无解时允许降级为至少一个。
5. 普通位置候选不改变 Z-order、激活状态和 owner/owned 关系；Z-order 变化只能来自 7.5 节的受控回退。

所有硬约束通过后，软代价按以下顺序比较：

```text
visibilityPreferenceRank
numberOfMovedWindows
totalManhattanDistance
distanceFromLastStableLayout
boundaryPenalty
edgeChangePenalty
stableWindowKeyTieBreak
```

`visibilityPreferenceRank` 的顺序为：左+上、右+下、仅上、无方向偏好。前三档严格优先于移动距离；“仅上”只表示恰好一条合格边且为上边缘，不提升其他含上边缘的组合。

### 7.2 候选生成

候选集合必须包含：

1. 不移动当前位置，用于验证当前布局是否已合法。
2. 目标窗口与每个高层阻挡框的左、右、上、下边界对齐，并预留 `RepairTargetEdge`。
3. 与工作区四条边对齐的合法位置。
4. 四方向和必要的水平/垂直组合位移，用于处理只能斜向脱离的布局。
5. 由所有相关阻挡窗口的边界坐标产生的去重位置。

每个候选先经过工作区、交互区、Z-order 和窗口分类验证；验证失败的候选不进入代价排序。候选生成不应只引用活动窗口 A，也不能假设一次移动就能满足所有窗口。

### 7.3 MVP 求解流程

```text
solve(snapshot, activeWindow, transaction):
    state = snapshot
    for step in 1..MAX_MOVES_PER_BATCH:
        violations = findViolations(state)
        if violations is empty:
            return Solved(state)

        violation = selectDeterministically(violations)
        candidates = generateCandidates(state, violation)
        valid = filterHardConstraints(candidates, state)
        if valid is empty:
            return Unsatisfiable(violation)

        move = minimumCost(valid)
        nextState = simulate(state, move)
        if hash(nextState) in transaction.seenStates:
            return Unsatisfiable(violation)
        transaction.seenStates.add(hash(nextState))
        state = nextState

    return TimeoutOrUnsatisfiable(state)
```

`simulate` 只修改内存快照，不调用 Windows API。求解器不得使用“访问过窗口就永不再处理”的简单 visited 规则；同一窗口在其他窗口移动后可以重新进入违规集合。

### 7.4 滞后和布局稳定

协调器先以 `PreferredExposedEdges=2` 求解；失败后以 `MinimumExposedEdges=1` 重新求解，并记录降级。达到首选数量时不触发修复；只达到最低数量时仍尝试恢复首选数量。一次拖动事务中，已选择的边方向作为软偏好，除非该方向无解或代价明显更高，否则不切换到另一条边。

事务保存 `lastStableLayout` 和 `seenStates`。若新计划使布局质量变差、产生周期或超过时间/移动次数上限，放弃该计划并进入无解/暂停状态。

### 7.5 Z-order 无解回退

协调器必须先完成首选边缘数和最低边缘数的纯位置求解。只有最终结果为 `Unsatisfiable`（不是 `Timeout`、快照无效或几何复杂度超限）时，才调用独立的 Z-order fallback：

1. 从同一快照中选择受管理、可见、当前桌面、非活动且非 topmost 的目标窗口；活动窗口及符合相同安全条件的受管理窗口可以作为插入参考点。
2. 将参考点按 `zIndex` 降序枚举。目标插到参考点之后，因此参考点及其上方构成保持不变的上层前缀；参考点越深，受影响的后缀越小。
3. 对同一参考点，将位于其下方且不是紧邻的目标按 `zIndex` 降序枚举，即最底层目标优先。
4. 每次仅模拟把一个目标提升到参考点下一层；参考点及其上方窗口的 `zIndex` 不变，目标原位置之间的窗口顺延一层。只有所有更深参考点均无解，才允许使用更高参考点，活动窗口是最后边界。
5. 在模拟后的顺序上重新运行完整位置求解；纯重排已经满足约束时允许位置计划为空。
6. 第一个完整合法方案立即返回；候选优先级严格高于移动距离，不得为了更短位置移动破坏更多上层顺序。
7. 所有候选失败时返回无重排、无移动的 `Unsatisfiable`，不得输出部分计划。

`LayoutHash` 同时包含矩形、`zIndex` 和 topmost 状态，使重排后的布局也参与事务周期检测。

## 8. 移动应用和验证

### 8.1 API 调用

普通移动使用 `SetWindowPos` 只修改位置，保留尺寸、激活和 Z-order。推荐使用：

```text
SWP_NOSIZE
SWP_NOACTIVATE
SWP_NOZORDER
SWP_NOOWNERZORDER
```

不应在移动中把窗口提升到 `HWND_TOP` 或改变 topmost 状态。调用线程不得持有窗口快照写锁，也不得阻塞等待事件回调。

Z-order fallback 使用活动窗口 HWND 作为 `hWndInsertAfter`，并使用：

```text
SWP_NOMOVE
SWP_NOSIZE
SWP_NOACTIVATE
SWP_NOOWNERZORDER
```

调用前验证目标和参考窗口的 HWND/进程身份、二者均非 topmost、目标当前位于参考窗口下方，且提升路径不会越过当前前台窗口。调用后立即验证前台窗口未变化且目标仍在其下方。

### 8.2 结果验证

每次移动或重排后重新读取窗口框、样式、显示器、可见性、topmost 和 Z-order。若实际位置与计划偏差超过容差、重排未到达计划层级，或应用在短时间内改回，则：

1. 取消剩余受影响计划。
2. 重新枚举相关窗口。
3. 将该窗口标记为本事务不可协作。
4. 重新求解；仍无解则暂停。

批量应用结束后必须执行一次全量验证，不能只验证刚移动的窗口。

## 9. 状态机

```text
Disabled
   |
   v
Discovering -> Idle <-> Dragging -> Settling -> Idle
                    |                 |
                    +-> Unsatisfiable+
                    |
                    +-> Suspended
```

- `Discovering`：建立窗口快照和分类结果。
- `Idle`：监听事件，不主动移动。
- `Dragging`：跟踪活动窗口并合并位置事件。
- `Settling`：求解、应用、验证当前批次。
- `Unsatisfiable`：保留用户位置，等待拓扑/尺寸变化或用户重试。
- `Suspended`：API/事件/权限异常或用户手动暂停。
- `Disabled`：不安装有效的自动布局处理，直到用户重新启用。

状态转换必须可取消。应用退出、用户停用或钩子注销时，停止新计划并释放钩子。

## 10. 错误和降级

| 场景 | 处理 |
| --- | --- |
| HWND 无效或已销毁 | 丢弃相关计划，刷新快照 |
| HWND 被复用 | 建立新实例，不复用旧事务 |
| SetWindowPos 失败 | 记录错误，停止该窗口重试，重新求解 |
| 应用改回位置 | 标记不可协作，进入无解/暂停 |
| 未管理置顶窗口覆盖工作区 | 作为阻挡窗口计算；无合法布局则无解 |
| 活动窗口最大化/全屏 | 暂停自动推挤，等待恢复 |
| 事件队列溢出 | 完整 EnumWindows 重建快照 |
| 区域碎片超过上限 | 返回 GeometryTooComplex，暂停本批次 |
| 求解超时或周期 | 放弃计划，保留当前布局并记录 |

## 11. 性能和可观测性

事件合并后只重算受影响显示器和 Z-order 区段；候选计算使用矩形边界索引，避免对所有窗口重复做完整区域减法。区域碎片、候选数量、状态数量、每批次耗时和 API 失败次数必须有计数器。

性能目标以基准测试验证，不把 `N^2` 矩形相交次数直接等同于端到端耗时。超过窗口数量或区域复杂度上限时，系统应优先暂停而不是降低约束精度。

## 12. 测试设计

### 12.1 几何单元测试

- 单个矩形完全覆盖、部分覆盖、无覆盖。
- 多阻挡矩形的 union/subtract 和碎片上限。
- 四条边、角落、窗口尺寸小于阈值、工作区边界。
- 候选硬约束过滤和确定性排序。
- 周期状态检测、滞后阈值和 DIP 到物理像素转换。

### 12.2 求解属性测试

随机生成 2 至 20 个矩形窗口，验证求解成功时所有窗口满足约束；验证无解时不会移动活动窗口、不会越过边界、不会产生重复状态。

### 12.3 Windows 集成测试

使用可控制的测试窗口覆盖：快速拖动、调整大小、最大化、Snap、前台切换、窗口销毁/重建、置顶窗口、菜单/提示窗口、不同 DPI 和应用拒绝移动。

### 12.4 验收日志

每个失败用例必须能从日志还原：事件顺序、参与窗口快照、候选拒绝原因、计划移动、实际矩形和最终状态。

## 13. 后续扩展点

1. 多显示器：将单显示器工作区抽象为多个独立求解域，并定义跨显示器迁移代价。
2. 用户配置：白名单、黑名单、阈值、锁定窗口和按进程保存配置。
3. 透明/非矩形窗口：增加窗口区域、DWM 属性和采样命中测试，必要时允许应用提供交互区。
4. 动画：把动画层放在求解器之后，动画期间仍以目标矩形验证约束，并限制事件回流。
5. 全局优化：在 MVP 求解器上增加有界 beam search 或整数/约束优化，不改变硬约束和无解策略。
