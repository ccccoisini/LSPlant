package com.zygisk.framework.runtime;

import android.app.AppComponentFactory;
import android.content.pm.ApplicationInfo;
import android.util.Log;

import java.lang.reflect.Constructor;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

import io.github.libxposed.api.XposedModule;

final class ModuleManager {
    private static final String TAG = "ZH.Runtime";

    private final HookRegistry hookRegistry = new HookRegistry();
    private final ArrayList<LoadedModule> modules = new ArrayList<LoadedModule>();
    private boolean packageReadyDispatched;

    void loadModules(
            ClassLoader frameworkClassLoader,
            ClassLoader appClassLoader,
            String processName,
            String packageName,
            ModuleDescriptor[] descriptors) {
        ClassLoader safeAppClassLoader = appClassLoader == null
                ? frameworkClassLoader : appClassLoader;
        BridgeClassLoader bridgeClassLoader =
                new BridgeClassLoader(frameworkClassLoader, safeAppClassLoader);
        for (ModuleDescriptor descriptor : descriptors) {
            if (!descriptor.matches(packageName, processName)) {
                NativeBridge.log(Log.INFO, TAG, "MODULE_SCOPE_SKIP id=" + descriptor.moduleId);
                continue;
            }
            try {
                ByteBuffer buffer = descriptor.dexBuffer.asReadOnlyBuffer();
                ClassLoader moduleClassLoader =
                        NativeBridge.createDexClassLoader(buffer, bridgeClassLoader);
                if (moduleClassLoader == null) {
                    throw new IllegalStateException("module classloader create failed");
                }
                loadEntry(descriptor, moduleClassLoader, processName);
                NativeBridge.log(Log.INFO, TAG, "MODULE_DEX_LOADED id=" + descriptor.moduleId);
            } catch (Throwable throwable) {
                NativeBridge.log(
                        Log.ERROR,
                        TAG,
                        "MODULE_ENTRY_FAILED code=LOAD_EXCEPTION id=" + descriptor.moduleId,
                        throwable);
            }
        }
    }

    void dispatchModuleLoaded(String processName) {
        ModuleLoadedParamImpl param = new ModuleLoadedParamImpl(false, processName);
        for (LoadedModule module : snapshot()) {
            try {
                module.entry.onModuleLoaded(param);
            } catch (Throwable throwable) {
                NativeBridge.log(Log.ERROR, TAG, "MODULE_ENTRY_FAILED code=ON_MODULE_LOADED", throwable);
            }
        }
    }

    void dispatchPackageLoaded(
            String packageName,
            ApplicationInfo applicationInfo,
            ClassLoader defaultClassLoader) {
        PackageLoadedParamImpl param = new PackageLoadedParamImpl(
                packageName,
                applicationInfo,
                true,
                defaultClassLoader);
        for (LoadedModule module : snapshot()) {
            try {
                module.entry.onPackageLoaded(param);
            } catch (Throwable throwable) {
                NativeBridge.log(Log.ERROR, TAG, "MODULE_ENTRY_FAILED code=ON_PACKAGE_LOADED", throwable);
            }
        }
    }

    void dispatchPackageReady(
            String packageName,
            ApplicationInfo applicationInfo,
            ClassLoader classLoader,
            AppComponentFactory appComponentFactory) {
        synchronized (this) {
            if (packageReadyDispatched) {
                return;
            }
            packageReadyDispatched = true;
        }
        PackageReadyParamImpl param = new PackageReadyParamImpl(
                packageName,
                applicationInfo,
                true,
                classLoader,
                classLoader,
                appComponentFactory);
        NativeBridge.log(Log.INFO, TAG, "PACKAGE_CLASSLOADER_READY package=" + packageName);
        for (LoadedModule module : snapshot()) {
            try {
                module.entry.onPackageReady(param);
            } catch (Throwable throwable) {
                NativeBridge.log(Log.ERROR, TAG, "MODULE_ENTRY_FAILED code=ON_PACKAGE_READY", throwable);
            }
        }
    }

    HookRegistry hookRegistry() {
        return hookRegistry;
    }

    private void loadEntry(
            ModuleDescriptor descriptor,
            ClassLoader moduleClassLoader,
            String processName) throws ReflectiveOperationException {
        for (String entryClass : descriptor.entryClasses) {
            Class<?> clazz = Class.forName(entryClass, true, moduleClassLoader);
            if (!XposedModule.class.isAssignableFrom(clazz)) {
                NativeBridge.log(
                        Log.ERROR,
                        TAG,
                        "MODULE_ENTRY_TYPE_MISMATCH entry=" + entryClass
                                + " classLoader=" + clazz.getClassLoader()
                                + " super=" + clazz.getSuperclass()
                                + " superLoader=" + (clazz.getSuperclass() == null
                                        ? null : clazz.getSuperclass().getClassLoader())
                                + " expected=" + XposedModule.class
                                + " expectedLoader=" + XposedModule.class.getClassLoader()
                                + " expectedHash=" + System.identityHashCode(XposedModule.class)
                                + " superHash=" + System.identityHashCode(clazz.getSuperclass()));
                throw new IllegalArgumentException(entryClass + " does not extend XposedModule");
            }
            Constructor<?> constructor = clazz.getConstructor();
            XposedModule module = (XposedModule) constructor.newInstance();
            FrameworkXposedInterface framework =
                    new FrameworkXposedInterface(descriptor.moduleId, hookRegistry);
            module.attachFramework(framework, new Runnable() {
                @Override
                public void run() {
                    NativeBridge.log(Log.INFO, TAG, "MODULE_DETACHED id=" + descriptor.moduleId);
                }
            });
            modules.add(new LoadedModule(descriptor.moduleId, module));
            NativeBridge.log(
                    Log.INFO,
                    TAG,
                    "MODULE_ENTRY_LOADED id=" + descriptor.moduleId
                            + " entry=" + entryClass
                            + " process=" + processName);
        }
    }

    private List<LoadedModule> snapshot() {
        synchronized (modules) {
            return new ArrayList<LoadedModule>(modules);
        }
    }

    private static final class LoadedModule {
        private final String moduleId;
        private final XposedModule entry;

        private LoadedModule(String moduleId, XposedModule entry) {
            this.moduleId = moduleId;
            this.entry = entry;
        }
    }
}
