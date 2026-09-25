# 模块开发说明

Java/Kotlin 模块模板是独立 Git 子模块，也可单独克隆：

```sh
git clone https://github.com/ccccoisini/hook_template.git
cd hook_template
./gradlew packageHookModule
```

输出 `dist/<id>-<version>.zip`。模板同时提供 Java 和 Kotlin 示例，修改入口实现以及
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

构建仓库子模块模板：

```sh
git submodule update --init hook_template
./scripts/build_hook_dex.sh
```

构建后 `module.dex` 已经混淆；`java_init.list` 会被自动改写为混淆后的入口类名，例如 `a.b`。模块依赖 API102 时只能使用 compileOnly/classpath，不能把 `io.github.libxposed.api.*` 打进业务 DEX。

设备端安装、查询、启停和删除命令请参阅独立的[CLI 使用指南](CLI_USAGE.md)。
