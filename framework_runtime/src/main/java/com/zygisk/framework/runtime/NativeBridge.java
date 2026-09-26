package com.zygisk.framework.runtime;

import android.util.Log;

import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;

/**
 * 连接 framework.dex 与 Zygisk Native Loader 的 JNI 边界。
 *
 * <p>该类会被 R8 混淆类名，Native 侧必须通过 framework.mapping 解析真实类名后再注册
 * native 方法；公开的 Java 方法名保持稳定，便于 RegisterNatives 绑定。</p>
 */
public final class NativeBridge {
    private static volatile Delegate delegate;

    private NativeBridge() {
    }

    /**
     * 设置单元测试使用的 Native 替身实现。
     *
     * @param testDelegate 测试替身；传入 {@code null} 后恢复真实 JNI 调用
     */
    public static void setDelegateForTests(Delegate testDelegate) {
        delegate = testDelegate;
    }

    /**
     * 使用 LSPlant 为目标方法安装底层 Hook。
     *
     * @param executable 需要 Hook 的目标方法或构造方法
     * @param hookerObject 保存 Java 分发状态的对象
     * @param callbackMethod LSPlant 回调到 hookerObject 的 Java 方法
     * @return LSPlant 生成的原方法 backup Method；失败时返回 {@code null}
     */
    public static Method hook(Executable executable, Object hookerObject, Method callbackMethod) {
        Delegate current = delegate;
        if (current != null) {
            return current.hook(executable, hookerObject, callbackMethod);
        }
        return nativeHook(executable, hookerObject, callbackMethod);
    }

    /**
     * 卸载目标方法的底层 LSPlant Hook。
     *
     * @param executable 已安装 Hook 的目标方法或构造方法
     * @return Native 层真实卸载结果
     */
    public static boolean unhook(Executable executable) {
        Delegate current = delegate;
        if (current != null) {
            return current.unhook(executable);
        }
        return nativeUnhook(executable);
    }

    /**
     * 查询目标方法是否已经被 LSPlant Hook。
     *
     * @param executable 待查询的目标方法或构造方法
     * @return 已 Hook 时返回 {@code true}
     */
    public static boolean isHooked(Executable executable) {
        Delegate current = delegate;
        if (current != null) {
            return current.isHooked(executable);
        }
        return nativeIsHooked(executable);
    }

    /**
     * 请求 ART 对指定方法去优化，避免被 inline 后 Hook 不触发。
     *
     * @param executable 需要去优化的目标方法
     * @return Native 层真实执行结果
     */
    public static boolean deoptimize(Executable executable) {
        Delegate current = delegate;
        if (current != null) {
            return current.deoptimize(executable);
        }
        return nativeDeoptimize(executable);
    }

    /**
     * 获取 Native 构建信息 JSON。
     *
     * @return Native 层返回的构建信息，失败时返回空 JSON
     */
    public static String getBuildInfo() {
        Delegate current = delegate;
        if (current != null) {
            return current.getBuildInfo();
        }
        String value = nativeGetBuildInfo();
        return value == null ? "{}" : value;
    }

    /**
     * 临时把 DEX 挂载到宿主 ClassLoader，预加载全部类后恢复宿主原始 dexElements。
     *
     * @param dexBuffer 指向 DEX 字节的 DirectByteBuffer
     * @param classLoader 宿主 ClassLoader
     * @return 预加载和恢复都成功时返回 {@code true}
     */
    public static boolean preloadDexInto(ByteBuffer dexBuffer, ClassLoader classLoader) {
        Delegate current = delegate;
        if (current != null) {
            return current.preloadDexInto(dexBuffer, classLoader);
        }
        return nativePreloadDexInto(dexBuffer, classLoader);
    }

    /**
     * 创建独立模块 ClassLoader，并把 DEX 写入该 ClassLoader 自身的 dexElements。
     *
     * @param dexBuffer 指向 DEX 字节的 DirectByteBuffer
     * @param parent 模块 ClassLoader 的父加载器
     * @return 创建成功的模块 ClassLoader
     */
    public static ClassLoader createDexClassLoader(ByteBuffer dexBuffer, ClassLoader parent) {
        Delegate current = delegate;
        if (current != null) {
            return current.createDexClassLoader(dexBuffer, parent);
        }
        return nativeCreateDexClassLoader(dexBuffer, parent);
    }

    static byte[] getRemotePreferencesSnapshot(String moduleId, String group) {
        return nativeGetRemotePreferencesSnapshot(moduleId, group);
    }

    static byte[] awaitRemotePreferencesUpdate() {
        return nativeAwaitRemotePreferencesUpdate();
    }

    /**
     * 输出统一 Tag 日志，并在 JVM 单元测试环境中自动降级到标准错误。
     *
     * @param priority Android 日志级别
     * @param tag 日志 Tag
     * @param message 日志内容
     */
    public static void log(int priority, String tag, String message) {
        Delegate current = delegate;
        if (current != null) {
            current.log(priority, tag, message, null);
            return;
        }
        try {
            Log.println(priority, tag, message);
        } catch (RuntimeException exception) {
            System.err.println(tag + ": " + message);
        }
    }

    /**
     * 输出带异常的统一 Tag 日志，并避免测试环境中的 android.jar Stub 崩溃。
     *
     * @param priority Android 日志级别
     * @param tag 日志 Tag
     * @param message 日志内容
     * @param throwable 需要记录的异常，可为 {@code null}
     */
    public static void log(int priority, String tag, String message, Throwable throwable) {
        Delegate current = delegate;
        if (current != null) {
            current.log(priority, tag, message, throwable);
            return;
        }
        try {
            if (throwable == null) {
                Log.println(priority, tag, message);
            } else {
                Log.println(priority, tag, message + '\n' + Log.getStackTraceString(throwable));
            }
        } catch (RuntimeException exception) {
            System.err.println(tag + ": " + message);
            if (throwable != null) {
                throwable.printStackTrace(System.err);
            }
        }
    }

    private static native Method nativeHook(
            Executable executable, Object hookerObject, Method callbackMethod);

    private static native boolean nativeUnhook(Executable executable);

    private static native boolean nativeIsHooked(Executable executable);

    private static native boolean nativeDeoptimize(Executable executable);

    private static native String nativeGetBuildInfo();

    private static native boolean nativePreloadDexInto(ByteBuffer dexBuffer, ClassLoader classLoader);

    private static native ClassLoader nativeCreateDexClassLoader(ByteBuffer dexBuffer, ClassLoader parent);

    private static native byte[] nativeGetRemotePreferencesSnapshot(String moduleId, String group);

    private static native byte[] nativeAwaitRemotePreferencesUpdate();

    /**
     * NativeBridge 的测试替身接口，用于 JVM 单元测试覆盖 Hook 链语义。
     */
    public interface Delegate {
        /**
         * 安装测试 Hook 并返回可调用的原方法备份。
         *
         * @param executable 目标方法或构造方法
         * @param hookerObject Java 分发对象
         * @param callbackMethod 回调方法
         * @return 原方法备份
         */
        Method hook(Executable executable, Object hookerObject, Method callbackMethod);

        /**
         * 卸载测试 Hook。
         *
         * @param executable 目标方法或构造方法
         * @return 卸载成功时返回 {@code true}
         */
        boolean unhook(Executable executable);

        /**
         * 查询测试 Hook 状态。
         *
         * @param executable 目标方法或构造方法
         * @return 已安装时返回 {@code true}
         */
        boolean isHooked(Executable executable);

        /**
         * 执行测试去优化逻辑。
         *
         * @param executable 目标方法或构造方法
         * @return 去优化成功时返回 {@code true}
         */
        boolean deoptimize(Executable executable);

        /**
         * 返回测试构建信息。
         *
         * @return 构建信息 JSON
         */
        String getBuildInfo();

        /**
         * 在测试环境模拟 DEX 预加载。
         *
         * @param dexBuffer DEX 字节
         * @param classLoader 宿主 ClassLoader
         * @return 测试替身固定的预加载结果
         */
        boolean preloadDexInto(ByteBuffer dexBuffer, ClassLoader classLoader);

        /**
         * 在测试环境模拟模块 ClassLoader 创建。
         *
         * @param dexBuffer DEX 字节
         * @param parent 父加载器
         * @return 测试替身返回的 ClassLoader
         */
        ClassLoader createDexClassLoader(ByteBuffer dexBuffer, ClassLoader parent);

        /**
         * 记录测试日志。
         *
         * @param priority Android 日志级别
         * @param tag 日志 Tag
         * @param message 日志内容
         * @param throwable 异常对象，可为 {@code null}
         */
        void log(int priority, String tag, String message, Throwable throwable);
    }
}
