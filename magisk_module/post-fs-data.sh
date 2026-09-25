#!/system/bin/sh
mkdir -p /data/adb/zygisk_framework/modules
mkdir -p /data/adb/zygisk_framework/bin
chown -R 0:0 /data/adb/zygisk_framework
chmod 0755 /data/adb/zygisk_framework
chmod 0755 /data/adb/zygisk_framework/bin
chmod 0755 /data/adb/zygisk_framework/bin/zygisk_framework
find /data/adb/zygisk_framework/modules -type d -exec chmod 0755 {} \;
find /data/adb/zygisk_framework/modules -type f -exec chmod 0644 {} \;
