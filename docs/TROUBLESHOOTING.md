# 排错说明

没有 `TARGET_MATCH` 或模块未加载：
检查已启用模块的 `scope.list` 是否命中当前包名/进程名，并用设备 CLI 的 `info <id>` 确认模块状态和 checksum。修改模块状态后重新启动目标应用进程。

`DEX_VERIFY_FAILED code=FRAMEWORK_DEX_INVALID`：
确认 `./gradlew packageMagiskModule` 已重新执行，且 `native_loader/build/generated/zygisk_framework/framework_dex.h`、`framework_mapping.h` 已由最新 `framework.dex` 生成。当前 Magisk 模块不再要求 `/zygisk/framework.dex` 作为运行时文件。

`DEX_VERIFY_FAILED code=FRAMEWORK_DEX_ELEMENTS_CREATE_FAILED` 或 `FRAMEWORK_CLASSLOADER_PATCH_FAILED`：
当前系统隐藏 API `DexPathList.makeInMemoryDexElements` 不可用，或独立 framework ClassLoader 的 `dexElements` 写入失败。收集 `zygisk_framework.Native` 日志确认具体 JNI 异常。

`MODULE_ENTRY_FAILED code=MODULE_FILE_INVALID`：
确认模块目录包含 `module.dex`、`module.sha256` 和 `META-INF/xposed/*`，并且文件不可 world-writable。

`LSPLANT_INIT_FAILED`：
收集 `zygisk_framework.Native` 日志，重点查看 ART symbol 解析失败的符号名。Android 15/厂商 ART 若缺少 `GetMethodShorty`，应看到 `fallback to reflection shorty` 并继续初始化；若缺少 `art_quick_to_interpreter_bridge`，会记录 `deoptimize fallback disabled`，基础 Hook 仍应继续。

`Deoptimize` 返回失败：
检查日志是否出现 `deoptimize fallback disabled`。这表示当前 ART 未导出 LSPlant 需要的 interpreter bridge/deopt 内部符号，框架会保持 Method Hook 可用，但显式去优化能力不可用。

没有 `TEMPLATE_HOOK_BEFORE/TEMPLATE_HOOK_AFTER`：
确认模板 `scope.list` 与目标应用包名一致，并重新 force-stop 后启动 App。

`HOOK_FAILED` 或 `HOOK_CALLBACK_FAILED`：
按日志附带的 package、process、feature、hook 定位，检查目标 ClassLoader、精确参数类型及回调。
查看模板 [开发与团队交接](../hook_template/DEVELOPMENT.md) 的异常和重试规则。

`GAID_HOOK_INSTALLED` 但没有 `GAID_HOOK_APPLIED`：
确认应用实际调用标准 Info.getId，且调用没有被内联或换成其他 SDK 路径；仅安装不能证明替换已执行。
`GAID_HOOK_SKIPPED` 表示缺类/缺方法，`GAID_HOOK_FAILED` 才表示查找或安装错误。

业务自行使用 Remote Preferences 时没有实时更新（默认模板不再订阅）：
检查目标进程日志是否出现 `REMOTE_PREFS_CHANNEL_READY mode=SHARED_MEMORY`。若出现
`SHARED_MEMORY_CREATE_FAILED`、`MAP_FAILED`、`SIZE_LIMIT` 或 `INOTIFY_SETUP_FAILED`，框架只使用启动时
快照；若出现 `MANAGER_STALLED`，说明 companion 已停止更新 heartbeat，需要重启目标进程建立新通道。
