#!/system/bin/sh
mkdir -p /data/adb/hook/modules
chown -R 0:0 /data/adb/hook
chmod 0755 /data/adb/hook
[ -f /data/adb/hook/target.txt ] && chmod 0644 /data/adb/hook/target.txt
find /data/adb/hook/modules -type d -exec chmod 0755 {} \;
find /data/adb/hook/modules -type f -exec chmod 0644 {} \;
