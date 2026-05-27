#!/system/bin/sh
# Enable global monitor mode on OnePlus dodge (qca_cld3_peach_v2 / con_mode=4).
# The fix: con_mode switch forces a firmware RE-LOAD; firmware is split across
# /vendor/firmware_mnt/image/peach (amss) and /odm/etc/wifi/peach (bdwlan.b0i),
# so we stage a consolidated copy and point the kernel firmware loader at it,
# while ueventd also services the request from the vendor/odm dirs.
MOD=qca_cld3_peach_v2
FW=/data/local/tmp/fw
PARAM=/sys/module/$MOD/parameters/con_mode

# AviumUI: a running Droidspaces container during the con_mode switch reboots
# the device. Abort with guidance rather than crashing.
if command -v droidspaces >/dev/null 2>&1; then
  if ! droidspaces show 2>/dev/null | grep -qi "No containers running"; then
    echo "ABORT: a Droidspaces container is running and will crash the monitor"
    echo "switch. Stop it first (see: droidspaces show ; droidspaces stop <name>)."
    exit 1
  fi
fi

# stage consolidated firmware once
if [ ! -f $FW/peach/bdwlan.b0i ] || [ ! -f $FW/peach/amss20.bin ]; then
  mkdir -p $FW/peach
  cp -f /vendor/firmware_mnt/image/peach/*.bin $FW/peach/ 2>/dev/null
  cp -f /vendor/firmware_mnt/image/peach/bdwlan.* $FW/peach/ 2>/dev/null
  cp -f /odm/etc/wifi/peach/* $FW/peach/ 2>/dev/null
  chcon -R u:object_r:firmware_file:s0 $FW 2>/dev/null
fi

# save + set firmware search path
cat /sys/module/firmware_class/parameters/path > /data/local/tmp/fw_path.bak 2>/dev/null
echo -n "$FW" > /sys/module/firmware_class/parameters/path

svc wifi disable 2>/dev/null
sleep 2
ip link set wlan0 down 2>/dev/null
sleep 1
echo 4 > "$PARAM"
sleep 4
ip link set wlan0 up 2>/dev/null
echo "con_mode=$(cat $PARAM)"
iw dev wlan0 info 2>/dev/null | grep -iE "type|channel"
echo "Monitor ready on wlan0. Set channel: iw dev wlan0 set channel <N>"
