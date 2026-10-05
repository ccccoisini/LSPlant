# 真机验收说明

前置条件：Android API 26+、已启用 Zygisk 的 root 环境、ADB 可用、已安装专用测试应用。
默认示例为 `io.hammer.developmentenvironmentdetection`；目标启动时必须读取 Android ID。
验收脚本会安装、启停并最终移除所传模块及其配置，请使用专用测试模块。

构建、安装框架：

```sh
./gradlew packageMagiskModule
./scripts/install_device.sh
adb reboot
adb wait-for-device
```

构建模板并验收：

```sh
(cd hook_template && ./gradlew :app:testDebugUnitTest packageHookModule)
./scripts/verify_device.sh \
  --package io.hammer.developmentenvironmentdetection \
  --module-zip hook_template/dist/example_hook-1.4.0.zip
```

多台设备时增加 `--serial SERIAL`。报告写入 `dist/device-verification-report.txt`。
脚本将测试模块 scope 设置为目标包，并先在设备执行 native 共享内存/futex 测试，再检查加载标记、
生命周期 before/after、固定 Android ID 实际替换，以及崩溃和安装失败。
CLI 配置测试使用通用 verification 组，覆盖值类型、缺键、特殊字符串、删除/清空幂等、升级保留和
随模块删除；不依赖模板注册 Remote Preferences 监听器。

## GAID 验收

默认行为区分三种结果：

- `PASS gaid=APPLIED`：观察到 GAID_HOOK_APPLIED，实际执行了标准 Info.getId 替换。
- `SKIP gaid=CLASS_OR_METHOD_NOT_FOUND`：目标 loader 没有标准类或签名。
- `NOT_VERIFIED gaid=NO_APPLIED_EVENT`：没有实际调用事件，不能视为 GAID 验收通过。

要强制验收 GAID，使用启动时会调用标准 GMS SDK Info.getId 的目标应用：

```sh
./scripts/verify_device.sh --package com.example.gaidtest \
  --module-zip hook_template/dist/example_hook-1.4.0.zip --require-gaid
```

此模式缺类、仅安装成功或未触发调用都失败。普通模式也会将真正的 GAID 安装失败记为失败。
目标必须在启动后的采样窗口内触发读取；脚本不会主动调用 GMS 获取接口，也不会伪造成功日志。
GAID 零值可能与系统原结果相同，单看返回值不足以证明 Hook，需同时检查实际触发事件。

模板单元测试的同名 GAID 夹具不进入正式 DEX，也不能替代真实 SDK/ART 验收。
框架单元测试和独立 transport/CLI 检查继续保留。

## Remote Preferences 实时验收

```sh
./scripts/verify_device.sh --package io.hammer.developmentenvironmentdetection \
  --module-zip hook_template/dist/example_hook-1.4.0.zip \
  --require-gaid --check-remote-preferences
```

脚本先清空 settings.<包名>，验证默认快照和启动时的标识替换。启用 --check-remote-preferences 后，
运行期间依次写入自定义标识、关闭 enabled、删除 android_id、清空配置组。每阶段在 15 秒窗口内
要求目标主进程发布匹配的完整 REMOTE_CONFIG_UPDATED 快照，且 PID 始终与启动采样相同。
其他包、子进程、仅 LOADED 或较早的不同快照不能替代该阶段成功。日志收集包含 HookTemplate.Config。

PASS remote_preferences=REMOTE_CONFIG_UPDATED 仅证明配置已接收。自定义标识的实际替换另报
PASS android_id_remote_value=APPLIED / PASS gaid_remote_value=APPLIED；未重新触发读取时报告
NOT_VERIFIED ...=NO_APPLIED_EVENT，不冒充实际替换成功。--require-gaid 仍要求启动采样中出现实际 GAID_HOOK_APPLIED。
该模式不主动刷新应用缓存，不修改 native/runtime，也不恢复旧模板的损坏文件注入测试。

## 1.3.0 验证记录

2026-10-05 在 Samsung SM-F7310、Android 15/API 35、KernelSU 环境，以
`io.hammer.developmentenvironmentdetection` 运行 `--require-gaid` 验收成功：五个 Hook 点全部安装，
生命周期 before/after、固定 Android ID 和 GAID 实际替换事件均出现，无崩溃标记。
native transport、CLI 类型/幂等/升级保留及测试模块移除检查通过；验收结束后已移除测试模块。
本机报告在 `dist/device-verification-report.txt`，生成文件不随源码提交。

模板 35 个 JVM 测试和父仓库 GAID 判定的 8 个测试通过。

## 1.4.0 验证记录

2026-10-05 在相同 Samsung SM-F7310、Android 15/API 35、KernelSU 设备，以
io.hammer.developmentenvironmentdetection 运行 --require-gaid --check-remote-preferences 验收成功。
五个 Hook 点全部安装，启动时生命周期、默认 Android ID 和 GAID 实际替换均有日志。
自定义配置、关闭开关、删除 android_id 和清空四个阶段均收到匹配的完整更新快照，PID 始终为 17011。
native transport、CLI 检查、无崩溃和测试模块移除检查通过。

该目标在配置更新期间没有再次触发标识读取，因此自定义值的 Android ID/GAID 实际替换记为 NOT_VERIFIED，
未作为真机替换通过。模板 JVM 测试通过假 Chain 验证同一个已安装 Hook 随更新返回新值、关闭时透传、
清空后恢复默认值，且原调用最多一次、无重复安装。

模板 55 个 JVM 测试通过；runtime 12 个既有测试任务通过（UP-TO-DATE）。父仓库日志判定 17 个测试通过，
中文公共注释检查通过。Release ZIP 包含单个 32920 字节 DEX，47 个类，混淆入口为 zhm.h0，
入口 public 无参构造、映射、SHA-256 及 API/测试夹具未打包检查通过。
报告仍位于 dist/device-verification-report.txt，构建产物和报告不提交。

父仓库日志判定测试运行命令为：

```sh
python3 -B -m unittest discover -s scripts/tests -v
```

其他设备、Android 版本和 GAID 动态加载/内联路径需分别验收。
