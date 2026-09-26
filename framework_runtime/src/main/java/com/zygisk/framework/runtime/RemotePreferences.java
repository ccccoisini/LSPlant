package com.zygisk.framework.runtime;

import android.content.SharedPreferences;
import android.util.Log;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

final class RemotePreferences implements SharedPreferences {
    private static final String TAG = "zygisk_framework.Runtime";

    private final Set<OnSharedPreferenceChangeListener> listeners =
            new HashSet<OnSharedPreferenceChangeListener>();
    private volatile Map<String, Object> values;

    RemotePreferences(Map<String, Object> initialValues) {
        values = RemotePreferencesCodec.immutableCopy(initialValues);
    }

    void replace(Map<String, Object> replacement) {
        Map<String, Object> next = RemotePreferencesCodec.immutableCopy(replacement);
        Map<String, Object> previous;
        ArrayList<OnSharedPreferenceChangeListener> listenerSnapshot;
        ArrayList<String> changed = new ArrayList<String>();
        synchronized (this) {
            previous = values;
            HashSet<String> keys = new HashSet<String>(previous.keySet());
            keys.addAll(next.keySet());
            for (String key : keys) {
                if (!Objects.equals(previous.get(key), next.get(key)) ||
                        previous.containsKey(key) != next.containsKey(key)) {
                    changed.add(key);
                }
            }
            if (changed.isEmpty()) return;
            values = next;
            listenerSnapshot = new ArrayList<OnSharedPreferenceChangeListener>(listeners);
        }
        java.util.Collections.sort(changed);
        for (String key : changed) {
            for (OnSharedPreferenceChangeListener listener : listenerSnapshot) {
                try {
                    listener.onSharedPreferenceChanged(this, key);
                } catch (Throwable throwable) {
                    NativeBridge.log(Log.ERROR, TAG,
                            "REMOTE_PREFS_LISTENER_FAILED key=" + key, throwable);
                }
            }
        }
    }

    @Override
    public Map<String, ?> getAll() {
        return RemotePreferencesCodec.publicCopy(values);
    }

    @Override
    public String getString(String key, String defValue) {
        Object value = values.get(key);
        return value == null ? defValue : (String) value;
    }

    @Override
    public Set<String> getStringSet(String key, Set<String> defValues) {
        Object value = values.get(key);
        if (value == null) {
            return defValues == null ? null : new HashSet<String>(defValues);
        }
        return new HashSet<String>(RemotePreferencesCodec.castStringSet(value));
    }

    @Override
    public int getInt(String key, int defValue) {
        Object value = values.get(key);
        return value == null ? defValue : (Integer) value;
    }

    @Override
    public long getLong(String key, long defValue) {
        Object value = values.get(key);
        return value == null ? defValue : (Long) value;
    }

    @Override
    public float getFloat(String key, float defValue) {
        Object value = values.get(key);
        return value == null ? defValue : (Float) value;
    }

    @Override
    public boolean getBoolean(String key, boolean defValue) {
        Object value = values.get(key);
        return value == null ? defValue : (Boolean) value;
    }

    @Override
    public boolean contains(String key) {
        return values.containsKey(key);
    }

    @Override
    public Editor edit() {
        throw new UnsupportedOperationException("Remote preferences are read-only in hooked apps");
    }

    @Override
    public synchronized void registerOnSharedPreferenceChangeListener(
            OnSharedPreferenceChangeListener listener) {
        listeners.add(Objects.requireNonNull(listener, "listener"));
    }

    @Override
    public synchronized void unregisterOnSharedPreferenceChangeListener(
            OnSharedPreferenceChangeListener listener) {
        listeners.remove(listener);
    }
}
