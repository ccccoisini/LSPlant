#!/system/bin/sh
SKIPMOUNT=false
PROPFILE=true
POSTFSDATA=true
LATESTARTSERVICE=true

ui_print "- Installing zygisk_framework"
mkdir -p /data/adb/zygisk_framework/modules
mkdir -p /data/adb/zygisk_framework/bin
mkdir -p /data/adb/zygisk_framework/data
if [ ! -s "$MODPATH/framework_cli/zygisk_framework" ]; then
  abort "zygisk_framework CLI is missing from the install ZIP"
fi
cp -f "$MODPATH/framework_cli/zygisk_framework" /data/adb/zygisk_framework/bin/zygisk_framework
if [ ! -s /data/adb/zygisk_framework/bin/zygisk_framework ]; then
  abort "zygisk_framework CLI could not be installed"
fi
chown -R 0:0 /data/adb/zygisk_framework
chmod 0755 /data/adb/zygisk_framework
chmod 0755 /data/adb/zygisk_framework/bin
chmod 0755 /data/adb/zygisk_framework/bin/zygisk_framework
chmod 0700 /data/adb/zygisk_framework/data
find /data/adb/zygisk_framework/data -type d -exec chmod 0700 {} \;
find /data/adb/zygisk_framework/data -type f -exec chmod 0600 {} \;
find /data/adb/zygisk_framework/modules -type d -exec chmod 0755 {} \;
find /data/adb/zygisk_framework/modules -type f -exec chmod 0644 {} \;
if [ -d "$MODPATH/modules" ]; then
  cp -af "$MODPATH/modules/." /data/adb/zygisk_framework/modules/
  chown -R 0:0 /data/adb/zygisk_framework/modules
  find /data/adb/zygisk_framework/modules -type d -exec chmod 0755 {} \;
  find /data/adb/zygisk_framework/modules -type f -exec chmod 0644 {} \;
fi
rm -rf "$MODPATH/modules" "$MODPATH/hook" "$MODPATH/framework_cli"
