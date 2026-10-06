package com.zygisk.framework.runtime;

import static org.junit.Assert.assertNotSame;
import static org.junit.Assert.assertSame;

import android.app.AndroidAppHelper;
import android.content.res.XModuleResources;
import android.content.res.XResForwarder;
import android.content.res.XResources;

import de.robv.android.xposed.XposedBridge;

import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;

/** 验证模块 ClassLoader 固定使用 framework API，并把目标类交给真实 App loader。 */
public class BridgeClassLoaderTest {
    /** 验证 API/框架类和目标应用类由各自的正确 ClassLoader 解析。 */
    @Test
    public void protectsFrameworkTypesAndDelegatesApplicationTypes() throws Exception {
        String bridgeName = XposedBridge.class.getName();
        String fixtureName = fixture.app.HookTargetFixture.class.getName();
        ShadowingClassLoader appLoader = new ShadowingClassLoader(
                BridgeClassLoaderTest.class.getClassLoader(), bridgeName, fixtureName);
        Class<?> shadowBridge = appLoader.loadClass(bridgeName);
        Class<?> appFixture = appLoader.loadClass(fixtureName);
        assertNotSame(XposedBridge.class, shadowBridge);
        assertNotSame(fixture.app.HookTargetFixture.class, appFixture);

        BridgeClassLoader bridge = new BridgeClassLoader(
                BridgeClassLoaderTest.class.getClassLoader(), appLoader);
        assertSame(XposedBridge.class, bridge.loadClass(bridgeName));
        assertSame(AndroidAppHelper.class, bridge.loadClass(AndroidAppHelper.class.getName()));
        assertSame(XResources.class, bridge.loadClass(XResources.class.getName()));
        assertSame(XModuleResources.class, bridge.loadClass(XModuleResources.class.getName()));
        assertSame(XResForwarder.class, bridge.loadClass(XResForwarder.class.getName()));
        assertSame(appFixture, bridge.loadClass(fixtureName));
    }

    private static final class ShadowingClassLoader extends ClassLoader {
        private final String shadowBridge;
        private final String isolatedFixture;

        ShadowingClassLoader(ClassLoader parent, String shadowBridge, String isolatedFixture) {
            super(parent);
            this.shadowBridge = shadowBridge;
            this.isolatedFixture = isolatedFixture;
        }

        @Override
        protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
            if (!name.equals(shadowBridge) && !name.equals(isolatedFixture)) {
                return super.loadClass(name, resolve);
            }
            synchronized (this) {
                Class<?> type = findLoadedClass(name);
                if (type == null) {
                    String resource = "/" + name.replace('.', '/') + ".class";
                    try (InputStream input = ShadowingClassLoader.class.getResourceAsStream(resource)) {
                        if (input == null) throw new ClassNotFoundException(name);
                        ByteArrayOutputStream output = new ByteArrayOutputStream();
                        byte[] buffer = new byte[4096];
                        int read;
                        while ((read = input.read(buffer)) >= 0) output.write(buffer, 0, read);
                        byte[] bytes = output.toByteArray();
                        type = defineClass(name, bytes, 0, bytes.length);
                    } catch (Exception exception) {
                        throw new ClassNotFoundException(name, exception);
                    }
                }
                if (resolve) resolveClass(type);
                return type;
            }
        }
    }
}
