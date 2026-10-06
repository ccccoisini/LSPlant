package de.robv.android.xposed;

import android.content.SharedPreferences;

import com.zygisk.framework.runtime.FrameworkServices;

import java.io.File;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/** 将 API 82 的 XSharedPreferences 只读接口映射到框架 Remote Preferences。 */
public final class XSharedPreferences implements SharedPreferences {
    private final String packageName;
    private final String fileName;
    private final SharedPreferences remote;
    private volatile Map<String, ?> snapshot;

    /** 使用传统的包名_preferences 配置文件名。 */
    public XSharedPreferences(String packageName) {
        this(packageName, packageName + "_preferences");
    }

    /** 使用指定的传统配置文件名映射到同名 CLI group。 */
    public XSharedPreferences(String packageName, String prefFileName) {
        this(packageName, prefFileName,
                FrameworkServices.getRemotePreferences(
                        FrameworkServices.moduleIdForPackage(packageName), prefFileName));
    }

    /** 只接受已加载模块包名下 shared_prefs 目录中的 XML 文件。 */
    public XSharedPreferences(File prefFile) {
        this(parsePackage(prefFile), parseName(prefFile));
    }

    private XSharedPreferences(String packageName, String fileName, SharedPreferences remote) {
        this.packageName = packageName;
        this.fileName = fileName;
        this.remote = remote;
        this.snapshot = immutableCopy(remote.getAll());
    }

    private static String parsePackage(File file) {
        if (file == null) throw new NullPointerException("prefFile");
        String[] path = file.getAbsolutePath().split("/");
        if (path.length == 6 && "data".equals(path[1]) && "data".equals(path[2])
                && "shared_prefs".equals(path[4])) {
            return path[3];
        }
        if (path.length == 7 && "data".equals(path[1])
                && ("user".equals(path[2]) || "user_de".equals(path[2]))
                && path[3].matches("[0-9]+") && "shared_prefs".equals(path[5])) {
            return path[4];
        }
        throw new IllegalArgumentException(
                "Only /data/data or /data/user shared_prefs paths are supported");
    }

    private static String parseName(File file) {
        String name = file.getName();
        if (!name.endsWith(".xml") || name.length() == 4) {
            throw new IllegalArgumentException("Preference file must end in .xml");
        }
        return name.substring(0, name.length() - 4);
    }

    private static Map<String, ?> immutableCopy(Map<String, ?> source) {
        HashMap<String, Object> copy = new HashMap<String, Object>();
        for (Map.Entry<String, ?> entry : source.entrySet()) {
            Object value = entry.getValue();
            copy.put(entry.getKey(), value instanceof Set<?> ? new HashSet<Object>((Set<?>) value) : value);
        }
        return Collections.unmodifiableMap(copy);
    }

    /** 旧版 API 的 makeWorldReadable 操作不适用于 root CLI 配置通道。 */
    public boolean makeWorldReadable() { return false; }

    /** 返回对应传统模块配置文件的逻辑路径。 */
    public File getFile() {
        return new File("/data/data/" + packageName + "/shared_prefs/" + fileName + ".xml");
    }

    /** 比较当前 CLI 配置快照与上次 reload 的内容。 */
    public boolean hasFileChanged() { return !snapshot.equals(immutableCopy(remote.getAll())); }

    /** 载入最新 Remote Preferences 快照。 */
    public synchronized void reload() {
        Map<String, ?> current = immutableCopy(remote.getAll());
        if (!snapshot.equals(current)) snapshot = current;
    }

    @Override public Map<String, ?> getAll() { return snapshot; }
    @Override public String getString(String key, String defValue) {
        Object value = snapshot.get(key); return value == null ? defValue : (String) value;
    }
    @Override public Set<String> getStringSet(String key, Set<String> defValues) {
        Object value = snapshot.get(key);
        if (value == null) return defValues == null ? null : new HashSet<String>(defValues);
        @SuppressWarnings("unchecked") Set<String> strings = (Set<String>) value;
        return new HashSet<String>(strings);
    }
    @Override public int getInt(String key, int defValue) {
        Object value = snapshot.get(key); return value == null ? defValue : (Integer) value;
    }
    @Override public long getLong(String key, long defValue) {
        Object value = snapshot.get(key); return value == null ? defValue : (Long) value;
    }
    @Override public float getFloat(String key, float defValue) {
        Object value = snapshot.get(key); return value == null ? defValue : (Float) value;
    }
    @Override public boolean getBoolean(String key, boolean defValue) {
        Object value = snapshot.get(key); return value == null ? defValue : (Boolean) value;
    }
    @Override public boolean contains(String key) { return snapshot.containsKey(key); }
    @Override public Editor edit() { throw new UnsupportedOperationException("XSharedPreferences is read-only"); }
    @Override public void registerOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener listener) {
        throw new UnsupportedOperationException("XSharedPreferences listeners are not supported");
    }
    @Override public void unregisterOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener listener) {
        throw new UnsupportedOperationException("XSharedPreferences listeners are not supported");
    }
}
