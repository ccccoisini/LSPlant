package com.zygisk.framework.runtime;

/**
 * 在模块 ClassLoader 和目标 App ClassLoader 之间提供受控委派的桥接加载器。
 *
 * <p>模块解析 Xposed API 与框架受保护类时固定走 framework loader，解析目标 App 类时优先走
 * app loader，从而避免 API 类身份冲突。</p>
 */
public final class BridgeClassLoader extends ClassLoader {
    private static final String[] FRAMEWORK_PREFIXES = {
            "de.robv.android.xposed.",
            "com.zygisk.framework.runtime.",
            "android.app.AndroidAppHelper",
            "android.content.res.XResources",
            "android.content.res.XModuleResources",
            "android.content.res.XResForwarder"
    };

    private final ClassLoader frameworkClassLoader;
    private volatile ClassLoader appClassLoader;

    /**
     * 创建模块使用的桥接 ClassLoader。
     *
     * @param frameworkClassLoader framework.dex 的 ClassLoader
     * @param appClassLoader 目标 App 的 ClassLoader
     */
    public BridgeClassLoader(ClassLoader frameworkClassLoader, ClassLoader appClassLoader) {
        super(ClassLoader.getSystemClassLoader());
        this.frameworkClassLoader = frameworkClassLoader;
        this.appClassLoader = appClassLoader;
    }

    /** 在真实 LoadedApk ClassLoader 建立后更新目标应用类的委派目标。 */
    public void updateAppClassLoader(ClassLoader loader) {
        if (loader != null) appClassLoader = loader;
    }

    /**
     * 按受保护包名和目标 App 优先级加载类。
     *
     * @param name 类全名
     * @param resolve 是否需要解析链接
     * @return 成功加载的类
     * @throws ClassNotFoundException 所有委派路径都无法找到该类时抛出
     */
    @Override
    protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
        synchronized (this) {
            Class<?> loaded = findLoadedClass(name);
            if (loaded != null) {
                return loaded;
            }
            Class<?> clazz;
            if (isFrameworkClass(name)) {
                clazz = frameworkClassLoader.loadClass(name);
            } else if (isPlatformClass(name)) {
                clazz = super.loadClass(name, false);
            } else {
                try {
                    clazz = appClassLoader.loadClass(name);
                } catch (ClassNotFoundException ignored) {
                    clazz = super.loadClass(name, false);
                }
            }
            if (resolve) {
                resolveClass(clazz);
            }
            return clazz;
        }
    }

    private static boolean isFrameworkClass(String name) {
        for (String prefix : FRAMEWORK_PREFIXES) {
            if (name.startsWith(prefix)) {
                return true;
            }
        }
        return false;
    }

    private static boolean isPlatformClass(String name) {
        return name.startsWith("java.")
                || name.startsWith("javax.")
                || name.startsWith("android.")
                || name.startsWith("dalvik.")
                || name.startsWith("kotlin.");
    }
}
