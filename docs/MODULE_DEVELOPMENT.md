# 模块开发说明

Java/Kotlin 模块模板是独立 Git 子模块，也可单独克隆：

```sh
git clone https://github.com/ccccoisini/hook_template.git
cd hook_template
./gradlew packageHookModule
```

输出 `dist/<id>-<version>.zip`。模板按 core、hooks、config 分组，默认登记生命周期、可配置 Android ID
和 GAID。新增业务实现 HookFeature 并加入 Main 的 HookRegistry 显式列表，使用 Reflect 查找成员、
HookInstaller 安装统一 before/after/replace 回调。完整接口与 Kotlin/Java 示例见
[模板开发与团队交接](../hook_template/DEVELOPMENT.md)。修改登记及
`app/src/main/resources/META-INF/xposed/` 中的模块属性、入口类和 scope 后构建。
默认 scope 是示例包名 `com.example.target`，发布前需要替换。

模块 ZIP 格式：

```text
/data/adb/zygisk_framework/modules/<module-id>/
├── module.dex
├── module.sha256
└── META-INF/xposed/
    ├── java_init.list
    ├── module.prop
    └── scope.list
```

入口类必须继承 `io.github.libxposed.api.XposedModule`，提供 public 无参构造方法，并避免在静态初始化里做重 I/O。

模块可在 `onModuleLoaded` 之后调用 `getRemotePreferences("<group>")`。返回对象在 Hook
进程中只读，支持标准六类 SharedPreferences 值和变更监听器；配置由设备端 root CLI 的
`prefs` 命令写入，运行中的目标进程无需重启即可收到更新。

同一模块需要分别配置多个目标 App 时，建议在 `onPackageReady` 获得包名后使用
`getRemotePreferences("settings.${packageName}")`，并让 CLI 写入相同 group。不同包的实例、缓存和
监听通知相互隔离。模板在 onPackageReady 中调用 ModuleConfig.initialize(context)，
由 config/ModuleConfig.kt 统一接收更新并发布不可变 ConfigSnapshot。业务在每次回调中调用
ModuleConfig.snapshot(packageName)，Kotlin 读取属性，Java 使用 getEnabled/getAndroidId/getGaid。
新增配置字段时，在 ConfigSnapshot 定义属性和默认值，再在 ModuleConfig 的字段表登记键名、中文名称、
解析规则和属性更新方式；读取、回退与日志由 ConfigSchema 自动处理，不需要修改订阅或刷新流程。
初始化日志展示完整配置，变化日志以中文展示具体字段的“旧值 → 新值”，异常也附带中文说明。
默认 enabled=true，android_id 为 16 位零值，gaid 为零 UUID；默认值集中在 DemoIdentifiers.kt。
enabled=false 时两个标识 Hook 透传，生命周期日志继续输出。删除键恢复默认值，类型/格式错误保留该键
上次有效值，读取/订阅失败保留快照或使用初始默认值。配置持久化由 CLI/框架负责，目标应用不能 edit 写入。
详细命令见 [CLI 使用指南](CLI_USAGE.md#remote-preferences)，完整读取示例见模板开发指南。

实时通道在 specialize 前由目标创建 memfd，再由 Root Companion 打开并初始化；目标只保留只读映射，
specialize 后不保留 companion socket 或共享内存 FD，也不要求 Zygisk 支持 `exemptFd()`。连续快速提交
可能合并中间版本；监听器始终按上次已观察
快照与最新完整快照之间的差异通知，并保证最终状态一致。

构建仓库子模块模板：

```sh
git submodule update --init hook_template
./scripts/build_hook_dex.sh
```

构建后 `module.dex` 已经混淆；`java_init.list` 会被自动改写为混淆后的入口类名，例如 `a.b`。模块依赖 API102 时只能使用 compileOnly/classpath，不能把 `io.github.libxposed.api.*` 打进业务 DEX。

设备端安装、查询、启停和删除命令请参阅独立的[CLI 使用指南](CLI_USAGE.md)。

## 开发验证和交接

在模板仓库运行 `./gradlew :app:testDebugUnitTest packageHookModule`。JVM 测试覆盖反射、筛选、
安装/卸载重试、回调结果异常，以及配置更新、并发快照与包名隔离，使用计数假链检查每次拦截最多执行一次下游；真机验收另见
[真机验收说明](DEVICE_TEST.md)。GAID 缺类允许跳过，必须观察 GAID_HOOK_APPLIED 才能确认其实际执行。

模板是独立 Git 子模块。交接时先提交模板仓库的源码、测试和文档，再由父仓库更新 gitlink，并提交
关联验收脚本及文档。父仓库提交不会自动包含子模块尚未提交的文件。
