# API 82 模块开发

`hook_template/` 是独立 Git 子模块，可单独克隆、构建和测试：

```sh
git clone https://github.com/ccccoisini/hook_template.git
cd hook_template
./gradlew :app:testDebugUnitTest packageHookModule
```

模板用官方 Maven `de.robv.android.xposed:api:82` 编译，依赖配置为 `compileOnly`。Release ZIP
只包含 `module.dex`、校验文件和 `META-INF/xposed` 元数据。旧 API 类型由框架 runtime 提供，Stub
和 `src/test` 夹具不会打进业务 DEX。Java/Kotlin 示例与配置热更新说明见
[模板开发与团队交接](../hook_template/DEVELOPMENT.md)。

入口类需要 public 无参构造，并实现一个或多个旧版接口：

```java
public final class Main implements IXposedHookZygoteInit, IXposedHookLoadPackage {
    @Override public void initZygote(StartupParam startupParam) throws Throwable { /* ... */ }
    @Override public void handleLoadPackage(LoadPackageParam param) throws Throwable { /* ... */ }
}
```

框架按模块 scope 选择进程，加载每个入口时独立捕获错误。命中的入口在该应用进程内调用一次
`initZygote`，随后在真实应用 `ClassLoader` 可用后、Application 创建前调用 `handleLoadPackage`。
如果 `Instrumentation.newApplication` Hook 不可用，则在 `Application.attach` 前回退；日志会标明
实际来源和时机。此处的 `initZygote` 是 specialize 后的逐进程预初始化，不是真正的 Zygote 阶段。
`LoadPackageParam` 含真实包名、进程名、`ApplicationInfo` 和目标 `ClassLoader`；应用的子进程也设置
`isFirstApplication=true`。

用旧版 `XposedHelpers.findClass/findMethodExact` 查找目标应用成员，再通过
`XposedBridge.hookMethod` 安装 `XC_MethodHook` 或 `XC_MethodReplacement`。方法与构造器均由 LSPlant
执行。每个成员只有一个底层 Hook，框架按 API 82 优先级分发 before、原调用和反向 after；参数、提前结果、
异常、回调故障、重复注册和卸载由 runtime 管理。`XposedBridge.invokeOriginalMethod` 直接走 LSPlant
backup。构造器 backup 初始化当前 ART 实例，不会另建对象。

模块 ZIP 元数据和入口：

```text
META-INF/xposed/java_init.list
META-INF/xposed/module.prop
META-INF/xposed/scope.list
```

`module.prop` 的 API 范围必须包含 82；`legacyPackageName` 是可选项，缺省为模块 ID。设置后，旧版
`XSharedPreferences(legacyPackageName, group)` 会把包名映射到该模块 ID，并读取同名 CLI 配置组。单参数
构造使用 `<legacyPackageName>_preferences` 组。文件构造只接受标准 `/data/data`、`/data/user` 或
`/data/user_de` 下已声明模块的 `shared_prefs/*.xml` 路径。`XSharedPreferences` 只读，支持快照、
`reload()` 和 `hasFileChanged()`；旧 API listener 不支持，`makeWorldReadable()` 返回 false。

模板为继续监听 CLI 实时更新，通过反射调用扩展
`FrameworkServices.getRemotePreferences(moduleId, group)`，这样业务 DEX 无需引用框架内部类。扩展
保留实时监听；旧版 `XSharedPreferences` 维持 listener 不支持的行为。不同模块与配置组互相隔离；多个模块
声明同一个 `legacyPackageName` 时，后加载模块会单独拒绝并记录冲突原因。

`hook_template/build.gradle.kts` 会检查 API 范围、模块 ID、入口、scope、单 DEX 和 32 MiB 上限，按
R8 mapping 改写 `java_init.list` 并计算 SHA-256。框架 CLI 安装同一 ZIP 契约。API 102 模块会在目标进程
逐个跳过并记录 `INCOMPATIBLE_API`，不影响 API 82 模块。完整边界见[兼容性矩阵](COMPATIBILITY.md)。

资源/布局 API 类型保留以支持旧入口类加载，但实际资源 Hook 会抛出明确的
`UnsupportedOperationException`。直接安装旧 APK、真正 Zygote Hook 和 `system_server` 不支持。
