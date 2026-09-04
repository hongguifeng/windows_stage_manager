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
- [ ] M7-01 完成两窗口 DryRun 到真实移动的闭环和验收场景。commit: 

## 第四迭代：多窗口稳定性与发布

- [ ] M8-01 支持三至二十窗口链式推挤、滞后和周期检测。commit: 
- [ ] M8-02 增加长时稳定、事件风暴和随机属性测试。commit: 
- [ ] M9-01 完成诊断、性能基准、故障降级和发布回滚。commit: 
- [ ] M9-02 完成用户文档、已知限制和候选发布包。commit: 

## 当前工作规则

1. 在 `DryRun` 关闭前，不允许任何自动布局代码调用 `SetWindowPos`。
2. 每个任务完成后先运行相关测试，再更新本条状态和 commit 列，最后提交。
3. 若实现发现需求或概要设计冲突，先更新文档和 TODO，再继续编码。
4. 任何无法满足硬约束的布局必须返回 `Unsatisfiable`，不得用越界或改 Z-order 规避。
