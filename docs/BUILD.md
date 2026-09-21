# 构建说明

推荐环境：

- JDK 21
- Android SDK Build Tools 35.0.0
- Android NDK 29.0.14206865
- CMake 3.31.0 或 3.28+
- Python 3、Git、ADB

一键构建：

```sh
./scripts/build_all.sh
```

脚本会执行完整检查、单元测试、DEX 混淆、Native 编译，并最终生成可直接安装的 Magisk/KernelSU 模块 ZIP：`dist/zygisk-lsplant-framework-<version>.zip`。结束前脚本会用 `unzip -t` 校验 ZIP 可读，并打印 SHA-256。

`scripts/build_all.sh` 会在编译前校验固定的 LSPlant commit，并自动判断 `third_party/patches/lsplant/*.patch` 是待应用、已应用还是与源码不兼容。当前仅保留 `android15-reflection-shorty.patch`，用于兼容 Android 15/厂商 ART 缺少 `GetMethodShorty`/interpreter bridge 内部符号；不兼容的补丁会立即终止构建，避免静默产出错误模块。

当前工程为了避开 AAR metadata 对 `compileSdk 37` 的本地 SDK 命名差异，使用 `libs/libxposed-api-102.0.0.jar` 作为真实 API102 编译输入。`framework.dex` 会把 API 类打入自身 DEX，并在 native-loader 构建前导出为 C++ header 编译进 `zygisk/*.so`；业务模块只把该 jar 放在 compileOnly/classpath，不会打进 `module.dex`。

DEX 混淆产物：

- `framework-runtime/build/outputs/framework/framework.dex`
- `framework-runtime/build/outputs/framework/framework.mapping`
- `native-loader/build/generated/zhook/framework_dex.h`
- `native-loader/build/generated/zhook/framework_mapping.h`
- `demo-hook-module/build/outputs/hook/module.dex`
- `demo-hook-module/build/outputs/hook/module.mapping`
- `demo-hook-module/build/outputs/hook/META-INF/xposed/java_init.list`

Native 产物只构建 arm64：

- `native-loader/build/intermediates/cmake/release/obj/arm64-v8a/libzygisk_hook.so`

Magisk ZIP 中只包含 `zygisk/*.so`、`hook/modules/*` 和模块元数据；`framework.dex`/`framework.mapping` 仅保留在根 `dist/` 目录用于调试，不再作为运行时文件下发。

检查动态依赖：

```sh
$ANDROID_HOME/ndk/29.0.14206865/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-readelf -d \
  native-loader/build/intermediates/cmake/release/obj/arm64-v8a/libzygisk_hook.so
```
