# 模块开发说明

业务模块目录格式：

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

构建 Demo 模块：

```sh
./gradlew :demo-hook-module:buildHookDex
```

构建后 `module.dex` 已经混淆；`java_init.list` 会被自动改写为混淆后的入口类名，例如 `a.b`。模块依赖 API102 时只能使用 compileOnly/classpath，不能把 `io.github.libxposed.api.*` 打进业务 DEX。

只替换模块：

```sh
./scripts/build_hook_dex.sh
adb push demo-hook-module/build/outputs/hook/module.dex /sdcard/Download/hammer-demo.dex
adb shell su -c 'cp /sdcard/Download/hammer-demo.dex /data/adb/zygisk_framework/modules/hammer-demo/module.dex'
adb shell su -c 'sha256sum /data/adb/zygisk_framework/modules/hammer-demo/module.dex | cut -d" " -f1 > /data/adb/zygisk_framework/modules/hammer-demo/module.sha256'
```
