package com.zygisk.framework.runtime;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import android.content.SharedPreferences;

import de.robv.android.xposed.XSharedPreferences;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import java.io.File;
import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.concurrent.atomic.AtomicReference;

/** 覆盖 API 82 XSharedPreferences 到模块 CLI 配置组的只读映射。 */
public class XSharedPreferencesTest {
    private final AtomicReference<byte[]> settingsSnapshot = new AtomicReference<byte[]>();
    private final AtomicReference<byte[]> defaultSnapshot = new AtomicReference<byte[]>();
    private final AtomicReference<byte[]> fileSnapshot = new AtomicReference<byte[]>();

    @Before
    public void setUp() {
        settingsSnapshot.set(snapshot("settings", "string", "value", "first"));
        defaultSnapshot.set(snapshot("legacy.sample_preferences", "boolean", "enabled", "true"));
        fileSnapshot.set(snapshot("custom", "int", "count", "7"));
        NativeBridge.setDelegateForTests(new NativeBridge.Delegate() {
            @Override public Method hook(Executable executable, Object hooker, Method callback) {
                return executable instanceof Method ? (Method) executable : null;
            }
            @Override public byte[] getRemotePreferencesSnapshot(String moduleId, String group) {
                if ("settings".equals(group)) return settingsSnapshot.get();
                if ("legacy.sample_preferences".equals(group)) return defaultSnapshot.get();
                if ("custom".equals(group)) return fileSnapshot.get();
                return new byte[0];
            }
            @Override public boolean unhook(Executable executable) { return true; }
            @Override public boolean isHooked(Executable executable) { return false; }
            @Override public boolean deoptimize(Executable executable) { return true; }
            @Override public String getBuildInfo() { return "{}"; }
            @Override public boolean preloadDexInto(ByteBuffer buffer, ClassLoader loader) { return true; }
            @Override public ClassLoader createDexClassLoader(ByteBuffer buffer, ClassLoader parent) { return parent; }
            @Override public void log(int priority, String tag, String message, Throwable throwable) { }
        });
        FrameworkServices.registerLegacyPackage("legacy.sample", "module.sample");
    }

    /** 清除 Native 替身，避免测试环境调用 JNI。 */
    @After
    public void tearDown() {
        NativeBridge.setDelegateForTests(null);
    }

    /** 验证快照只读，并在 reload 前后正确报告远端变更。 */
    @Test
    public void packageConstructorReloadsSnapshotAndUsesNamedGroup() {
        XSharedPreferences preferences = new XSharedPreferences("legacy.sample", "settings");
        assertEquals("first", preferences.getString("value", "default"));
        assertEquals("fallback", preferences.getString("missing", "fallback"));
        assertFalse(preferences.hasFileChanged());
        assertFalse(preferences.makeWorldReadable());
        assertThrows(UnsupportedOperationException.class, preferences::edit);
        assertThrows(UnsupportedOperationException.class,
                () -> preferences.registerOnSharedPreferenceChangeListener((prefs, key) -> { }));

        settingsSnapshot.set(snapshot("settings", "string", "value", "second"));
        RemotePreferencesRegistry.getInstance().applyUpdate(
                update("module.sample", "settings", settingsSnapshot.get()));
        assertTrue(preferences.hasFileChanged());
        assertEquals("first", preferences.getString("value", "default"));
        preferences.reload();
        assertEquals("second", preferences.getString("value", "default"));
        assertFalse(preferences.hasFileChanged());
    }

    /** 验证单参数构造和标准 shared_prefs 文件路径都按 CLI group 名读取。 */
    @Test
    public void defaultAndFileConstructorsMapStandardNames() {
        XSharedPreferences defaults = new XSharedPreferences("legacy.sample");
        assertTrue(defaults.getBoolean("enabled", false));
        assertEquals("legacy.sample_preferences", defaults.getFile().getName().replace(".xml", ""));

        XSharedPreferences fromFile = new XSharedPreferences(
                new File("/data/user/0/legacy.sample/shared_prefs/custom.xml"));
        assertEquals(7, fromFile.getInt("count", 0));
        assertEquals("custom.xml", fromFile.getFile().getName());
        assertThrows(IllegalArgumentException.class,
                () -> new XSharedPreferences(new File("/tmp/custom.xml")));
    }

    /** 验证缺少包映射和重复映射会给出明确错误。 */
    @Test
    public void packageMappingsRejectUnknownAndConflictingOwners() {
        assertThrows(IllegalArgumentException.class,
                () -> new XSharedPreferences("legacy.unknown", "settings"));
        FrameworkServices.registerLegacyPackage("legacy.conflict", "module.one");
        assertThrows(IllegalArgumentException.class,
                () -> FrameworkServices.registerLegacyPackage("legacy.conflict", "module.two"));
    }

    private static byte[] snapshot(String group, String type, String key, String value) {
        return ("ZPREFS\t1\t" + encoded(group) + "\n" + type + "\t" + encoded(key)
                + "\t" + ("string".equals(type) ? encoded(value) : value) + "\n")
                .getBytes(StandardCharsets.UTF_8);
    }

    private static byte[] update(String moduleId, String group, byte[] snapshot) {
        byte[] moduleBytes = moduleId.getBytes(StandardCharsets.UTF_8);
        byte[] groupBytes = group.getBytes(StandardCharsets.UTF_8);
        ByteBuffer buffer = ByteBuffer.allocate(12 + moduleBytes.length + groupBytes.length + snapshot.length)
                .order(ByteOrder.LITTLE_ENDIAN);
        buffer.putInt(moduleBytes.length).put(moduleBytes);
        buffer.putInt(groupBytes.length).put(groupBytes);
        buffer.putInt(snapshot.length).put(snapshot);
        return buffer.array();
    }

    private static String encoded(String value) {
        return Base64.getEncoder().encodeToString(value.getBytes(StandardCharsets.UTF_8));
    }
}
