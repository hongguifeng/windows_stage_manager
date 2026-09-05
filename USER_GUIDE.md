# Windows Stage Manager 用户指南

## 首次启动

1. 解压版本化发布包并启动 `stage_manager.exe`。
2. 程序常驻系统托盘；默认 `dry_run=false`，拖动结束后会自动整理符合条件的非活动窗口。
3. 打开 `%LOCALAPPDATA%\WindowsStageManager\logs\manager.log`，可确认 `application_start` 中的实际 `dry_run` 值。

## 配置

配置文件为 `%LOCALAPPDATA%\WindowsStageManager\settings.ini`。首次运行没有配置文件时使用内置安全默认值。关键配置如下：

```ini
enabled=true
dry_run=false
max_managed_windows=20
max_consecutive_failures=3
```

程序开箱即可工作，只改变非活动窗口的位置，不改变尺寸、激活状态或 Z-order。若希望先观察计划或排查问题，将 `dry_run` 改为 `true` 并重启程序。

## 日常操作与故障处理

- 托盘菜单可暂停/恢复管理或退出程序。
- `Ctrl+Alt+F12` 会立即停用自动管理并取消当前移动事务。
- 连续 API 或快照故障达到阈值后，程序会自动停用；检查日志、恢复 `dry_run=true` 后再从托盘重新启用。
- 显示器、DPI、工作区变化以及队列溢出会触发全量快照重建。
- `Unsatisfiable` 表示当前布局没有满足硬约束的安全移动方案，不会强行越界或移动活动窗口。

## 回滚

发布目录中的 `current.json` 指向当前 ZIP，`rollback.json` 指向上一个已提升 ZIP。回滚时：退出程序，备份日志和配置，解压 `rollback.json` 指定的版本到新的目录，并保持 `dry_run=true` 启动验证。不要覆盖正在运行的可执行文件。
