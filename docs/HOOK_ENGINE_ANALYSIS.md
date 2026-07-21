# Hook Engine Analysis

## LSPlant 与 libart

当前工程仍以 LSPlant 作为 Java Hook 引擎。LSPlant 初始化需要解析 `libart.so` 内部符号，并通过 Dobby inline hook 后端接入 ART 运行时关键路径；这类行为可能表现为 ART 代码页权限变化或 trampoline/inline patch，因此检测侧可能认为 `libart.so` 被修改。

已做的缓解：

- `preAppSpecialize` 不再主动初始化 LSPlant。
- LSPlant 延迟到第一次 `NativeBridge.nativeHook` 或 `nativeDeoptimize` 时初始化。
- 目标命中但没有实际 Java Hook 前，不会因为 framework 启动阶段提前触发 LSPlant 初始化。

不能彻底规避的部分：

- 只要继续使用 LSPlant 的完整 Hook 能力，第一次安装 Java Hook 时仍需要进入 LSPlant 初始化和 ART method hook 流程。
- 仅调整 loader、dexElements 或 Root Companion 协议，不能把 LSPlant 的底层行为变成 Pine 的底层行为。

## Pine 的差异

`zygiskPine` 以 Pine runtime 自身的 trampoline/ArtMethod 处理为核心，运行路径和 LSPlant 不同。它仍会操作 ART method/runtime 状态，但不等价于当前 LSPlant + Dobby inline hook ART 符号的初始化方式，所以在部分检测上看起来更“少碰 libart”。

## 可选方向

- 保留 LSPlant：继续当前实现，优点是 API102 适配成本低；缺点是无法承诺完全不触达 `libart.so`。
- 引入 Pine 后端：抽象 `lsplant_engine` 为可切换 HookEngine，把 `Hook/Unhook/IsHooked/Deoptimize` 映射到 Pine；优点是检测特征更接近 `zygiskPine`，缺点是需要重新适配 Hook 链、backup method、deoptimize 和 API102 语义。
- 双后端：保留 LSPlant 为默认实现，增加 Pine experimental backend，通过构建开关或模块配置选择。
