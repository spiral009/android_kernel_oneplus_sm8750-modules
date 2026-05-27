#!/system/bin/sh
# Self-contained injection test. Survives adb drops (run via nohup).
# Phase A: mon_inject_enable=0 -> confirm injected frames REACH the driver.
# Phase B: mon_inject_enable=1 -> attempt real TX to firmware.
LOG=/data/local/tmp/inject_test.log
PARAM=/sys/module/qca_cld3_peach_v2/parameters
exec > "$LOG" 2>&1
echo "==== INJECT TEST $(date) ===="

# enable monitor (firmware fix + con_mode=4)
sh /data/local/tmp/mon_on.sh
sleep 2
iw dev wlan0 set channel 6
echo "con_mode=$(cat $PARAM/con_mode) chan-set-done"

echo "---- PHASE A: mon_inject_enable=0 (do frames reach driver?) ----"
echo 0 > $PARAM/mon_inject_enable
dmesg -c >/dev/null 2>&1
/data/local/tmp/mon_inject wlan0 15
sleep 1
echo "AVIUM MON-INJECT lines seen (phase A):"
dmesg | grep "AVIUM MON-INJECT" | tail -20
echo "phaseA_count=$(dmesg | grep -c 'AVIUM MON-INJECT')"

echo "---- PHASE B: mon_inject_enable=1 (real TX attempt) ----"
echo 1 > $PARAM/mon_inject_enable
echo "inject_enable=$(cat $PARAM/mon_inject_enable)"
dmesg -c >/dev/null 2>&1
echo "AVIUM_MARKER_PHASEB_TX > kmsg"; echo "AVIUM_MARKER_PHASEB_TX" > /dev/kmsg
/data/local/tmp/mon_inject wlan0 15
sleep 2
echo "dmesg after en=1 inject (MON-INJECT / mgmt / tx / error):"
dmesg | grep -iE "AVIUM MON-INJECT|mgmt|tx_send|inject|cdp|Call trace|BUG" | tail -25
echo "phaseB_inject_count=$(dmesg | grep -c 'AVIUM MON-INJECT')"
echo "survived_phaseB=YES uptime=$(cat /proc/uptime)"

echo "---- REVERT ----"
echo 0 > $PARAM/mon_inject_enable
sh /data/local/tmp/mon_off.sh
echo "==== DONE con_mode=$(cat $PARAM/con_mode) ===="
