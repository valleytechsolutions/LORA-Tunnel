# LORA Tunnel

**LORA Tunnel 0.6.0 · MIT open source**

LORA Tunnel turns a supported handheld into a standalone Wi-Fi, Bluetooth Low Energy and LoRa wardriving recorder. It captures radio observations, adds receiver time and location when GNSS provides them, and saves sessions to microSD for later review and export. A separate Recon mode groups supported Wi-Fi/BLE indicators and provides optional audible alerts. The Messages mode provides Meshtastic and MeshCore channel and private messaging when capture is stopped.

The firmware runs on **Heltec WiFi LoRa 32 Expansion Kit V2 / V4-R8** and **M5Stack Cardputer ADV with Cap LoRa-1262 (U214)**. Each board has its own binary. Capture runs on the handheld without a laptop, phone or Kismet server.

[Install with the web flasher](https://valleytech-lora-tunnel.pages.dev/) · [Download 0.6.0](https://github.com/valleytechsolutions/LORA-Tunnel/releases/tag/v0.6.0) · [Project website](https://valleytechsolutions.tech/pages/lora-tunnel) · [Gitea distribution](https://git.churchofmalware.org/Valleytech/LORA-Tunnel)

## What changed in 0.6.0

Choose **LongTurbo** or another supported Meshtastic preset, select an automatic or explicit frequency slot, and configure the MeshCore receive profile under **Settings → Mesh radio / decode settings**. Wardriver can stay on Meshtastic, stay on MeshCore, or alternate between both. Known-key MeshCore group-text decoding joins the existing Meshtastic payload decoder and signed MeshCore public-announcement decoder. Raw received bytes and receive-time radio settings remain in the log.

**Home → Messages** adds channel messages, private conversations, identity announcements, public-key review and receipt status for both protocols. Transmission starts OFF and requires confirmation. Messages uses one protocol at a time and pauses Wi-Fi/BLE/LoRa capture. The physical keyboard on Cardputer and touchscreen keyboard on Heltec support message and credential entry. [Messaging controls and compatibility](docs/MESSAGING.md).

This release also repairs Heltec touch-report handling and command feedback, binds contact-trust confirmation to the reviewed public key, distinguishes different MeshCore group messages sharing a name/time, and automatically stops capture when storage fails. Separate Launcher application images are supplied for **both boards**. [Release notes and validation limits](docs/RELEASE_0_6_0.md) · [Receive settings and decoder scope](docs/COMBINED_MESH.md).

Combined wardriving uses one selected profile per protocol and a default 30-second dwell. The single SX1262 cannot receive both profiles simultaneously or monitor every preset. Existing configurations remain Meshtastic-only until the user changes the mode.

## Supported hardware

| Target | Required hardware | Interface and memory |
| --- | --- | --- |
| Heltec | [WiFi LoRa 32 Expansion Kit V2 / V4-R8](https://heltec.org/project/v4-r8-ex/), 863–928 MHz variant | 240 × 320 touchscreen, 16 MB flash, 8 MB PSRAM, onboard GNSS and microSD |
| Cardputer | [Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv) with [Cap LoRa-1262, U214](https://docs.m5stack.com/en/cap/Cap_LoRa-1262), 868–923 MHz | 240 × 135 display, physical keyboard, 8 MB flash, no PSRAM; cap supplies LoRa and GNSS |
| Wio Tracker L2 (source build, untested on hardware) | Seeed Wio Tracker L2 | 320 × 240 touchscreen, landscape or portrait; 16 MB flash, 8 MB PSRAM, L76K GNSS, microSD |

The Heltec V1, low-band Heltec kits, original Cardputer and other M5Stack caps are separate hardware. Check the board and fitted radio before installing. [Heltec hardware notes](docs/HARDWARE.md) · [Cardputer setup](docs/CARDPUTER_ADV.md) · [Wio Tracker L2 notes](docs/WIO_TRACKER_L2.md).

## Wardriver and Recon

| Function | Behavior |
| --- | --- |
| Wi-Fi | Passive 2.4 GHz AP observations with BSSID, available SSID, channel, RSSI and security metadata. Current Wi-Fi scan channels are 1–11. |
| BLE | Passive advertisements with address type, RSSI, available name and service/manufacturer data. Random addresses are not permanent device identities. |
| LoRa | Receive-only Meshtastic/MeshCore profiles, local supported-payload decoding, RSSI/SNR, raw bytes, per-packet profile snapshots and slow switching. |
| Messages | Separate Meshtastic/MeshCore channel and private messaging, explicit TX permission, public-key trust review and differentiated delivery receipts. |
| GNSS | Receiver UTC, fix validity, age and available quality information. Missing or stale coordinates remain explicit. Mesh-advertised positions are separate. |
| Storage | Versioned JSONL sessions with bounded queues, checked buffered writes and visible drop/error counters. A storage failure stops capture and marks the log incomplete. |
| Recon | Separate passive Wi-Fi/BLE evidence views, category filters, optional SD rules and once-per-device-per-session alerts. LoRa stops in Recon. |
| Export/upload | Local WiGLE CSV export and optional idle-only WiGLE, Wardrift and WDGWars CSV uploads. Mesh export has a separate, limited contract. |

Wi-Fi and BLE share the ESP32-S3 radio and run on a schedule. A Recon match is an indicator, not proof of ownership, recording or identity. A relayed mesh packet does not establish that its original sender is nearby.

| Heltec Home | Wardriver | Recon |
| --- | --- | --- |
| ![Heltec Home](assets/ui/home.png) | ![Heltec Wardriver](assets/ui/wardriver.png) | ![Heltec Recon](assets/ui/recon.png) |

| Cardputer Home | Wardriver | Recon |
| --- | --- | --- |
| ![Cardputer Home](assets/ui/cardputer/home.png) | ![Cardputer Wardriver](assets/ui/cardputer/wardriver.png) | ![Cardputer Recon](assets/ui/cardputer/recon.png) |

These interface previews are rendered from the drawing code with synthetic data. They are not photographs or recorded networks. [Interface guide](docs/UI_GUIDE.md) · [Preview details](assets/ui/README.md).

## Install and start a session

1. Identify the exact board and retain its current recovery image. Read the [Heltec recovery procedure](docs/RECOVERY.md) or [Cardputer installation guide](docs/CARDPUTER_ADV.md).
2. Prepare a FAT32 microSD card with the supplied `lora-tunnel` folder. Copy `config.example.json` to `config.json` **on the card**. Keep an existing private configuration when updating; the release starter contains empty credentials. [SD setup](sdcard/README.md).
3. Select the matching board in the [web flasher](https://valleytech-lora-tunnel.pages.dev/). Factory images are merged binaries for USB installation at `0x0`; installing one replaces Launcher and the existing boot layout.
4. To retain Bmorcelli Launcher, use the separate [Heltec Launcher application](https://github.com/valleytechsolutions/LORA-Tunnel/releases/download/v0.6.0/LORA-Tunnel-0.6.0-Heltec-V4-R8-TFT-HIGH-BAND-LAUNCHER-APP.bin) or [Cardputer Launcher application](https://github.com/valleytechsolutions/LORA-Tunnel/releases/download/v0.6.0/LORA-Tunnel-0.6.0-Cardputer-ADV-LoRa1262-LAUNCHER-APP.bin). Install through Launcher's SD menu and let Launcher choose the destination. Never flash an application-only BIN at `0x0`.
5. On first boot, select the LoRa region for your location or choose **Skip LoRa**. LoRa remains off until a supported region is selected. Stop capture and open **Settings → Mesh radio / decode settings** to choose protocol mode, Meshtastic preset/slot, MeshCore profile, dwell and public decoding.
6. Open Wardriver and start a session. Check storage and GPS status. Use **Stop + Save** and wait for saving to finish before removing power or the card. Native sessions are stored under `/sessions/`.

Heltec uses touch controls, with optional physical navigation buttons. Cardputer uses its keyboard. Software shutdown saves before sleep; wake controls differ by board. [User guide](docs/USER_GUIDE.md) · [Web flasher guide](docs/WEB_FLASHER.md) · [Cardputer controls](docs/CARDPUTER_ADV.md).

## Configuration and uploads

The SD configuration holds radio mode, modem preset, slot, dwell time, region, detection preferences and optional credentials. The default MeshCore profile is a regional starting point, not automatic discovery of the local network. Match the protocol, frequency, bandwidth and spreading factor to the network being observed. Passive decoding accepts up to four Meshtastic channel keys and four MeshCore group keys configured privately; unknown or unsupported encrypted payloads remain recorded as such. Messages has its own channel/key settings. Preset and key settings require a working SD configuration; region-only NVS recovery returns to that region's LongFast starting profile.

WiGLE, Wardrift and WDGWars uploads are opt-in and run only while capture is stopped and saved. Automatic uploads are off by default. Configure home Wi-Fi and the credentials for the chosen service; starting capture interrupts upload work. The Heltec provides an on-screen credential keyboard and the Cardputer uses its physical keyboard. Credentials are plaintext on the removable card and must stay out of source control and shared logs. [Upload setup and eligibility](docs/UPLOADS.md).

WiGLE export requires valid receiver coordinates and UTC. Native logs retain observations that do not qualify. Mesh observations are not ordinary WiGLE rows; the WDGWars mesh candidate exporter remains separate from CSV uploads and its live ingestion is unverified. [Log schema](docs/LOG_FORMAT.md) · [Export instructions](docs/EXPORTS.md).

## Build and flash from source

Use Python 3.10 or newer and PlatformIO Core 6.2.0. The platform and libraries are pinned in `platformio.ini`; each board's wiring and memory limits are isolated in its board configuration.

```sh
python -m pip install platformio==6.2.0
python -m platformio run -e heltec-v4-r8-tft
python -m platformio run -e cardputer-adv-lora1262
python -m platformio run -e wio-tracker-l2
```

Select the actual USB port before upload. Keep firmware, partition table and bootloader from the same build. The Heltec test unit requires the documented [esptool 5.4.0 legacy-stub fallback](docs/RECOVERY.md#legacy-stub-installation-fallback) when the default loader fails. A successful build is distinct from a verified flash and a successful radio capture.

Release downloads include factory and Launcher application images for both boards, source, separate dependency/relinking archives, an SD starter and SHA-256 checksums. [Rebuild and licensing materials](docs/RELINKING.md).

## Validation and current limits

The [0.6.0 release record](docs/RELEASE_0_6_0.md) separates software checks, the owner's acceptance, and instrumented hardware observations. Its preceding chat5 Heltec build automatically stopped a capture after a write I/O error; the storage cause remains unidentified and that run did not achieve 30-minute capture acceptance. A malformed USB status line also interrupted one monitor. Follow-up receive-only checks completed without observed reset, watchdog or allocation failure. These results do not establish lossless reception, durable complete logs or a completed peer-interoperability matrix.

Controlled channel/private peer exchanges, delivery receipts, final-image Launcher round trips and this messaging build's Cardputer hardware checks remain independently unverified. Messages supports private traffic addressed to its own trusted identity; passive Wardriver does not decrypt other users' private messages. Meshtastic channel text and NodeInfo are unsigned, so peers requiring `STRICT` signatures reject them; received XEdDSA signatures are not verified. Arbitrary hopping lists, a full upstream mesh-node stack and Kismet datasource/PCAP-NG integration are outside this release. [Messaging limits](docs/MESSAGING.md) · [Historical 0.5.0 results](docs/RELEASE_0_5_0.md).

## Credits and license

Original LORA Tunnel code is released under the [MIT License](LICENSE). Third-party components keep their own licenses. [Full attribution and license inventory](docs/THIRD_PARTY.md).

LoRaPipe and Kismet inspired the capture/profile approach and informed protocol research. LORA Tunnel implements the capture, display, storage and supported decoding paths locally. Meshtastic and MeshCore protocol definitions informed the original parsers. RadioLib supplies the SX1262 driver; Arduino-ESP32, NimBLE-Arduino, TinyGPSPlus, ArduinoJson, LovyanGFX and the M5Stack libraries support the hardware. Crypto and the retained libsodium-derived validation constants have their corresponding notices.

Recon includes the attributed Dragon Detector parser subset and inherited ESP32Marauder/Flock Noir credits; OpenDroneID retains its Apache-2.0 license. The browser installer uses ESPHome's ESP Web Tools with its notices. Heltec and M5Stack are the hardware manufacturers. LORA Tunnel is an independent Valleytech Solutions project.
