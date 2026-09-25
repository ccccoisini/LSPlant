# 设备端 CLI 使用指南

`zygisk_framework` 用于在 Android 设备上安装和管理 Hook 模块。它随框架 Magisk ZIP 一同安装，
设备端路径固定为 `/data/adb/zygisk_framework/bin/zygisk_framework`。本指南介绍命令、模块安装包要求
以及操作生效时机。

## 使用前准备

- 设备已安装并启用本框架，且 `/data/adb/zygisk_framework/bin/zygisk_framework` 文件存在。
- 命令必须以 root 身份运行。CLI 会优先使用 KernelSU 的
  `/data/adb/ksu/bin/busybox`，否则使用 Magisk 的 `/data/adb/magisk/busybox`；找不到这两个 BusyBox
  时命令会报错。
- 安装命令的输入是模板项目生成的模块 ZIP（例如 `dist/<模块 ID>-<版本>.zip`），不是 APK，也不是
  框架本身的 Magisk ZIP。可先从电脑用 `adb push` 将 ZIP 复制到设备可访问的位置。

进入 root shell 后可直接运行：

```sh
su
/data/adb/zygisk_framework/bin/zygisk_framework help
```

也可以在电脑终端通过 `su -c` 执行单条命令：

```sh
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework list'
```

## 命令一览

```text
zygisk_framework install <module.zip> [--force]
zygisk_framework list
zygisk_framework info <module-id>
zygisk_framework disable <module-id>
zygisk_framework enable <module-id>
zygisk_framework remove <module-id>
zygisk_framework version
zygisk_framework help
```

| 命令 | 作用 |
| --- | --- |
| `install <module.zip>` | 校验并安装模块；若 ID 已存在则替换为 ZIP 中的版本。 |
| `install <module.zip> --force` | 允许安装较低 `versionCode` 的版本（强制降级）。 |
| `list` | 列出已安装模块的 ID、版本、启用状态和名称。 |
| `info <module-id>` | 查看模块完整元数据、scope 和 DEX checksum 校验结果。 |
| `disable <module-id>` | 禁用模块；保留文件，后续新进程不加载该模块。 |
| `enable <module-id>` | 重新启用已禁用模块。 |
| `remove <module-id>` | 删除模块及其设备端文件。 |
| `version` | 显示 CLI 版本。 |
| `help` | 显示命令帮助。 |

`<module-id>` 必须与模块 ZIP 内 `module.prop` 声明的 ID 完全一致，而不是 ZIP 文件名。ID 以英文字母
开头，后续仅可包含英文字母、数字、点、下划线和连字符，最长 128 个字符。

## 安装示例

在模板仓库中构建模块，并把产物推送到设备：

```sh
./gradlew packageHookModule
adb push dist/example_hook-1.1.0.zip /sdcard/Download/
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework install /sdcard/Download/example_hook-1.1.0.zip'
```

`example_hook-1.1.0.zip` 只是示例名称，请替换为实际产物。CLI 不根据文件名推断模块身份，最终安装目录
由包内 `META-INF/xposed/module.prop` 的 `id` 决定。再次安装相同或更高 `versionCode` 会替换现有版本；
较低版本默认拒绝，确认需要降级时加 `--force`：

```sh
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework install /sdcard/Download/example_hook-1.0.0.zip --force'
```

## 模块包校验与安全边界

CLI 在安装前校验 ZIP、必需元数据、入口类、scope、API 兼容范围以及 `module.dex` 的 SHA-256。模块 API
范围必须覆盖 API 102，DEX 必须非空且不超过 32 MiB。ZIP 必须包含以下文件：

```text
module.dex
module.sha256
META-INF/xposed/java_init.list
META-INF/xposed/module.prop
META-INF/xposed/scope.list
```

安装时只提取以上固定文件，不会解压 ZIP 中的其他内容；ZIP 路径和文件名不会决定设备安装路径。遇到损坏
ZIP、缺失元数据、无效 ID、API 范围不兼容或 checksum 不匹配时，安装会失败。CLI 使用模块目录内的暂存和
备份目录完成替换；验证或替换失败时会清理暂存内容，并尝试恢复之前安装的版本。并发模块操作由锁串行化。

## 查看、启停和删除示例

```sh
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework list'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework info example_hook'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework disable example_hook'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework enable example_hook'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework remove example_hook'
```

禁用、启用、安装和删除只影响之后启动的进程；CLI 不会主动结束正在运行的 App。要让变更作用于目标
应用，请自行停止并重新启动该应用。模块是否会在进程中加载，还取决于 `scope.list` 是否包含该应用包名或
对应进程名。

卸载框架会删除整个 `/data/adb/zygisk_framework` 数据目录，因此设备端 CLI 和已安装的业务模块也会一并
删除。需要保留模块时，请在卸载框架前备份相应模块 ZIP。
