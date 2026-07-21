# Zygisk + LSPlant + libxposed API 102 Hook 框架实现需求与真机验收文档

> 当前实现已在后续优化中调整：`framework.dex`/`framework.mapping` 会导出为 native header 并编译进 Zygisk `.so`；Root Companion 不再下发 framework FD；framework 和业务模块 DEX 均通过临时 `dexElements` 挂载预加载，入口执行后恢复宿主原始 `dexElements`。本文后续部分保留了最初实现需求中的历史方案描述。

> 本文档用于交给代码开发 AI，要求其直接完成项目创建、编码、编译、打包、测试脚本和真机验收。
---

## 1. 项目目标

实现一个运行于 Zygisk 的 Android Java Hook 框架，核心组合如下：

- 进程注入：Zygisk；
- ART Java Hook：使用 **LSPosed/LSPlant 最新 `master` 源码自行编译**；
- Java 模块 API：兼容 `libxposed/api` **API 102**；
- Native Inline Hook 后端：如 LSPlant 初始化需要，必须使用用户指定的 **JingMatrix/Dobby**；
- Java Hook 业务代码必须可以作为独立工程开发，并单独编译为 DEX，由 Zygisk 框架动态装载；
- 全局 Hook 目标从 `/data/adb/hook/target.txt` 读取；
- 必须使用包名 `io.hammer.developmentenvironmentdetection` 完成 ADB 真机 Hook 验收；
- 所有公开类、公开构造方法和公开方法必须添加中文 `/** ... */` 文档注释。

最终项目不能只是 LSPlant 单方法 Demo，而应形成可持续开发的最小可用 Hook 框架，至少具备：

1. Zygisk 目标进程筛选；
2. LSPlant 初始化与 Java 方法 Hook；
3. libxposed API 102 核心 Hook API 适配；
4. 独立模块 DEX 构建与装载；
5. 模块入口、作用域和生命周期分发；
6. Hook 链、优先级、原方法调用、卸载和同 ID 原子替换；
7. ADB 真机自动验收脚本；
8. 完整中文开发、构建、安装和排错文档。

---

## 2. 已核对的上游基线

### 2.1 LSPlant

上游仓库：

```text
https://github.com/LSPosed/LSPlant
```

要求：

- 构建时拉取远端 `master` 最新提交；
- 必须执行递归子模块初始化，因为 LSPlant 源码依赖 DexBuilder 子模块；
- 必须从源码参与当前项目编译，禁止用 Maven Central 中旧 AAR、旧 `.so` 或未知预编译文件替代；
- 构建日志和产物中必须记录实际使用的 commit SHA；
- 为保证发布版本可复现，完成开发后在 `versions.lock` 中锁定已通过真机验收的 SHA；
- 后续执行 `update_lsplant.sh` 时才更新到新的远端 `master`，更新后必须重新运行全部单元测试和真机测试。

本文编写时核对到的 `master` HEAD 为：

```text
a96c7978a9fe578e0e265cc8e45cb117fa8250a3
```

该 SHA 只是本文编写时的参考值，开发 AI 必须在实际构建开始时再次查询远端，不得把它永久冒充为“最新版本”。

当前 LSPlant 源码构建特征：

- CMake 最低版本：3.28；
- C++ 标准：C++23；
- 当前上游 Gradle 配置使用 NDK `29.0.14206865`；
- 支持 Android API 21～37；
- 支持 `armeabi-v7a`、`arm64-v8a`、`x86`、`x86_64`、`riscv64`；
- 当前源码已启用 Android flexible page size 相关构建配置。

当前本工程打包策略只输出 `arm64-v8a`，不再打包 `armeabi-v7a`。

本框架因 libxposed API 102 的最低要求，整体最低 Android 版本设为 API 26。

### 2.2 libxposed API 102

上游仓库：

```text
https://github.com/libxposed/api
```

依赖版本：

```kotlin
implementation("io.github.libxposed:api:102.0.0")
```

独立 Hook 模块只能使用：

```kotlin
compileOnly("io.github.libxposed:api:102.0.0")
```

不得把另一份 `io.github.libxposed.api.*` 重复打进业务模块 DEX，否则会产生类身份冲突。

本文编写时核对到的仓库 HEAD 为：

```text
45e7c5cfe54725b6d828d8b7be65e22ce60c67e4
```

API 102 至少涉及：

- `XposedModule` 和模块生命周期；
- `XposedInterface.hook(Executable)`；
- Hook 优先级；
- Hook 异常模式；
- `Chain.proceed()`、修改参数和修改 `thisObject`；
- 原方法 Invoker；
- HookHandle 卸载；
- API 102 的 Hook ID 和原子替换；
- API 102 热重载相关生命周期接口。

### 2.3 Dobby

必须使用指定仓库：

```text
https://github.com/JingMatrix/Dobby
```

本文编写时查询到的最新提交参考值：

```text
05a09ac6807a6bb1726350e40ea4b127c1c79809
```

Dobby 必须从该仓库源码静态编译进入 Zygisk Native 库，建议关闭无关功能：

```cmake
set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(Plugin.ImportTableReplace OFF CACHE BOOL "" FORCE)
set(Plugin.Android.BionicLinkerUtil OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/Dobby dobby)
```

LSPlant 的 inline hook 回调使用：

- `DobbyHook`；
- `DobbyDestroy`。

不得在 LSPlant 已建立 ART Hook 后启用 Zygisk 的 `DLCLOSE_MODULE_LIBRARY`，否则 Native 代码被卸载后会造成崩溃。

---

## 3. 必须交付的目录结构

建议项目结构如下；可以微调名称，但职责不得混合：

```text
zygisk-lsplant-framework/
├── settings.gradle.kts
├── build.gradle.kts
├── gradle.properties
├── versions.lock
├── README.md
├── LICENSES/
│   ├── LSPlant-LGPL-3.0.txt
│   ├── Dobby-Apache-2.0.txt
│   └── libxposed-Apache-2.0.txt
├── third_party/
│   ├── LSPlant/                    # 上游最新源码，含递归子模块
│   └── Dobby/                      # JingMatrix/Dobby 源码
├── native-loader/
│   ├── build.gradle.kts
│   └── src/main/cpp/
│       ├── CMakeLists.txt
│       ├── zygisk_entry.cpp
│       ├── target_client.cpp
│       ├── companion.cpp
│       ├── fd_protocol.cpp
│       ├── lsplant_engine.cpp
│       ├── art_symbol_resolver.cpp
│       ├── dex_loader.cpp
│       ├── jni_bridge.cpp
│       └── include/
├── framework-runtime/
│   ├── build.gradle.kts
│   └── src/main/java/...           # API 102 框架实现，输出 framework.dex
├── hook-module-template/
│   ├── build.gradle.kts
│   ├── src/main/java/...
│   └── src/main/resources/META-INF/xposed/
├── demo-hook-module/
│   ├── build.gradle.kts
│   ├── src/main/java/...
│   └── src/main/resources/META-INF/xposed/
├── magisk-module/
│   ├── module.prop
│   ├── customize.sh
│   ├── post-fs-data.sh
│   ├── service.sh
│   └── zygisk/
├── scripts/
│   ├── prepare_sources.sh
│   ├── update_lsplant.sh
│   ├── build_all.sh
│   ├── build_hook_dex.sh
│   ├── install_device.sh
│   ├── verify_device.sh
│   ├── verify_comments.py
│   └── collect_logs.sh
└── dist/
    ├── zygisk-lsplant-framework-<version>.zip
    ├── framework.dex
    ├── demo-hook-module/
    │   ├── module.dex
    │   └── META-INF/xposed/...
    ├── build-info.json
    ├── SHA256SUMS
    └── device-verification-report.txt
```

---

## 4. 总体架构

### 4.1 分层原则

框架必须分为以下四层：

1. **Zygisk Native Loader**
   负责进程生命周期、目标筛选、文件描述符保活、LSPlant 初始化、DEX 装载和 JNI 注册。

2. **LSPlant Native Engine**
   使用最新 LSPlant 源码，Dobby 作为 inline hook/unhook 后端，并提供可靠的 `libart.so` 符号解析。

3. **libxposed API 102 Runtime**
   提供 `XposedInterface` 的框架侧实现、Hook 链和生命周期管理。

4. **独立 Hook Module DEX**
   业务 Hook 代码独立开发，仅依赖 API 102，编译后无需重新编译 Native 框架即可替换。

### 4.2 进程执行流程

目标流程必须如下：

```text
Zygisk onLoad
    ↓
preAppSpecialize
    ↓
读取 nice_name / app_data_dir
    ↓
通过 Root Companion 查询 /data/adb/hook/target.txt
    ↓
未命中：立即结束，不初始化 LSPlant，不装载 DEX
    ↓
命中：获取 framework.dex 和匹配模块 module.dex 的 FD
    ↓
对需要跨 specialization 使用的 FD 调用 exemptFd
    ↓
初始化 LSPlant + Dobby + ART Symbol Resolver
    ↓
postAppSpecialize
    ↓
创建 framework.dex 的 InMemoryDexClassLoader
    ↓
RegisterNatives 到 NativeBridge
    ↓
安装 Android 包生命周期 Bootstrap Hook
    ↓
目标 App ClassLoader 就绪
    ↓
创建 BridgeClassLoader 与独立模块 ClassLoader
    ↓
读取 java_init.list，实例化 XposedModule
    ↓
attachFramework
    ↓
分发 onModuleLoaded / onPackageLoaded / onPackageReady
    ↓
模块通过 libxposed API 102 安装 Hook
```

---

## 5. `/data/adb/hook/target.txt` 规范

### 5.1 固定路径

必须读取：

```text
/data/adb/hook/target.txt
```

不得改成只读取 Magisk 模块内部路径，也不得把目标包名硬编码进 C++。

### 5.2 文件格式

支持：

- UTF-8 文本；
- 一行一个包名或进程名；
- 空行忽略；
- 以 `#` 开头的行作为注释；
- 行首、行尾空格必须去除；
- 重复项去重；
- 单行最大 512 字节；
- 文件最大 64 KiB；
- 非法行只记录一次错误并跳过，不得导致 Zygote 崩溃。

真机测试文件内容：

```text
# Hook framework real-device acceptance target
io.hammer.developmentenvironmentdetection
```

### 5.3 匹配规则

一行普通包名：

```text
io.hammer.developmentenvironmentdetection
```

默认匹配：

```text
io.hammer.developmentenvironmentdetection
io.hammer.developmentenvironmentdetection:任意子进程名
```

如一行本身包含冒号，则视为精确进程规则，只匹配该进程。

不得使用模糊 `contains` 匹配，例如目标为 `com.demo.app` 时不能误匹配 `com.demo.application2`。

建议逻辑：

```text
processName == rule
或
rule 不含冒号且 processName.startsWith(rule + ":")
```

### 5.4 Root Companion

优先通过 `REGISTER_ZYGISK_COMPANION` 实现 Root Companion：

- Companion 读取 `/data/adb/hook/target.txt`；
- 根据 `stat` 的 inode、size、mtime 做缓存；
- 文件变化后自动刷新，编辑目标文件后只需强制停止并重新启动 App，不要求重启手机；
- `preAppSpecialize` 通过 `connectCompanion()` 发送进程名；
- Companion 返回是否命中以及需要装载的 DEX 文件描述符；
- DEX FD 使用 Unix Domain Socket 的 `SCM_RIGHTS` 传递；
- 通信协议必须包含 magic、version、operation、payloadLength 和状态码；
- 对长度、数量和 FD 数量进行上限校验；
- Companion 失败时必须 fail-closed，即不注入，不能默认对所有进程生效。

不建议在 `postAppSpecialize` 直接打开 `/data/adb/hook`，因为此时已经进入目标 App 沙箱和 SELinux 限制环境。

### 5.5 权限

安装脚本至少创建：

```sh
mkdir -p /data/adb/hook/modules
chown -R 0:0 /data/adb/hook
chmod 0755 /data/adb/hook
chmod 0644 /data/adb/hook/target.txt
```

模块 DEX 不得 world-writable：

```sh
find /data/adb/hook/modules -type d -exec chmod 0755 {} \;
find /data/adb/hook/modules -type f -exec chmod 0644 {} \;
```

运行时必须拒绝装载：

- 非普通文件；
- 文件 owner 不可信；
- 文件可被任意用户写入；
- 超过设定大小上限的 DEX；
- SHA-256 与模块清单不一致的 DEX。

---

## 6. 独立 Hook 模块格式

### 6.1 模块部署目录

每个业务模块使用独立目录：

```text
/data/adb/hook/modules/<module-id>/
├── module.dex
├── module.sha256
└── META-INF/xposed/
    ├── java_init.list
    ├── module.prop
    └── scope.list
```

示例：

```text
/data/adb/hook/modules/hammer-demo/
```

### 6.2 `java_init.list`

内容为模块入口类全名，一行一个：

```text
com.example.hook.demo.HammerDemoModule
```

入口类必须：

- 继承 `io.github.libxposed.api.XposedModule`；
- 提供 public 无参构造方法；
- 不包含静态初始化中的重型 I/O；
- 不自行打包另一份 libxposed API 类。

### 6.3 `module.prop`

MVP 示例：

```properties
minApiVersion=102
targetApiVersion=102
staticScope=true
autoHotReload=false
```

若热重载功能尚未真正实现，必须保持 `autoHotReload=false`，不得仅修改配置声称支持。

### 6.4 `scope.list`

测试模块内容：

```text
io.hammer.developmentenvironmentdetection
```

模块只有同时满足以下两项才可装载：

1. 当前进程命中 `/data/adb/hook/target.txt`；
2. 当前包或进程命中模块自己的 `scope.list`。

### 6.5 DEX 构建

业务模块工程依赖：

```kotlin
dependencies {
    compileOnly("io.github.libxposed:api:102.0.0")
}
```

必须提供 Gradle Task 或脚本：

```text
./gradlew :demo-hook-module:buildHookDex
```

输出：

```text
demo-hook-module/build/outputs/hook/module.dex
```

要求：

- 使用 D8；
- `min-api=26`；
- API 102 为 `compileOnly`，不得进入 DEX；
- 业务模块自己的依赖需要一并 dex 化；
- 禁止重复打入 Android framework 类；
- 构建结束打印 DEX SHA-256；
- 同时复制 `META-INF/xposed` 元数据；
- 提供 `installHookModule` Task 或 ADB 安装脚本，支持只替换模块 DEX。

---

## 7. Native Loader 实现要求

### 7.1 Zygisk API

使用官方发布的最新稳定 `zygisk.hpp`，不要直接复制 Magisk 仓库内部未稳定的私有头文件。

Native 入口至少实现：

- `onLoad`；
- `preAppSpecialize`；
- `postAppSpecialize`；
- Root Companion；
- 可选 `preServerSpecialize` / `postServerSpecialize`。

MVP 默认只处理普通 App，不注入 `system_server`。只有完成 system_server 独立测试后，才能设置 `PROP_CAP_SYSTEM`。

### 7.2 生命周期状态

每个 fork 后进程必须拥有独立状态对象，至少包含：

- 当前进程名；
- 是否目标进程；
- 是否 LSPlant 初始化成功；
- framework.dex FD；
- 模块描述列表和 module.dex FD；
- JavaVM；
- framework ClassLoader GlobalRef；
- NativeBridge Class GlobalRef；
- 已注册 Native 方法状态；
- 错误码和一次性日志标志。

禁止把某个 App 的 JNI GlobalRef 复用到另一个 fork 进程。

### 7.3 文件描述符

在 `preAppSpecialize` 中取得需要跨阶段使用的 FD 后：

- 调用 `api->exemptFd(fd)`；
- 检查返回值；
- 失败则停止本进程注入；
- `postAppSpecialize` 完成 mmap 或完整读取后立即关闭；
- 所有错误路径均关闭 FD；
- 不允许泄漏到 App 的长期运行阶段。

### 7.4 DEX 内存装载

Android API 26 及以上使用：

```text
dalvik.system.InMemoryDexClassLoader
```

要求：

- 通过只读 mmap 或受控 ByteBuffer 装载；
- 校验文件大小和 DEX magic；
- 校验 SHA-256；
- 不把 DEX 写入目标 App 私有目录；
- 不依赖目标 App 的外部存储权限；
- 装载完成后释放不再需要的 Native 映射；
- 错误不得导致目标 App 崩溃，必须记录明确日志后停止模块初始化。

---

## 8. LSPlant 与 Dobby 集成要求

### 8.1 编译方式

LSPlant 必须作为源码子工程静态链接到 Zygisk `.so`。

参考 CMake 结构：

```cmake
cmake_minimum_required(VERSION 3.28)
project(zygisk_lsplant_framework)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/../../../third_party/Dobby dobby)

set(LSPLANT_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(
    ${CMAKE_SOURCE_DIR}/../../../third_party/LSPlant/lsplant/src/main/jni
    lsplant
)

add_library(zygisk_hook SHARED
    zygisk_entry.cpp
    companion.cpp
    target_client.cpp
    fd_protocol.cpp
    lsplant_engine.cpp
    art_symbol_resolver.cpp
    dex_loader.cpp
    jni_bridge.cpp
)

target_link_libraries(zygisk_hook
    PRIVATE
    lsplant_static
    dobby
    log
    dl
)
```

实际路径和 target 名称应根据拉取到的最新源码校正，不能为了套用示例而修改上游 API。

### 8.2 LSPlant 初始化

目标进程中只允许初始化一次。

`lsplant::InitInfo` 至少设置：

- `inline_hooker`；
- `inline_unhooker`；
- `art_symbol_resolver`；
- `art_symbol_prefix_resolver`；
- 明确且唯一的 generated class/source/field/method 前缀，避免与其他框架冲突。

Dobby 回调要求：

```text
inline_hooker(target, hooker)
    → DobbyHook
    → 成功时返回 origin trampoline
    → 失败返回 nullptr

inline_unhooker(target)
    → DobbyDestroy
    → 返回明确 bool
```

禁止复制 LSPlant 测试代码中固定 `4096` 页面大小的处理。页面大小必须通过：

```cpp
sysconf(_SC_PAGESIZE)
```

动态获取，以兼容 4 KiB 和 16 KiB 页面设备。

### 8.3 ART Symbol Resolver

不能仅假设 `dlsym` 可以解析全部 ART 内部符号。

必须实现或集成可靠的 `libart.so` ELF 解析器：

- 通过 `dl_iterate_phdr` 查找当前进程实际加载的 `libart.so`；
- 兼容 APEX 路径和传统系统路径；
- 计算 load bias；
- 支持 `.dynsym`；
- 在文件存在时支持 `.symtab`；
- 支持按完整名称查找；
- 支持按前缀查找第一个匹配符号；
- 校验符号类型、地址范围和所在映射；
- 解析失败记录具体符号名，但不得输出无限重复日志；
- 不允许返回不属于 `libart.so` 映射范围的伪地址。

常见路径仅作为候选，不能硬编码为唯一值：

```text
/apex/com.android.art/lib64/libart.so
/apex/com.android.art/lib/libart.so
/system/lib64/libart.so
/system/lib/libart.so
```

### 8.4 Native 依赖检查

最终 Zygisk `.so` 不应依赖无法从目标进程解析的额外私有共享库。

构建后执行：

```sh
llvm-readelf -d libzygisk_hook.so
```

除 Android 系统库外，不应出现未随模块正确提供的 `NEEDED` 项。

优先将 C++ Runtime、LSPlant 和 Dobby 静态合并进最终 Zygisk `.so`。

---

## 9. Java ClassLoader 设计

### 9.1 问题

framework.dex 需要看到 API 102 类；业务 module.dex 同时需要看到：

- `io.github.libxposed.api.*`；
- framework runtime 类；
- 目标 App 的类；
- Android framework 类。

不能简单让 module.dex 只以系统 ClassLoader 为 parent，否则无法解析目标 App 类；也不能只以目标 App ClassLoader 为 parent，否则无法解析 framework.dex 中的 API 实现类。

### 9.2 必须实现 BridgeClassLoader

实现一个受控的 `BridgeClassLoader`：

- 持有 framework ClassLoader；
- 持有目标 App ClassLoader；
- `io.github.libxposed.api.*` 和框架内部受保护包名必须始终从 framework ClassLoader 加载；
- 目标 App 类优先交给 App ClassLoader；
- Android/Java 标准类遵循父加载器；
- 业务模块自己的类由 module 的 InMemoryDexClassLoader 加载；
- 禁止模块定义与 API 或框架内部包同名的类；
- 检测到重复类时拒绝装载并输出错误。

建议受保护前缀：

```text
io.github.libxposed.api.
com.example.zygiskhook.runtime.
```

实际框架包名由实现者统一定义。

### 9.3 NativeBridge

framework.dex 中定义 `NativeBridge`，由 Native 层在装载 class 后调用 `RegisterNatives`。

至少提供：

- native Hook；
- native Unhook；
- native IsHooked；
- native Deoptimize；
- native 获取构建信息；
- native 日志桥；
- 可选 native class initializer Hook；
- 可选 native 获取已注册 JNI 函数地址。

不得依赖 framework.dex 再次 `System.loadLibrary()` Zygisk 库。

---

## 10. libxposed API 102 Runtime 实现

## 10.1 实现策略

框架工程使用：

```kotlin
implementation("io.github.libxposed:api:102.0.0")
```

实现一个框架侧 `XposedInterface`，并为每个独立模块创建隔离的 `ModuleRuntime`。

模块入口实例化后：

1. 验证类继承 `XposedModule`；
2. 调用 public 无参构造；
3. 调用 `attachFramework(interfaceImpl, detachRunnable)`；
4. 分发生命周期；
5. 捕获模块异常，不能让异常穿过 Zygisk/ART 边界导致 App 崩溃。

### 10.2 Capability 属性

MVP 的 `getFrameworkProperties()` 必须真实返回，不得虚报：

- 未完成 system_server Hook：不要设置 `PROP_CAP_SYSTEM`；
- 未完成 remote preferences/files：不要设置 `PROP_CAP_REMOTE`；
- 动态 DEX 模块需要访问 API：不要设置 `PROP_RT_API_PROTECTION`。

### 10.3 一目标方法一个 LSPlant Native Hook

同一个 `Executable` 无论被多少模块 Hook，底层只安装一个 LSPlant Hook。

Java 层维护：

```text
HookRegistry
└── Executable → HookRecord
    ├── LSPlant backup Method
    ├── Dispatcher callback
    ├── immutable HookNode snapshot
    ├── native installed flag
    └── lock / generation
```

每个 `HookNode` 至少包含：

- 所属模块 ID；
- Hooker；
- priority；
- exceptionMode；
- 可空 ID；
- 创建序号；
- active 状态。

调用 Hook 时：

- 高 priority 先执行；
- 同 priority 保持稳定创建顺序；
- 调用期间使用不可变快照；
- 新增、删除或替换不能影响当前已经开始的调用；
- 不允许持锁执行用户 Hooker。

### 10.4 LSPlant 参数转换

LSPlant callback 的 `Object[]` 规则与 API 102 `Chain` 不同，框架必须转换：

- 非静态方法：LSPlant `args[0]` 是 `thisObject`；
- 非静态方法：API `Chain.getArgs()` 从 LSPlant `args[1]` 开始；
- 静态方法：LSPlant 参数没有空的 `thisObject` 占位；
- 构造方法：`args[0]` 是正在初始化的对象；
- API `Chain.getThisObject()` 对静态方法返回 `null`。

### 10.5 Chain

必须实现：

- `getExecutable()`；
- `getThisObject()`；
- `getArgs()`，返回不可修改 List；
- `getArg(index)`；
- `proceed()`；
- `proceed(newArgs)`；
- `proceedWith(newThis)`；
- `proceedWith(newThis, newArgs)`。

要求：

- 每个 Chain 仅允许在当前调用线程和当前调用栈中使用；
- Hook 回调返回后再次使用 Chain 必须抛出明确异常；
- 静态方法调用 `proceedWith` 必须报错；
- 参数数量和类型错误必须抛出与反射调用一致的异常；
- 到达 Hook 链末尾时调用 LSPlant backup Method；
- 原方法异常需要解包 `InvocationTargetException` 并传播真实 cause；
- 原方法只能执行调用方要求的次数，框架不能隐式重复执行。

### 10.6 ExceptionMode

实现：

- `DEFAULT`；
- `PROTECTIVE`；
- `PASSTHROUGH`。

`PROTECTIVE` 必须符合 API 102 行为：

- Hooker 在调用 `proceed()` 前抛出异常：记录异常，并跳过该 Hook，继续后续链；
- Hooker 在 `proceed()` 后抛出异常：保留已经取得的下游结果或异常；
- `proceed()` 本身抛出的原方法或下游异常始终传播；
- 防护逻辑不能吞掉原方法异常。

开发 AI 必须为上述每种情况编写单元测试。

### 10.7 Hook ID 与原子替换

API 102 的 `setId(id)` 必须实现：

- 唯一范围：同一模块、同一 `Executable`、同一 ID；
- 新 Hook 使用相同 ID 时原子替换旧 Hook；
- 当前正在执行的调用继续使用旧快照；
- 下一次调用看到新 Hook；
- 旧 HookHandle 失效；
- 不同模块允许使用相同 ID；
- `HookHandle.replaceHook()` 保留 executable、priority、exceptionMode 和 ID。

### 10.8 Unhook

`HookHandle.unhook()` 必须幂等：

- 多次调用不崩溃；
- 只删除对应逻辑 Hook；
- HookRecord 中仍有其他 Hook 时不得卸载 LSPlant Native Hook；
- 最后一个逻辑 Hook 删除后才调用 `lsplant::UnHook`；
- Native Unhook 后不得再调用旧 backup Method；
- 并发调用中使用的旧快照允许安全结束，底层 Native Unhook 时机必须避免悬空 backup。

建议使用调用计数或延迟 Native Unhook，直到旧快照无活动调用。

### 10.9 Invoker

至少实现：

- Method Invoker；
- `Invoker.Type.ORIGIN`；
- `Invoker.Type.Chain.FULL`；
- 指定 `maxPriority` 的 Chain 调用；
- 绕过 Java access check；
- 对静态和实例方法正确处理 receiver。

Constructor Invoker 和 `invokeSpecial/newInstanceSpecial` 难度较高，但若声明“API 102 完整兼容”则必须实现并测试：

- 构造 Hook 的 backup 是可在现有对象上执行初始化逻辑的 Method；
- 原始构造实例创建不能通过临时 Unhook 实现；
- 需要安全的对象分配与 backup constructor 调用策略；
- `newInstanceSpecial` 必须验证父子类关系；
- 不允许返回未初始化或类型错误对象。

若第一阶段尚未实现，必须在 `COMPATIBILITY.md` 明确标记为未支持，并且项目名称、README 和日志不得声称“完整 API 102 兼容”。

### 10.10 Deoptimize

`deoptimize(Executable)` 调用 `lsplant::Deoptimize`。

要求：

- 返回真实成功状态；
- 不把失败静默改成成功；
- 日志包含目标类和方法；
- Demo 不应无条件对整个系统大量 deoptimize；
- 仅在 Hook 因 ART inline 未触发且已确认 caller 时使用。

### 10.11 Class Initializer

`hookClassInitializer(Class<?>)` 属于 API 102 接口。

若实现完整兼容，必须：

- 在 class 初始化前安装；
- 已经初始化的 class 明确记录无法触发；
- Chain 中 executable、thisObject、args 和返回值符合 API 定义；
- 不能通过伪造普通 Method 冒充成功。

若 LSPlant 当前公开接口不足以稳定实现，第一阶段可以明确返回 `HookFailedError`，但必须记录在兼容性矩阵中。

### 10.12 Remote API

MVP 可以不支持：

- `getRemotePreferences`；
- `listRemoteFiles`；
- `openRemoteFile`。

不支持时：

- 不设置 `PROP_CAP_REMOTE`；
- 抛出 `UnsupportedOperationException`；
- 文档明确说明。

---

## 11. Android 包生命周期适配

### 11.1 不能只在 `postAppSpecialize` 假设 App ClassLoader 已就绪

Zygisk `postAppSpecialize` 很早，目标 App 的最终 ClassLoader 和自定义 `AppComponentFactory` 可能尚未建立。

必须通过 LSPlant 先对 Android framework 的加载生命周期安装 Bootstrap Hook，再分发：

- `onPackageLoaded`；
- `onPackageReady`。

### 11.2 推荐策略

针对不同 Android 版本建立适配层：

- 监听 `LoadedApk` 默认 ClassLoader 创建或取得时机；
- 监听 `AppComponentFactory.instantiateClassLoader` 或对应内部路径；
- 获取最终 App ClassLoader 后触发 `onPackageReady`；
- 每个 package 每个进程只触发一次；
- shared UID、`createPackageContext(...CONTEXT_INCLUDE_CODE)` 场景不能混淆主包；
- Android 内部方法变化时使用版本策略类，不在一处堆积大量 SDK 判断。

Fallback：

- 可在 `Application.attach(Context)` 取得 Context 和 ClassLoader；
- 使用 fallback 时必须日志标记 `LIFECYCLE_DEGRADED_MODE`；
- 不得在 README 中把 fallback 的时序描述成完全等同官方 API 102 生命周期。

### 11.3 目标测试

对 `io.hammer.developmentenvironmentdetection` 至少确认：

- `onModuleLoaded` 调用一次；
- `onPackageLoaded` 调用一次；
- `onPackageReady` 调用一次；
- ClassLoader 可以成功 `Class.forName` 目标 App 类或至少读取目标 App `Application` 类；
- 多次打开 Activity 不重复装载模块入口。

---

## 12. Demo Hook 模块

Demo 模块必须使用 API 102，而不是直接调用自定义 JNI Hook 接口。

### 12.1 入口类示例要求

示例结构：

```java
package com.example.hook.demo;

import android.app.Application;
import android.app.Instrumentation;
import android.util.Log;

import androidx.annotation.NonNull;

import java.lang.reflect.Method;

import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.XposedModule;

/**
 * 用于真机验收的 API 102 Hook 模块入口。
 */
public final class HammerDemoModule extends XposedModule {

    private static final String TAG = "ZHook.Module";
    private static final String TARGET_PACKAGE =
            "io.hammer.developmentenvironmentdetection";

    /**
     * 创建真机验收模块入口。
     */
    public HammerDemoModule() {
    }

    /**
     * 在模块代码装载到目标进程后记录框架和进程信息。
     *
     * @param param 当前进程的模块装载参数
     */
    @Override
    public void onModuleLoaded(@NonNull ModuleLoadedParam param) {
        log(Log.INFO, TAG, "DEMO_MODULE_LOADED process=" + param.getProcessName());
    }

    /**
     * 在目标包最终类加载器就绪后安装 Application 创建 Hook。
     *
     * @param param 当前包及其类加载器信息
     */
    @Override
    public void onPackageReady(@NonNull PackageReadyParam param) {
        if (!TARGET_PACKAGE.equals(param.getPackageName())) {
            return;
        }

        try {
            Method method = Instrumentation.class.getDeclaredMethod(
                    "callApplicationOnCreate", Application.class);

            hook(method)
                    .setId("hammer-demo/application-on-create")
                    .setPriority(XposedInterface.PRIORITY_DEFAULT)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(new ApplicationCreateHook());

            log(Log.INFO, TAG, "DEMO_HOOK_INSTALLED method=" + method);
        } catch (ReflectiveOperationException exception) {
            log(Log.ERROR, TAG, "DEMO_HOOK_INSTALL_FAILED", exception);
        }
    }

    /**
     * 验证 Hook 链、原方法调用和参数读取是否正常。
     */
    private final class ApplicationCreateHook implements XposedInterface.Hooker {

        /**
         * 在 Application.onCreate 调用前后输出真机验收日志。
         *
         * @param chain 当前 Hook 调用链
         * @return 原方法返回值；该目标方法正常返回 null
         * @throws Throwable 原方法或后续 Hook 抛出的异常
         */
        @Override
        public Object intercept(@NonNull XposedInterface.Chain chain) throws Throwable {
            Application application = (Application) chain.getArg(0);
            log(Log.INFO, TAG,
                    "DEMO_BEFORE package=" + application.getPackageName());

            Object result = chain.proceed();

            log(Log.INFO, TAG,
                    "DEMO_AFTER package=" + application.getPackageName());
            return result;
        }
    }
}
```

开发 AI 应根据最终 API 和项目包名修正 import，但不得把示例改成直接调用 LSPlant JNI，从而绕开 API 102 Runtime。

### 12.2 Demo 验证点

必须看到：

```text
DEMO_MODULE_LOADED
DEMO_HOOK_INSTALLED
DEMO_BEFORE package=io.hammer.developmentenvironmentdetection
DEMO_AFTER package=io.hammer.developmentenvironmentdetection
```

并确认目标 App 正常进入主界面，无崩溃、无无限递归。

---

## 13. 中文注释硬性要求

### 13.1 Java/Kotlin

所有以下元素必须使用中文 `/** ... */`：

- public class/interface/enum/record；
- public constructor；
- public method；
- protected method；
- 所有 `@Override public` 生命周期和 API 实现方法；
- 对外暴露的 public field；
- JNI 对应 Java native 方法。

方法注释至少说明：

- 方法作用；
- `@param`；
- 有返回值时 `@return`；
- 可能抛异常时 `@throws`；
- 并发或生命周期限制，如适用。

不接受只写：

```java
/** 方法。 */
```

必须提供能帮助维护者理解行为的中文说明。

### 13.2 C/C++

所有对外头文件函数、公开类方法、JNI Export 和 Zygisk/Companion 入口前必须使用：

```cpp
/**
 * 中文说明。
 *
 * @param ...
 * @return ...
 */
```

上游第三方源码不要求补注释，禁止为了满足检查而大规模改写 LSPlant 或 Dobby 上游代码。

### 13.3 自动检查

提供：

```text
scripts/verify_comments.py
```

检查本项目源码，不扫描 `third_party`、生成目录和构建目录。

验收命令：

```sh
python3 scripts/verify_comments.py
```

必须返回 0，报告：

```text
PUBLIC_DOCUMENTATION_CHECK: PASS
missing=0
```

---

## 14. 日志规范

统一 Tag：

```text
ZHook.Native
ZHook.Runtime
ZHook.Module
ZHook.Companion
```

关键成功日志：

```text
TARGET_CONFIG_LOADED
TARGET_MATCH process=io.hammer.developmentenvironmentdetection
DEX_FD_RECEIVED
LSPLANT_INIT_OK
FRAMEWORK_DEX_LOADED
NATIVE_BRIDGE_REGISTERED
PACKAGE_CLASSLOADER_READY
MODULE_DEX_LOADED id=hammer-demo
MODULE_ENTRY_LOADED
HOOK_INSTALLED
DEMO_BEFORE
DEMO_AFTER
```

关键失败日志必须带错误码：

```text
TARGET_CONFIG_ERROR code=...
DEX_VERIFY_FAILED code=...
LSPLANT_INIT_FAILED code=...
ART_SYMBOL_NOT_FOUND symbol=...
MODULE_ENTRY_FAILED code=...
HOOK_INSTALL_FAILED code=...
```

要求：

- 日志不能输出用户敏感数据；
- 不在高频 Hook 中无上限刷日志；
- 同一初始化失败默认只输出一次完整堆栈；
- Release 构建可降低调试日志，但验收日志必须保留；
- 日志中打印 LSPlant、Dobby、API 版本和 commit SHA。

---

## 15. 构建要求

### 15.1 环境

建议：

- JDK 21；
- Android SDK Platform 35 或更新且兼容的稳定版本；
- Android Build Tools 35.0.0 或与当前 LSPlant 上游一致版本；
- Android NDK `29.0.14206865`，若上游更新则跟随并记录；
- CMake 3.28 或更新稳定版本；
- Ninja；
- Python 3；
- Git；
- ADB。

### 15.2 ABI

强制构建：

```text
arm64-v8a
armeabi-v7a
```

原因：目标 App 可能运行在 64 位或 32 位进程。

可选：

```text
x86
x86_64
riscv64
```

安装脚本应根据设备 ABI 选择正确 Zygisk `.so`。

### 15.3 更新上游脚本

`prepare_sources.sh` 必须：

1. 克隆或更新 LSPlant；
2. `git fetch origin master`；
3. checkout 到 `origin/master` 的 detached HEAD；
4. `git submodule update --init --recursive`；
5. 克隆或更新 JingMatrix/Dobby；
6. 写入 `versions.lock` 或临时 build info；
7. 输出 commit SHA；
8. 工作区存在未提交第三方修改时停止，防止覆盖本地 patch。

建议命令逻辑：

```sh
git -C third_party/LSPlant fetch origin master
git -C third_party/LSPlant checkout --detach origin/master
git -C third_party/LSPlant submodule sync --recursive
git -C third_party/LSPlant submodule update --init --recursive
```

### 15.4 一键构建

必须支持：

```sh
./scripts/build_all.sh
```

或：

```sh
./gradlew clean assembleRelease buildHookDex packageMagiskModule
```

输出：

- Magisk 模块 ZIP；
- 各 ABI Zygisk `.so`；
- framework.dex；
- demo module.dex；
- 模块元数据；
- build-info.json；
- SHA256SUMS；
- Native 未剥离符号文件；
- ProGuard/R8 mapping，如启用混淆。

### 15.5 `build-info.json`

至少包含：

```json
{
  "frameworkVersion": "0.1.0",
  "libxposedApi": "102.0.0",
  "lsplantCommit": "实际 SHA",
  "dobbyCommit": "实际 SHA",
  "zygiskApi": "实际版本",
  "ndkVersion": "实际版本",
  "cmakeVersion": "实际版本",
  "abis": ["arm64-v8a"],
  "buildTimeUtc": "ISO-8601"
}
```

---

## 16. 单元测试和宿主测试

至少编写以下测试：

### 16.1 配置解析

- 空文件；
- 注释；
- 空格；
- CRLF；
- 重复包名；
- 主进程匹配；
- 子进程匹配；
- 精确子进程规则；
- 相似包名不误匹配；
- 超长行；
- 超大文件；
- 文件更新缓存刷新。

### 16.2 模块元数据

- 合法 `java_init.list`；
- 空入口；
- 非法类名；
- API 版本过高或过低；
- scope 命中与不命中；
- DEX SHA-256 不匹配；
- 重复模块 ID。

### 16.3 Hook 链

- 单 Hook；
- 多 Hook priority 顺序；
- 同 priority 稳定顺序；
- `proceed()`；
- 修改参数；
- 修改 receiver；
- 静态方法；
- 实例方法；
- void 方法；
- primitive 返回值；
- object 返回值；
- 原方法抛异常；
- Hooker 抛异常；
- PROTECTIVE；
- PASSTHROUGH；
- 重复调用 proceed；
- 递归调用；
- 多线程并发调用。

### 16.4 HookHandle

- 多次 unhook；
- 最后 Hook 删除后 Native unhook；
- 相同 ID 替换；
- 不同模块相同 ID；
- replaceHook；
- 旧 handle 失效；
- 替换时进行中的调用使用旧快照。

### 16.5 ClassLoader

- 模块解析 API 102 类；
- 模块解析目标 App 类；
- 模块无法覆盖 API 类；
- 两个模块同名业务类互不污染；
- 模块卸载后无不必要强引用；
- 错误模块不影响其他模块。

---

## 17. ADB 真机验收

## 17.1 前置条件

真机必须满足：

- Android API 26 或以上；
- bootloader/root 环境已准备；
- Magisk Zygisk 已启用；
- ADB 可连接；
- 已安装测试 App：

```text
io.hammer.developmentenvironmentdetection
```

先确认 ABI：

```sh
adb shell getprop ro.product.cpu.abilist
adb shell dumpsys package io.hammer.developmentenvironmentdetection \
  | grep -E 'primaryCpuAbi|secondaryCpuAbi'
```

### 17.2 安装 Magisk 模块

```sh
adb push dist/zygisk-lsplant-framework-0.1.0.zip /sdcard/Download/
adb shell su -c 'magisk --install-module /sdcard/Download/zygisk-lsplant-framework-0.1.0.zip'
adb reboot
adb wait-for-device
```

重启后确认：

```sh
adb shell su -c 'magisk -V'
adb shell su -c 'ls -la /data/adb/modules/zygisk_lsplant_framework'
```

### 17.3 写入目标配置

```sh
adb shell su -c 'mkdir -p /data/adb/hook'
adb shell su -c 'printf "%s\n" "io.hammer.developmentenvironmentdetection" > /data/adb/hook/target.txt'
adb shell su -c 'chown 0:0 /data/adb/hook/target.txt'
adb shell su -c 'chmod 0644 /data/adb/hook/target.txt'
adb shell su -c 'cat /data/adb/hook/target.txt'
```

输出必须为：

```text
io.hammer.developmentenvironmentdetection
```

### 17.4 安装独立 Demo DEX

```sh
adb shell su -c 'mkdir -p /data/adb/hook/modules/hammer-demo/META-INF/xposed'

adb push dist/demo-hook-module/module.dex /sdcard/Download/hammer-demo.dex

adb shell su -c 'cp /sdcard/Download/hammer-demo.dex /data/adb/hook/modules/hammer-demo/module.dex'

adb push dist/demo-hook-module/META-INF/xposed/java_init.list /sdcard/Download/java_init.list
adb push dist/demo-hook-module/META-INF/xposed/module.prop /sdcard/Download/module.prop
adb push dist/demo-hook-module/META-INF/xposed/scope.list /sdcard/Download/scope.list

adb shell su -c 'cp /sdcard/Download/java_init.list /data/adb/hook/modules/hammer-demo/META-INF/xposed/java_init.list'
adb shell su -c 'cp /sdcard/Download/module.prop /data/adb/hook/modules/hammer-demo/META-INF/xposed/module.prop'
adb shell su -c 'cp /sdcard/Download/scope.list /data/adb/hook/modules/hammer-demo/META-INF/xposed/scope.list'

adb shell su -c 'sha256sum /data/adb/hook/modules/hammer-demo/module.dex | cut -d" " -f1 > /data/adb/hook/modules/hammer-demo/module.sha256'
adb shell su -c 'chown -R 0:0 /data/adb/hook/modules/hammer-demo'
adb shell su -c 'find /data/adb/hook/modules/hammer-demo -type d -exec chmod 0755 {} \;'
adb shell su -c 'find /data/adb/hook/modules/hammer-demo -type f -exec chmod 0644 {} \;'
```

### 17.5 启动测试 App

```sh
adb shell am force-stop io.hammer.developmentenvironmentdetection
adb logcat -c
adb shell monkey -p io.hammer.developmentenvironmentdetection \
  -c android.intent.category.LAUNCHER 1
sleep 5
adb shell pidof io.hammer.developmentenvironmentdetection
```

### 17.6 查看验收日志

```sh
adb logcat -d \
  -s ZHook.Native:V ZHook.Runtime:V ZHook.Module:V ZHook.Companion:V AndroidRuntime:E
```

必须包含：

```text
TARGET_MATCH process=io.hammer.developmentenvironmentdetection
LSPLANT_INIT_OK
FRAMEWORK_DEX_LOADED
MODULE_DEX_LOADED id=hammer-demo
MODULE_ENTRY_LOADED
DEMO_HOOK_INSTALLED
DEMO_BEFORE package=io.hammer.developmentenvironmentdetection
DEMO_AFTER package=io.hammer.developmentenvironmentdetection
```

### 17.7 验证目标 App 稳定性

```sh
adb shell pidof io.hammer.developmentenvironmentdetection
adb logcat -d | grep -E 'FATAL EXCEPTION|Fatal signal|Abort message|zygisk_hook|lsplant|dobby'
```

通过条件：

- App 进程仍存在；
- App 可进入可操作界面；
- 无 `FATAL EXCEPTION`；
- 无 Native `Fatal signal 6/11`；
- 无 Hook 无限递归；
- 原方法正常执行；
- Application 创建只出现一组符合预期的 BEFORE/AFTER。

### 17.8 验证非目标进程不注入

将任意非目标 App 强制停止并打开，例如系统设置：

```sh
adb logcat -c
adb shell am force-stop com.android.settings
adb shell monkey -p com.android.settings -c android.intent.category.LAUNCHER 1
sleep 3
adb logcat -d -s ZHook.Native:V ZHook.Runtime:V ZHook.Module:V
```

不得出现：

```text
TARGET_MATCH process=com.android.settings
LSPLANT_INIT_OK process=com.android.settings
MODULE_ENTRY_LOADED process=com.android.settings
```

### 17.9 验证配置热刷新

把目标文件临时改成不存在的包：

```sh
adb shell su -c 'printf "%s\n" "com.example.not.target" > /data/adb/hook/target.txt'
adb shell am force-stop io.hammer.developmentenvironmentdetection
adb logcat -c
adb shell monkey -p io.hammer.developmentenvironmentdetection \
  -c android.intent.category.LAUNCHER 1
sleep 3
```

不得出现目标匹配和 Hook 日志。

恢复：

```sh
adb shell su -c 'printf "%s\n" "io.hammer.developmentenvironmentdetection" > /data/adb/hook/target.txt'
```

此测试用于证明 Companion 会根据 mtime 刷新配置，不要求重启手机。

### 17.10 验证 DEX 可独立替换

修改 Demo Java Hook 日志，例如把版本从 `demo-v1` 改为 `demo-v2`，只执行：

```sh
./gradlew :demo-hook-module:buildHookDex
adb push demo-hook-module/build/outputs/hook/module.dex /sdcard/Download/hammer-demo.dex
adb shell su -c 'cp /sdcard/Download/hammer-demo.dex /data/adb/hook/modules/hammer-demo/module.dex'
adb shell su -c 'sha256sum /data/adb/hook/modules/hammer-demo/module.dex | cut -d" " -f1 > /data/adb/hook/modules/hammer-demo/module.sha256'
adb shell am force-stop io.hammer.developmentenvironmentdetection
adb shell monkey -p io.hammer.developmentenvironmentdetection -c android.intent.category.LAUNCHER 1
```

通过条件：

- 不重新编译 Zygisk Native `.so`；
- 不重新安装 Magisk 模块；
- 新进程加载新 module.dex；
- 日志出现 `demo-v2`；
- 旧 DEX 逻辑不再执行。

---

## 18. 自动化真机验收脚本

必须提供：

```text
scripts/verify_device.sh
```

脚本参数示例：

```sh
./scripts/verify_device.sh \
  --serial DEVICE_SERIAL \
  --package io.hammer.developmentenvironmentdetection \
  --module hammer-demo
```

脚本必须自动完成：

1. 检查 ADB；
2. 检查 root；
3. 检查 API level；
4. 检查 ABI；
5. 检查 Zygisk 模块文件；
6. 写入 `target.txt`；
7. 安装 demo module.dex 和元数据；
8. force-stop；
9. 清空 logcat；
10. 启动 App；
11. 等待 PID；
12. 收集日志；
13. 检查全部成功 marker；
14. 检查崩溃 marker；
15. 执行非目标进程测试；
16. 生成报告。

输出：

```text
dist/device-verification-report.txt
```

报告至少包含：

- 设备型号；
- Android 版本和 API；
- ABI；
- Magisk/Zygisk 版本；
- framework version；
- LSPlant SHA；
- Dobby SHA；
- module.dex SHA-256；
- 目标 PID；
- 每个验收项 PASS/FAIL；
- 关键日志；
- 最终结论。

最终成功输出：

```text
REAL_DEVICE_HOOK_ACCEPTANCE: PASS
TARGET_PACKAGE: io.hammer.developmentenvironmentdetection
```

只要任何关键 marker 缺失或存在崩溃，脚本必须非 0 退出。

---

## 19. 稳定性和安全边界

必须做到：

- 目标配置读取失败时不注入；
- LSPlant 初始化失败时不继续加载模块；
- 单个业务模块失败不影响其他模块；
- Java Hooker 异常按 ExceptionMode 处理；
- JNI 层每次调用后检查并处理 pending exception；
- JNI LocalRef 在循环中及时释放；
- GlobalRef 有明确所有权和释放时机；
- 不从错误线程复用 `JNIEnv*`；
- Native 线程通过 JavaVM Attach/Detach；
- 不在 Zygote 父进程持有目标 App 对象；
- 不在 signal handler 执行复杂 JNI；
- 不使用固定页面大小；
- 支持 16 KiB page size；
- Release 构建关闭 Dobby debug；


---

## 20. API 102 兼容性分级

必须提供 `COMPATIBILITY.md`，逐项记录：

| 能力 | MVP 必须 | 完整兼容要求 |
|---|---:|---:|
| 模块入口与 attachFramework | 是 | 是 |
| onModuleLoaded | 是 | 是 |
| onPackageLoaded/onPackageReady | 是 | 是 |
| Method Hook | 是 | 是 |
| Constructor Hook | 是 | 是 |
| 多 Hook 链 | 是 | 是 |
| priority | 是 | 是 |
| ExceptionMode | 是 | 是 |
| proceed/参数替换 | 是 | 是 |
| Method ORIGIN Invoker | 是 | 是 |
| HookHandle.unhook | 是 | 是 |
| API 102 Hook ID 替换 | 是 | 是 |
| deoptimize | 是 | 是 |
| Constructor Invoker | 可延期但必须声明 | 是 |
| invokeSpecial/newInstanceSpecial | 可延期但必须声明 | 是 |
| hookClassInitializer | 可延期但必须声明 | 是 |
| Hot Reload | 可延期且关闭配置 | 是 |
| Remote Preferences/Files | 可延期且不设置 capability | 是 |
| system_server | 可延期且不设置 capability | 是 |

第一版真机验收至少达到 MVP 全部“是”项目。

---

## 21. 最终交付物

开发 AI 必须交付：

1. 完整可编译源码；
2. 最新 LSPlant 源码编译集成；
3. JingMatrix/Dobby 源码编译集成；
4. libxposed API 102 Runtime；
5. 独立 Java Hook 模块模板；
6. 针对测试包的 Demo Hook DEX；
7. Magisk/Zygisk 模块 ZIP；
8. `target.txt` 解析和 Companion；
9. 单元测试；
10. 一键构建脚本；
11. 一键安装脚本；
12. 一键 ADB 真机验收脚本；
13. 真机验收报告；
14. `README.md`；
15. `BUILD.md`；
16. `MODULE_DEVELOPMENT.md`；
17. `DEVICE_TEST.md`；
18. `COMPATIBILITY.md`；
19. `TROUBLESHOOTING.md`；

---

## 22. 禁止以以下结果作为完成

以下情况均视为未完成：

- 只写架构说明，没有代码；
- 只编译成功，没有真机 Hook；
- 只直接调用 LSPlant，没有 API 102 适配；
- Java Hook 代码仍编译在 Zygisk `.so` 对应主工程中，不能单独输出 DEX；
- 目标包名硬编码，未读取 `/data/adb/hook/target.txt`；
- 使用旧版或预编译 LSPlant；
- 使用非 JingMatrix/Dobby；
- 只 Hook 自建 JVM 单元测试类，没有测试指定真机包；
- 只出现“模块已加载”日志，没有实际 Hook 前后日志；
- Hook 后没有调用原方法导致 App 启动逻辑被破坏；
- 非目标 App 也初始化 LSPlant；
- App 有崩溃或 ANR；
- 公开方法缺少中文 `/** ... */` 注释；
- 把未实现 capability 虚报为已支持；
- 未生成真机验收报告。

---

## 23. 最终验收清单

开发 AI 完成后必须逐项给出证据：

```text
[ ] LSPlant 来自构建时最新 master 源码
[ ] 已记录 LSPlant commit SHA
[ ] LSPlant 子模块完整
[ ] Dobby 来自 JingMatrix/Dobby
[ ] API 版本为 102.0.0
[ ] framework.dex 构建成功
[ ] module.dex 可独立构建
[ ] module.dex 未包含重复 API 类
[ ] target.txt 路径正确
[ ] 主进程和子进程匹配正确
[ ] 非目标进程不初始化 LSPlant
[ ] DobbyHook/DobbyDestroy 后端正常
[ ] ART 符号解析成功
[ ] LSPlant 初始化成功
[ ] Hook 链顺序测试通过
[ ] proceed 原方法测试通过
[ ] 参数替换测试通过
[ ] ExceptionMode 测试通过
[ ] Hook ID 原子替换测试通过
[ ] Unhook 幂等测试通过
[ ] 中文公开方法注释检查通过
[ ] arm64-v8a 构建通过
[ ] armeabi-v7a 构建通过
[ ] 16 KiB 页面实现无固定 4096 假设
[ ] Magisk 模块安装成功
[ ] 指定 App 进程启动成功
[ ] DEMO_BEFORE 日志存在
[ ] DEMO_AFTER 日志存在
[ ] 指定 App 无 Java 崩溃
[ ] 指定 App 无 Native 崩溃
[ ] 非目标 App 无注入日志
[ ] 修改 target.txt 后无需重启即可生效
[ ] 只替换 module.dex 后新逻辑生效
[ ] device-verification-report.txt 结论为 PASS
```

最终只有同时满足以下条件才可宣布完成：

```text
BUILD: PASS
UNIT_TESTS: PASS
PUBLIC_DOCUMENTATION_CHECK: PASS
MAGISK_MODULE_INSTALL: PASS
REAL_DEVICE_HOOK_ACCEPTANCE: PASS
TARGET_PACKAGE: io.hammer.developmentenvironmentdetection
```

---

## 24. 上游参考

```text
LSPlant:
https://github.com/LSPosed/LSPlant

libxposed API:
https://github.com/libxposed/api

libxposed Example:
https://github.com/libxposed/example

Dobby:
https://github.com/JingMatrix/Dobby

Magisk / Zygisk:
https://github.com/topjohnwu/Magisk
```

构建时应再次核对这些仓库的最新文档和实际接口，遇到上游 API 变化时以实际最新源码为准，并在提交记录中说明兼容性调整。
