# API 102 兼容性矩阵

| 能力 | 当前状态 | 说明 |
|---|---:|---|
| 模块入口与 attachFramework | 支持 | 每个模块独立 `FrameworkXposedInterface`。 |
| onModuleLoaded | 支持 | Java runtime 装载后分发一次。 |
| onPackageLoaded/onPackageReady | 支持 | `Application.attach(Context)` Bootstrap；失败时降级并记录 `LIFECYCLE_DEGRADED_MODE`。 |
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
| Remote Preferences/Files | 未支持 | 不设置 `PROP_CAP_REMOTE`。 |
| system_server | 未支持 | 不设置 `PROP_CAP_SYSTEM`。 |
| ART `.symtab` 完整解析 | 支持 | Native 侧通过 `dl_iterate_phdr + dlsym + ELF .dynsym/.symtab` 解析；stripped ART 缺符号时按 Android 15 LSPlant patch 降级。 |

本地已完成构建和 JVM 单元测试；真机验收需要在目标设备上运行 `scripts/verify_device.sh` 后生成 PASS 报告。
