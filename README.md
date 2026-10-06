# zygisk_framework

这是 `codex/xposed-api82` 分支：Zygisk + LSPlant 框架为旧版 Xposed 源代码提供 API 82 应用进程兼容。

核心能力：

- Zygisk Root Companion 按已安装模块的 `scope.list` 选择进程；没有匹配模块时 fail-closed。
- Native 侧从源码静态链接 LSPlant 和 JingMatrix/Dobby。
- `framework.dex` 由 R8 混淆后导出为 C++ header，并编译进 native loader；Zygisk 通过内置 mapping 找到混淆后的 `NativeBridge` 和 `RuntimeBootstrap`。
- Framework 和模块 DEX 都通过临时 `dexElements` 挂载预加载，完成入口调用后恢复宿主原始 `dexElements`。
- 独立业务模块输出混淆后的 `module.dex`，并根据 `module.mapping` 重写 `META-INF/xposed/java_init.list`；模板实现 `IXposedHookLoadPackage`、`IXposedHookZygoteInit` 和旧版方法/构造器 Hook API。
- `hook_template/` 是独立 Java/Kotlin Hook 模块项目，以 Git 子模块提供，包含按应用隔离的 Remote Preferences 全局快照和可配置 Android ID/GAID 示例。
- 设备端 CLI 管理模块 ZIP，入口为 `/data/adb/zygisk_framework/bin/zygisk_framework`。
- Root Companion 在 specialize 前接管目标进程预创建的 memfd，CLI 写入后通过只读映射和 futex 实时通知目标进程。

常用命令：

```sh
./scripts/prepare_sources.sh
./gradlew :framework_runtime:testDebugUnitTest
./gradlew :framework_runtime:exportApi82TestRuntime
./gradlew :framework_runtime:buildFrameworkDex
./gradlew packageMagiskModule
python3 scripts/verify_comments.py
```

初始化并构建 Java/Kotlin API 82 模板：

```sh
git submodule update --init hook_template
./scripts/build_hook_dex.sh
```

产物位于 `dist/`，Magisk 模块为 `dist/zygisk_framework-0.5.0.zip`。业务模块 ZIP
可通过设备 CLI 的 `install/list/info/scope/prefs/remove/enable/disable` 命令管理。

CLI 命令和设备端模块管理方法见[CLI 使用指南](docs/CLI_USAGE.md)。更多文档见 [`docs/`](docs/README.md)。

本分支兼容范围为应用进程内的方法和构造器 Hook。`initZygote` 在目标应用进程 specialize 后逐进程调用，和旧框架真正的 Zygote 回调时机不同。资源/布局 Hook、直接安装旧 APK 和 `system_server` 不在支持范围内；完整矩阵见[兼容性说明](docs/COMPATIBILITY.md)。
