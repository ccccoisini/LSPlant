# Xposed API 82 应用进程兼容性

本分支提供旧版 `de.robv.android.xposed` API 82 的应用进程内兼容实现。模块以
`module.dex + ZIP` 安装，由框架 CLI 管理。API 82 官方依赖只用于编译；运行时类由
`framework.dex` 中的兼容层提供，LSPlant/JNI 负责实际方法与构造器 Hook。

| 能力 | 状态 | 说明 |
|---|---|---|
| `IXposedHookLoadPackage` | 支持 | 真实 App ClassLoader 可用后、Application 创建前分发；Instrumentation Hook 不可用时在 `Application.attach` 前回退。 |
| `IXposedHookZygoteInit` | 有边界支持 | 每个命中模块在每个目标应用进程调用一次；这是 specialize 后的逐进程初始化，不是真正的 Zygote 阶段。 |
| 多入口类 | 支持 | 入口加载、构造和生命周期异常按入口隔离；模块包名映射冲突会拒绝后加载的模块并记录原因。 |
| 方法 Hook | 支持 | 每个 `Executable` 只安装一个 LSPlant Hook；回调按 API 82 优先级进入、逆序退出。 |
| 构造器 Hook | 支持 | 原方法 backup 在 ART 提供的当前实例上运行，不额外创建实例。 |
| 参数、结果和异常 | 支持 | 支持参数替换、提前返回、异常替换、`XC_MethodReplacement` 和回调故障隔离。 |
| `invokeOriginalMethod` | 支持 | 通过 LSPlant backup 绕过当前回调链。 |
| 批量 Hook | 支持 | `hookAllMethods` 和 `hookAllConstructors`。 |
| 卸载 | 支持 | 共享底层 Hook 按逻辑回调计数；活动调用结束后卸载，native 卸载失败保留回调并允许重试。 |
| `XposedHelpers` | 支持 | 使用官方 API 82 源码及其必要辅助实现；API/框架类由 framework loader 固定解析，目标类使用 App ClassLoader。 |
| `AndroidAppHelper` | 支持 | 提供当前包、进程、ApplicationInfo、Application 和配置访问；`initZygote` 阶段 Application 仍为 null。 |
| `XSharedPreferences` | 只读兼容 | 包名映射到相同名称的 CLI 配置组；支持快照、`reload()`、`hasFileChanged()`。旧 listener 不支持；`makeWorldReadable()` 返回 false。 |
| Remote Preferences 扩展 | 支持 | 模板通过 `FrameworkServices.getRemotePreferences(moduleId, group)` 反射调用，保留实时监听。 |
| 资源与布局 Hook | 不支持 | 只保留旧类型和入口声明以便模块类加载；实际调用抛 `UnsupportedOperationException`。 |
| 直接安装旧 APK | 不支持 | 使用模板或兼容构建流程输出本框架的 module ZIP。 |
| 真正的 Zygote 初始化、system_server | 不支持 | 不会向这些进程加载本框架。 |

模块元数据中的 `legacyPackageName` 可把旧 `XSharedPreferences(packageName, group)` 包名映射到 CLI 模块 ID；缺省时使用模块 ID。两个模块映射到同一包名时，后加载者会被单独跳过。标准 `/data/data`、`/data/user` 和 `/data/user_de` shared_prefs 文件路径可映射；其他路径明确报不支持。

API 102 模块在启动时按模块单独判定为不兼容并记录 `MODULE_ENTRY_FAILED code=INCOMPATIBLE_API`，不会阻止 API 82 模块继续加载。Android 最低版本为 API 26，ABI 为 arm64。
