# Windows 11 台前调度式窗口管理器 TODO

本清单依据：

- `Windows 台前调度式窗口管理器需求说明.md`
- `Windows 台前调度式窗口管理器概要设计.md`
- `Windows 台前调度式窗口管理器详细开发计划.md`

## 状态约定

- `[ ]` 未开始
- `[-]` 进行中
- `[x]` 已完成

每个已完成条目必须对应一个独立 Git 提交。`commit` 列记录提交哈希或短哈希。

## 第一迭代：可观察、可停止，但不自动移动真实窗口

### M0 工程基线

- [x] M0-01 初始化 CMake/C++20 工程、Debug/Release 构建和测试 runner。commit: 4ba5d0d
- [x] M0-02 增加 Windows 应用清单、Per-Monitor DPI Awareness V2 和编译警告基线。commit: ccc4bf0
- [x] M0-03 增加统一配置、错误码、结构化日志和版本信息。commit: c787fa5

### M1 托盘进程与生命周期

- [x] M1-01 实现 WinMain、单实例互斥体、隐藏消息窗口和安全退出。commit: 4ece360
- [x] M1-02 实现托盘图标、启用/暂停状态和紧急停用快捷键。commit: 64079ae
- [x] M1-03 实现配置持久化、DryRun 默认策略和显示器/DPI 变化通知。commit: 43709fc

### M2 WinEvent 事件管线

- [x] M2-01 实现有界事件队列、事件类型和值对象及单元测试。commit: c528346
- [x] M2-02 实现 OUTOFCONTEXT WinEvent hook、对象过滤和 HookThread 消息循环。commit: 70cb5ed
- [x] M2-03 实现事件合并、周期 reconciliation 请求和内部移动令牌接口。commit: cdcb3e0

### M3 窗口发现、分类、身份和检查工具

- [x] M3-01 实现窗口枚举、窗口快照、DPI/工作区读取和 DWM 属性读取。commit: cfd0748
- [x] M3-02 实现 HWND 实例身份、生命周期刷新和保守窗口分类器。commit: 9341d91
- [x] M3-03 实现当前快照 Z-order、虚拟桌面判断和 window_inspector 工具。commit: 2ee76ef

## 第二迭代：纯内存几何和求解器

- [x] M4-01 实现 Rect、Region、DIP 转换和工作区边界。commit: 6e65c10
- [x] M4-02 实现交互区、union/subtract 和命中采样模型。commit: 4b23821
- [x] M4-03 完成几何单元测试和属性测试。commit: 2d5dd78
- [x] M5-01 实现 LayoutSnapshot、Violation 和候选生成。commit: 9edf826
- [x] M5-02 实现硬约束过滤、软代价和确定性排序。commit: 5b4e955
- [x] M5-03 实现模拟求解、状态哈希、超时和无解结果。commit: 6d5c53b

## 第三迭代：真实窗口应用与 MVP

- [x] M6-01 实现 SetWindowPos 应用、内部事件抑制和后验验证。commit: 255c1cb
- [x] M6-02 实现应用拒绝移动、窗口销毁和新事务抢占处理。commit: 54712ee
- [x] M7-01 完成两窗口 DryRun 到真实移动的闭环和验收场景。commit: d93a759

## 第四迭代：多窗口稳定性与发布

- [x] M8-01 支持三至二十窗口链式推挤、滞后和周期检测。commit: 40ddd34
- [x] M8-02 增加长时稳定、事件风暴和随机属性测试。commit: 94756a4
- [x] M9-01 完成诊断、性能基准、故障降级和发布回滚。commit: 4d2358b / cba73c4 / 28a2f33 / e0cd833
- [x] M9-02 完成用户文档、已知限制和候选发布包。commit: ba241cf

## 当前工作规则

1. 在 `DryRun` 关闭前，不允许任何自动布局代码调用 `SetWindowPos`。
2. 每个任务完成后先运行相关测试，再更新本条状态和 commit 列，最后提交。
3. 若实现发现需求或概要设计冲突，先更新文档和 TODO，再继续编码。
4. 任何无法满足硬约束的布局必须返回 `Unsatisfiable`，不得越界或让其他窗口跨越活动窗口；活动窗口只可在非活动到活动的切换且居中开关启用时居中，拖动时必须保留用户位置；Z-order 只能作为纯位置求解失败后的受控 fallback。

## 第五迭代：受控 Z-order 无解回退

- [x] M10-01 实现从最底层非活动窗口开始的纯内存 Z-order fallback 求解与测试。commit: 541f8ee
- [x] M10-02 实现安全 Win32 重排、dry-run 隔离和后验不变量验证。commit: 6a581cc
- [x] M10-03 在两边缘/单边缘纯位置方案均无解后接入 fallback，并增加端到端测试和诊断。commit: 9c1e942
- [x] M10-04 更新需求、设计、计划、验收与 RC8 发布资料。commit: 156193a
- [x] M10-05 将直接提升到活动窗口下方改为保护最长上层前缀的渐进式局部重排。commit: b7bb41f

## 第六迭代：托盘参数控制面

- [x] M11-01 实现全部参数的强类型菜单命令、预设与依赖归一化。commit: 7838833
- [x] M11-02 实现托盘多级参数菜单、当前值勾选、持久化和内部管理器热重建。commit: dfa6d12
- [x] M11-03 增加真实进程 DryRun 托盘切换、配置落盘、日志和退出集成测试。commit: 1589e01

## 第七迭代：中心导向布局

- [x] M12-01 将被遮挡窗口候选改为优先接近工作区中心，不固定偏向左上角。commit: 6e68b64
- [x] M12-02 仅在非活动到活动的前台切换时居中新窗口，并在拖动、组合修复、Z-order fallback 和移动上限路径中保持原子性。commit: 79ac420
- [x] M12-03 同步需求、设计、计划、指南、验收用例与文档回归测试。commit: 2b22580

## 第八迭代：独立边缘与中文配置体验

- [x] M13-01 多边缘目标扣除共享角部，确保一条外露窄带不能重复计为两条边缘。commit: 0a82c7b
- [x] M13-02 将新激活窗口居中改为默认开启、可持久化的托盘开关。commit: 3fa64e1
- [x] M13-03 中文化托盘并为连续数值参数增加带范围校验的“自定义…”输入。commit: d7baf83
