#!/system/bin/sh
SKIPMOUNT=false
PROPFILE=true
POSTFSDATA=true
LATESTARTSERVICE=true

ui_print "- Installing zygisk_framework"
mkdir -p /data/adb/zygisk_framework/modules
if [ ! -f /data/adb/zygisk_framework/target.txt ]; then
  printf "%s\n" "# Hook framework real-device acceptance target" \
    "io.hammer.developmentenvironmentdetection" > /data/adb/zygisk_framework/target.txt
fi
chown -R 0:0 /data/adb/zygisk_framework
chmod 0755 /data/adb/zygisk_framework
chmod 0644 /data/adb/zygisk_framework/target.txt
find /data/adb/zygisk_framework/modules -type d -exec chmod 0755 {} \;
find /data/adb/zygisk_framework/modules -type f -exec chmod 0644 {} \;
if [ -d "$MODPATH/modules" ]; then
  cp -af "$MODPATH/modules/." /data/adb/zygisk_framework/modules/
  chown -R 0:0 /data/adb/zygisk_framework/modules
  find /data/adb/zygisk_framework/modules -type d -exec chmod 0755 {} \;
  find /data/adb/zygisk_framework/modules -type f -exec chmod 0644 {} \;
fi
rm -rf "$MODPATH/modules" "$MODPATH/hook"
