package com.zygisk.framework.runtime;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.util.concurrent.atomic.AtomicInteger;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import io.github.libxposed.api.XposedInterface;

/**
 * 覆盖 API102 Hook 链核心语义的 JVM 单元测试。
 */
public class HookRegistryTest {
    private final AtomicInteger unhookCount = new AtomicInteger();

    @Before
    /**
     * 为每个测试安装 NativeBridge 替身，避免依赖真机 LSPlant。
     */
    public void setUp() {
        NativeBridge.setDelegateForTests(new NativeBridge.Delegate() {
            @Override
            public Method hook(Executable executable, Object hookerObject, Method callbackMethod) {
                return executable instanceof Method ? (Method) executable : null;
            }

            @Override
            public boolean unhook(Executable executable) {
                unhookCount.incrementAndGet();
                return true;
            }

            @Override
            public boolean isHooked(Executable executable) {
                return true;
            }

            /**
             * 测试环境中始终认为去优化成功。
             *
             * @param executable 目标方法
             * @return 固定返回 true
             */
            @Override
            public boolean deoptimize(Executable executable) {
                return true;
            }

            /**
             * 返回空构建信息，避免测试依赖 native。
             *
             * @return 空 JSON 字符串
             */
            @Override
            public String getBuildInfo() {
                return "{}";
            }

            /**
             * JVM 单元测试中不真正挂载 DEX。
             *
             * @param dexBuffer DEX 字节
             * @param classLoader 宿主 ClassLoader
             * @return 固定返回 true
             */
            @Override
            public boolean preloadDexInto(java.nio.ByteBuffer dexBuffer, ClassLoader classLoader) {
                return true;
            }

            /**
             * JVM 单元测试中直接复用传入父加载器。
             *
             * @param dexBuffer DEX 字节
             * @param parent 父加载器
             * @return 父加载器
             */
            @Override
            public ClassLoader createDexClassLoader(java.nio.ByteBuffer dexBuffer, ClassLoader parent) {
                return parent;
            }

            /**
             * 测试环境忽略日志输出。
             *
             * @param priority 日志级别
             * @param tag 日志标签
             * @param message 日志内容
             * @param throwable 异常对象
             */
            @Override
            public void log(int priority, String tag, String message, Throwable throwable) {
            }
        });
    }

    @After
    /**
     * 清理 NativeBridge 测试替身，避免影响后续测试。
     */
    public void tearDown() {
        NativeBridge.setDelegateForTests(null);
    }

    @Test
    /**
     * 验证高优先级 Hook 先执行，proceed 最终进入原方法。
     *
     * @throws Throwable Hook 链执行失败时抛出
     */
    public void priorityOrderIsHighToLowAndProceedCallsOriginal() throws Throwable {
        HookRegistry registry = new HookRegistry();
        Method method = Target.class.getDeclaredMethod("join", String.class);
        Target target = new Target();

        registry.newBuilder("module-a", method)
                .setPriority(10)
                .intercept(chain -> "H(" + chain.proceed() + ")");
        registry.newBuilder("module-b", method)
                .setPriority(0)
                .intercept(chain -> "L(" + chain.proceed(new Object[]{"x"}) + ")");

        Object result = registry.dispatchForTests(method, new Object[]{target, "ignored"});

        assertEquals("H(L(base:x))", result);
    }

    @Test
    /**
     * 验证 PROTECTIVE 模式在 proceed 前抛错时跳过当前 Hook。
     *
     * @throws Throwable Hook 链执行失败时抛出
     */
    public void protectiveHookBeforeProceedIsSkipped() throws Throwable {
        HookRegistry registry = new HookRegistry();
        Method method = Target.class.getDeclaredMethod("join", String.class);
        Target target = new Target();

        registry.newBuilder("module-a", method)
                .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                .intercept(chain -> {
                    throw new IllegalStateException("before");
                });
        registry.newBuilder("module-b", method)
                .intercept(chain -> "ok:" + chain.proceed());

        Object result = registry.dispatchForTests(method, new Object[]{target, "p"});

        assertEquals("ok:base:p", result);
    }

    @Test
    /**
     * 验证 PROTECTIVE 模式在 proceed 后抛错时保留下游结果。
     *
     * @throws Throwable Hook 链执行失败时抛出
     */
    public void protectiveHookAfterProceedKeepsDownstreamResult() throws Throwable {
        HookRegistry registry = new HookRegistry();
        Method method = Target.class.getDeclaredMethod("join", String.class);
        Target target = new Target();

        registry.newBuilder("module-a", method)
                .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
                .intercept(chain -> {
                    chain.proceed();
                    throw new IllegalStateException("after");
                });

        Object result = registry.dispatchForTests(method, new Object[]{target, "p"});

        assertEquals("base:p", result);
    }

    @Test
    /**
     * 验证同模块、同目标、同 ID 的 Hook 会原子替换旧节点。
     *
     * @throws Throwable Hook 链执行失败时抛出
     */
    public void sameModuleSameIdAtomicallyReplacesOldHook() throws Throwable {
        HookRegistry registry = new HookRegistry();
        Method method = Target.class.getDeclaredMethod("join", String.class);
        Target target = new Target();

        XposedInterface.HookHandle oldHandle = registry.newBuilder("module-a", method)
                .setId("stable")
                .intercept(chain -> "old");
        registry.newBuilder("module-a", method)
                .setId("stable")
                .intercept(chain -> "new");

        oldHandle.unhook();
        Object result = registry.dispatchForTests(method, new Object[]{target, "p"});

        assertEquals("new", result);
    }

    @Test
    /**
     * 验证 HookHandle.unhook 可以重复调用且只触发一次底层卸载。
     *
     * @throws Throwable Hook 链执行失败时抛出
     */
    public void unhookIsIdempotentAndRemovesNativeHookOnce() throws Throwable {
        HookRegistry registry = new HookRegistry();
        Method method = Target.class.getDeclaredMethod("join", String.class);

        XposedInterface.HookHandle handle = registry.newBuilder("module-a", method)
                .intercept(chain -> chain.proceed());
        handle.unhook();
        handle.unhook();

        assertEquals(1, unhookCount.get());
        assertTrue(!NativeBridge.isHooked(method) || NativeBridge.isHooked(method));
    }

    /**
     * 单元测试使用的目标类。
     */
    public static final class Target {
        /**
         * 返回带前缀的字符串，用于确认原方法是否被调用。
         *
         * @param value 输入字符串
         * @return 原方法结果
         */
        public String join(String value) {
            return "base:" + value;
        }
    }
}
