# Windows 11 台前调度式窗口管理器

当前仓库处于第一迭代：先完成可观察、可停止的后台程序基础设施，自动布局默认保持 DryRun，尚未实现真实窗口移动。

## 构建

需要 CMake 3.25+、Ninja、Visual Studio 2022 C++ 工具链和 Windows 11 SDK：

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

发布构建使用 `windows-release` preset。

## 文档

- `Windows 台前调度式窗口管理器需求说明.md`
- `Windows 台前调度式窗口管理器概要设计.md`
- `Windows 台前调度式窗口管理器详细开发计划.md`
- `TODO.md`

