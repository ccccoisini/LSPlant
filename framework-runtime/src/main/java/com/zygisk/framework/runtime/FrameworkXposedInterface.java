package com.zygisk.framework.runtime;

import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.os.ParcelFileDescriptor;

import java.io.FileNotFoundException;
import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.util.Objects;

import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.error.HookFailedError;

final class FrameworkXposedInterface implements XposedInterface {
    private final String moduleId;
    private final HookRegistry registry;
    private final ApplicationInfo applicationInfo;

    FrameworkXposedInterface(String moduleId, HookRegistry registry) {
        this.moduleId = Objects.requireNonNull(moduleId, "moduleId");
        this.registry = Objects.requireNonNull(registry, "registry");
        this.applicationInfo = new ApplicationInfo();
        this.applicationInfo.packageName = moduleId;
    }

    @Override
    public String getFrameworkName() {
        return BuildConfig.FRAMEWORK_NAME;
    }

    @Override
    public String getFrameworkVersion() {
        return BuildConfig.FRAMEWORK_VERSION;
    }

    @Override
    public long getFrameworkVersionCode() {
        return BuildConfig.FRAMEWORK_VERSION_CODE;
    }

    @Override
    public long getFrameworkProperties() {
        return 0L;
    }

    @Override
    public HookBuilder hook(Executable executable) {
        return registry.newBuilder(moduleId, executable);
    }

    @Override
    public HookBuilder hookClassInitializer(Class<?> clazz) {
        throw new HookFailedError("API102 hookClassInitializer is not supported in MVP");
    }

    @Override
    public boolean deoptimize(Executable executable) {
        return NativeBridge.deoptimize(executable);
    }

    @Override
    public Invoker<?, Method> getInvoker(Method method) {
        return new MethodInvoker(registry, method);
    }

    @Override
    public <T> CtorInvoker<T> getInvoker(Constructor<T> constructor) {
        return new ConstructorInvoker<T>(constructor);
    }

    @Override
    public void log(int priority, String tag, String message) {
        NativeBridge.log(priority, tag, message);
    }

    @Override
    public void log(int priority, String tag, String message, Throwable throwable) {
        NativeBridge.log(priority, tag, message, throwable);
    }

    @Override
    public ApplicationInfo getModuleApplicationInfo() {
        return applicationInfo;
    }

    @Override
    public SharedPreferences getRemotePreferences(String group) {
        throw new UnsupportedOperationException("Remote preferences are not supported in MVP");
    }

    @Override
    public String[] listRemoteFiles() {
        throw new UnsupportedOperationException("Remote files are not supported in MVP");
    }

    @Override
    public ParcelFileDescriptor openRemoteFile(String name) throws FileNotFoundException {
        throw new FileNotFoundException("Remote files are not supported in MVP: " + name);
    }
}
