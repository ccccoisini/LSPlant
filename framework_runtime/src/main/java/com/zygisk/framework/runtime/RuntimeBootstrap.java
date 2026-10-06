package com.zygisk.framework.runtime;

import android.app.Application;
import android.app.Instrumentation;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.util.Log;

import java.lang.reflect.Method;
import java.nio.ByteBuffer;

import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;

/**
 * framework.dex 被 Native 装载后的 Java 启动入口。
 *
 * <p>Native 侧通过 mapping 找到该类的混淆后名称，然后调用本类公开静态方法完成模块 DEX
 * 装载、生命周期分发和 Bootstrap Hook 安装。</p>
 */
public final class RuntimeBootstrap {
    private static final String TAG = "zygisk_framework.Runtime";

    private RuntimeBootstrap() {
    }

    /**
     * 启动 API 82 runtime 并装载独立 Hook 模块。
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
        RemotePreferencesRegistry.getInstance().startListener();

        ModuleDescriptor[] descriptors = buildDescriptors(
                moduleIds,
                moduleDexBuffers,
                javaInitLists,
                moduleProps,
                scopeLists);
        ModuleManager manager = new ModuleManager();
        ClassLoader safeAppLoader = appClassLoader == null
                ? ClassLoader.getSystemClassLoader() : appClassLoader;
        ApplicationInfo applicationInfo = new ApplicationInfo();
        applicationInfo.packageName = packageName;
        FrameworkServices.setApplicationState(packageName, processName,
                applicationInfo, safeAppLoader, null);
        manager.loadModules(
                RuntimeBootstrap.class.getClassLoader(),
                safeAppLoader,
                processName,
                packageName,
                descriptors);
        installApplicationLifecycleHooks(manager, packageName, processName,
                applicationInfo, safeAppLoader);
    }

    private static ModuleDescriptor[] buildDescriptors(
            String[] moduleIds,
            ByteBuffer[] moduleDexBuffers,
            String[] javaInitLists,
            String[] moduleProps,
            String[] scopeLists) {
        int count = moduleIds == null ? 0 : moduleIds.length;
        java.util.ArrayList<ModuleDescriptor> descriptors = new java.util.ArrayList<ModuleDescriptor>();
        for (int index = 0; index < count; index++) {
            try {
                descriptors.add(ModuleDescriptor.create(
                        moduleIds[index], moduleDexBuffers[index], valueAt(javaInitLists, index),
                        valueAt(moduleProps, index), valueAt(scopeLists, index)));
            } catch (Throwable throwable) {
                String reason = throwable instanceof ModuleDescriptor.IncompatibleApiException
                        ? "INCOMPATIBLE_API" : "INVALID_METADATA";
                NativeBridge.log(Log.ERROR, TAG, "MODULE_ENTRY_FAILED code=" + reason
                        + " id=" + moduleIds[index], throwable);
            }
        }
        return descriptors.toArray(new ModuleDescriptor[0]);
    }

    private static String valueAt(String[] values, int index) {
        return values == null || index >= values.length ? "" : values[index];
    }

    private static void installApplicationLifecycleHooks(
            final ModuleManager manager,
            final String packageName,
            final String processName,
            final ApplicationInfo fallbackInfo,
            final ClassLoader fallbackClassLoader) {
        XposedBridge.bindRegistry(manager.hookRegistry());
        XposedBridge.setCurrentModule(BuildConfig.FRAMEWORK_ID);
        boolean hookInstalled = false;
        try {
            Method newApplication = Instrumentation.class.getDeclaredMethod(
                    "newApplication", ClassLoader.class, String.class, Context.class);
            XposedBridge.hookMethod(newApplication, new XC_MethodHook(XC_MethodHook.PRIORITY_HIGHEST) {
                        /**
                         * 在 Application 实例创建前分发 package ready 生命周期。
                         *
                         * @param chain 当前 Hook 调用链
                         * @return 原方法返回值
                         * @throws Throwable 原方法或后续 Hook 抛出的异常
                         */
                        @Override
                        protected void beforeHookedMethod(MethodHookParam param) {
                            ClassLoader loader = (ClassLoader) param.args[0];
                            Context context = (Context) param.args[2];
                            ApplicationInfo info = context == null
                                    ? fallbackInfo
                                    : new ApplicationInfo(context.getApplicationInfo());
                            ClassLoader effectiveLoader = loader == null ? fallbackClassLoader : loader;
                            FrameworkServices.setApplicationState(packageName, processName,
                                    info, effectiveLoader, null);
                            if (manager.dispatchLoadPackage(
                                    packageName, processName, info, effectiveLoader)) {
                                NativeBridge.log(Log.INFO, TAG,
                                        "LIFECYCLE_DISPATCH source=Instrumentation.newApplication "
                                                + "timing=before-application-constructor");
                            }
                        }
                    });
            hookInstalled = true;
        } catch (Throwable throwable) {
            NativeBridge.log(
                    Log.WARN,
                    TAG,
                    "LIFECYCLE_FALLBACK reason=INSTRUMENTATION_NEW_APPLICATION_HOOK_FAILED",
                    throwable);
        }
        try {
            Method attach = Application.class.getDeclaredMethod("attach", Context.class);
            XposedBridge.hookMethod(attach, new XC_MethodHook(XC_MethodHook.PRIORITY_HIGHEST) {
                        /**
                         * 在 Application.attach 调用原方法前分发 package ready 生命周期。
                         *
                         * @param chain 当前 Hook 调用链
                         * @return 原方法返回值
                         * @throws Throwable 原方法或后续 Hook 抛出的异常
                         */
                        @Override
                        protected void beforeHookedMethod(MethodHookParam param) {
                            Context context = (Context) param.args[0];
                            ApplicationInfo info = context == null
                                    ? fallbackInfo : context.getApplicationInfo();
                            ClassLoader loader = context == null
                                    ? fallbackClassLoader : context.getClassLoader();
                            Application app = (Application) param.thisObject;
                            FrameworkServices.setApplicationState(packageName, processName,
                                    info, loader, app);
                            if (manager.dispatchLoadPackage(packageName, processName, info, loader)) {
                                NativeBridge.log(Log.WARN, TAG,
                                        "LIFECYCLE_FALLBACK source=Application.attach "
                                                + "timing=after-application-constructor-before-attach");
                            }
                        }
                    });
            hookInstalled = true;
        } catch (Throwable throwable) {
            if (!hookInstalled) {
                NativeBridge.log(
                        Log.WARN,
                        TAG,
                        "LIFECYCLE_DEGRADED_MODE reason=APPLICATION_ATTACH_HOOK_FAILED",
                        throwable);
            } else {
                NativeBridge.log(
                        Log.WARN,
                        TAG,
                        "LIFECYCLE_FALLBACK reason=APPLICATION_ATTACH_HOOK_FAILED",
                        throwable);
            }
        }
        XposedBridge.clearCurrentModule();
        if (!hookInstalled) NativeBridge.log(Log.ERROR, TAG,
                "LIFECYCLE_UNAVAILABLE reason=NO_APPLICATION_HOOK");
    }
}
