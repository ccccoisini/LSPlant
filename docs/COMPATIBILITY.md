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
| Remote Preferences | 支持 | Root CLI 写入、Hook 进程只读；通过 specialize 前映射的共享内存和 futex 实时更新，不依赖 companion socket FD 豁免。 |
| Remote Files | 未支持 | 因能力尚不完整，暂不设置 `PROP_CAP_REMOTE`。 |
| system_server | 未支持 | 不设置 `PROP_CAP_SYSTEM`。 |
| ART `.symtab` 完整解析 | 支持 | Native 侧通过 `dl_iterate_phdr + dlsym + ELF .dynsym/.symtab` 解析；stripped ART 缺符号时按 Android 15 LSPlant patch 降级。 |

## Remote Preferences 共享内存边界

- 目标进程在 specialize 前创建空 `memfd`，Companion handler 通过 `/proc/<pid>/fd/<n>` 校验、打开并
  初始化它，再返回初始快照和区域元数据；目标建立只读映射后关闭 FD，handler 随即关闭 socket。映射跨
  specialize 保留，因此不依赖 Zygisk Next 对反向 `SCM_RIGHTS` 或 FD 豁免的支持。
- 每个目标进程按命中模块分配固定槽，整个共享区域最大 64 MiB。超过限制或 memfd/mmap/inotify 初始化失败
  时记录 `REMOTE_PREFS_DEGRADED`，保留启动时快照，Hook 主链路继续运行。
- Companion 进程重启后，已运行目标的旧映射不会重连。heartbeat 超过 45 秒未更新时 reader 停止等待并
  保留最后有效缓存；新启动的目标进程会建立新通道。
- `PROP_CAP_REMOTE` 仍不设置，因为 Remote Files 尚未实现。

共享内存实时通道已在 Samsung SM-F731N、Android 15/API 35、KernelSU 与 Zygisk Next 环境完成真机
验收：CLI 连续更新期间目标 PID 保持不变，Runtime listener 与下一次 Android ID Hook 均读取到新配置。
