# Zygisk LSPlant Hook Framework

这是一个 Zygisk + LSPlant + libxposed API 102 的最小可迭代 Hook 框架工程。

核心能力：

- Zygisk Root Companion 读取 `/data/adb/hook/target.txt`，未命中目标时 fail-closed。
- Native 侧从源码静态链接 LSPlant 和 JingMatrix/Dobby。
- `framework.dex` 由 R8 混淆后导出为 C++ header，并编译进 native loader；Zygisk 通过内置 mapping 找到混淆后的 `NativeBridge` 和 `RuntimeBootstrap`。
- Framework 和模块 DEX 都通过临时 `dexElements` 挂载预加载，完成入口调用后恢复宿主原始 `dexElements`。
- 独立业务模块输出混淆后的 `module.dex`，并根据 `module.mapping` 重写 `META-INF/xposed/java_init.list`。
- Demo 模块使用 libxposed API 102 Hook `Instrumentation.callApplicationOnCreate(Application)`，用于 `io.hammer.developmentenvironmentdetection` 真机验收。

常用命令：

```sh
./scripts/prepare_sources.sh
./gradlew :framework-runtime:testDebugUnitTest
./gradlew :framework-runtime:buildFrameworkDex :demo-hook-module:buildHookDex
./gradlew packageMagiskModule
python3 scripts/verify_comments.py
```

产物位于 `dist/`，Magisk 模块为 `dist/zygisk-lsplant-framework-0.1.0.zip`。

更多文档见 [`docs/`](docs/README.md)。
