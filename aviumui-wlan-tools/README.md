# AviumUI WLAN monitor / injection / AWDL tools

Helper scripts for the qcacld-3.0 monitor-mode work on the OnePlus dodge
(SM8750, "peach"/WCN7851, `qca_cld3_peach_v2`). They pair with the driver
patches in this repo (commits enabling monitor injection, active-monitor flag,
regulatory unlock, and monitor-channel reporting).

Run on-device as root (e.g. from Termux `su`). Most scripts expect to live in
`/data/local/tmp/`.

## Scripts

| File | Where | What it does |
|------|-------|--------------|
| `build_wlan.sh` | build host | Rebuilds `qca_cld3_peach_v2.ko` against the running kernel (vermagic-matching). Needs clang-r563880c + kernel-build-tools (pahole) in PATH; outputs a stripped `.ko` to `/tmp`. |
| `mon_on.sh` | device | Enter global monitor mode (`con_mode=4`). Fixes the firmware reload (stages a consolidated copy of the split `amss`/`bdwlan` firmware to `/data/local/tmp/fw` and points the kernel firmware loader at it). Aborts if a Droidspaces container is running (that combo reboots the device). |
| `mon_off.sh` | device | Revert to normal STA (`con_mode=0`), restore firmware path, re-enable Wi-Fi. |
| `awdl_capture.sh [secs] [out.pcap]` | device | Monitor + channel-hop the AWDL social channels (6/44/149), capture to pcap, auto-revert. |
| `awdl_decode.sh <pcap>` | Termux (`pkg install tshark`) | Extract AWDL action frames, peers, Apple vendor frames (OUI `00:17:f2`), Bonjour/mDNS device names and `_airdrop._tcp` records. |
| `mon_inject.c` | build in chroot/Termux (`gcc -static`) | Minimal AF_PACKET 802.11 injector (sets `PACKET_QDISC_BYPASS`); used to test the injection path. |
| `tests/inject_test.sh` | device | Confirms injected frames reach the driver (`mon_inject_enable` 0 then 1). |
| `tests/deauth_cap.sh` | device | Targeted deauth + simultaneous monitor capture, to check OTA transmission. |

## Quick start

```sh
# monitor capture
su -c 'sh /data/local/tmp/mon_on.sh'
su -c 'iw dev wlan0 set channel 6'
su -c 'tcpdump -i wlan0 -w /sdcard/cap.pcap'
su -c 'sh /data/local/tmp/mon_off.sh'

# AirDrop / AWDL (trigger AirDrop on a nearby Apple device first)
su -c 'sh /data/local/tmp/awdl_capture.sh 60 /sdcard/airdrop.pcap'
sh /data/local/tmp/awdl_decode.sh /sdcard/airdrop.pcap
```

## Status / findings

- **Monitor capture:** works (2.4 + 5 GHz incl. DFS/AWDL channels), reversible.
- **airodump-ng channel `-1`:** fixed in-driver (`get_channel` reports the
  monitor channel) — `--ignore-negative-one` no longer needed.
- **Injection:** the kernel path works (frames reach `ndo_start_xmit` after the
  monitor TX queue is woken), but the FullMAC **firmware does not radiate** TX
  from a monitor vdev — verified by deauth test (0 frames on air, target
  unaffected). Raw injection / OWL / OpenDrop are blocked by the firmware.
- **AirDrop:** passive AWDL frame + mDNS metadata capture works; file contents
  are TLS-encrypted; active participation needs injection (blocked).

Tested on hardware. Module deploy is replace-on-partition + reboot
(`/vendor_dlkm/lib/modules/qca_cld3_peach_v2.ko`) — the driver cannot be
runtime-unloaded (`rmmod` hangs in `cnss_driver_event_post`).
