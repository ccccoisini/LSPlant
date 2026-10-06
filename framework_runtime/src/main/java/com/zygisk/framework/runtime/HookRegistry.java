package com.zygisk.framework.runtime;

import android.util.Log;

import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

import de.robv.android.xposed.XC_MethodHook;

/** 把旧版 Xposed 回调链复用到底层 LSPlant Hook。 */
public final class HookRegistry {
    private static final String TAG = "zygisk_framework.Runtime";
    private final Map<Executable, HookRecord> records =
            new ConcurrentHashMap<Executable, HookRecord>();

    /** 注册一个逻辑回调；相同回调对象重复注册会返回同一个逻辑 Hook。 */
    public XC_MethodHook.Unhook install(String moduleId, Executable executable,
                                        XC_MethodHook callback) {
        if (executable == null || callback == null) throw new NullPointerException();
        HookRecord record = records.computeIfAbsent(executable, HookRecord::new);
        return record.install(callback);
    }

    /** 移除指定成员上的回调。 */
    public boolean remove(Executable executable, XC_MethodHook callback) {
        HookRecord record = records.get(executable);
        return record == null || record.remove(callback);
    }

    /** JVM 测试入口：直接执行 native 回调对应的 Hook 分发。 */
    Object dispatchForTests(Executable executable, Object[] args) throws Throwable {
        HookRecord record = records.get(executable);
        if (record == null) return invokeReflectively(executable,
                executable instanceof Method && Modifier.isStatic(executable.getModifiers())
                        ? null : args[0], executable instanceof Method &&
                        Modifier.isStatic(executable.getModifiers()) ? args :
                        Arrays.copyOfRange(args, 1, args.length));
        return record.dispatch(args);
    }

    /** 通过 Member 形态查找原方法 backup。 */
    public Object invokeOriginal(Executable executable, Object receiver, Object[] args)
            throws Throwable {
        HookRecord record = records.get(executable);
        if (record != null) return record.invokeOriginal(receiver, args);
        return invokeReflectively(executable, receiver, args);
    }

    private static Object invokeReflectively(Executable executable, Object receiver, Object[] args)
            throws Throwable {
        try {
            if (executable instanceof Method) {
                Method method = (Method) executable;
                method.setAccessible(true);
                return method.invoke(Modifier.isStatic(method.getModifiers()) ? null : receiver, args);
            }
            Constructor<?> constructor = (Constructor<?>) executable;
            constructor.setAccessible(true);
            return constructor.newInstance(args);
        } catch (InvocationTargetException exception) {
            throw exception.getCause();
        }
    }

    private static final class HookRecord {
        private final Executable executable;
        private final Object lock = new Object();
        private volatile List<Node> snapshot = Collections.emptyList();
        private volatile Method backupMethod;
        private boolean nativeInstalled;
        private boolean pendingUnhook;
        private int activeCalls;

        HookRecord(Executable executable) { this.executable = executable; }

        XC_MethodHook.Unhook install(XC_MethodHook callback) {
            synchronized (lock) {
                for (Node node : snapshot) {
                    if (node.callback == callback) return de.robv.android.xposed.XposedBridge.newUnhook(callback, executable);
                }
                if (!nativeInstalled) {
                    try { executable.setAccessible(true); }
                    catch (Throwable throwable) { throw new IllegalStateException(throwable); }
                    Method backup = NativeBridge.hook(executable, this, DISPATCH_METHOD);
                    if (backup == null) throw new IllegalStateException("LSPlant Hook failed: " + executable);
                    backupMethod = backup;
                    nativeInstalled = true;
                }
                pendingUnhook = false;
                ArrayList<Node> next = new ArrayList<Node>(snapshot);
                next.add(new Node(callback));
                Collections.sort(next, (left, right) -> left.callback.compareTo(right.callback));
                snapshot = Collections.unmodifiableList(next);
                return de.robv.android.xposed.XposedBridge.newUnhook(callback, executable);
            }
        }

        boolean remove(XC_MethodHook callback) {
            synchronized (lock) {
                ArrayList<Node> next = new ArrayList<Node>(snapshot);
                boolean removed = false;
                for (int index = 0; index < next.size(); index++) {
                    if (next.get(index).callback == callback) {
                        next.remove(index);
                        removed = true;
                        break;
                    }
                }
                if (!removed) return true;
                if (next.isEmpty() && nativeInstalled && activeCalls == 0) {
                    if (!NativeBridge.unhook(executable)) return false;
                    nativeInstalled = false;
                    pendingUnhook = false;
                    NativeBridge.log(Log.INFO, TAG, "HOOK_UNHOOK result=true executable=" + executable);
                    snapshot = Collections.unmodifiableList(next);
                    return true;
                }
                snapshot = Collections.unmodifiableList(next);
                if (snapshot.isEmpty()) requestUnhookLocked();
                return true;
            }
        }

        private void requestUnhookLocked() {
            if (!nativeInstalled) return;
            if (activeCalls != 0) {
                pendingUnhook = true;
                return;
            }
            finishUnhookLocked();
        }

        private void finishUnhookLocked() {
            if (snapshot.isEmpty() && nativeInstalled) {
                boolean ok = NativeBridge.unhook(executable);
                if (ok) nativeInstalled = false;
                pendingUnhook = false;
                NativeBridge.log(ok ? Log.INFO : Log.ERROR, TAG,
                        "HOOK_UNHOOK result=" + ok + " executable=" + executable);
            }
        }

        /** 按当前回调快照完成 API 82 before/原方法/after 分发。 */
        public Object dispatch(Object[] rawArgs) throws Throwable {
            Object[] safe = rawArgs == null ? new Object[0] : rawArgs;
            boolean constructor = executable instanceof Constructor<?>;
            boolean isStatic = !constructor && Modifier.isStatic(executable.getModifiers());
            Object receiver = isStatic || safe.length == 0 ? null : safe[0];
            Object[] args = (isStatic ? safe : Arrays.copyOfRange(safe, Math.min(1, safe.length), safe.length));
            List<Node> callbacks;
            synchronized (lock) {
                activeCalls++;
                callbacks = snapshot;
            }
            try {
                if (callbacks.isEmpty()) return invokeOriginal(receiver, args);
                XC_MethodHook.MethodHookParam param = new XC_MethodHook.MethodHookParam();
                param.method = executable;
                param.thisObject = receiver;
                param.args = args;
                int beforeCount = 0;
                for (Node node : callbacks) {
                    try {
                        node.callback.dispatchBefore(param);
                    } catch (Throwable throwable) {
                        XposedBridgeLog.log(throwable);
                        param.setResult(null);
                        param.clearReturnEarly();
                    }
                    beforeCount++;
                    if (param.isReturnEarly()) break;
                }
                if (!param.isReturnEarly()) {
                    try { param.setResult(invokeOriginal(receiver, param.args)); }
                    catch (Throwable throwable) { param.setThrowable(throwable); }
                }
                for (int index = beforeCount - 1; index >= 0; index--) {
                    Object previousResult = param.getResult();
                    Throwable previousThrowable = param.getThrowable();
                    try {
                        callbacks.get(index).callback.dispatchAfter(param);
                    } catch (Throwable throwable) {
                        XposedBridgeLog.log(throwable);
                        if (previousThrowable == null) param.setResult(previousResult);
                        else param.setThrowable(previousThrowable);
                    }
                }
                return param.getResultOrThrowable();
            } finally {
                synchronized (lock) {
                    activeCalls--;
                    if (activeCalls == 0 && pendingUnhook) finishUnhookLocked();
                }
            }
        }

        Object invokeOriginal(Object receiver, Object[] args) throws Throwable {
            Method backup = backupMethod;
            if (backup == null) return invokeReflectively(executable, receiver, args);
            return NativeBridge.invokeOriginalForHook(executable, backup,
                    Modifier.isStatic(executable.getModifiers())
                            && !(executable instanceof Constructor<?>) ? null : receiver, args);
        }
    }

    private static final class Node {
        final XC_MethodHook callback;
        Node(XC_MethodHook callback) { this.callback = callback; }
    }

    private static final Method DISPATCH_METHOD;
    static {
        try { DISPATCH_METHOD = HookRecord.class.getDeclaredMethod("dispatch", Object[].class); }
        catch (NoSuchMethodException exception) { throw new ExceptionInInitializerError(exception); }
    }

    private static final class XposedBridgeLog {
        static void log(Throwable throwable) {
            NativeBridge.log(Log.ERROR, TAG, "Xposed callback failed", throwable);
        }
    }
}
