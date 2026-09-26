# 真机验收说明

前置条件：

- Android API 26+
- Magisk 已启用 Zygisk
- ADB 可用且设备已 root
- 已安装 `io.hammer.developmentenvironmentdetection`

安装 Magisk 模块：

```sh
./gradlew packageMagiskModule
./scripts/install_device.sh
adb reboot
adb wait-for-device
```

自动验收：

```sh
./scripts/verify_device.sh \
  --package io.hammer.developmentenvironmentdetection \
  --module-zip hook_template/dist/example_hook-1.2.0.zip
```

成功时报告写入：

```text
dist/device-verification-report.txt
```

脚本会先在设备执行 native 共享内存/futex 测试，再检查 `TARGET_MATCH`、`LSPLANT_INIT_OK`、
`FRAMEWORK_DEX_LOADED`、`MODULE_DEX_LOADED`、模板生命周期 Hook、Remote Preferences 初始读取与实时
更新，并确认配置更新过程中目标 PID 保持不变、损坏文件不会覆盖最后有效快照。
