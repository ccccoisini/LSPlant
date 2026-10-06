package de.robv.android.xposed;

import de.robv.android.xposed.services.BaseService;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/** API 82 的 SELinux 查询接口；文件访问由框架桥接层管理。 */
public final class SELinuxHelper {
    private SELinuxHelper() { }

    /** 查询当前 Android 系统是否启用了 SELinux。 */
    public static boolean isSELinuxEnabled() {
        return invokeBoolean("isSELinuxEnabled", false);
    }

    /** 查询当前 SELinux 策略是否处于强制模式。 */
    public static boolean isSELinuxEnforced() {
        return invokeBoolean("isSELinuxEnforced", false);
    }

    /** 获取当前进程的安全上下文；系统不支持时返回 null。 */
    public static String getContext() {
        try {
            Object value = invoke("getContext");
            return value instanceof String ? (String) value : null;
        } catch (ReflectiveOperationException | LinkageError | RuntimeException ignored) {
            return null;
        }
    }

    /** 当前桥接不提供 Xposed 特权文件服务；配置请使用 XSharedPreferences 或框架配置扩展。 */
    public static BaseService getAppDataFileService() {
        throw new UnsupportedOperationException(
                "Xposed app-data file service is unavailable in the API 82 app-process bridge");
    }

    private static boolean invokeBoolean(String method, boolean fallback) {
        try {
            Object value = invoke(method);
            return value instanceof Boolean ? (Boolean) value : fallback;
        } catch (ReflectiveOperationException | LinkageError | RuntimeException ignored) {
            return fallback;
        }
    }

    private static Object invoke(String name) throws ReflectiveOperationException {
        Class<?> selinux = Class.forName("android.os.SELinux", false,
                SELinuxHelper.class.getClassLoader());
        Method method = selinux.getDeclaredMethod(name);
        method.setAccessible(true);
        try {
            return method.invoke(null);
        } catch (InvocationTargetException exception) {
            Throwable cause = exception.getCause();
            if (cause instanceof RuntimeException) throw (RuntimeException) cause;
            if (cause instanceof Error) throw (Error) cause;
            throw exception;
        }
    }
}
