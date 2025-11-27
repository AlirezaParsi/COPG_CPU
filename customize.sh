#!/system/bin/sh

MODDIR=${0%/*}
CONFIG_DIR="/data/adb/modules/COPG_CPU"

mkdir -p $CONFIG_DIR

cp -f $MODDIR/apps.json $CONFIG_DIR/
cp -f $MODDIR/cpuinfo_spoof $CONFIG_DIR/

chmod 644 $CONFIG_DIR/apps.json
chmod 444 $CONFIG_DIR/cpuinfo_spoof
chown root:root $CONFIG_DIR/cpuinfo_spoof

chcon u:object_r:system_file:s0 $CONFIG_DIR/cpuinfo_spoof

echo "COPG CPU installed successfully!"
