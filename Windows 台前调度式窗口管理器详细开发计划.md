# Windows 11 台前调度式窗口管理器详细开发计划

文档版本：1.2
依据文档：`Windows 台前调度式窗口管理器需求说明.md`、`Windows 台前调度式窗口管理器概要设计.md`  
计划状态：可用于拆分任务和评估 MVP 进度

## 1. 计划目标

本计划把需求和概要设计拆成可以独立开发、测试和验收的工作包。开发顺序遵循以下原则：

1. 先建立可观察、可停止的 Windows 后台程序，再接入自动移动。
2. 先在纯内存中验证矩形几何和求解器，再操作真实窗口。
3. 先完成两个窗口的安全闭环，再增加多窗口链式推挤。
4. 所有自动移动都必须可验证、可取消、可诊断。
5. 在没有合法位置布局时先明确完成位置求解；仅按受控、底层优先策略尝试 Z-order 回退，仍无解时不输出部分计划。

最终交付物包括：

- Windows 11 交互用户会话中的托盘后台程序。
- 事件采集、窗口快照、窗口分类、矩形可见性和有界求解器。
- 两窗口 MVP、三至二十窗口稳定求解和无解降级。
- 几何单元测试、求解属性测试和 Windows 集成测试。
- 构建、诊断、性能基准和发布文档。

本计划不包含多显示器协同、动画和透明窗口的精确像素级可见性；这些属于后续版本。

## 2. 统一技术基线

### 2.1 实现技术

| 项目 | 选择 | 说明 |
| --- | --- | --- |
| 语言 | C++20 | 便于直接使用 Win32/DWM API，并保持几何核心为纯 C++ |
| 构建 | CMake 3.28+ | 使用 Presets 统一 Debug/Release 和测试配置 |
| 编译器 | Visual Studio 2022 MSVC | Windows 11 SDK，默认 x64 |
| 平台 API | User32、DWM、Shell/COM | `SetWinEventHook`、`EnumWindows`、`SetWindowPos` 等 |
| 测试 | GoogleTest + 自建 Win32 测试窗口 | 几何/求解器使用单元和属性测试，平台行为使用集成测试 |
| 日志 | `spdlog` 或等价结构化日志接口 | 统一批次 ID、事务 ID 和错误码，不记录窗口内容 |
| 静态检查 | `/W4`、`/permissive-`、警告视为错误、clang-format | CI 中执行编译和静态检查 |
| 打包 | MSIX 或轻量安装包，优先提供可解压便携版 | MVP 先保证可重复构建和干净卸载 |

第三方库应通过 `vcpkg.json` 或明确版本的 CMake FetchContent 管理。几何核心不得依赖 Windows 消息循环，便于在非 Windows 逻辑测试中运行。

### 2.2 MVP 固定边界

开始编码前将以下决策写入配置和测试夹具，避免实现期间反复改变范围：

- 每个交互用户会话运行一个托盘进程，不做 Windows 服务。
- 只管理当前虚拟桌面、活动显示器上的普通顶级矩形窗口。
- 最小化、最大化、无边框全屏、置顶、系统 UI、菜单、提示和分类不确定的窗口不管理，但仍可作为保守阻挡窗口。
- 所有阈值以 DIP 保存，计算时转换为目标显示器物理像素。
- 活动窗口位置默认是硬约束；不合法布局返回 `Unsatisfiable`。
- 管理器不主动改变尺寸、激活状态和 owner/owned 关系；普通求解不改变 Z-order，只有位置无解后的受控 fallback 可以提升一个非活动窗口，并以保持最长上层前缀为首要目标。
- 可见性采用“不透明矩形 + 边缘交互区”模型，透明/非矩形窗口暂时排除。

### 2.3 默认参数

```text
MinExposedEdge      = 48 DIP
MinExposedDepth     = 24 DIP
PreferredExposedEdges = 2
MinimumExposedEdges = 1
RepairTargetEdge    = 64 DIP
MinOnscreenWidth    = 100 DIP
MinOnscreenHeight   = 100 DIP
EventCoalesceWindow = 8..16 ms
ReconcileInterval   = 250 ms（初始值，可通过基准测试调整）
MaxMovesPerBatch    = 32
MaxSolverStates     = 512
MaxSolveTime        = 16 ms（超时返回 Timeout）
```

参数必须集中在配置结构中，禁止在事件回调、几何函数和求解器中写死常量。

## 3. 代码结构和模块边界

建议目录结构如下：

```text
/
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  cmake/
  src/
    app/
      main.cpp
      app_lifecycle.*
      tray_controller.*
      settings.*
    platform/win32/
      win_event_hook.*
      win_window_provider.*
      win_desktop_provider.*
      win_dpi.*
      win_move_applier.*
      win_message_window.*
    window/
      window_key.*
      window_snapshot.*
      window_classifier.*
      z_order_snapshot.*
      event_queue.*
      event_coalescer.*
    geometry/
      rect.*
      region.*
      interaction_zone.*
      visibility_engine.*
    solver/
      layout_snapshot.*
      violation.*
      candidate_generator.*
      cost_model.*
      constraint_solver.*
      layout_hash.*
    diagnostics/
      logger.*
      metrics.*
      trace_context.*
  tests/
    unit/
      geometry/
      solver/
      window_model/
    integration/
      win_event_tests/
      window_movement_tests/
      failure_mode_tests/
    fixtures/
      test_window/
  tools/
    scenario_runner/
  docs/
```

平台模块只负责 Windows API 适配；`geometry` 和 `solver` 不直接包含 `HWND`、窗口消息或全局状态。

建议建立以下最小接口，方便替换真实平台依赖：

```text
IEventSource       -> 事件钩子或测试事件源
IWindowProvider    -> 枚举、读取窗口和 Z-order
IDesktopProvider   -> 工作区、DPI、虚拟桌面
IGeometryEngine    -> 区域减法、交互区和命中验证
ILayoutSolver      -> 从 LayoutSnapshot 产生 SolveResult
IMoveApplier       -> SetWindowPos 和结果验证
IClock             -> 合并窗口和超时测试
IDiagnostics       -> 日志和指标
```

不要求每个类都抽象化；纯计算模块优先使用值对象，平台边界才引入接口。

## 4. 总体依赖和里程碑

```text
M0 工程基线
 |\
 | M1 托盘进程与生命周期
 | M2 WinEvent 事件管线
 | M3 窗口发现、分类、快照
  \ /
   M4 几何和可见性引擎
        |
        M5 纯内存候选求解器
        |
        M6 SetWindowPos 应用与验证
        |
        M7 两窗口 MVP 集成
        |
        M8 多窗口链式推挤和稳定性
        |
        M9 故障、安全、性能和发布
```

粗略工作量为 75 至 90 人日，包含测试和集成缓冲，不包含正式签名、视觉设计和多显示器 V2。单人全职开发约 15 至 18 周；两人可以让平台管线与几何核心部分并行，预计 9 至 12 周，最后仍需保留至少两周的集成回归。

## 5. 详细工作包

### M0：工程基线和设计冻结

预计：3 人日  
依赖：无  
交付门槛：所有后续任务可以在同一构建命令下运行

任务：

1. 创建 CMake 工程、Presets、x64 Debug/Release 配置和测试目标。
2. 配置 Windows 应用清单，声明 Per-Monitor DPI Awareness V2。
3. 配置 MSVC 警告、代码格式、基础静态检查和崩溃转储策略。
4. 建立 `src`、`tests`、`tools` 和 `docs` 目录边界。
5. 加入版本信息、构建号和最小配置文件解析。
6. 确定日志级别、错误码命名、时间戳和批次/事务 ID 格式。
7. 将默认阈值、上限和 MVP 排除规则写成单一配置结构。

产物：

- 可重复的 `cmake --preset windows-debug` 和 `cmake --build`。
- 空壳程序能够启动、退出，测试 runner 能执行一个通过的 smoke test。
- `CONTRIBUTING.md` 或等价开发约定，明确线程、所有权和错误处理规则。

退出条件：

- Debug/Release 均能构建。
- 未初始化的窗口句柄、跨线程 API 和未处理错误在代码审查清单中有明确规则。

### M1：托盘进程、生命周期和安全开关

预计：5 人日  
依赖：M0  
交付门槛：程序能安全驻留、暂停和退出，但尚不移动窗口

任务：

1. 实现 `WinMain`、单实例互斥体和隐藏消息窗口。
2. 实现托盘图标、启用/暂停菜单、当前状态显示和紧急停用入口。
3. 使用 `RegisterHotKey` 提供开发期和发布期的停用快捷键。
4. 实现 `Enabled / Suspended / Disabled` 状态持久化和启动默认值。
5. 处理退出、会话注销、系统关机和窗口类注销。
6. 处理 `WM_DISPLAYCHANGE`、`WM_SETTINGCHANGE`、DPI/工作区变化，触发重新枚举。
7. 为开发阶段增加 `DryRun` 模式：记录计划移动但不调用 `SetWindowPos`。

产物：

- 托盘可执行程序和状态机单元测试。
- 开发期可以一键暂停所有自动移动。

退出条件：

- 程序异常退出或手动停用后，不再安装有效自动移动处理。
- 重复启动不会产生两个互相竞争的实例。

### M2：WinEvent 事件管线

预计：7 人日  
依赖：M0、M1  
交付门槛：能可靠采集事件，不运行求解器

任务：

1. 在 `HookThread` 安装 `WINEVENT_OUTOFCONTEXT` 钩子。
2. 分开配置 `EVENT_SYSTEM_MOVESIZESTART/END`、`EVENT_SYSTEM_FOREGROUND` 和 `EVENT_OBJECT_LOCATIONCHANGE/SHOW/HIDE/DESTROY`。
3. 对对象事件强制检查 `idObject == OBJID_WINDOW`、`idChild == CHILDID_SELF`，并过滤无效或非根窗口句柄。
4. 设计有界事件队列，记录事件类型、HWND、线程 ID、时间戳和来源。
5. 实现 HookThread 消息循环、注销、线程退出和队列关闭协议。
6. 实现 8 至 16 ms 事件合并：同一 HWND 的位置变化保留最新值，生命周期事件优先。
7. 处理队列溢出、钩子失效和事件顺序异常，统一转为完整重新枚举请求。
8. 增加 250 ms 周期 reconciliation 定时器，修复漏事件和外部移动。
9. 让回调保持非阻塞，禁止在回调中调用 `EnumWindows`、`SetWindowPos` 或求解器。

单元测试：

- 事件队列满、关闭、重复事件和优先级。
- 合并窗口的时间边界和生命周期事件覆盖位置事件。
- 内部移动事件与用户 `MOVESIZESTART` 的优先级。

集成测试：

- 移动测试窗口，确认收到 start、location、end。
- 让子控件频繁变化，确认不会被当作顶级窗口事件。
- 注销和重新安装钩子，确认没有访问已释放对象。

退出条件：

- 事件采集线程可独立运行 30 分钟无队列增长或未处理异常。
- 日志能按批次还原事件顺序。

### M3：窗口发现、身份、分类和快照

预计：8 人日  
依赖：M0、M2  
交付门槛：能生成一致的当前窗口快照

任务：

1. 实现 `IWindowProvider`，用 `EnumWindows` 获取顶级候选。
2. 读取 `GetWindowThreadProcessId`、`GetAncestor(GA_ROOT)`、`GetWindow(GW_OWNER)`、样式和扩展样式。
3. 读取 `IsWindowVisible`、`IsIconic`、`IsZoomed`、DWM cloaked 状态和进程会话信息。
4. 读取 `GetWindowRect`、`DWMWA_EXTENDED_FRAME_BOUNDS`、`MonitorFromWindow`、`GetMonitorInfo` 和 `GetDpiForWindow`。
5. 使用 `GetTopWindow(nullptr)` 与 `GetWindow(GW_HWNDNEXT)` 建立本次快照的 Z-order，不保存跨事件的稳定整数。
6. 初始化 `IVirtualDesktopManager`，判断窗口是否在当前虚拟桌面。
7. 实现 `WindowKey(hwnd, processId, instanceGeneration)` 和销毁/复用识别。
8. 实现保守分类器：根顶级、可见、非最小化、非最大化、非置顶、非工具窗口、非 cloaked、尺寸足够且不是系统 UI。
9. 实现 owner/owned 和模态窗口的整体排除或不可拆分标记。
10. 将分类不确定、API 查询失败和权限不足统一记为 `Unmanaged`。
11. 增加快照版本号、刷新原因和参与显示器字段。

工具和测试：

- 开发期 `window_inspector` 输出 HWND、进程、owner、样式、visualRect、placementRect、DPI 和 zIndex。
- 测试窗口提供普通、置顶、工具、最小化、最大化、透明和 owner/owned 变体。

退出条件：

- 连续刷新快照时窗口身份不漂移，HWND 复用不会继承旧布局。
- 当前桌面外窗口不会进入活动求解集合。
- `zIndex` 只在快照有效，窗口栈变化会触发新快照。

### M4：矩形、DPI 和可见性几何引擎

预计：10 人日  
依赖：M0、M3  
交付门槛：纯内存几何测试覆盖核心边界

任务：

1. 实现半开区间 `Rect`、大小、平移、包含、相交和距离操作。
2. 实现 DIP 与物理像素转换，并测试 100%、150%、200% DPI。
3. 实现非重叠矩形列表 `Region` 的 union、subtract、裁剪和碎片上限。
4. 统一 placementRect、visualRect、workArea 的坐标系和边界约定。
5. 实现四个边缘交互区生成，使用 `RepairTargetEdge` 和 `MinExposedDepth`。
6. 实现高层阻挡窗口集合和 `exposed(zone)` 计算。
7. 实现“未遮挡区域包含最小矩形”的判断，而不是只比较总面积。
8. 实现命中采样：`WindowFromPoint` 返回值经 `GetAncestor(GA_ROOT)` 后与目标根窗口比较。
9. 处理视觉框与放置框差异，避免 DWM 扩展边界和 `GetWindowRect` 混用。
10. 区域碎片超过上限时返回 `GeometryTooComplex`，不降级为可见。
11. 对小于阈值的窗口返回不可管理原因。

单元测试：

- 完全覆盖、部分覆盖、相邻边界、零面积交集和负坐标。
- 多窗口 union/subtract 的碎片化、重复矩形和包含关系。
- 四边交互区、角落、工作区裁剪和最小尺寸。
- 命中子窗口后归属根窗口的逻辑。

属性测试：

- 随机生成矩形集合，验证 subtract 后区域不与阻挡集合相交。
- 验证区域面积、边界和矩形列表之间的一致性。
- 验证相同输入产生相同的标准化 Region。

退出条件：

- 几何模块不依赖 HWND、消息循环或全局可变状态。
- 所有默认阈值只通过配置注入。
- 关键几何测试在 Debug/Release 均通过。

### M5：纯内存候选生成和约束求解器

预计：12 人日  
依赖：M4  
交付门槛：不调用 Windows API 即可得到确定性布局计划

任务：

1. 定义不可变 `LayoutSnapshot`、`Violation`、`MovePlan` 和 `SolveResult`。
2. 实现所有受管理窗口的违规扫描，活动窗口只作为固定约束参与计算。
3. 实现候选生成：当前位置、所有相关阻挡框边界、工作区边界、四方向和必要的水平/垂直组合位移。
4. 候选生成后去重，并明确记录来源和方向。
5. 实现硬约束过滤：活动窗口不动、尺寸不变、工作区内、交互区达标、显示器/虚拟桌面合法；普通位置候选保持 Z-order。
6. 实现软代价：移动窗口数量、总 Manhattan 距离、距稳定布局距离、边界偏离、边方向切换和平局键。
7. 实现内存 `simulate`，禁止候选评估调用 `SetWindowPos`。
8. 实现 `LayoutHash`，记录事务内已见布局，检测周期。
9. 实现最大移动数、最大状态数和最大耗时；超限返回 `Timeout` 或 `Unsatisfiable`。
10. 确保同一输入产生相同候选顺序和结果。
11. 实现滞后：低于 `MinExposedEdge` 才触发，修复目标为 `RepairTargetEdge`，保留优先边方向。
12. 记录每个候选被拒绝的第一个硬约束原因，便于诊断。

必须覆盖的纯内存场景：

- A 覆盖 B，四方向中存在一个合法最短位置。
- A 覆盖 B，但四个单轴候选都无效，只有组合位移可行。
- A、B、C 形成链式遮挡。
- 所有候选都违反工作区或被不可移动阻挡窗口覆盖。
- 同一窗口因其他窗口移动再次违规，不能被永久 visited 阻止。
- 两个候选代价相同，使用稳定平局规则。

退出条件：

- 求解成功时，模拟布局所有硬约束通过。
- 求解无解时，结果包含违规窗口和候选拒绝原因。
- 求解器不会修改输入快照，不会调用平台 API。

### M6：真实窗口移动、内部事件抑制和验证

预计：9 人日  
依赖：M2、M3、M5  
交付门槛：DryRun 计划可以安全应用到测试窗口

任务：

1. 实现 `IMoveApplier`，使用 `SetWindowPos` 只修改位置。
2. 普通移动使用 `SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER`；受控重排使用 `SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER`。
3. 为每次批次生成 `layoutGeneration`，记录 HWND、期望矩形和事务 ID。
4. 应用计划后立即刷新实际矩形、样式、显示器和可见性。
5. 若窗口销毁、API 失败、实际位置偏差过大或应用改回位置，取消剩余计划并重新快照。
6. 对已匹配内部移动令牌的位置事件只更新状态，不创建新的用户事务。
7. 若用户在应用过程中开始新的 `MOVESIZESTART`，取消旧计划并优先用户操作。
8. 设计计划应用顺序，并在批次结束执行全量验证；验证失败时不继续盲目推挤。
9. 实现单窗口失败计数和“本事务不可协作”标记。
10. 在开发期保留 DryRun、单步应用和恢复上一个稳定位置的调试开关。

集成测试：

- 移动一个测试窗口，验证另一个窗口被计划移动且未激活。
- 让测试窗口在 `WM_WINDOWPOSCHANGING` 中拒绝或改回位置。
- 在应用计划中销毁目标窗口。
- 记录管理器自己的 location change，验证没有递归布局循环。

退出条件：

- 两窗口测试连续运行 30 分钟无自触发循环。
- 所有成功报告的移动都能在后验快照中复核。
- API 失败不会导致无限重试或改变活动窗口。

### M7：两窗口 MVP 闭环

预计：7 人日  
依赖：M1、M2、M3、M4、M5、M6  
交付门槛：可交付的首个可用切片

任务：

1. 将事件协调器、快照、求解器和应用器串成完整批次流水线。
2. 实现 `MOVESIZESTART -> LOCATIONCHANGE 合并 -> 求解 -> 应用 -> 验证 -> MOVESIZEEND` 状态流。
3. 只允许两个普通窗口进入自动管理，其他窗口只作为阻挡集合。
4. 实现活动窗口跨显示器时暂停并重新识别显示器。
5. 增加托盘状态：运行、暂停、无解、API 错误和队列重建。
6. 编写 MVP 手工验收脚本和自动化场景 runner。
7. 默认启用 DryRun 首次运行，用户明确启用后才允许真实移动；发布前确认最终默认策略。

MVP 验收：

- 两个窗口部分重叠和完全覆盖都能得到合规交互区或明确无解。
- 活动窗口保持用户位置和层级，非活动窗口保持尺寸和激活状态；只有位置无解时允许按受控策略改变一个非活动窗口层级。
- 最大化、全屏、置顶、菜单和分类不确定窗口不会被移动。
- 暂停快捷键生效，管理器自身事件不会造成循环。

退出条件：

- 需求文档中的 MVP 验收标准全部有测试编号或手工步骤。
- 发现严重问题时可以一键回到 DryRun/Disabled，不必卸载程序。

### M8：多窗口链式推挤和稳定性

预计：10 人日  
依赖：M7  
交付门槛：3 至 20 个窗口下布局不抖动、不循环

任务：

1. 移除两窗口数量限制，但保留最大窗口数配置。
2. 处理 A -> B -> C -> D 的链式遮挡和多阻挡 union。
3. 根据受影响窗口和 Z-order 区段缩小重算范围，必要时退回全量扫描。
4. 增加事务稳定布局、优先边方向、移动窗口集合和 `seenStates` 维护。
5. 验证候选应用后的所有窗口，而不是只验证刚移动的窗口。
6. 实现周期、状态数、移动次数和耗时上限的统一策略。
7. 对布局变差、代价反复、应用改回位置和几何复杂度超限进入无解/暂停。
8. 加入快速拖动、调整大小、Windows Snap 和前台切换场景。
9. 增加多窗口属性测试和可重复的随机种子记录。

退出条件：

- 连续拖动 10 分钟没有窗口来回抖动。
- 求解成功时所有受管理窗口合规；超限时状态明确为无解/超时。
- 相同快照重放得到相同 MovePlan。

### M9：故障安全、诊断、性能和发布

预计：10 至 12 人日  
依赖：M8  
交付门槛：候选发布版本可以在真实桌面安全运行

任务：

1. 完成事件钩子重建、队列溢出、窗口销毁/复用和权限失败的降级。
2. 增加周期 reconciliation、显示器/工作区/DPI 变化重建和配置重载。
3. 完成结构化日志：批次、事务、快照摘要、候选拒绝、API 错误、耗时和最终状态。
4. 增加性能计数器：窗口数、阻挡数、Region 碎片数、候选数、状态数、应用次数和失败次数。
5. 建立 N=2、5、10、20 的矩形布局基准，分别测量几何、求解、API 应用和端到端耗时。
6. 测试复杂区域超过上限时是否暂停，而不是错误放宽可见性。
7. 完成 Debug/Release、干净机器、无管理员权限和不同 DPI 的安装测试。
8. 完成崩溃恢复、配置损坏、版本升级和卸载清理。
9. 编写用户操作说明、已知限制和故障诊断指南。
10. 形成候选发布包和回滚包，发布前默认关闭实验性动画和全局优化。

退出条件：

- 所有 P0/P1 风险有关闭证据或明确发布阻断记录。
- 真实桌面长时间运行不会持续增加队列、线程或句柄。
- 无解和暂停均能恢复，且不会阻止用户正常拖动窗口。

## 6. 关键实现顺序

按一次事件批次执行的实际顺序如下，任务实现必须保持这个边界：

```text
WinEvent callback
    -> 只写入 EventQueue
Coordinator 合并事件
    -> 确认活动窗口和事务
    -> 刷新参与窗口快照及当前 Z-order
    -> 构建不可变 LayoutSnapshot
Solver
    -> 找违规
    -> 生成候选
    -> 硬约束过滤
    -> 模拟和代价排序
    -> 产生 MovePlan 或 Unsatisfiable
Applier
    -> 写入内部移动令牌
    -> SetWindowPos
    -> 刷新实际矩形
Verifier
    -> 全量检查
    -> 进入 Idle、继续 Settling 或 Unsatisfiable
```

禁止以下快捷实现：

- 在 WinEvent 回调中直接移动窗口。
- 只比较两两矩形是否包含，不计算高层窗口 union。
- 只按候选移动距离排序，不先验证硬约束。
- 以 `visitedWindows` 代替布局状态周期检测。
- 用任意大边界代价代替工作区硬约束。
- 用 `GetForegroundWindow` 替代拖动事件中的活动 HWND。

## 7. 测试计划

### 7.1 测试分层

| 层级 | 目标 | 运行频率 |
| --- | --- | --- |
| 几何单元测试 | Rect、Region、交互区和 DPI 转换 | 每次提交 |
| 求解器单元测试 | 候选、硬约束、代价、周期和超时 | 每次提交 |
| 属性测试 | 随机矩形布局不变量 | 每次提交，夜间扩大样本 |
| 平台适配测试 | 快照、分类、Z-order、DWM 和虚拟桌面 | Windows CI 每次提交 |
| 窗口集成测试 | 真实事件、SetWindowPos、应用拒绝移动 | 合并前和夜间 |
| 长时稳定测试 | 事件风暴、拖动、资源泄漏和恢复 | 每个候选版本 |
| 性能基准 | N=2/5/10/20 和区域碎片上限 | 每周或性能变更时 |

### 7.2 必测场景矩阵

| 编号 | 场景 | 预期结果 |
| --- | --- | --- |
| T01 | 两个窗口部分重叠 | 不必要时不移动；违规时产生合规交互区 |
| T02 | A 完全覆盖 B | B 移动到合法候选或返回无解 |
| T03 | A/B/C 链式遮挡 | 继续处理低层窗口，不循环 |
| T04 | 四个方向均有候选 | 先硬约束过滤，再按确定性代价选择 |
| T05 | 只有组合位移可行 | 不能因四个单轴候选失败而误报成功 |
| T06 | 最大化/无边框全屏活动窗口 | 暂停自动推挤，不移动其他窗口 |
| T07 | 未管理置顶窗口覆盖工作区 | 作为阻挡；无解时停止推挤 |
| T08 | 菜单、提示、通知和子控件事件 | 不进入管理集合，不移动子控件 |
| T09 | 窗口销毁并快速复用 HWND | 不继承旧快照或事务 |
| T10 | 应用拒绝或改回位置 | 取消剩余计划，重新快照并降级 |
| T11 | 快速拖动和调整大小 | 事件合并，无明显抖动和队列失控 |
| T12 | 100/150/200% DPI | 阈值按 DIP 缩放，坐标一致 |
| T13 | Windows Snap/前台切换 | 用户操作优先，最终状态可验证 |
| T14 | 队列溢出/钩子重建 | 全量重新枚举，不使用过期计划 |
| T15 | 用户暂停 | 后续事件不触发自动移动 |

### 7.3 属性不变量

求解器成功返回时，测试必须验证：

1. 活动窗口矩形与输入相同。
2. 所有受管理窗口尺寸、显示器、虚拟桌面和 Z-order 合法；活动窗口未被任何计划越过。
3. 每个受管理窗口优先至少两个不同边缘的交互区满足 RepairTargetEdge；两边缘无解时至少一个，并标记降级。
4. 合法候选严格按左+上、右+下、仅上、无方向偏好排序，并在边缘组合相同时才比较移动距离。
5. 所有窗口满足最小屏上区域。
6. MovePlan 不包含重复状态或相同窗口的无意义零位移。
7. 模拟结果与重新计算的违规集合一致。

求解器返回无解、超时或几何复杂时，测试必须验证它没有产生部分未验证的移动计划。

## 8. 性能和稳定性验证

### 8.1 基准设置

建立可重复的 `scenario_runner`，生成固定窗口尺寸、重叠比例、阻挡密度和随机种子。至少记录：

```text
snapshot_time
geometry_time
solver_time
apply_time
verify_time
end_to_end_time
candidate_count
state_count
region_fragment_count
event_queue_depth
api_failure_count
```

目标是矩形模型、20 个窗口下 P95 批次处理不超过 16 ms；若真实 API 或桌面负载导致超时，必须返回超时/暂停并记录，而不是继续占用事件线程。

### 8.2 长时测试

每个候选版本至少执行以下长时场景：

- 5 个窗口连续快速拖动 30 分钟。
- 20 个窗口每秒随机触发移动/调整大小事件 10 分钟。
- 反复启停管理器、注销/安装钩子和切换虚拟桌面。
- 在测试窗口销毁、重建、拒绝移动和改变样式的同时拖动其他窗口。

检查线程数、句柄数、队列深度、日志大小、CPU 占用和窗口位置漂移。

## 9. 风险清单和应对

| 风险 | 影响 | 触发信号 | 应对 |
| --- | --- | --- | --- |
| WinEvent 延迟或漏事件 | 约束短暂失效、快照过期 | 队列时间戳异常、周期重建频繁 | 合并事件、周期 reconciliation、全量重建 |
| 管理器自触发事件 | 无限循环、窗口抖动 | 内部 generation 未匹配、同位置重复事件 | 内部移动令牌、回调不求解、状态哈希 |
| 没有合法布局 | 窗口越界或强行移动 | 所有候选硬约束失败 | `Unsatisfiable`，保留用户位置，暂停批次 |
| Z-order/owner 关系复杂 | 误判遮挡或破坏模态对话框 | topmost/owned 窗口进入管理集 | 保守分类、按组排除、每批次重建 Z-order |
| DPI/视觉框不一致 | 阈值错误、点击区偏移 | 不同缩放下测试失败 | PMv2、统一物理坐标、DWM visualRect 与 placementRect 分离 |
| 应用反复改回位置 | 布局不稳定 | 实际框与计划框持续偏离 | 标记不可协作、停止重试、进入无解 |
| 区域碎片爆炸 | 超时、内存增长 | Region 碎片超过上限 | 限制碎片、返回 GeometryTooComplex |
| 跨完整性级别/安全桌面 | API 查询或移动失败 | 错误码、窗口不可访问 | 不管理或无解，不循环重试 |
| 虚拟桌面 API 不可用 | 误移动隐藏桌面窗口 | 当前桌面判断失败 | 禁用跨桌面自动移动并记录限制 |
| 事件线程阻塞 | 丢事件、拖动卡顿 | HookThread 延迟增长 | 回调只入队，求解在 CoordinatorThread |
| 用户误操作或体验不佳 | 失去信任 | 大幅跳跃、频繁暂停 | DryRun、紧急停用、稳定性指标和保守默认值 |

## 10. 发布门槛和回滚

### 10.1 发布前检查

1. 需求文档中的 FR/NFR 和验收条目均有实现状态。
2. 所有 P0/P1 测试通过；P2 问题有已知限制说明。
3. 默认配置不会管理置顶、最大化、全屏或分类不确定窗口。
4. 首次启动提供暂停入口，日志目录和配置目录可清理。
5. 在无管理员权限、100/150/200% DPI 和至少两个 Windows 11 更新版本验证。
6. 运行长时稳定测试，无线程、句柄、队列和窗口漂移异常。
7. 发布包包含版本号、构建号、卸载路径和已知限制。

### 10.2 分阶段发布

```text
内部开发版：默认 DryRun，输出完整计划和诊断
测试版：两个窗口真实移动，增加显式暂停提示
候选版：开放 2..20 窗口，启用无解和故障降级
稳定版：完成长时测试、回滚包和用户文档
```

任何版本发现持续自触发、活动窗口被移动、窗口越界或无法暂停，立即回滚到 DryRun/Disabled 行为，不等待后续算法优化。

## 11. 第一个开发迭代的具体清单

第一个迭代不实现自动移动，目标是完成可运行的工程和可观察窗口快照：

1. 完成 M0 的 CMake、清单、日志和测试 runner。
2. 完成 M1 的托盘、单实例、暂停和紧急停用。
3. 完成 M2 的 OUTOFCONTEXT hook、事件队列和事件录制器。
4. 完成 M3 的 `window_inspector`，输出当前顶级窗口和本次 Z-order。
5. 创建 `TestWindowHarness`，能启动、定位、改变样式、销毁和重建窗口。
6. 用录制日志确认移动、调整大小、显示/隐藏和销毁事件顺序。
7. 将所有未完成自动布局路径固定为 DryRun，避免半成品移动真实窗口。

该迭代的验收结果应是：可以在真实 Windows 11 桌面上启动程序，看到稳定的窗口快照和事件日志，并能安全暂停/退出；没有任何窗口被自动移动。
