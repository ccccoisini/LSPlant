package com.zygisk.framework.runtime;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotSame;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import android.content.SharedPreferences;

import org.junit.Test;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Base64;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * 覆盖只读 Remote Preferences 的类型、隔离和实时更新语义。
 */
public class RemotePreferencesTest {
    /**
     * 验证全部 SharedPreferences 类型、默认值和防御性副本。
     */
    @Test
    public void supportsAllTypesDefaultsAndDefensiveCopies() {
        RemotePreferences preferences = new RemotePreferences(RemotePreferencesCodec.decodeSnapshot(
                "settings",
                snapshot("settings",
                        entry("string", "name", encoded("value")),
                        entry("string-set", "items", "2", encoded("a"), encoded("b")),
                        entry("int", "count", "7"),
                        entry("long", "large", "9223372036854775807"),
                        entry("float", "ratio", "1.25"),
                        entry("boolean", "enabled", "true"))));

        assertEquals("value", preferences.getString("name", null));
        assertEquals(new HashSet<String>(Arrays.asList("a", "b")),
                preferences.getStringSet("items", null));
        assertEquals(7, preferences.getInt("count", 0));
        assertEquals(Long.MAX_VALUE, preferences.getLong("large", 0));
        assertEquals(1.25f, preferences.getFloat("ratio", 0), 0f);
        assertTrue(preferences.getBoolean("enabled", false));
        assertEquals("fallback", preferences.getString("missing", "fallback"));
        assertEquals(11, preferences.getInt("missing-int", 11));
        assertEquals(12L, preferences.getLong("missing-long", 12L));
        assertEquals(1.5f, preferences.getFloat("missing-float", 1.5f), 0f);
        assertTrue(preferences.getBoolean("missing-boolean", true));
        assertFalse(preferences.contains("missing"));

        Set<String> defaultSet = new HashSet<String>(Arrays.asList("default"));
        Set<String> returnedDefaultSet = preferences.getStringSet("missing-set", defaultSet);
        assertNotSame(defaultSet, returnedDefaultSet);
        returnedDefaultSet.add("copy-only");
        assertEquals(new HashSet<String>(Arrays.asList("default")), defaultSet);

        Set<String> returnedSet = preferences.getStringSet("items", null);
        returnedSet.add("mutated");
        assertFalse(preferences.getStringSet("items", null).contains("mutated"));
        Map<String, ?> all = preferences.getAll();
        assertNotSame(all, preferences.getAll());
        @SuppressWarnings("unchecked")
        Set<String> allSet = (Set<String>) all.get("items");
        allSet.add("copy-only");
        assertFalse(preferences.getStringSet("items", null).contains("copy-only"));
    }

    /**
     * 验证 Hook 进程中的对象只读且类型错误不会被静默转换。
     */
    @Test
    public void editIsAlwaysReadOnlyAndWrongTypeThrows() {
        RemotePreferences preferences = new RemotePreferences(RemotePreferencesCodec.decodeSnapshot(
                "settings", snapshot("settings", entry("string", "value", encoded("x")))));

        assertThrows(UnsupportedOperationException.class, preferences::edit);
        assertThrows(ClassCastException.class, () -> preferences.getBoolean("value", false));
    }

    /**
     * 验证实例按模块和组隔离，并在同一作用域内复用。
     */
    @Test
    public void registryCachesPerModuleAndGroup() {
        RemotePreferencesRegistry registry = new RemotePreferencesRegistry((moduleId, group) ->
                snapshot(group, entry("string", "owner", encoded(moduleId))));

        SharedPreferences first = registry.get("module-a", "settings");
        assertSame(first, registry.get("module-a", "settings"));
        assertNotSame(first, registry.get("module-a", "other"));
        assertNotSame(first, registry.get("module-b", "settings"));
        assertEquals("module-a", first.getString("owner", null));
        assertEquals("module-b", registry.get("module-b", "settings").getString("owner", null));
    }

    /**
     * 验证实时替换、监听通知、去重以及损坏更新回退。
     */
    @Test
    public void updatesNotifyOnlyChangedKeysAndMalformedUpdateKeepsLastSnapshot() {
        RemotePreferencesRegistry registry = new RemotePreferencesRegistry((moduleId, group) ->
                snapshot(group,
                        entry("boolean", "enabled", "true"),
                        entry("string", "removed", encoded("old"))));
        SharedPreferences preferences = registry.get("module-a", "settings");
        AtomicInteger notifications = new AtomicInteger();
        SharedPreferences.OnSharedPreferenceChangeListener listener = (prefs, key) ->
                notifications.incrementAndGet();
        preferences.registerOnSharedPreferenceChangeListener(listener);

        byte[] replacement = snapshot("settings",
                entry("boolean", "enabled", "false"),
                entry("int", "added", "3"));
        registry.applyUpdate(update("module-a", "settings", replacement));
        assertFalse(preferences.getBoolean("enabled", true));
        assertEquals(3, preferences.getInt("added", 0));
        assertFalse(preferences.contains("removed"));
        assertEquals(3, notifications.get());

        registry.applyUpdate(update("module-a", "settings", replacement));
        assertEquals(3, notifications.get());
        registry.applyUpdate(update("module-a", "settings", "broken".getBytes(StandardCharsets.UTF_8)));
        assertEquals(3, preferences.getInt("added", 0));
        assertEquals(3, notifications.get());

        preferences.unregisterOnSharedPreferenceChangeListener(listener);
        registry.applyUpdate(update("module-a", "settings", new byte[0]));
        assertTrue(preferences.getAll().isEmpty());
        assertEquals(3, notifications.get());
    }

    /**
     * 验证不存在的组也会返回稳定空实例，并能接收之后创建的组快照。
     */
    @Test
    public void missingGroupReceivesFutureSnapshotOnSameInstance() {
        RemotePreferencesRegistry registry = new RemotePreferencesRegistry((moduleId, group) -> new byte[0]);
        SharedPreferences preferences = registry.get("module-a", "future");
        assertTrue(preferences.getAll().isEmpty());

        registry.applyUpdate(update("module-a", "future",
                snapshot("future", entry("string", "created", encoded("now")))));

        assertSame(preferences, registry.get("module-a", "future"));
        assertEquals("now", preferences.getString("created", null));
    }

    /**
     * 验证 native 合并快速提交后，Java 只按最终完整快照计算变更键。
     */
    @Test
    public void coalescedUpdatesNotifyFromLastObservedToFinalState() {
        RemotePreferencesRegistry registry = new RemotePreferencesRegistry((moduleId, group) ->
                snapshot(group,
                        entry("boolean", "enabled", "true"),
                        entry("int", "count", "1")));
        SharedPreferences preferences = registry.get("module-a", "settings");
        AtomicInteger notifications = new AtomicInteger();
        preferences.registerOnSharedPreferenceChangeListener((prefs, key) ->
                notifications.incrementAndGet());

        registry.applyUpdate(update("module-a", "settings",
                snapshot("settings",
                        entry("boolean", "enabled", "true"),
                        entry("int", "count", "3"))));

        assertTrue(preferences.getBoolean("enabled", false));
        assertEquals(3, preferences.getInt("count", 0));
        assertEquals(1, notifications.get());
    }

    /**
     * 验证越界浮点更新被视为损坏数据，不覆盖最后一次有效快照。
     */
    @Test
    public void outOfRangeFloatUpdateKeepsLastSnapshot() {
        RemotePreferencesRegistry registry = new RemotePreferencesRegistry((moduleId, group) ->
                snapshot(group, entry("float", "ratio", "1.25")));
        SharedPreferences preferences = registry.get("module-a", "settings");

        registry.applyUpdate(update("module-a", "settings",
                snapshot("settings", entry("float", "ratio", "1e1000"))));

        assertEquals(1.25f, preferences.getFloat("ratio", 0f), 0f);
    }

    private static byte[] snapshot(String group, String... entries) {
        StringBuilder output = new StringBuilder();
        output.append("ZPREFS\t1\t").append(encoded(group)).append('\n');
        for (String entry : entries) output.append(entry).append('\n');
        return output.toString().getBytes(StandardCharsets.UTF_8);
    }

    private static String entry(String type, String key, String... values) {
        StringBuilder output = new StringBuilder(type).append('\t').append(encoded(key));
        for (String value : values) output.append('\t').append(value);
        return output.toString();
    }

    private static String encoded(String value) {
        return Base64.getEncoder().encodeToString(value.getBytes(StandardCharsets.UTF_8));
    }

    private static byte[] update(String moduleId, String group, byte[] snapshot) {
        byte[] moduleBytes = moduleId.getBytes(StandardCharsets.UTF_8);
        byte[] groupBytes = group.getBytes(StandardCharsets.UTF_8);
        ByteBuffer buffer = ByteBuffer.allocate(
                12 + moduleBytes.length + groupBytes.length + snapshot.length)
                .order(ByteOrder.LITTLE_ENDIAN);
        buffer.putInt(moduleBytes.length).put(moduleBytes);
        buffer.putInt(groupBytes.length).put(groupBytes);
        buffer.putInt(snapshot.length).put(snapshot);
        return buffer.array();
    }
}
