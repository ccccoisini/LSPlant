# API 102 兼容性矩阵

| 能力 | 当前状态 | 说明 |
|---|---:|---|
| 模块入口与 attachFramework | 支持 | 每个模块独立 `FrameworkXposedInterface`。 |
| onModuleLoaded | 支持 | Java runtime 装载后分发一次。 |
| onPackageLoaded/onPackageReady | 支持 | 优先通过 `Instrumentation.newApplication(...)` 在 Application 创建前 Bootstrap，并保留 `Application.attach(Context)` 兼容回退；两个入口均不可用时记录 `LIFECYCLE_DEGRADED_MODE`。 |
| Method Hook | 支持 | 一目标方法一个 LSPlant Hook，Java 快照分发。 |
| Constructor Hook | 部分 | LSPlant 通道可接入；Constructor Invoker 未完整支持。 |
| 多 Hook 链 | 支持 | 不持锁执行用户 Hook。 |
| priority | 支持 | 高 priority 先执行，同 priority 按创建序稳定。 |
| ExceptionMode | 支持 | 单元测试覆盖 PROTECTIVE/PASSTHROUGH 基础语义。 |
| proceed/参数替换 | 支持 | 支持 `proceed`、`proceed(newArgs)`、`proceedWith`。 |
| Method ORIGIN Invoker | 支持 | 反射调用原方法。 |
| HookHandle.unhook | 支持 | 幂等，最后一个逻辑 Hook 删除后 native unhook。 |
| API 102 Hook ID 替换 | 支持 | 同模块、同 Executable、同 ID 原子替换。 |
| deoptimize | 条件支持 | 透传 LSPlant `Deoptimize` 返回值；Android 15/厂商 ART 缺少 `art_quick_to_interpreter_bridge` 等符号时会降级返回失败，但不影响基础 Method Hook。 |
| Constructor Invoker | 未完整支持 | `newInstance` 可用，`invokeSpecial/newInstanceSpecial` 抛出 `UnsupportedOperationException`。 |
| hookClassInitializer | 未支持 | 抛出 `HookFailedError`，不虚报能力。 |
| Hot Reload | 未支持 | Demo `autoHotReload=false`。 |
| Remote Preferences | 条件支持 | Root CLI 写入、Hook 进程只读；初始快照始终可用，实时推送要求 companion socket 能跨 specialize 保留。 |
| Remote Files | 未支持 | 因能力尚不完整，暂不设置 `PROP_CAP_REMOTE`。 |
| system_server | 未支持 | 不设置 `PROP_CAP_SYSTEM`。 |
| ART `.symtab` 完整解析 | 支持 | Native 侧通过 `dl_iterate_phdr + dlsym + ELF .dynsym/.symtab` 解析；stripped ART 缺符号时按 Android 15 LSPlant patch 降级。 |

## 后续 TODO：companion socket

- 调研 Zygisk `exemptFd()` 返回失败且 `AppSpecializeArgs.fds_to_ignore` 不可用的 specialize 路径。目前此类
  设备会记录 `REMOTE_PREFS_DEGRADED reason=SOCKET_EXEMPT_FAILED`，保留启动时最后有效快照，Hook 主链路
  继续运行，但无法接收实时更新。
- 评估不依赖 specialize 后保留 companion socket 的通知通道，例如共享内存事件或独立 Binder 服务；在
  找到兼容方案前不绕过 Zygisk 的 FD 清理，也不从 Hook 进程自行重连 root 服务。
- 在支持 FD 豁免的 Magisk/Zygisk 环境补充实时推送 PASS 报告，并保留当前 Samsung Android 15 +
  KernelSU + Zygisk Next 的降级用例作为回归测试。

本地构建、JVM 单元测试和降级路径真机验收已完成；完整实时推送仍需在支持 companion socket 跨
specialize 保留的设备上运行 `scripts/verify_device.sh` 生成 PASS 报告。
