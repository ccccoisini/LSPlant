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
zygisk_framework scope <module-id> list
zygisk_framework scope <module-id> add <target> [target...]
zygisk_framework scope <module-id> remove <target> [target...]
zygisk_framework scope <module-id> set <target> [target...]
zygisk_framework scope <module-id> clear
zygisk_framework prefs <module-id> <group> list
zygisk_framework prefs <module-id> <group> get <key>
zygisk_framework prefs <module-id> <group> set <key> string <value>
zygisk_framework prefs <module-id> <group> set <key> string-set [value...]
zygisk_framework prefs <module-id> <group> set <key> int|long|float|boolean <value>
zygisk_framework prefs <module-id> <group> remove <key>
zygisk_framework prefs <module-id> <group> clear
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
| `scope <module-id> list` | 查看模块当前生效的 scope 目标。 |
| `scope <module-id> add <target> [...]` | 添加一个或多个目标；已存在的目标不会重复写入。 |
| `scope <module-id> remove <target> [...]` | 删除一个或多个精确匹配的目标；不存在的目标会提示并跳过。 |
| `scope <module-id> set <target> [...]` | 用给定目标整体替换当前 scope；至少需要一个目标。 |
| `scope <module-id> clear` | 清空 scope.list；模块保留安装状态，但不会注入任何新进程。 |
| `prefs <module-id> <group> list/get` | 查看 Remote Preferences 组或单个键。 |
| `prefs <module-id> <group> set` | 按显式类型原子写入一个值。 |
| `prefs <module-id> <group> remove/clear` | 删除单个键或清空整个组。 |
| `disable <module-id>` | 禁用模块；保留文件，后续新进程不加载该模块。 |
| `enable <module-id>` | 重新启用已禁用模块。 |
| `remove <module-id>` | 删除模块及其 Remote Preferences 数据。 |
| `version` | 显示 CLI 版本。 |
| `help` | 显示命令帮助。 |

`<module-id>` 必须与模块 ZIP 内 `module.prop` 声明的 ID 完全一致，而不是 ZIP 文件名。ID 以英文字母
开头，后续仅可包含英文字母、数字、点、下划线和连字符，最长 128 个字符。scope 目标必须以英文字母、
数字或下划线开头，其余字符仅可使用英文字母、数字、下划线、点、冒号、美元符号和连字符，每条最多 512
个字符。

## 安装示例

在模板仓库中构建模块，并把产物推送到设备：

```sh
./gradlew packageHookModule
adb push dist/example_hook-1.2.0.zip /sdcard/Download/
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework install /sdcard/Download/example_hook-1.2.0.zip'
```

`example_hook-1.2.0.zip` 只是示例名称，请替换为实际产物。CLI 不根据文件名推断模块身份，最终安装目录
由包内 `META-INF/xposed/module.prop` 的 `id` 决定。再次安装相同或更高 `versionCode` 会替换现有版本；
较低版本默认拒绝，确认需要降级时加 `--force`：

```sh
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework install /sdcard/Download/example_hook-1.2.0.zip --force'
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

## Remote Preferences

Preferences 只允许 root CLI 写入，Hook 进程通过 API 102 的 `getRemotePreferences(group)` 取得只读
`SharedPreferences`。支持 `string`、`string-set`、`int`、`long`、`float` 和 `boolean`：

```sh
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework prefs example_hook settings.com.example.target set enabled boolean true'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework prefs example_hook settings.com.example.target set android_id string 0123456789abcdef'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework prefs example_hook settings.com.example.target set targets string-set alpha beta'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework prefs example_hook settings.com.example.target get enabled'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework prefs example_hook settings.com.example.target list'
```

模板约定每个目标 App 使用 `settings.<packageName>` group。这样同一模块同时 Hook 多个 App 时可以分别
配置；框架仍允许模块按自身需求使用任意合法 group 名称。

写入、删除和清空使用 root-only 临时文件原子替换。目标在 specialize 前创建 memfd，Root Companion
打开并初始化后，目标仅保留只读映射；后续通过 inotify、完整模块快照和 futex 通知已运行的目标进程，
不保留 companion socket 或共享内存 FD。已注册的
`OnSharedPreferenceChangeListener` 会在框架后台线程收到真正发生变化的键，不需要重启应用。连续快速写入
可能合并中间状态，但最终完整状态不会丢失；通道异常时保留最后一次有效快照，不影响 Hook 主链路。

数据位于 `/data/adb/zygisk_framework/data/<module-id>/preferences/`。同 ID 模块升级或强制降级不会覆盖
配置；`remove <module-id>` 会删除配置。单组最大 1 MiB、每模块最多 64 组且总计最大 4 MiB，单个字符串
或字符串集合成员最大 64 KiB。

## 查看、启停和删除示例

```sh
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework list'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework info example_hook'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework scope example_hook list'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework scope example_hook add io.hammer.developmentenvironmentdetection'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework scope example_hook remove com.example.target'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework scope example_hook set io.hammer.developmentenvironmentdetection'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework scope example_hook clear'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework disable example_hook'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework enable example_hook'
adb shell su -c '/data/adb/zygisk_framework/bin/zygisk_framework remove example_hook'
```

禁用、启用、安装、scope 修改和删除只影响之后启动的进程；CLI 不会主动结束正在运行的 App。要让这些
变更作用于目标应用，请自行停止并重新启动该应用。模块是否会在进程中加载，还取决于 `scope.list` 是否
包含该应用包名或对应进程名。`prefs` 是例外：它会实时通知仍在运行且已加载该模块的进程。

scope 目标使用精确包名或进程名。与框架运行时的匹配规则一致：不含冒号的包名也会匹配它的子进程
（例如 `com.example.app:remote`）；带冒号的进程名只匹配该进程。scope 修改通过同目录临时文件原子替换，
并与安装、启停、删除操作共用互斥锁。`clear` 会写入空的 `scope.list`，模块仍显示为已启用，但不会匹配
任何进程；重新安装模块 ZIP 会恢复 ZIP 中携带的 scope。

卸载框架会删除整个 `/data/adb/zygisk_framework` 数据目录，因此设备端 CLI 和已安装的业务模块也会一并
删除。需要保留模块时，请在卸载框架前备份相应模块 ZIP。
