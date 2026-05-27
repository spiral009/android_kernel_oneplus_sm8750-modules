#!/system/bin/sh
# Revert monitor mode -> normal STA on OnePlus dodge.
MOD=qca_cld3_peach_v2
PARAM=/sys/module/$MOD/parameters/con_mode
ip link set wlan0 down 2>/dev/null
echo 0 > "$PARAM"
sleep 2
# restore the original firmware search path (don't break the Droidspaces chroot)
if [ -s /data/local/tmp/fw_path.bak ]; then
  cat /data/local/tmp/fw_path.bak > /sys/module/firmware_class/parameters/path 2>/dev/null
fi
svc wifi enable 2>/dev/null
sleep 3
echo "con_mode=$(cat $PARAM)"
iw dev wlan0 link 2>/dev/null | head -2
