package com.example.zygiskhook.runtime;

import android.app.AppComponentFactory;
import android.content.pm.ApplicationInfo;

import io.github.libxposed.api.XposedModuleInterface;

final class ModuleLoadedParamImpl implements XposedModuleInterface.ModuleLoadedParam {
    private final boolean systemServer;
    private final String processName;

    ModuleLoadedParamImpl(boolean systemServer, String processName) {
        this.systemServer = systemServer;
        this.processName = processName;
    }

    @Override
    public boolean isSystemServer() {
        return systemServer;
    }

    @Override
    public String getProcessName() {
        return processName;
    }
}

class PackageLoadedParamImpl implements XposedModuleInterface.PackageLoadedParam {
    private final String packageName;
    private final ApplicationInfo applicationInfo;
    private final boolean firstPackage;
    private final ClassLoader defaultClassLoader;

    PackageLoadedParamImpl(
            String packageName,
            ApplicationInfo applicationInfo,
            boolean firstPackage,
            ClassLoader defaultClassLoader) {
        this.packageName = packageName;
        this.applicationInfo = applicationInfo;
        this.firstPackage = firstPackage;
        this.defaultClassLoader = defaultClassLoader;
    }

    @Override
    public String getPackageName() {
        return packageName;
    }

    @Override
    public ApplicationInfo getApplicationInfo() {
        return applicationInfo;
    }

    @Override
    public boolean isFirstPackage() {
        return firstPackage;
    }

    @Override
    public ClassLoader getDefaultClassLoader() {
        return defaultClassLoader;
    }
}

final class PackageReadyParamImpl extends PackageLoadedParamImpl
        implements XposedModuleInterface.PackageReadyParam {
    private final ClassLoader classLoader;
    private final AppComponentFactory appComponentFactory;

    PackageReadyParamImpl(
            String packageName,
            ApplicationInfo applicationInfo,
            boolean firstPackage,
            ClassLoader defaultClassLoader,
            ClassLoader classLoader,
            AppComponentFactory appComponentFactory) {
        super(packageName, applicationInfo, firstPackage, defaultClassLoader);
        this.classLoader = classLoader;
        this.appComponentFactory = appComponentFactory;
    }

    @Override
    public ClassLoader getClassLoader() {
        return classLoader;
    }

    @Override
    public AppComponentFactory getAppComponentFactory() {
        return appComponentFactory;
    }
}
