package com.example.zygiskhook.runtime;

import android.app.Application;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.util.Log;

import java.lang.reflect.Method;
import java.nio.ByteBuffer;

import io.github.libxposed.api.XposedInterface;

/**
 * framework.dex 被 Native 装载后的 Java 启动入口。
 *
 * <p>Native 侧通过 mapping 找到该类的混淆后名称，然后调用本类公开静态方法完成模块 DEX
 * 装载、生命周期分发和 Bootstrap Hook 安装。</p>
 */
public final class RuntimeBootstrap {
    private static final String TAG = "ZHook.Runtime";

    private RuntimeBootstrap() {
    }

    /**
     * 启动 API102 runtime 并装载独立 Hook 模块。
     *
     * @param appClassLoader 当前目标进程可用的 App ClassLoader
     * @param processName 当前进程名
     * @param packageName 当前包名
     * @param moduleIds 模块 ID 数组
     * @param moduleDexBuffers 每个模块的只读 DEX ByteBuffer
     * @param javaInitLists 每个模块的 java_init.list 内容
     * @param moduleProps 每个模块的 module.prop 内容
     * @param scopeLists 每个模块的 scope.list 内容
     */
    public static void bootstrap(
            ClassLoader appClassLoader,
            String processName,
            String packageName,
            String[] moduleIds,
            ByteBuffer[] moduleDexBuffers,
            String[] javaInitLists,
            String[] moduleProps,
            String[] scopeLists) {
        NativeBridge.log(Log.INFO, TAG, "FRAMEWORK_DEX_LOADED process=" + processName);
        NativeBridge.log(Log.INFO, TAG, "NATIVE_BRIDGE_REGISTERED build=" + NativeBridge.getBuildInfo());

        ModuleDescriptor[] descriptors = buildDescriptors(
                moduleIds,
                moduleDexBuffers,
                javaInitLists,
                moduleProps,
                scopeLists);
        ModuleManager manager = new ModuleManager();
        ClassLoader safeAppLoader = appClassLoader == null
                ? ClassLoader.getSystemClassLoader() : appClassLoader;
        manager.loadModules(
                RuntimeBootstrap.class.getClassLoader(),
                safeAppLoader,
                processName,
                packageName,
                descriptors);

        ApplicationInfo applicationInfo = new ApplicationInfo();
        applicationInfo.packageName = packageName;
        manager.dispatchModuleLoaded(processName);
        manager.dispatchPackageLoaded(packageName, applicationInfo, safeAppLoader);
        installApplicationAttachHook(manager, packageName, applicationInfo, safeAppLoader);
    }

    private static ModuleDescriptor[] buildDescriptors(
            String[] moduleIds,
            ByteBuffer[] moduleDexBuffers,
            String[] javaInitLists,
            String[] moduleProps,
            String[] scopeLists) {
        int count = moduleIds == null ? 0 : moduleIds.length;
        ModuleDescriptor[] descriptors = new ModuleDescriptor[count];
        for (int index = 0; index < count; index++) {
            descriptors[index] = ModuleDescriptor.create(
                    moduleIds[index],
                    moduleDexBuffers[index],
                    valueAt(javaInitLists, index),
                    valueAt(moduleProps, index),
                    valueAt(scopeLists, index));
        }
        return descriptors;
    }

    private static String valueAt(String[] values, int index) {
        return values == null || index >= values.length ? "" : values[index];
    }

    private static void installApplicationAttachHook(
            final ModuleManager manager,
            final String packageName,
            final ApplicationInfo fallbackInfo,
            final ClassLoader fallbackClassLoader) {
        FrameworkXposedInterface framework =
                new FrameworkXposedInterface("zygisk-framework", manager.hookRegistry());
        try {
            Method attach = Application.class.getDeclaredMethod("attach", Context.class);
            framework.hook(attach)
                    .setId("framework/application-attach")
                    .setPriority(XposedInterface.PRIORITY_HIGHEST)
                    .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                    .intercept(new XposedInterface.Hooker() {
                        /**
                         * 在 Application.attach 调用原方法前分发 package ready 生命周期。
                         *
                         * @param chain 当前 Hook 调用链
                         * @return 原方法返回值
                         * @throws Throwable 原方法或后续 Hook 抛出的异常
                         */
                        @Override
                        public Object intercept(XposedInterface.Chain chain) throws Throwable {
                            Context context = (Context) chain.getArg(0);
                            ApplicationInfo info = context == null
                                    ? fallbackInfo : context.getApplicationInfo();
                            ClassLoader loader = context == null
                                    ? fallbackClassLoader : context.getClassLoader();
                            manager.dispatchPackageReady(packageName, info, loader, null);
                            return chain.proceed();
                        }
                    });
        } catch (Throwable throwable) {
            NativeBridge.log(
                    Log.WARN,
                    TAG,
                    "LIFECYCLE_DEGRADED_MODE reason=APPLICATION_ATTACH_HOOK_FAILED",
                    throwable);
            manager.dispatchPackageReady(packageName, fallbackInfo, fallbackClassLoader, null);
        }
    }
}
