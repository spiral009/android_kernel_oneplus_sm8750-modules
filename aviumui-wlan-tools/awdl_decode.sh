#!/system/bin/sh
# AviumUI AWDL / AirDrop pcap decoder. Run where tshark exists (Termux:
# `pkg install tshark`, or the chroot). Extracts AWDL frames, peers, and any
# Bonjour/mDNS device names + AirDrop service records.
# Usage: awdl_decode.sh <capture.pcap>
PCAP="${1:?usage: awdl_decode.sh <pcap>}"
TSHARK=$(command -v tshark || echo tshark)

echo "########## AWDL action frames ##########"
"$TSHARK" -r "$PCAP" -Y awdl -T fields \
  -e frame.number -e wlan.sa -e wlan.da -e awdl.type 2>/dev/null | head -60

echo; echo "########## AWDL peers (devices announcing AWDL) ##########"
"$TSHARK" -r "$PCAP" -Y awdl -T fields -e wlan.sa 2>/dev/null | sort | uniq -c | sort -rn

echo; echo "########## Apple vendor-specific action frames (OUI 00:17:f2) ##########"
echo -n "count: "
"$TSHARK" -r "$PCAP" -Y 'wlan.fixed.category==127 && wlan.tag.oui==0x0017f2' 2>/dev/null | wc -l

echo; echo "########## Bonjour / mDNS names (sender device names leak here) ##########"
"$TSHARK" -r "$PCAP" -Y 'mdns || dns' -T fields \
  -e dns.resp.name -e dns.ptr.domain_name -e dns.txt 2>/dev/null \
  | tr '\t' '\n' | grep -iE 'airdrop|_tcp|\.local|iphone|ipad|macbook' | sort -u | head -40

echo; echo "########## AirDrop service (_airdrop._tcp) ##########"
"$TSHARK" -r "$PCAP" -Y 'dns.qry.name contains "airdrop" || dns.resp.name contains "airdrop"' \
  -T fields -e wlan.sa -e dns.resp.name 2>/dev/null | sort -u | head

echo; echo "########## AWDL data / IPv6 link-local (AirDrop HTTPS rides here, TLS-encrypted) ##########"
echo -n "AWDL-borne IPv6 packets: "
"$TSHARK" -r "$PCAP" -Y 'awdl && ipv6' 2>/dev/null | wc -l
echo "(file contents are TLS-encrypted and not recoverable from the capture)"
