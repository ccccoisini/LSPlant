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
  --module-zip hook_template/dist/example_hook-1.3.0.zip
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
  --module-zip hook_template/dist/example_hook-1.3.0.zip --require-gaid
```

此模式缺类、仅安装成功或未触发调用都失败。普通模式也会将真正的 GAID 安装失败记为失败。
目标必须在启动后的采样窗口内触发读取；脚本不会主动调用 GMS 获取接口，也不会伪造成功日志。
GAID 零值可能与系统原结果相同，单看返回值不足以证明 Hook，需同时检查实际触发事件。

模板单元测试的同名 GAID 夹具不进入正式 DEX，也不能替代真实 SDK/ART 验收。
Remote Preferences 的实时监听、损坏快照恢复和目标 PID 不变的旧模板断言已移除；框架单元测试
和独立 transport/CLI 检查继续保留。

## 本次实现验证

2026-10-05 在 Samsung SM-F7310、Android 15/API 35、KernelSU 环境，以
`io.hammer.developmentenvironmentdetection` 运行 `--require-gaid` 验收成功：五个 Hook 点全部安装，
生命周期 before/after、固定 Android ID 和 GAID 实际替换事件均出现，无崩溃标记。
native transport、CLI 类型/幂等/升级保留及测试模块移除检查通过；验收结束后已移除测试模块。
本机报告在 `dist/device-verification-report.txt`，生成文件不随源码提交。

模板 35 个 JVM 测试通过，Release ZIP 的单 DEX、入口公共无参构造、混淆映射、SHA-256 及
API/测试夹具未打包检查通过。父仓库 GAID 日志判定的 8 个测试通过，运行命令为：

```sh
python3 -B -m unittest discover -s scripts/tests -v
```

其他设备、Android 版本和 GAID 动态加载/内联路径需分别验收。
