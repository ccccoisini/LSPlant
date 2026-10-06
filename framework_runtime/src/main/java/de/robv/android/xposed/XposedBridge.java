package de.robv.android.xposed;

import android.util.Log;

import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Arrays;
import java.util.Set;

import de.robv.android.xposed.callbacks.XCallback;
import de.robv.android.xposed.callbacks.XC_LoadPackage;
import com.zygisk.framework.runtime.HookRegistry;
import com.zygisk.framework.runtime.NativeBridge;

/** Xposed API 82 的 Java 入口，使用框架内的 LSPlant Hook 注册表。 */
public final class XposedBridge {
    public static final ClassLoader BOOTCLASSLOADER = ClassLoader.getSystemClassLoader();
    public static final String TAG = "Xposed";
    public static int XPOSED_BRIDGE_VERSION = 82;
    private static volatile HookRegistry registry;
    private static final ThreadLocal<String> CURRENT_MODULE = new ThreadLocal<String>();

    private XposedBridge() { }

    /** 绑定当前进程唯一 Hook 注册表。 */
    public static void bindRegistry(HookRegistry hookRegistry) {
        registry = hookRegistry;
    }

    /** 创建旧版公开卸载句柄。 */
    public static XC_MethodHook.Unhook newUnhook(XC_MethodHook callback, Member member) {
        return callback.new Unhook(member);
    }

    /** 在模块入口回调期间设置当前模块身份。 */
    public static void setCurrentModule(String moduleId) {
        CURRENT_MODULE.set(moduleId);
    }

    /** 清除当前模块身份，避免线程复用时把 Hook 归属给错误模块。 */
    public static void clearCurrentModule() {
        CURRENT_MODULE.remove();
    }

    /** 返回兼容目标进程中固定的 Xposed API 版本。 */
    public static int getXposedVersion() { return 82; }

    /** 为一个方法或构造器添加回调，并返回对应卸载句柄。 */
    public static XC_MethodHook.Unhook hookMethod(Member method, XC_MethodHook callback) {
        if (!(method instanceof Executable)) {
            throw new IllegalArgumentException("Only methods and constructors can be hooked: " + method);
        }
        HookRegistry current = registry;
        if (current == null) throw new IllegalStateException("Xposed runtime is not initialized");
        String moduleId = CURRENT_MODULE.get();
        return current.install(moduleId == null ? "unknown" : moduleId,
                (Executable) method, callback);
    }

    /** 卸载指定回调；重复卸载安全。 */
    @Deprecated
    public static void unhookMethod(Member method, XC_MethodHook callback) {
        HookRegistry current = registry;
        if (current != null && method instanceof Executable) {
            if (!current.remove((Executable) method, callback)) {
                throw new IllegalStateException("Native unhook failed: " + method);
            }
        }
    }

    /** 为类中所有同名声明方法安装回调。 */
    public static Set<XC_MethodHook.Unhook> hookAllMethods(
            Class<?> hookClass, String methodName, XC_MethodHook callback) {
        java.util.LinkedHashSet<XC_MethodHook.Unhook> result = new java.util.LinkedHashSet<XC_MethodHook.Unhook>();
        for (Method method : hookClass.getDeclaredMethods()) {
            if (method.getName().equals(methodName)) result.add(hookMethod(method, callback));
        }
        return result;
    }

    /** 为类中所有构造器安装回调。 */
    public static Set<XC_MethodHook.Unhook> hookAllConstructors(
            Class<?> hookClass, XC_MethodHook callback) {
        java.util.LinkedHashSet<XC_MethodHook.Unhook> result = new java.util.LinkedHashSet<XC_MethodHook.Unhook>();
        for (Constructor<?> constructor : hookClass.getDeclaredConstructors()) {
            result.add(hookMethod(constructor, callback));
        }
        return result;
    }

    /** 在 Hook 存续期间调用目标方法的 LSPlant backup，绕过当前 Hook 链。 */
    public static Object invokeOriginalMethod(Member method, Object thisObject, Object[] args)
            throws InvocationTargetException, IllegalAccessException {
        HookRegistry current = registry;
        if (current != null && method instanceof Executable) {
            try {
                return current.invokeOriginal((Executable) method, thisObject,
                        args == null ? new Object[0] : args);
            } catch (InvocationTargetException exception) {
                throw exception;
            } catch (IllegalAccessException exception) {
                throw exception;
            } catch (Throwable throwable) {
                throw new InvocationTargetException(throwable);
            }
        }
        if (method instanceof Method) {
            Method target = (Method) method;
            target.setAccessible(true);
            return target.invoke(Modifier.isStatic(target.getModifiers()) ? null : thisObject,
                    args == null ? new Object[0] : args);
        }
        if (method instanceof Constructor<?>) {
            Constructor<?> target = (Constructor<?>) method;
            target.setAccessible(true);
            try { return target.newInstance(args == null ? new Object[0] : args); }
            catch (InstantiationException exception) { throw new InvocationTargetException(exception); }
        }
        throw new IllegalArgumentException("Unsupported member: " + method);
    }

    /** 把旧版模块日志写入 Android logcat。 */
    public static void log(String text) {
        NativeBridge.log(Log.ERROR, TAG, text == null ? "null" : text);
    }

    /** 把旧版模块异常写入 Android logcat。 */
    public static void log(Throwable throwable) {
        NativeBridge.log(Log.ERROR, TAG, "Xposed module error", throwable);
    }

    /** 资源回调类型保留供旧模块加载；当前应用进程运行时不提供资源注入。 */
    public static void hookInitPackageResources(
            de.robv.android.xposed.callbacks.XC_InitPackageResources callback) {
        throw new UnsupportedOperationException("Xposed resource hooks are not supported");
    }

    /** Xposed 回调集合的线程安全、有序快照实现。 */
    public static final class CopyOnWriteSortedSet<E> {
        private volatile Object[] elements = new Object[0];

        public synchronized boolean add(E value) {
            if (indexOf(value) >= 0) return false;
            Object[] next = Arrays.copyOf(elements, elements.length + 1);
            next[elements.length] = value;
            Arrays.sort(next);
            elements = next;
            return true;
        }

        public synchronized boolean remove(E value) {
            int index = indexOf(value);
            if (index < 0) return false;
            Object[] next = new Object[elements.length - 1];
            System.arraycopy(elements, 0, next, 0, index);
            System.arraycopy(elements, index + 1, next, index, elements.length - index - 1);
            elements = next;
            return true;
        }

        private int indexOf(Object value) {
            for (int index = 0; index < elements.length; index++) {
                if (value.equals(elements[index])) return index;
            }
            return -1;
        }

        /** 返回当前回调集合的不可变快照。 */
        public Object[] getSnapshot() { return elements; }
    }
}
