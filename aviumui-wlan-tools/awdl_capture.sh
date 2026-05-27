#!/system/bin/sh
# AviumUI AWDL / AirDrop capture.
# Enables monitor mode, hops the AWDL social channels (6, 44, 149) and writes
# a pcap. Self-reverting. Trigger AirDrop / AirPlay on a nearby Apple device
# while this runs so AWDL frames are on the air.
# Usage: awdl_capture.sh [duration_sec] [outfile.pcap]
DUR="${1:-60}"
OUT="${2:-/sdcard/awdl_$(date +%Y%m%d_%H%M%S).pcap}"
LOG=/data/local/tmp/awdl_capture.log
exec > "$LOG" 2>&1
echo "==== AWDL CAPTURE $(date) dur=${DUR}s out=$OUT ===="
sh /data/local/tmp/mon_on.sh
sleep 2
timeout "$DUR" tcpdump -i wlan0 -nn -s 0 -w "$OUT" -U 2>>"$LOG" &
TD=$!
END=$(( $(date +%s) + DUR ))
while [ "$(date +%s)" -lt "$END" ]; do
  for c in 6 44 149; do iw dev wlan0 set channel $c 2>/dev/null; sleep 1.2; done
done
kill $TD 2>/dev/null; sleep 1
echo "captured file: $(ls -la "$OUT" 2>/dev/null)"
echo "total frames:  $(tcpdump -r "$OUT" -nn 2>/dev/null | wc -l)"
echo "action frames: $(tcpdump -r "$OUT" -nn 2>/dev/null | grep -ic Action)"
sh /data/local/tmp/mon_off.sh
echo "==== DONE — pcap: $OUT ===="
echo "Decode it (in Termux, after: pkg install tshark):"
echo "  sh /data/local/tmp/awdl_decode.sh $OUT"
