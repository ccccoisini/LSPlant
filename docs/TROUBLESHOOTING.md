# 排错说明

`TARGET_CONFIG_ERROR code=READ_FAILED`：
检查 `/data/adb/zygisk_framework/target.txt` 是否存在，owner 是否为 root，权限是否为 `0644`。

`DEX_VERIFY_FAILED code=FRAMEWORK_DEX_INVALID`：
确认 `./gradlew packageMagiskModule` 已重新执行，且 `native-loader/build/generated/zygisk_framework/framework_dex.h`、`framework_mapping.h` 已由最新 `framework.dex` 生成。当前 Magisk 模块不再要求 `/zygisk/framework.dex` 作为运行时文件。

`DEX_VERIFY_FAILED code=FRAMEWORK_DEX_ELEMENTS_CREATE_FAILED` 或 `FRAMEWORK_CLASSLOADER_PATCH_FAILED`：
当前系统隐藏 API `DexPathList.makeInMemoryDexElements` 不可用，或独立 framework ClassLoader 的 `dexElements` 写入失败。收集 `zygisk_framework.Native` 日志确认具体 JNI 异常。

`MODULE_ENTRY_FAILED code=MODULE_FILE_INVALID`：
确认模块目录包含 `module.dex`、`module.sha256` 和 `META-INF/xposed/*`，并且文件不可 world-writable。

`LSPLANT_INIT_FAILED`：
收集 `zygisk_framework.Native` 日志，重点查看 ART symbol 解析失败的符号名。Android 15/厂商 ART 若缺少 `GetMethodShorty`，应看到 `fallback to reflection shorty` 并继续初始化；若缺少 `art_quick_to_interpreter_bridge`，会记录 `deoptimize fallback disabled`，基础 Hook 仍应继续。

`Deoptimize` 返回失败：
检查日志是否出现 `deoptimize fallback disabled`。这表示当前 ART 未导出 LSPlant 需要的 interpreter bridge/deopt 内部符号，框架会保持 Method Hook 可用，但显式去优化能力不可用。

没有 `DEMO_BEFORE/DEMO_AFTER`：
确认目标包为 `io.hammer.developmentenvironmentdetection`，`scope.list` 和 `/data/adb/zygisk_framework/target.txt` 都命中该包，并重新 force-stop 后启动 App。
