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

脚本会执行完整检查、单元测试、DEX 混淆、Native 编译，并最终生成可直接安装的 Magisk/KernelSU 模块 ZIP：`dist/zygisk_framework-<version>.zip`。结束前脚本会用 `unzip -t` 校验 ZIP 可读，并打印 SHA-256。

`scripts/build_all.sh` 会在编译前校验固定的 LSPlant commit，并自动判断 `third_party/patches/lsplant/*.patch` 是待应用、已应用还是与源码不兼容。当前仅保留 `android15-reflection-shorty.patch`，用于兼容 Android 15/厂商 ART 缺少 `GetMethodShorty`/interpreter bridge 内部符号；不兼容的补丁会立即终止构建，避免静默产出错误模块。

当前工程使用 `libs/libxposed-api-102.0.0.jar` 作为真实 API102 编译输入。`framework.dex` 会把 API 类打入自身 DEX，并在 `native_loader` 构建前导出为 C++ header 编译进 `zygisk/*.so`；独立 `hook_template` 项目只把该 jar 放在 compileOnly/classpath，不会打进业务 `module.dex`。

DEX 混淆产物：

- `framework_runtime/build/outputs/framework/framework.dex`
- `framework_runtime/build/outputs/framework/framework.mapping`
- `native_loader/build/generated/zygisk_framework/framework_dex.h`
- `native_loader/build/generated/zygisk_framework/framework_mapping.h`
- `hook_template/build/outputs/hook/module.dex` 与 `module.mapping`

Native 产物只构建 arm64：

- `native_loader/build/intermediates/cmake/release/obj/arm64-v8a/libzygisk_framework.so`

框架 Magisk ZIP 中包含 `zygisk/*.so`、设备端 CLI 和框架元数据，不再内置业务模块。安装脚本会把 CLI 安装到 `/data/adb/zygisk_framework/bin/zygisk_framework`；业务模块由 CLI 安装到 `/data/adb/zygisk_framework/modules/`。框架卸载会删除整个 `/data/adb/zygisk_framework` 数据目录。`framework.dex`/`framework.mapping` 仅保留在根 `dist/` 目录用于调试，不作为运行时文件下发。

检查动态依赖：

```sh
$ANDROID_HOME/ndk/29.0.14206865/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-readelf -d \
  native_loader/build/intermediates/cmake/release/obj/arm64-v8a/libzygisk_framework.so
```
