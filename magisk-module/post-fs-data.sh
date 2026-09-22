#!/system/bin/sh
mkdir -p /data/adb/zygisk_framework/modules
chown -R 0:0 /data/adb/zygisk_framework
chmod 0755 /data/adb/zygisk_framework
[ -f /data/adb/zygisk_framework/target.txt ] && chmod 0644 /data/adb/zygisk_framework/target.txt
find /data/adb/zygisk_framework/modules -type d -exec chmod 0755 {} \;
find /data/adb/zygisk_framework/modules -type f -exec chmod 0644 {} \;
