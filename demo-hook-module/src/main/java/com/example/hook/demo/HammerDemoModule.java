package com.example.hook.demo;

import android.app.Application;
import android.content.pm.ApplicationInfo;
import android.util.Log;

import java.lang.reflect.Method;

import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.XposedModule;

/**
 * 用于真机验收的 API 102 Hook 模块入口。
 */
public final class HammerDemoModule extends XposedModule {
    private static final String TAG = "zygisk_framework.Module";
    private static final String TARGET_PACKAGE = "io.hammer.developmentenvironmentdetection";

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
    public void onModuleLoaded(ModuleLoadedParam param) {
        log(Log.INFO, TAG, "DEMO_MODULE_LOADED process=" + param.getProcessName() + " demo-v1");
    }

    /**
     * 在目标包最终类加载器就绪后安装 Application 创建 Hook。
     *
     * @param param 当前包及其类加载器信息
     */
    @Override
    public void onPackageReady(PackageReadyParam param) {
        if (!TARGET_PACKAGE.equals(param.getPackageName())) {
            return;
        }

        try {
            Class<?> applicationClass = resolveApplicationClass(param);
            Method method = findOnCreateMethod(applicationClass);
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

    private static Class<?> resolveApplicationClass(PackageReadyParam param)
            throws ClassNotFoundException {
        ApplicationInfo info = param.getApplicationInfo();
        String className = info == null ? null : info.className;
        if (className == null || className.length() == 0) {
            return Application.class;
        }
        if (className.charAt(0) == '.') {
            className = param.getPackageName() + className;
        }
        ClassLoader classLoader = param.getClassLoader();
        Class<?> clazz = Class.forName(className, false, classLoader);
        if (!Application.class.isAssignableFrom(clazz)) {
            throw new ClassNotFoundException(className + " is not an Application");
        }
        return clazz;
    }

    private static Method findOnCreateMethod(Class<?> applicationClass)
            throws NoSuchMethodException {
        Class<?> current = applicationClass;
        while (current != null && Application.class.isAssignableFrom(current)) {
            try {
                return current.getDeclaredMethod("onCreate");
            } catch (NoSuchMethodException ignored) {
                current = current.getSuperclass();
            }
        }
        return Application.class.getDeclaredMethod("onCreate");
    }

    private final class ApplicationCreateHook implements XposedInterface.Hooker {
        /**
         * 在 Application.onCreate 调用前后输出真机验收日志。
         *
         * @param chain 当前 Hook 调用链
         * @return 原方法返回值；该目标方法正常返回 null
         * @throws Throwable 原方法或后续 Hook 抛出的异常
         */
        @Override
        public Object intercept(XposedInterface.Chain chain) throws Throwable {
            Application application = (Application) chain.getThisObject();
            log(Log.INFO, TAG, "DEMO_BEFORE package=" + application.getPackageName());
            Object result = chain.proceed();
            log(Log.INFO, TAG, "DEMO_AFTER package=" + application.getPackageName());
            return result;
        }
    }
}
