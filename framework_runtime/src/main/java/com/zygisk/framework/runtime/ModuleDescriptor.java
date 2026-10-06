package com.zygisk.framework.runtime;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Properties;
import java.util.Set;

final class ModuleDescriptor {
    static final class IncompatibleApiException extends IllegalArgumentException {
        IncompatibleApiException(String message) { super(message); }
    }

    final String moduleId;
    final String modulePath;
    final String legacyPackageName;
    final ByteBuffer dexBuffer;
    final List<String> entryClasses;
    final Properties properties;
    final Set<String> scopeRules;

    private ModuleDescriptor(
            String moduleId,
            String modulePath,
            String legacyPackageName,
            ByteBuffer dexBuffer,
            List<String> entryClasses,
            Properties properties,
            Set<String> scopeRules) {
        this.moduleId = moduleId;
        this.modulePath = modulePath;
        this.legacyPackageName = legacyPackageName;
        this.dexBuffer = dexBuffer;
        this.entryClasses = entryClasses;
        this.properties = properties;
        this.scopeRules = scopeRules;
    }

    static ModuleDescriptor create(
            String moduleId,
            ByteBuffer dexBuffer,
            String javaInitList,
            String moduleProp,
            String scopeList) {
        List<String> entries = parseList(javaInitList, true);
        if (entries.isEmpty()) {
            throw new IllegalArgumentException("java_init.list is empty for " + moduleId);
        }
        Properties props = new Properties();
        try {
            props.load(new java.io.StringReader(moduleProp == null ? "" : moduleProp));
        } catch (java.io.IOException exception) {
            throw new IllegalArgumentException("module.prop parse failed for " + moduleId, exception);
        }
        int minApi = parseInt(props.getProperty("minApiVersion"), 82);
        int targetApi = parseInt(props.getProperty("targetApiVersion"), 82);
        if (minApi > 82 || targetApi < 82) {
            throw new IncompatibleApiException(
                    "Unsupported API range min=" + minApi + " target=" + targetApi);
        }
        String legacyPackageName = props.getProperty("legacyPackageName", moduleId).trim();
        if (!legacyPackageName.matches("[A-Za-z0-9_][A-Za-z0-9_.]*")) {
            throw new IllegalArgumentException("Invalid legacyPackageName: " + legacyPackageName);
        }
        return new ModuleDescriptor(
                moduleId,
                "/data/adb/zygisk_framework/modules/" + moduleId,
                legacyPackageName,
                dexBuffer,
                entries,
                props,
                new HashSet<String>(parseList(scopeList, false)));
    }

    boolean matches(String packageName, String processName) {
        if (scopeRules.isEmpty()) {
            return false;
        }
        for (String rule : scopeRules) {
            if (matchesRule(rule, packageName) || matchesRule(rule, processName)) {
                return true;
            }
        }
        return false;
    }

    private static List<String> parseList(String text, boolean classNames) {
        ArrayList<String> result = new ArrayList<String>();
        String safe = text == null ? "" : text;
        String[] lines = safe.replace("\r\n", "\n").replace('\r', '\n').split("\n");
        for (String raw : lines) {
            String line = raw.trim();
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }
            if (classNames && !line.matches("[A-Za-z_$][A-Za-z0-9_$]*(\\.[A-Za-z_$][A-Za-z0-9_$]*)*")) {
                throw new IllegalArgumentException("Invalid class name: " + line);
            }
            result.add(line);
        }
        return result;
    }

    private static boolean matchesRule(String rule, String processName) {
        if (rule == null || processName == null) {
            return false;
        }
        if (processName.equals(rule)) {
            return true;
        }
        return !rule.contains(":") && processName.startsWith(rule + ":");
    }

    private static int parseInt(String value, int fallback) {
        if (value == null) {
            return fallback;
        }
        try {
            return Integer.parseInt(value.trim().toLowerCase(Locale.ROOT));
        } catch (NumberFormatException exception) {
            return fallback;
        }
    }
}
