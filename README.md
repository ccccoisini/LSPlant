# zygisk_framework

这是一个 Zygisk + LSPlant + libxposed API 102 的最小可迭代 Hook 框架工程。

核心能力：

- Zygisk Root Companion 按已安装模块的 `scope.list` 选择进程；没有匹配模块时 fail-closed。
- Native 侧从源码静态链接 LSPlant 和 JingMatrix/Dobby。
- `framework.dex` 由 R8 混淆后导出为 C++ header，并编译进 native loader；Zygisk 通过内置 mapping 找到混淆后的 `NativeBridge` 和 `RuntimeBootstrap`。
- Framework 和模块 DEX 都通过临时 `dexElements` 挂载预加载，完成入口调用后恢复宿主原始 `dexElements`。
- 独立业务模块输出混淆后的 `module.dex`，并根据 `module.mapping` 重写 `META-INF/xposed/java_init.list`。
- `hook_template/` 是独立 Java/Kotlin Hook 模块项目，以 Git 子模块提供。
- 设备端 CLI 管理模块 ZIP，入口为 `/data/adb/zygisk_framework/bin/zygisk_framework`。
- Root Companion 提供只读 Remote Preferences，CLI 写入后实时推送给已运行的目标进程。

常用命令：

```sh
./scripts/prepare_sources.sh
./gradlew :framework_runtime:testDebugUnitTest
./gradlew :framework_runtime:buildFrameworkDex
./gradlew packageMagiskModule
python3 scripts/verify_comments.py
```

初始化并构建 Java/Kotlin 模板：

```sh
git submodule update --init hook_template
./scripts/build_hook_dex.sh
```

产物位于 `dist/`，Magisk 模块为 `dist/zygisk_framework-0.4.0.zip`。业务模块 ZIP
可通过设备 CLI 的 `install/list/info/scope/prefs/remove/enable/disable` 命令管理。

CLI 命令和设备端模块管理方法见[CLI 使用指南](docs/CLI_USAGE.md)。更多文档见 [`docs/`](docs/README.md)。
