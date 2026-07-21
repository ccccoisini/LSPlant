package com.example.zygiskhook.runtime;

import android.util.Log;

import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.error.HookFailedError;

final class HookRegistry {
    private static final String TAG = "ZHook.Runtime";
    private static final Method DISPATCH_METHOD;

    static {
        try {
            DISPATCH_METHOD = HookRecord.class.getDeclaredMethod("dispatch", Object[].class);
        } catch (NoSuchMethodException exception) {
            throw new ExceptionInInitializerError(exception);
        }
    }

    private final Map<Executable, HookRecord> records = new ConcurrentHashMap<Executable, HookRecord>();
    private final AtomicLong sequence = new AtomicLong();

    XposedInterface.HookBuilder newBuilder(String moduleId, Executable executable) {
        return new HookBuilderImpl(this, moduleId, executable);
    }

    XposedInterface.HookHandle install(
            String moduleId,
            Executable executable,
            int priority,
            XposedInterface.ExceptionMode exceptionMode,
            String id,
            XposedInterface.Hooker hooker) {
        Objects.requireNonNull(executable, "executable");
        Objects.requireNonNull(exceptionMode, "exceptionMode");
        Objects.requireNonNull(hooker, "hooker");
        executable.setAccessible(true);
        HookRecord record = records.computeIfAbsent(executable, HookRecord::new);
        HookNode node = new HookNode(
                moduleId,
                executable,
                hooker,
                priority,
                exceptionMode,
                id,
                sequence.incrementAndGet());
        HookHandleImpl handle = new HookHandleImpl(this, node);
        node.handle = handle;
        record.add(node);
        return handle;
    }

    XposedInterface.HookHandle replace(HookNode oldNode, XposedInterface.Hooker hooker) {
        Objects.requireNonNull(hooker, "hooker");
        HookRecord record = records.get(oldNode.executable);
        if (record == null) {
            throw new IllegalStateException("Hook handle is no longer active");
        }
        HookNode node = new HookNode(
                oldNode.moduleId,
                oldNode.executable,
                hooker,
                oldNode.priority,
                oldNode.exceptionMode,
                oldNode.id,
                sequence.incrementAndGet());
        HookHandleImpl handle = new HookHandleImpl(this, node);
        node.handle = handle;
        record.replaceNode(oldNode, node);
        return handle;
    }

    void unhook(HookNode node) {
        HookRecord record = records.get(node.executable);
        if (record == null) {
            node.deactivate();
            return;
        }
        if (record.remove(node)) {
            records.remove(node.executable, record);
        }
    }

    Object dispatchForTests(Executable executable, Object[] lsplantArgs) throws Throwable {
        HookRecord record = records.get(executable);
        if (record == null) {
            throw new IllegalStateException("No hook record for " + executable);
        }
        return record.dispatch(lsplantArgs);
    }

    private static final class HookBuilderImpl implements XposedInterface.HookBuilder {
        private final HookRegistry registry;
        private final String moduleId;
        private final Executable executable;
        private int priority = XposedInterface.PRIORITY_DEFAULT;
        private XposedInterface.ExceptionMode exceptionMode = XposedInterface.ExceptionMode.DEFAULT;
        private String id;

        private HookBuilderImpl(HookRegistry registry, String moduleId, Executable executable) {
            this.registry = registry;
            this.moduleId = moduleId;
            this.executable = executable;
        }

        @Override
        public XposedInterface.HookBuilder setPriority(int priority) {
            this.priority = priority;
            return this;
        }

        @Override
        public XposedInterface.HookBuilder setExceptionMode(
                XposedInterface.ExceptionMode exceptionMode) {
            this.exceptionMode = exceptionMode == null
                    ? XposedInterface.ExceptionMode.DEFAULT : exceptionMode;
            return this;
        }

        @Override
        public XposedInterface.HookHandle intercept(XposedInterface.Hooker hooker) {
            return registry.install(moduleId, executable, priority, exceptionMode, id, hooker);
        }

        @Override
        public XposedInterface.HookBuilder setId(String id) {
            this.id = id;
            return this;
        }
    }

    private static final class HookHandleImpl implements XposedInterface.HookHandle {
        private final HookRegistry registry;
        private final HookNode node;
        private volatile boolean valid = true;

        private HookHandleImpl(HookRegistry registry, HookNode node) {
            this.registry = registry;
            this.node = node;
        }

        @Override
        public Executable getExecutable() {
            return node.executable;
        }

        @Override
        public void unhook() {
            if (!valid) {
                return;
            }
            registry.unhook(node);
        }

        @Override
        public String getId() {
            return node.id;
        }

        @Override
        public XposedInterface.HookHandle replaceHook(XposedInterface.Hooker hooker) {
            if (!valid || !node.active) {
                throw new IllegalStateException("Hook handle is no longer active");
            }
            return registry.replace(node, hooker);
        }

        private void invalidate() {
            valid = false;
        }
    }

    private static final class HookNode {
        private final String moduleId;
        private final Executable executable;
        private final XposedInterface.Hooker hooker;
        private final int priority;
        private final XposedInterface.ExceptionMode exceptionMode;
        private final String id;
        private final long sequence;
        private volatile boolean active = true;
        private HookHandleImpl handle;

        private HookNode(
                String moduleId,
                Executable executable,
                XposedInterface.Hooker hooker,
                int priority,
                XposedInterface.ExceptionMode exceptionMode,
                String id,
                long sequence) {
            this.moduleId = moduleId;
            this.executable = executable;
            this.hooker = hooker;
            this.priority = priority;
            this.exceptionMode = exceptionMode;
            this.id = id;
            this.sequence = sequence;
        }

        private void deactivate() {
            active = false;
            if (handle != null) {
                handle.invalidate();
            }
        }
    }

    private static final class HookRecord {
        private final Executable executable;
        private final Object mutationLock = new Object();
        private final Map<String, HookNode> nodesByScopedId = new HashMap<String, HookNode>();
        private final List<HookNode> nodes = new ArrayList<HookNode>();
        private volatile List<HookNode> snapshot = Collections.emptyList();
        private volatile Method backupMethod;
        private int activeCalls;
        private boolean nativeInstalled;
        private boolean pendingNativeUnhook;

        private HookRecord(Executable executable) {
            this.executable = executable;
        }

        private void add(HookNode node) {
            synchronized (mutationLock) {
                ensureNativeInstalled();
                if (node.id != null) {
                    String key = scopedId(node.moduleId, node.id);
                    HookNode old = nodesByScopedId.remove(key);
                    if (old != null) {
                        nodes.remove(old);
                        old.deactivate();
                    }
                    nodesByScopedId.put(key, node);
                }
                nodes.add(node);
                publishSnapshotLocked();
            }
        }

        private void replaceNode(HookNode oldNode, HookNode newNode) {
            synchronized (mutationLock) {
                if (!nodes.remove(oldNode)) {
                    oldNode.deactivate();
                    throw new IllegalStateException("Hook handle is no longer active");
                }
                if (oldNode.id != null) {
                    nodesByScopedId.remove(scopedId(oldNode.moduleId, oldNode.id));
                    nodesByScopedId.put(scopedId(newNode.moduleId, newNode.id), newNode);
                }
                oldNode.deactivate();
                nodes.add(newNode);
                publishSnapshotLocked();
            }
        }

        private boolean remove(HookNode node) {
            synchronized (mutationLock) {
                if (!nodes.remove(node)) {
                    node.deactivate();
                    return false;
                }
                if (node.id != null) {
                    nodesByScopedId.remove(scopedId(node.moduleId, node.id));
                }
                node.deactivate();
                publishSnapshotLocked();
                if (nodes.isEmpty()) {
                    requestNativeUnhookLocked();
                    return true;
                }
                return false;
            }
        }

        public Object dispatch(Object[] lsplantArgs) throws Throwable {
            beginCall();
            try {
                Object[] safeArgs = lsplantArgs == null ? new Object[0] : lsplantArgs;
                boolean isStatic = Modifier.isStatic(executable.getModifiers());
                boolean isConstructor = executable instanceof Constructor<?>;
                Object thisObject = (isStatic && !isConstructor) ? null : firstOrNull(safeArgs);
                Object[] apiArgs = (isStatic && !isConstructor)
                        ? safeArgs.clone()
                        : Arrays.copyOfRange(safeArgs, Math.min(1, safeArgs.length), safeArgs.length);
                return invokeFrom(0, thisObject, apiArgs);
            } finally {
                endCall();
            }
        }

        private Object invokeFrom(int index, Object thisObject, Object[] args) throws Throwable {
            List<HookNode> current = snapshot;
            int next = index;
            while (next < current.size() && !current.get(next).active) {
                next++;
            }
            if (next >= current.size()) {
                return invokeOriginal(thisObject, args);
            }

            HookNode node = current.get(next);
            ChainImpl chain = new ChainImpl(this, next + 1, executable, thisObject, args);
            try {
                return node.hooker.intercept(chain);
            } catch (Throwable throwable) {
                if (node.exceptionMode == XposedInterface.ExceptionMode.PROTECTIVE) {
                    if (!chain.hasProceeded()) {
                        NativeBridge.log(
                                Log.WARN,
                                TAG,
                                "HOOK_PROTECTIVE_SKIP module=" + node.moduleId
                                        + " executable=" + executable,
                                throwable);
                        return invokeFrom(next + 1, thisObject, args);
                    }
                    if (chain.hasDownstreamThrowable()) {
                        throw chain.getDownstreamThrowable();
                    }
                    return chain.getDownstreamResult();
                }
                throw throwable;
            } finally {
                chain.invalidate();
            }
        }

        private Object invokeOriginal(Object thisObject, Object[] args) throws Throwable {
            try {
                if (backupMethod != null) {
                    backupMethod.setAccessible(true);
                    return backupMethod.invoke(thisObject, args);
                }
                if (executable instanceof Method) {
                    Method method = (Method) executable;
                    method.setAccessible(true);
                    return method.invoke(Modifier.isStatic(method.getModifiers()) ? null : thisObject, args);
                }
                Constructor<?> constructor = (Constructor<?>) executable;
                constructor.setAccessible(true);
                if (thisObject != null) {
                    return null;
                }
                return constructor.newInstance(args);
            } catch (InvocationTargetException exception) {
                throw exception.getCause();
            }
        }

        private void ensureNativeInstalled() {
            if (nativeInstalled) {
                return;
            }
            Method backup = NativeBridge.hook(executable, this, DISPATCH_METHOD);
            if (backup == null) {
                throw new HookFailedError("LSPlant Hook failed: " + executable);
            }
            backupMethod = backup;
            nativeInstalled = true;
            NativeBridge.log(Log.INFO, TAG, "HOOK_INSTALLED executable=" + executable);
        }

        private void requestNativeUnhookLocked() {
            if (!nativeInstalled) {
                return;
            }
            if (activeCalls > 0) {
                pendingNativeUnhook = true;
                return;
            }
            boolean result = NativeBridge.unhook(executable);
            nativeInstalled = false;
            backupMethod = null;
            pendingNativeUnhook = false;
            NativeBridge.log(Log.INFO, TAG, "HOOK_UNHOOKED result=" + result + " executable=" + executable);
        }

        private void beginCall() {
            synchronized (mutationLock) {
                activeCalls++;
            }
        }

        private void endCall() {
            synchronized (mutationLock) {
                activeCalls--;
                if (activeCalls == 0 && pendingNativeUnhook) {
                    requestNativeUnhookLocked();
                }
            }
        }

        private void publishSnapshotLocked() {
            ArrayList<HookNode> ordered = new ArrayList<HookNode>(nodes);
            Collections.sort(ordered, new Comparator<HookNode>() {
                @Override
                public int compare(HookNode left, HookNode right) {
                    int priorityCompare = Integer.compare(right.priority, left.priority);
                    if (priorityCompare != 0) {
                        return priorityCompare;
                    }
                    return Long.compare(left.sequence, right.sequence);
                }
            });
            snapshot = Collections.unmodifiableList(ordered);
        }

        private static String scopedId(String moduleId, String id) {
            return moduleId + '\u0000' + id;
        }

        private static Object firstOrNull(Object[] array) {
            return array.length == 0 ? null : array[0];
        }
    }

    private static final class ChainImpl implements XposedInterface.Chain {
        private final HookRecord record;
        private final int nextIndex;
        private final Executable executable;
        private final Thread ownerThread = Thread.currentThread();
        private Object thisObject;
        private Object[] args;
        private boolean valid = true;
        private boolean proceeded;
        private Object downstreamResult;
        private Throwable downstreamThrowable;

        private ChainImpl(
                HookRecord record,
                int nextIndex,
                Executable executable,
                Object thisObject,
                Object[] args) {
            this.record = record;
            this.nextIndex = nextIndex;
            this.executable = executable;
            this.thisObject = thisObject;
            this.args = args == null ? new Object[0] : args.clone();
        }

        @Override
        public Executable getExecutable() {
            ensureUsable();
            return executable;
        }

        @Override
        public Object getThisObject() {
            ensureUsable();
            return thisObject;
        }

        @Override
        public List<Object> getArgs() {
            ensureUsable();
            return Collections.unmodifiableList(Arrays.asList(args.clone()));
        }

        @Override
        public Object getArg(int index) throws IndexOutOfBoundsException, ClassCastException {
            ensureUsable();
            return args[index];
        }

        @Override
        public Object proceed() throws Throwable {
            ensureUsable();
            return proceedWithInternal(thisObject, args);
        }

        @Override
        public Object proceed(Object[] newArgs) throws Throwable {
            ensureUsable();
            return proceedWithInternal(thisObject, newArgs);
        }

        @Override
        public Object proceedWith(Object newThis) throws Throwable {
            ensureUsable();
            if (Modifier.isStatic(executable.getModifiers()) && !(executable instanceof Constructor<?>)) {
                throw new IllegalStateException("Static method does not have thisObject");
            }
            return proceedWithInternal(newThis, args);
        }

        @Override
        public Object proceedWith(Object newThis, Object[] newArgs) throws Throwable {
            ensureUsable();
            if (Modifier.isStatic(executable.getModifiers()) && !(executable instanceof Constructor<?>)) {
                throw new IllegalStateException("Static method does not have thisObject");
            }
            return proceedWithInternal(newThis, newArgs);
        }

        private Object proceedWithInternal(Object newThis, Object[] newArgs) throws Throwable {
            proceeded = true;
            thisObject = newThis;
            args = newArgs == null ? new Object[0] : newArgs.clone();
            try {
                downstreamResult = record.invokeFrom(nextIndex, thisObject, args);
                downstreamThrowable = null;
                return downstreamResult;
            } catch (Throwable throwable) {
                downstreamThrowable = throwable;
                throw throwable;
            }
        }

        private void ensureUsable() {
            if (!valid) {
                throw new IllegalStateException("Chain is no longer valid outside current hook call");
            }
            if (Thread.currentThread() != ownerThread) {
                throw new IllegalStateException("Chain can only be used from the hook call thread");
            }
        }

        private void invalidate() {
            valid = false;
        }

        private boolean hasProceeded() {
            return proceeded;
        }

        private boolean hasDownstreamThrowable() {
            return downstreamThrowable != null;
        }

        private Throwable getDownstreamThrowable() {
            return downstreamThrowable;
        }

        private Object getDownstreamResult() {
            return downstreamResult;
        }
    }
}
