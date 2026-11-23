# LORA Tunnel for CardputerZero 1.3.0

LORA Tunnel turns the M5Stack CardputerZero with the Cap LoRa-1262 into an all-in-one LoRa, Wi-Fi and Bluetooth wardriver. MIT open source.

## Install

On the CardputerZero, connected to Wi-Fi, run this in a terminal or over SSH:

```sh
curl -fL -o /tmp/lora-tunnel.deb https://github.com/valleytechsolutions/LORA-Tunnel/releases/download/cardputer-zero-v1.3.0/lora-tunnel-cardputer-zero.deb && sudo apt install -y /tmp/lora-tunnel.deb && sudo reboot
```

It installs next to your other apps and replaces nothing. After the reboot, open **LORA Tunnel** in APPLaunch and pick your region. If Bluetooth reports it's switched off, run `sudo rfkill unblock bluetooth` once.

## New in 1.3.0

- **Recon knows more:** dashcams and fleet cameras, in-car fleet routers, Sonos speakers, the Flipper Devices Bluetooth ID, the Dragon Detector fox beacon, and whether an AirTag or other Find My accessory is away from its owner.
- **Evil twins:** an open Wi-Fi network using the name of a protected one nearby is flagged, with the network it copies.
- **BLE advert bursts:** a sudden flood of new Apple pop-up, Fast Pair, Swift Pair or tracker adverts is flagged once, as one entry, instead of filling the list.
- Fleet routers, smart speakers and the fox beacon start off in **Settings → Recon → Categories**. Evil twins and advert bursts start on, also for people updating from an older version.
- The new rules come from Dragon Detector and the public IEEE and Bluetooth registries. LORA Tunnel stays MIT.

## New in 1.2.0

- **Check for updates:** LORA Tunnel tells you when a newer version is in the App Store or on GitHub, and how to install it. It checks once when it opens (you can turn that off) and any time from **Settings → Updates**. It only reads version numbers and never installs anything by itself; the Store does the update with one key.

## New in 1.1.1

- **Every symbol types with Sym:** text fields (your mesh name, channel keys and names, hashtags) now accept every symbol printed on the keys, such as `_ - + = # @ /`. The built-in keyboard sends its own codes for Sym symbols, and LORA Tunnel now reads them the same way the CardputerZero launcher does, including the system keymap file. A USB keyboard still works as a normal PC keyboard.

## What's in it

- **One-press wardriving:** LoRa, Wi-Fi and BLE together. Every Wi-Fi network and BLE device is logged with GPS to a WiGLE-ready `wigle.csv`.
- **Full Meshtastic decoding:**
  - text, alerts, detection sensors and range tests;
  - positions and node info (hardware, role, public key);
  - every telemetry kind;
  - traceroutes with per-hop SNR, neighbor lists, routing ACKs, map reports, waypoints and paxcounter.
- **Full MeshCore decoding:** signed adverts, public, hashtag and private group text (including signed and CLI), group data, traces with SNR, discovery, anonymous requests, ACKs and multipart.
- **Recon:**
  - Flags ALPR and Flock cameras, body cameras, Ring and other cameras, trackers (AirTag, SmartTag, Tile), Remote ID drones, camera glasses, skimmer modules and hacking tools.
  - Uses the same rules as the LORA Tunnel ADV firmware.
  - Alerts, a "following you" check and a signal finder.
- **Messaging:** Meshtastic and MeshCore channels. Transmit is off until you turn it on, and it keeps to region power and duty-cycle limits.
- **Logs:** LoRaTAP PCAP (Wireshark), JSONL, SQLite, CSV and KML exports.

Files: `lora-tunnel-cardputer-zero.deb` (same file as `lora-tunnel_1.2.0_all.deb`) and `SHA256SUMS`.
