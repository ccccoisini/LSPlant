package com.zygisk.framework.runtime;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicBoolean;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;

/** 覆盖 API 82 方法与构造器 Hook 链核心语义。 */
public class HookRegistryTest {
    private final AtomicInteger hookCount = new AtomicInteger();
    private final AtomicInteger unhookCount = new AtomicInteger();
    private final AtomicBoolean failUnhook = new AtomicBoolean();

    @Before
    public void setUp() throws Exception {
        NativeBridge.setDelegateForTests(new NativeBridge.Delegate() {
            @Override public Method hook(Executable executable, Object object, Method callbackMethod) {
                hookCount.incrementAndGet();
                if (executable instanceof Method) return (Method) executable;
                try { return Fixture.class.getDeclaredMethod("constructorBackup", int.class); }
                catch (NoSuchMethodException exception) { throw new AssertionError(exception); }
            }
            @Override public boolean unhook(Executable executable) {
                unhookCount.incrementAndGet(); return !failUnhook.get();
            }
            @Override public boolean isHooked(Executable executable) { return true; }
            @Override public boolean deoptimize(Executable executable) { return true; }
            @Override public String getBuildInfo() { return "{}"; }
            @Override public boolean preloadDexInto(ByteBuffer buffer, ClassLoader loader) { return true; }
            @Override public ClassLoader createDexClassLoader(ByteBuffer buffer, ClassLoader parent) { return parent; }
            @Override public void log(int priority, String tag, String message, Throwable throwable) { }
        });
    }

    /** 清除测试替身，避免影响其他运行时用例。 */
    @After
    public void tearDown() { NativeBridge.setDelegateForTests(null); }

    /** 验证不同模块只共享一个 Native Hook，逻辑回调可以分别卸载。 */
    @Test
    public void callbackOrderShortCircuitAndExceptionRecoveryMatchApi82() throws Throwable {
        HookRegistry registry = newRegistry();
        XposedBridge.bindRegistry(registry);
        XposedBridge.setCurrentModule("module-a");
        Method method = Fixture.class.getDeclaredMethod("join", String.class);
        Fixture receiver = new Fixture();
        List<String> events = new ArrayList<String>();
        registry.install("module-a", method, new XC_MethodHook(100) {
            @Override protected void beforeHookedMethod(MethodHookParam param) { events.add("b100"); }
            @Override protected void afterHookedMethod(MethodHookParam param) { events.add("a100"); }
        });
        registry.install("module-b", method, new XC_MethodHook(0) {
            @Override protected void beforeHookedMethod(MethodHookParam param) {
                events.add("b0"); param.args[0] = "changed";
            }
            @Override protected void afterHookedMethod(MethodHookParam param) { events.add("a0"); }
        });
        registry.install("module-c", method, new XC_MethodHook(-50) {
            @Override protected void beforeHookedMethod(MethodHookParam param) {
                events.add("b-50"); throw new IllegalStateException("recover");
            }
        });

        assertEquals("original:changed", registry.dispatchForTests(method,
                new Object[]{receiver, "initial"}));
        assertEquals(java.util.Arrays.asList("b100", "b0", "b-50", "a0", "a100"), events);
        assertEquals(1, hookCount.get());
        XposedBridge.clearCurrentModule();
    }

    /** 验证 Native 卸载失败保留回调，并允许之后重试。 */
    @Test
    public void earlyResultRunsOnlyItsMatchingAfterCallbacks() throws Throwable {
        HookRegistry registry = newRegistry();
        Method method = Fixture.class.getDeclaredMethod("join", String.class);
        Fixture receiver = new Fixture();
        List<String> events = new ArrayList<String>();
        registry.install("module", method, new XC_MethodHook(100) {
            @Override protected void beforeHookedMethod(MethodHookParam param) { events.add("before-high"); }
            @Override protected void afterHookedMethod(MethodHookParam param) { events.add("after-high"); }
        });
        registry.install("module", method, new XC_MethodHook(0) {
            @Override protected void beforeHookedMethod(MethodHookParam param) {
                events.add("before-low"); param.setResult("short-circuit");
            }
            @Override protected void afterHookedMethod(MethodHookParam param) { events.add("after-low"); }
        });
        registry.install("module", method, new XC_MethodHook(-10) {
            @Override protected void beforeHookedMethod(MethodHookParam param) { events.add("never"); }
        });
        assertEquals("short-circuit", registry.dispatchForTests(method, new Object[]{receiver, "x"}));
        assertEquals(java.util.Arrays.asList("before-high", "before-low", "after-low", "after-high"), events);
    }

    /** 验证 API 82 批量方法与构造器 Hook 覆盖全部声明成员。 */
    @Test
    public void constructorBackupInitializesExistingInstanceExactlyOnce() throws Throwable {
        HookRegistry registry = newRegistry();
        Constructor<Fixture> constructor = Fixture.class.getDeclaredConstructor(int.class);
        Fixture receiver = new Fixture();
        AtomicInteger after = new AtomicInteger();
        registry.install("module", constructor, new XC_MethodHook() {
            @Override protected void afterHookedMethod(MethodHookParam param) { after.incrementAndGet(); }
        });
        Object result = registry.dispatchForTests(constructor, new Object[]{receiver, 42});
        assertEquals(null, result);
        assertEquals(42, receiver.value);
        assertEquals(1, after.get());
    }

    @Test
    public void unhookIsIdempotentAndDefersNativeRemovalUntilCurrentCallReturns() throws Throwable {
        HookRegistry registry = newRegistry();
        Method method = Fixture.class.getDeclaredMethod("join", String.class);
        Fixture receiver = new Fixture();
        XC_MethodHook callback = new XC_MethodHook() {
            @Override protected void beforeHookedMethod(MethodHookParam param) {
                registry.remove(method, this);
            }
            @Override protected void afterHookedMethod(MethodHookParam param) { param.setResult("finished"); }
        };
        XC_MethodHook.Unhook unhook = registry.install("module", method, callback);
        assertEquals("finished", registry.dispatchForTests(method, new Object[]{receiver, "x"}));
        unhook.unhook();
        assertEquals(1, unhookCount.get());
        assertEquals("original:y", registry.dispatchForTests(method, new Object[]{receiver, "y"}));
        assertTrue(registry.remove(method, callback));
    }

    @Test
    public void duplicateCallbackObjectInstallsOneNodeAndOriginalInvocationBypassesCallback() throws Throwable {
        HookRegistry registry = newRegistry();
        Method method = Fixture.class.getDeclaredMethod("join", String.class);
        Fixture receiver = new Fixture();
        AtomicInteger calls = new AtomicInteger();
        XC_MethodHook callback = new XC_MethodHook() {
            @Override protected void beforeHookedMethod(MethodHookParam param) { calls.incrementAndGet(); }
        };
        registry.install("module", method, callback);
        registry.install("module", method, callback);
        assertEquals("original:x", XposedBridge.invokeOriginalMethod(method, receiver, new Object[]{"x"}));
        assertEquals("original:y", registry.dispatchForTests(method, new Object[]{receiver, "y"}));
        assertEquals(1, calls.get());
        assertEquals(1, hookCount.get());
    }

    @Test
    public void callbacksFromDifferentModulesShareOneNativeHookAndUnhookIndependently() throws Throwable {
        HookRegistry registry = newRegistry();
        Method method = Fixture.class.getDeclaredMethod("join", String.class);
        Fixture receiver = new Fixture();
        XC_MethodHook first = new XC_MethodHook(100) {
            @Override protected void beforeHookedMethod(MethodHookParam param) { param.setResult("module-a"); }
        };
        XC_MethodHook second = new XC_MethodHook(0) {
            @Override protected void beforeHookedMethod(MethodHookParam param) { param.setResult("module-b"); }
        };
        XposedBridge.setCurrentModule("module-a");
        XC_MethodHook.Unhook firstHandle = XposedBridge.hookMethod(method, first);
        XposedBridge.setCurrentModule("module-b");
        XC_MethodHook.Unhook secondHandle = XposedBridge.hookMethod(method, second);
        XposedBridge.clearCurrentModule();

        assertEquals("module-a", registry.dispatchForTests(method, new Object[]{receiver, "x"}));
        firstHandle.unhook();
        assertEquals("module-b", registry.dispatchForTests(method, new Object[]{receiver, "x"}));
        secondHandle.unhook();
        assertEquals("original:x", registry.dispatchForTests(method, new Object[]{receiver, "x"}));
        assertEquals(1, hookCount.get());
        assertEquals(1, unhookCount.get());
    }

    @Test
    public void nativeUnhookFailureKeepsCallbackAndCanBeRetried() throws Throwable {
        HookRegistry registry = newRegistry();
        Method method = Fixture.class.getDeclaredMethod("join", String.class);
        Fixture receiver = new Fixture();
        XC_MethodHook.Unhook handle = registry.install("module", method, new XC_MethodHook() {
            @Override protected void beforeHookedMethod(MethodHookParam param) { param.setResult("still-active"); }
        });
        failUnhook.set(true);
        assertThrows(IllegalStateException.class, handle::unhook);
        assertEquals("still-active", registry.dispatchForTests(method, new Object[]{receiver, "x"}));
        failUnhook.set(false);
        handle.unhook();
        assertEquals("original:x", registry.dispatchForTests(method, new Object[]{receiver, "x"}));
        assertEquals(2, unhookCount.get());
    }

    @Test
    public void bulkMethodsAndConstructorsInstallAndRemoveEachDeclaredMember() throws Throwable {
        HookRegistry registry = newRegistry();
        XC_MethodHook replacement = new XC_MethodHook() {
            @Override protected void beforeHookedMethod(MethodHookParam param) { param.setResult("bulk"); }
        };
        java.util.Set<XC_MethodHook.Unhook> methods = XposedBridge.hookAllMethods(
                Fixture.class, "same", replacement);
        assertEquals(2, methods.size());
        Fixture receiver = new Fixture();
        assertEquals("bulk", registry.dispatchForTests(
                Fixture.class.getDeclaredMethod("same"), new Object[]{receiver}));
        assertEquals("bulk", registry.dispatchForTests(
                Fixture.class.getDeclaredMethod("same", String.class), new Object[]{receiver, "x"}));
        for (XC_MethodHook.Unhook handle : methods) handle.unhook();

        XC_MethodHook constructorHook = new XC_MethodHook() {
            @Override protected void beforeHookedMethod(MethodHookParam param) {
                ((Fixture) param.thisObject).value = 77;
                param.setResult(null);
            }
        };
        java.util.Set<XC_MethodHook.Unhook> constructors =
                XposedBridge.hookAllConstructors(Fixture.class, constructorHook);
        assertEquals(2, constructors.size());
        for (Constructor<?> constructor : Fixture.class.getDeclaredConstructors()) {
            Fixture instance = new Fixture();
            Object[] args = constructor.getParameterTypes().length == 0
                    ? new Object[]{instance} : new Object[]{instance, 1};
            registry.dispatchForTests(constructor, args);
            assertEquals(77, instance.value);
        }
        for (XC_MethodHook.Unhook handle : constructors) handle.unhook();
        assertEquals(4, hookCount.get());
    }

    @Test
    public void afterCallbackFailureRestoresPriorThrowable() throws Throwable {
        HookRegistry registry = newRegistry();
        Method method = Fixture.class.getDeclaredMethod("fail");
        Fixture receiver = new Fixture();
        registry.install("module", method, new XC_MethodHook() {
            @Override protected void afterHookedMethod(MethodHookParam param) { throw new IllegalStateException("after"); }
        });
        try {
            registry.dispatchForTests(method, new Object[]{receiver});
            throw new AssertionError("Expected original failure");
        } catch (IllegalArgumentException expected) {
            assertEquals("original", expected.getMessage());
        }
    }

    private HookRegistry newRegistry() {
        HookRegistry registry = new HookRegistry();
        XposedBridge.bindRegistry(registry);
        return registry;
    }

    public static final class Fixture {
        int value;
        public Fixture() { }
        public Fixture(int value) { this.value = value; }
        public String join(String input) { return "original:" + input; }
        public String same() { return "zero"; }
        public String same(String value) { return value; }
        public void constructorBackup(int initialValue) { this.value = initialValue; }
        public String fail() { throw new IllegalArgumentException("original"); }
    }
}
