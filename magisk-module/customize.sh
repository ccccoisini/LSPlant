#!/system/bin/sh
SKIPMOUNT=false
PROPFILE=true
POSTFSDATA=true
LATESTARTSERVICE=true

ui_print "- Installing Zygisk LSPlant Hook Framework"
mkdir -p /data/adb/hook/modules
if [ ! -f /data/adb/hook/target.txt ]; then
  printf "%s\n" "# Hook framework real-device acceptance target" \
    "io.hammer.developmentenvironmentdetection" > /data/adb/hook/target.txt
fi
chown -R 0:0 /data/adb/hook
chmod 0755 /data/adb/hook
chmod 0644 /data/adb/hook/target.txt
find /data/adb/hook/modules -type d -exec chmod 0755 {} \;
find /data/adb/hook/modules -type f -exec chmod 0644 {} \;
if [ -d "$MODPATH/hook/modules" ]; then
  cp -af "$MODPATH/hook/modules/." /data/adb/hook/modules/
  chown -R 0:0 /data/adb/hook/modules
  find /data/adb/hook/modules -type d -exec chmod 0755 {} \;
  find /data/adb/hook/modules -type f -exec chmod 0644 {} \;
fi
