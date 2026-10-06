package com.zygisk.framework.runtime;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertThrows;

import org.junit.Test;

import java.nio.ByteBuffer;

/** 验证模块 API 82 元数据、legacyPackageName 和 scope 的解析。 */
public class ModuleDescriptorTest {
    /** 验证 API 82 范围与可选旧包名映射，并拒绝仅面向 API 102 的模块。 */
    @Test
    public void validatesApiRangeAndLegacyPackageName() {
        ModuleDescriptor descriptor = ModuleDescriptor.create(
                "module.sample", ByteBuffer.allocate(1), "example.Entry",
                "minApiVersion=82\ntargetApiVersion=82\nlegacyPackageName=legacy.sample\n",
                "com.example.target\n");
        assertEquals("legacy.sample", descriptor.legacyPackageName);
        assertEquals("/data/adb/zygisk_framework/modules/module.sample", descriptor.modulePath);
        assertTrue(descriptor.matches("com.example.target", "com.example.target:worker"));
        assertFalse(descriptor.matches("com.example.other", "com.example.other"));

        ModuleDescriptor defaultAlias = ModuleDescriptor.create(
                "module.default", ByteBuffer.allocate(1), "example.Entry",
                "minApiVersion=82\ntargetApiVersion=82\n", "com.example.target");
        assertEquals("module.default", defaultAlias.legacyPackageName);

        assertThrows(ModuleDescriptor.IncompatibleApiException.class, () -> ModuleDescriptor.create(
                "module.new", ByteBuffer.allocate(1), "example.Entry",
                "minApiVersion=102\ntargetApiVersion=102\n", "com.example.target"));
    }

    /** 验证不合法的 legacyPackageName 不会污染包名映射表。 */
    @Test
    public void rejectsInvalidLegacyPackageName() {
        assertThrows(IllegalArgumentException.class, () -> ModuleDescriptor.create(
                "module.bad", ByteBuffer.allocate(1), "example.Entry",
                "legacyPackageName=../bad\n", "com.example.target"));
    }
}
