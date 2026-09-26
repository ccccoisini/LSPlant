package com.zygisk.framework.runtime;

import android.content.SharedPreferences;
import android.util.Log;

import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

final class RemotePreferencesRegistry {
    interface SnapshotProvider {
        byte[] load(String moduleId, String group);
    }

    private static final String TAG = "zygisk_framework.Runtime";
    private static final RemotePreferencesRegistry INSTANCE =
            new RemotePreferencesRegistry(new SnapshotProvider() {
                @Override
                public byte[] load(String moduleId, String group) {
                    return NativeBridge.getRemotePreferencesSnapshot(moduleId, group);
                }
            });

    private final SnapshotProvider snapshotProvider;
    private final ConcurrentHashMap<String, ConcurrentHashMap<String, RemotePreferences>> modules =
            new ConcurrentHashMap<String, ConcurrentHashMap<String, RemotePreferences>>();
    private final AtomicBoolean listenerStarted = new AtomicBoolean();

    RemotePreferencesRegistry(SnapshotProvider snapshotProvider) {
        this.snapshotProvider = Objects.requireNonNull(snapshotProvider, "snapshotProvider");
    }

    static RemotePreferencesRegistry getInstance() {
        return INSTANCE;
    }

    synchronized SharedPreferences get(final String moduleId, final String group) {
        validateName(moduleId, 512, "moduleId");
        validateName(group, 128, "group");
        ConcurrentHashMap<String, RemotePreferences> groups = modules.computeIfAbsent(
                moduleId, ignored -> new ConcurrentHashMap<String, RemotePreferences>());
        return groups.computeIfAbsent(group, ignored -> {
            Map<String, Object> initial;
            try {
                initial = RemotePreferencesCodec.decodeSnapshot(
                        group, snapshotProvider.load(moduleId, group));
            } catch (Throwable throwable) {
                NativeBridge.log(Log.WARN, TAG,
                        "REMOTE_PREFS_INVALID id=" + moduleId + " group=" + group, throwable);
                initial = java.util.Collections.emptyMap();
            }
            return new RemotePreferences(initial);
        });
    }

    void startListener() {
        if (!listenerStarted.compareAndSet(false, true)) return;
        Thread reader = new Thread(new Runnable() {
            @Override
            public void run() {
                while (true) {
                    byte[] event = NativeBridge.awaitRemotePreferencesUpdate();
                    if (event == null) return;
                    applyUpdate(event);
                }
            }
        }, "zygisk-framework-prefs");
        reader.setDaemon(true);
        reader.start();
    }

    void applyUpdate(byte[] event) {
        try {
            RemotePreferencesCodec.Update update = RemotePreferencesCodec.decodeUpdate(event);
            Map<String, Object> decoded = RemotePreferencesCodec.decodeSnapshot(
                    update.group, update.snapshot);
            RemotePreferences preferences;
            synchronized (this) {
                ConcurrentHashMap<String, RemotePreferences> groups = modules.get(update.moduleId);
                preferences = groups == null ? null : groups.get(update.group);
            }
            if (preferences != null) preferences.replace(decoded);
            NativeBridge.log(Log.INFO, TAG,
                    "REMOTE_PREFS_UPDATE id=" + update.moduleId + " group=" + update.group);
        } catch (Throwable throwable) {
            NativeBridge.log(Log.WARN, TAG, "REMOTE_PREFS_INVALID reason=UPDATE_DECODE", throwable);
        }
    }

    private static void validateName(String value, int maxBytes, String label) {
        Objects.requireNonNull(value, label);
        byte[] encoded = value.getBytes(StandardCharsets.UTF_8);
        boolean validUtf16 = value.equals(new String(encoded, StandardCharsets.UTF_8));
        if (value.isEmpty() || value.indexOf('\0') >= 0 || !validUtf16 || encoded.length > maxBytes) {
            throw new IllegalArgumentException(label + " is empty or too long");
        }
    }
}
