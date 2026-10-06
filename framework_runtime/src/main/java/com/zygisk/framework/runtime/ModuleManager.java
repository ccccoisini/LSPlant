package com.zygisk.framework.runtime;

import android.app.Application;
import android.app.Instrumentation;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.util.Log;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.IXposedHookZygoteInit;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

/** 在目标应用进程内装载 API 82 模块并分发旧版入口。 */
final class ModuleManager {
    private static final String TAG = "zygisk_framework.Runtime";
    private final HookRegistry hookRegistry = new HookRegistry();
    private final ArrayList<LoadedModule> modules = new ArrayList<LoadedModule>();
    private boolean packageDispatched;
    private BridgeClassLoader bridgeClassLoader;

    void loadModules(ClassLoader frameworkLoader, ClassLoader appLoader,
                     String processName, String packageName, ModuleDescriptor[] descriptors) {
        XposedBridge.bindRegistry(hookRegistry);
        ClassLoader safeAppLoader = appLoader == null ? frameworkLoader : appLoader;
        BridgeClassLoader bridge = new BridgeClassLoader(frameworkLoader, safeAppLoader);
        bridgeClassLoader = bridge;
        for (ModuleDescriptor descriptor : descriptors) {
            if (!descriptor.matches(packageName, processName)) continue;
            ClassLoader moduleLoader;
            try {
                moduleLoader = NativeBridge.createDexClassLoader(
                        descriptor.dexBuffer.asReadOnlyBuffer(), bridge);
                if (moduleLoader == null) throw new IllegalStateException("module ClassLoader create failed");
            } catch (Throwable throwable) {
                NativeBridge.log(Log.ERROR, TAG,
                        "MODULE_ENTRY_FAILED code=CLASS_LOADER id=" + descriptor.moduleId, throwable);
                continue;
            }
            for (String entryClass : descriptor.entryClasses) {
                try {
                    loadEntry(descriptor, moduleLoader, entryClass);
                } catch (Throwable throwable) {
                    NativeBridge.log(Log.ERROR, TAG, "MODULE_ENTRY_FAILED code=LOAD_EXCEPTION id="
                            + descriptor.moduleId + " entry=" + entryClass, throwable);
                }
            }
        }
    }

    private void loadEntry(ModuleDescriptor descriptor, ClassLoader moduleLoader, String entryClass)
            throws ReflectiveOperationException {
        Class<?> clazz = Class.forName(entryClass, true, moduleLoader);
        boolean loadPackage = IXposedHookLoadPackage.class.isAssignableFrom(clazz);
        boolean zygoteInit = IXposedHookZygoteInit.class.isAssignableFrom(clazz);
        if (!loadPackage && !zygoteInit) {
            throw new IllegalArgumentException(entryClass + " implements no supported Xposed entry interface");
        }
        Constructor<?> constructor = clazz.getConstructor();
        Object entry = constructor.newInstance();
        FrameworkServices.registerLegacyPackage(descriptor.legacyPackageName, descriptor.moduleId);
        LoadedModule loaded = new LoadedModule(descriptor.moduleId, descriptor.modulePath,
                descriptor.legacyPackageName, entry, loadPackage, zygoteInit);
        modules.add(loaded);
        if (zygoteInit) dispatchZygote(loaded);
        NativeBridge.log(Log.INFO, TAG, "MODULE_ENTRY_LOADED id=" + descriptor.moduleId + " entry=" + entryClass);
    }

    private void dispatchZygote(LoadedModule module) {
        XposedBridge.setCurrentModule(module.moduleId);
        try {
            IXposedHookZygoteInit.StartupParam param = new IXposedHookZygoteInit.StartupParam();
            param.modulePath = module.modulePath;
            param.startsSystemServer = false;
            ((IXposedHookZygoteInit) module.entry).initZygote(param);
            NativeBridge.log(Log.INFO, TAG, "LEGACY_INIT_ZYGOTE_DISPATCHED id=" + module.moduleId
                    + " processScope=app");
        } catch (Throwable throwable) {
            NativeBridge.log(Log.ERROR, TAG, "MODULE_ENTRY_FAILED code=INIT_ZYGOTE id=" + module.moduleId, throwable);
        } finally {
            XposedBridge.clearCurrentModule();
        }
    }

    boolean dispatchLoadPackage(String packageName, String processName,
                                ApplicationInfo applicationInfo, ClassLoader classLoader) {
        synchronized (this) {
            if (packageDispatched) return false;
            packageDispatched = true;
        }
        BridgeClassLoader bridge = bridgeClassLoader;
        if (bridge != null) bridge.updateAppClassLoader(classLoader);
        ApplicationInfo safeInfo = applicationInfo == null ? new ApplicationInfo() : applicationInfo;
        if (safeInfo.packageName == null) safeInfo.packageName = packageName;
        FrameworkServices.setApplicationState(packageName, processName, safeInfo, classLoader, null);
        XC_LoadPackage.LoadPackageParam param =
                new XC_LoadPackage.LoadPackageParam(new XposedBridge.CopyOnWriteSortedSet<XC_LoadPackage>());
        param.packageName = packageName;
        param.processName = processName;
        param.appInfo = safeInfo;
        param.classLoader = classLoader;
        param.isFirstApplication = true;
        NativeBridge.log(Log.INFO, TAG, "HANDLE_LOAD_PACKAGE package=" + packageName + " process=" + processName);
        for (LoadedModule module : snapshot()) {
            if (!module.loadPackage) continue;
            XposedBridge.setCurrentModule(module.moduleId);
            try {
                ((IXposedHookLoadPackage) module.entry).handleLoadPackage(param);
            } catch (Throwable throwable) {
                NativeBridge.log(Log.ERROR, TAG, "MODULE_ENTRY_FAILED code=HANDLE_LOAD_PACKAGE id="
                        + module.moduleId, throwable);
            } finally {
                XposedBridge.clearCurrentModule();
            }
        }
        return true;
    }

    HookRegistry hookRegistry() { return hookRegistry; }

    private synchronized List<LoadedModule> snapshot() { return new ArrayList<LoadedModule>(modules); }

    private static final class LoadedModule {
        final String moduleId;
        final String modulePath;
        final String legacyPackageName;
        final Object entry;
        final boolean loadPackage;
        final boolean zygoteInit;

        LoadedModule(String moduleId, String modulePath, String legacyPackageName, Object entry,
                     boolean loadPackage, boolean zygoteInit) {
            this.moduleId = moduleId;
            this.modulePath = modulePath;
            this.legacyPackageName = legacyPackageName;
            this.entry = entry;
            this.loadPackage = loadPackage;
            this.zygoteInit = zygoteInit;
        }
    }
}
