# 模块开发说明

Java/Kotlin 模块模板是独立 Git 子模块，也可单独克隆：

```sh
git clone https://github.com/ccccoisini/hook_template.git
cd hook_template
./gradlew packageHookModule
```

输出 `dist/<id>-<version>.zip`。模板按 core、hooks、config 分组，默认登记生命周期、固定 Android ID
和固定 GAID。新增业务实现 HookFeature 并加入 Main 的 HookRegistry 显式列表，使用 Reflect 查找成员、
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
监听通知相互隔离。默认模板已经移除远程配置示例，
固定标识集中在 DemoIdentifiers.kt，CLI 写入 enabled/android_id 不会改变其行为。

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
安装/卸载重试和回调结果异常，使用计数假链检查每次拦截最多执行一次下游；真机验收另见
[真机验收说明](DEVICE_TEST.md)。GAID 缺类允许跳过，必须观察 GAID_HOOK_APPLIED 才能确认其实际执行。

模板是独立 Git 子模块。交接时先提交模板仓库的源码、测试和文档，再由父仓库更新 gitlink，并提交
关联验收脚本及文档。父仓库提交不会自动包含子模块尚未提交的文件。
