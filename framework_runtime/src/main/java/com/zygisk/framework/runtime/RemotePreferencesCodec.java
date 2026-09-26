package com.zygisk.framework.runtime;

import java.math.BigDecimal;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

final class RemotePreferencesCodec {
    private static final int MAX_GROUP_BYTES = 128;
    private static final int MAX_KEY_BYTES = 512;
    private static final int MAX_VALUE_BYTES = 64 * 1024;
    private static final int MAX_ENTRIES = 4096;
    private static final int MAX_SNAPSHOT_BYTES = 1024 * 1024;

    private RemotePreferencesCodec() {
    }

    static Map<String, Object> decodeSnapshot(String expectedGroup, byte[] data) {
        if (data == null || data.length == 0) {
            return Collections.emptyMap();
        }
        if (data.length > MAX_SNAPSHOT_BYTES) {
            throw new IllegalArgumentException("Remote preferences snapshot is too large");
        }
        String text = new String(data, StandardCharsets.UTF_8);
        if (!text.endsWith("\n")) {
            throw new IllegalArgumentException("Remote preferences snapshot is truncated");
        }
        String[] lines = text.split("\n", -1);
        String[] header = lines[0].split("\t", -1);
        if (header.length != 3 || !"ZPREFS".equals(header[0]) || !"1".equals(header[1])) {
            throw new IllegalArgumentException("Unsupported remote preferences format");
        }
        String group = decodeUtf8(header[2], MAX_GROUP_BYTES);
        if (!expectedGroup.equals(group)) {
            throw new IllegalArgumentException("Remote preferences group mismatch");
        }
        HashMap<String, Object> values = new HashMap<String, Object>();
        for (int lineIndex = 1; lineIndex < lines.length - 1; lineIndex++) {
            String line = lines[lineIndex];
            if (line.isEmpty()) continue;
            if (values.size() >= MAX_ENTRIES) {
                throw new IllegalArgumentException("Too many remote preference entries");
            }
            String[] fields = line.split("\t", -1);
            if (fields.length < 3) {
                throw new IllegalArgumentException("Malformed remote preference entry");
            }
            String key = decodeUtf8(fields[1], MAX_KEY_BYTES);
            if (key.isEmpty() || values.containsKey(key)) {
                throw new IllegalArgumentException("Duplicate or empty remote preference key");
            }
            Object value;
            switch (fields[0]) {
                case "string":
                    requireFieldCount(fields, 3);
                    value = decodeUtf8(fields[2], MAX_VALUE_BYTES);
                    break;
                case "string-set":
                    int count = Integer.parseInt(fields[2]);
                    if (count < 0 || count > MAX_ENTRIES || fields.length != count + 3) {
                        throw new IllegalArgumentException("Invalid string-set size");
                    }
                    LinkedHashSet<String> set = new LinkedHashSet<String>();
                    for (int index = 3; index < fields.length; index++) {
                        if (!set.add(decodeUtf8(fields[index], MAX_VALUE_BYTES))) {
                            throw new IllegalArgumentException("Duplicate string-set item");
                        }
                    }
                    value = Collections.unmodifiableSet(set);
                    break;
                case "int":
                    requireFieldCount(fields, 3);
                    value = Integer.valueOf(fields[2]);
                    break;
                case "long":
                    requireFieldCount(fields, 3);
                    value = Long.valueOf(fields[2]);
                    break;
                case "float":
                    requireFieldCount(fields, 3);
                    value = parseFloat(fields[2]);
                    break;
                case "boolean":
                    requireFieldCount(fields, 3);
                    if (!"true".equals(fields[2]) && !"false".equals(fields[2])) {
                        throw new IllegalArgumentException("Invalid boolean value");
                    }
                    value = Boolean.valueOf(fields[2]);
                    break;
                default:
                    throw new IllegalArgumentException("Unknown remote preference type");
            }
            values.put(key, value);
        }
        return immutableCopy(values);
    }

    static Update decodeUpdate(byte[] data) {
        if (data == null || data.length < 12) {
            throw new IllegalArgumentException("Remote preferences update is truncated");
        }
        ByteBuffer buffer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN);
        String moduleId = readString(buffer, 512);
        String group = readString(buffer, MAX_GROUP_BYTES);
        byte[] snapshot = readBytes(buffer, MAX_SNAPSHOT_BYTES);
        if (moduleId.isEmpty() || group.isEmpty() || buffer.hasRemaining()) {
            throw new IllegalArgumentException("Malformed remote preferences update");
        }
        return new Update(moduleId, group, snapshot);
    }

    static Map<String, ?> publicCopy(Map<String, Object> source) {
        HashMap<String, Object> copy = new HashMap<String, Object>();
        for (Map.Entry<String, Object> entry : source.entrySet()) {
            Object value = entry.getValue();
            if (value instanceof Set) {
                value = new HashSet<String>(castStringSet(value));
            }
            copy.put(entry.getKey(), value);
        }
        return copy;
    }

    static Map<String, Object> immutableCopy(Map<String, Object> source) {
        HashMap<String, Object> copy = new HashMap<String, Object>();
        for (Map.Entry<String, Object> entry : source.entrySet()) {
            Object value = entry.getValue();
            if (value instanceof Set) {
                value = Collections.unmodifiableSet(new LinkedHashSet<String>(castStringSet(value)));
            }
            copy.put(entry.getKey(), value);
        }
        return Collections.unmodifiableMap(copy);
    }

    @SuppressWarnings("unchecked")
    static Set<String> castStringSet(Object value) {
        return (Set<String>) value;
    }

    private static void requireFieldCount(String[] fields, int expected) {
        if (fields.length != expected) {
            throw new IllegalArgumentException("Malformed remote preference entry");
        }
    }

    private static Float parseFloat(String value) {
        if ("NaN".equals(value) || "Infinity".equals(value) || "-Infinity".equals(value)) {
            return Float.valueOf(value);
        }
        Float parsed = Float.valueOf(value);
        if (parsed.isInfinite()) {
            throw new IllegalArgumentException("Float preference is outside the 32-bit range");
        }
        if (parsed.floatValue() == 0.0f && new BigDecimal(value).signum() != 0) {
            throw new IllegalArgumentException("Float preference underflows the 32-bit range");
        }
        return parsed;
    }

    private static String decodeUtf8(String encoded, int maxBytes) {
        byte[] bytes = Base64.getDecoder().decode(encoded);
        if (bytes.length > maxBytes) {
            throw new IllegalArgumentException("Remote preference text is too large");
        }
        return decodeRawUtf8(bytes);
    }

    private static String decodeRawUtf8(byte[] bytes) {
        String decoded = new String(bytes, StandardCharsets.UTF_8);
        if (!java.util.Arrays.equals(bytes, decoded.getBytes(StandardCharsets.UTF_8)) || decoded.indexOf('\0') >= 0) {
            throw new IllegalArgumentException("Remote preference text is not valid UTF-8");
        }
        return decoded;
    }

    private static String readString(ByteBuffer buffer, int maxBytes) {
        return decodeRawUtf8(readBytes(buffer, maxBytes));
    }

    private static byte[] readBytes(ByteBuffer buffer, int maxBytes) {
        if (buffer.remaining() < Integer.BYTES) {
            throw new IllegalArgumentException("Missing remote preference field size");
        }
        int size = buffer.getInt();
        if (size < 0 || size > maxBytes || size > buffer.remaining()) {
            throw new IllegalArgumentException("Invalid remote preference field size");
        }
        byte[] output = new byte[size];
        buffer.get(output);
        return output;
    }

    static final class Update {
        final String moduleId;
        final String group;
        final byte[] snapshot;

        Update(String moduleId, String group, byte[] snapshot) {
            this.moduleId = moduleId;
            this.group = group;
            this.snapshot = snapshot;
        }
    }
}
