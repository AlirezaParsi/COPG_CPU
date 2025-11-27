#!/system/bin/sh

MODDIR=${0%/*}
CONFIG_DIR="/data/adb/modules/COPG_CPU"

ui_print "**************************************"
ui_print "* COPG CPU Module Installer"
ui_print "**************************************"

ui_print "- Creating module directory..."
mkdir -p $CONFIG_DIR

ui_print "- Copying configuration files..."
cp -f $MODDIR/apps.json $CONFIG_DIR/
cp -f $MODDIR/cpuinfo_spoof $CONFIG_DIR/

ui_print "- Setting file permissions..."

chmod 0644 $CONFIG_DIR/apps.json
chown root:root $CONFIG_DIR/apps.json
chcon u:object_r:system_file:s0 $CONFIG_DIR/apps.json

chmod 0444 $CONFIG_DIR/cpuinfo_spoof
chown root:root $CONFIG_DIR/cpuinfo_spoof
chcon u:object_r:system_file:s0 $CONFIG_DIR/cpuinfo_spoof

ui_print "- Creating debug log file..."
touch $CONFIG_DIR/debug.log
chmod 0644 $CONFIG_DIR/debug.log
chown root:root $CONFIG_DIR/debug.log
chcon u:object_r:system_file:s0 $CONFIG_DIR/debug.log

chmod 0755 $CONFIG_DIR
chown root:root $CONFIG_DIR
chcon u:object_r:system_file:s0 $CONFIG_DIR

ui_print "- Installation complete!"
ui_print "**************************************"
