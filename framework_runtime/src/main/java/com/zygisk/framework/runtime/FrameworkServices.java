package com.zygisk.framework.runtime;

import android.app.Application;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/** 提供给 API 82 模块适配层的框架扩展。 */
public final class FrameworkServices {
    private static final Map<String, String> MODULES_BY_PACKAGE =
            new ConcurrentHashMap<String, String>();
    private static volatile String packageName;
    private static volatile String processName;
    private static volatile ApplicationInfo applicationInfo;
    private static volatile ClassLoader classLoader;
    private static volatile Application application;

    private FrameworkServices() { }

    /** 返回 root CLI 管理的只读实时配置组。 */
    public static SharedPreferences getRemotePreferences(String moduleId, String group) {
        return RemotePreferencesRegistry.getInstance().get(moduleId, group);
    }

    /** 将旧 Xposed 模块包名关联到 CLI 模块 ID；冲突时拒绝第二个模块。 */
    public static void registerLegacyPackage(String legacyPackageName, String moduleId) {
        String previous = MODULES_BY_PACKAGE.putIfAbsent(legacyPackageName, moduleId);
        if (previous != null && !previous.equals(moduleId)) {
            throw new IllegalArgumentException("legacyPackageName collision: " + legacyPackageName);
        }
    }

    /** 按旧版模块包名解析对应的 CLI 模块 ID。 */
    public static String moduleIdForPackage(String legacyPackageName) {
        String result = MODULES_BY_PACKAGE.get(legacyPackageName);
        if (result == null) throw new IllegalArgumentException("No loaded module for package " + legacyPackageName);
        return result;
    }

    /** 记录正在加载的应用上下文，供 AndroidAppHelper 查询。 */
    public static void setApplicationState(String packageValue, String processValue,
                                           ApplicationInfo info, ClassLoader loader,
                                           Application app) {
        packageName = packageValue;
        processName = processValue;
        applicationInfo = info;
        classLoader = loader;
        if (app != null) application = app;
    }

    public static String currentPackageName() { return packageName; }
    public static String currentProcessName() { return processName; }
    public static ApplicationInfo currentApplicationInfo() { return applicationInfo; }
    public static ClassLoader currentClassLoader() { return classLoader; }
    public static Application currentApplication() { return application; }
}
