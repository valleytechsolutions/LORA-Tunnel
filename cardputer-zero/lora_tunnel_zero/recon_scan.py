"""Recon receivers and live device table.

Wi-Fi comes from the CM0's own radio through `iw` scan results (dBm), with
NetworkManager's `nmcli` as the fallback. BLE comes from BlueZ over D-Bus
(python3-dbus). Both are passive: the app reads beacons and advertisements and
never connects, pairs or probes. Everything runs as the normal user.
"""

from collections import deque
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import threading
import time
import wave
import zlib

from . import recon
from .store import MIN_FREE_BYTES, atomic_json, create_session, read_metadata

WIFI_INTERVAL = 8.0
BLE_INTERVAL = 1.0
LOG_INTERVAL = 30.0
ALERT_REPEAT = 300.0
FOLLOW_MIN_SECONDS = 600
FOLLOW_MIN_METERS = 800
MAX_DEVICES = 2000
TWIN_HITS, TWIN_RECENT, TWIN_HOLD, TWIN_QUIET = 2, 20.0, 120.0, 1800.0
BURST_WINDOW, BURST_ADDRESSES, BURST_VARIANTS, BURST_QUIET = 5.0, 12, 24, 600.0
BURST_NAMES = {"apple": "Apple pop-up adverts", "fastpair": "Google Fast Pair adverts",
               "swiftpair": "Microsoft Swift Pair adverts", "tracker": "Tracker adverts"}


class ReconUnavailable(RuntimeError):
    pass


# ------------------------------------------------------------------ Wi-Fi
def wireless_interfaces():
    try:
        return sorted(p.name for p in Path("/sys/class/net").iterdir() if (p / "wireless").exists())
    except OSError:
        return []


def _unescape(text):
    """iw prints non-printable SSID bytes as \\xNN."""
    raw = bytearray()
    i = 0
    while i < len(text):
        if text[i] == "\\" and i + 3 < len(text) and text[i + 1] == "x":
            try:
                raw.append(int(text[i + 2:i + 4], 16))
                i += 4
                continue
            except ValueError:
                pass
        raw.extend(text[i].encode("utf-8"))
        i += 1
    return raw.decode("utf-8", "replace")


def parse_iw_scan(text):
    networks, current = [], None
    for line in text.splitlines():
        match = re.match(r"^BSS ([0-9a-fA-F:]{17})", line)
        if match:
            current = {"bssid": match.group(1).upper(), "ssid": "", "rssi": None, "channel": None,
                       "frequency": None, "security": "open"}
            networks.append(current)
            continue
        if current is None:
            continue
        stripped = line.strip()
        if stripped.startswith("freq:"):
            try:
                current["frequency"] = int(float(stripped.split()[1]))
            except (IndexError, ValueError):
                pass
        elif stripped.startswith("signal:"):
            try:
                current["rssi"] = float(stripped.split()[1])
            except (IndexError, ValueError):
                pass
        elif stripped.startswith("SSID:"):
            current["ssid"] = _unescape(line.split("SSID:", 1)[1].strip())[:32]
        elif stripped.startswith("DS Parameter set: channel"):
            try:
                current["channel"] = int(stripped.rsplit(" ", 1)[1])
            except ValueError:
                pass
        elif stripped.startswith("RSN:"):
            current["security"] = "WPA2/3"
        elif stripped.startswith("WPA:") and current["security"] == "open":
            current["security"] = "WPA"
        elif stripped.startswith("capability:") and "Privacy" in stripped and current["security"] == "open":
            current["security"] = "WEP"
    for network in networks:
        if network["channel"] is None and network["frequency"]:
            network["channel"] = _channel(network["frequency"])
    return networks


def _channel(freq):
    if 2412 <= freq <= 2472:
        return (freq - 2407) // 5
    if freq == 2484:
        return 14
    if 5000 <= freq <= 5900:
        return (freq - 5000) // 5
    if 5955 <= freq <= 7115:
        return (freq - 5950) // 5
    return None


def _split_terse(line):
    fields, value, escape = [], "", False
    for ch in line:
        if escape:
            value += ch
            escape = False
        elif ch == "\\":
            escape = True
        elif ch == ":":
            fields.append(value)
            value = ""
        else:
            value += ch
    fields.append(value)
    return fields


def parse_nmcli(text):
    networks = []
    for line in text.splitlines():
        fields = _split_terse(line)
        if len(fields) < 6 or not re.fullmatch(r"[0-9A-Fa-f:]{17}", fields[0]):
            continue
        bssid, ssid, chan, freq, signal, security = fields[:6]
        try:
            quality = int(signal)
        except ValueError:
            quality = None
        networks.append({"bssid": bssid.upper(), "ssid": ssid[:32],
                         # nmcli reports 0-100 quality; this is NetworkManager's own inverse mapping.
                         "rssi": None if quality is None else quality / 2 - 100,
                         "channel": int(chan) if chan.isdigit() else None,
                         "frequency": int(freq.split()[0]) if freq.split() and freq.split()[0].isdigit() else None,
                         "security": security or "open"})
    return networks


class WifiScanner:
    name = "Wi-Fi"

    def __init__(self, interface=None, runner=subprocess.run):
        self.interface = interface or (wireless_interfaces() or [None])[0]
        self.runner = runner
        self.method = None

    def available(self):
        return self.interface is not None and (shutil.which("iw") or shutil.which("nmcli")) is not None

    def _run(self, args, timeout=12):
        return self.runner(args, capture_output=True, text=True, timeout=timeout, check=False)

    def scan(self):
        if not self.available():
            raise ReconUnavailable("No Wi-Fi interface or scan tool (iw, nmcli)")
        if shutil.which("nmcli"):
            # Ask NetworkManager for a fresh scan; results arrive in iw's cache too.
            self._run(["nmcli", "device", "wifi", "rescan", "ifname", self.interface], timeout=6)
        if shutil.which("iw"):
            result = self._run(["iw", "dev", self.interface, "scan", "dump"])
            if result.returncode == 0 and "BSS " in result.stdout:
                self.method = "iw"
                return parse_iw_scan(result.stdout)
        if shutil.which("nmcli"):
            result = self._run(["nmcli", "-t", "-e", "yes", "-f", "BSSID,SSID,CHAN,FREQ,SIGNAL,SECURITY",
                                "device", "wifi", "list", "ifname", self.interface])
            if result.returncode == 0:
                self.method = "nmcli"
                return parse_nmcli(result.stdout)
        raise ReconUnavailable("Wi-Fi scan results are unavailable")

    def close(self):
        pass


# ------------------------------------------------------------------ BLE
def ble_refusal(name, text=""):
    """A short, fixable reason BlueZ refused to power on or start discovery."""
    name, text = str(name or ""), str(text or "").lower()
    if name.endswith(".Blocked") or "rfkill" in text or "blocked" in text:
        return "Bluetooth is switched off: run  sudo rfkill unblock bluetooth"
    if name.endswith((".AccessDenied", ".NotPermitted", ".NotAuthorized")):
        return "No Bluetooth permission: reboot once after installing"
    if name.endswith(".NotReady"):
        return "Bluetooth adapter is not ready: run  sudo rfkill unblock bluetooth"
    return "Bluetooth refused the scan (%s)" % (name.rsplit(".", 1)[-1] or "unknown")


class BleScanner:
    """BlueZ discovery over D-Bus; one long-lived connection keeps discovery on."""

    name = "BLE"

    def __init__(self):
        self.bus = self.adapter = None
        self.started = False

    def open(self):
        """Connect to BlueZ and start LE discovery. Safe to call again after a refusal."""
        try:
            import dbus
        except ImportError as exc:
            raise ReconUnavailable("BLE needs python3-dbus") from exc
        self.dbus = dbus
        try:
            bus = dbus.SystemBus()
            manager = dbus.Interface(bus.get_object("org.bluez", "/"), "org.freedesktop.DBus.ObjectManager")
            objects = manager.GetManagedObjects()
        except dbus.exceptions.DBusException as exc:
            if "AccessDenied" in str(exc.get_dbus_name()):
                raise ReconUnavailable(ble_refusal(exc.get_dbus_name())) from exc
            raise ReconUnavailable("Bluetooth service (bluetoothd) is not running") from exc
        path = next((p for p, ifaces in objects.items() if "org.bluez.Adapter1" in ifaces), None)
        if path is None:
            raise ReconUnavailable("No Bluetooth adapter found")
        adapter = dbus.Interface(bus.get_object("org.bluez", path), "org.bluez.Adapter1")
        props = dbus.Interface(bus.get_object("org.bluez", path), "org.freedesktop.DBus.Properties")
        try:
            if not props.Get("org.bluez.Adapter1", "Powered"):
                props.Set("org.bluez.Adapter1", "Powered", dbus.Boolean(True))
            adapter.SetDiscoveryFilter({"Transport": dbus.String("le"), "DuplicateData": dbus.Boolean(True)})
            adapter.StartDiscovery()
        except dbus.exceptions.DBusException as exc:
            if "InProgress" not in str(exc.get_dbus_name()):
                raise ReconUnavailable(ble_refusal(exc.get_dbus_name(), str(exc))) from exc
        # Only a fully started scanner is kept, so a refusal (Bluetooth blocked, adapter
        # still powering up) is retried on the next scan instead of silently finding nothing.
        self.bus, self.manager, self.adapter_path, self.adapter = bus, manager, path, adapter
        self.started = True
        return self

    def scan(self):
        if not self.started:
            self.open()
        try:
            objects = self.manager.GetManagedObjects()
        except self.dbus.exceptions.DBusException as exc:
            self.close()
            raise ReconUnavailable("Bluetooth service stopped") from exc
        seen = []
        for path, interfaces in objects.items():
            device = interfaces.get("org.bluez.Device1")
            if not device or not str(path).startswith(str(self.adapter_path)) or "RSSI" not in device:
                continue
            manufacturer = {int(k): bytes(v) for k, v in device.get("ManufacturerData", {}).items()}
            service_data = {str(k): bytes(v) for k, v in device.get("ServiceData", {}).items()}
            uuids = [str(u) for u in device.get("UUIDs", [])]
            name = str(device.get("Name", "")) if "Name" in device else ""
            seen.append({"address": str(device.get("Address", "")).upper(),
                         "address_type": 0 if str(device.get("AddressType", "random")) == "public" else 1,
                         "rssi": int(device["RSSI"]), "name": name, "uuids": uuids,
                         "manufacturer": manufacturer, "service_data": service_data,
                         "tx_power": int(device["TxPower"]) if "TxPower" in device else None})
        return seen

    def close(self):
        if self.started and self.adapter is not None:
            try:
                self.adapter.StopDiscovery()
            except Exception:
                pass
        self.started = False
        self.bus = self.adapter = None


# ------------------------------------------------------------------ WiGLE CSV
WIGLE_COLUMNS = ("MAC", "SSID", "AuthMode", "FirstSeen", "Channel", "Frequency", "RSSI", "CurrentLatitude",
                 "CurrentLongitude", "AltitudeMeters", "AccuracyMeters", "RCOIs", "MfgrId", "Type")
WIGLE_FIX_SECONDS = 10


def wigle_auth(security):
    """WiGLE-style capability string from iw/nmcli security text."""
    text = (security or "").upper()
    if not text or text in ("OPEN", "--", "NONE"):
        return "[ESS]"
    parts = []
    for token in re.split(r"[\s/,]+", text):
        if not token:
            continue
        if token in ("WPA1", "WPA"):
            parts.append("[WPA]")
        elif token.startswith("WPA") or token == "WEP":
            parts.append("[%s]" % token)
        elif token in ("802.1X", "EAP"):
            parts.append("[EAP]")
        elif token == "OWE":
            parts.append("[OWE]")
    return "".join(dict.fromkeys(parts)) + "[ESS]"


def _csv_text(value):
    value = str(value or "").replace("\r", "\\r").replace("\n", "\\n")
    return value


class WigleWriter:
    """Live WigleWifi-1.6 CSV: one row per device per sighting interval, only with a fresh GPS fix."""

    def __init__(self, path, version, include_random_ble=False):
        import csv
        self.path = Path(path)
        new = not self.path.exists()
        fd = os.open(str(self.path), os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
        self.file = os.fdopen(fd, "a", encoding="utf-8", newline="")
        self.writer = csv.writer(self.file, lineterminator="\n")
        self.include_random_ble = include_random_ble
        self.rows = 0
        if new:
            self.writer.writerow(["WigleWifi-1.6", "appRelease=LORA-Tunnel-Zero-" + version,
                                  "model=CardputerZero", "release=" + version, "device=LORA-Tunnel",
                                  "display=embedded", "board=CardputerZero+CapLoRa1262", "brand=Valleytech"])
            self.writer.writerow(WIGLE_COLUMNS)
            self.file.flush()

    @staticmethod
    def row(transport, item, gps, now, include_random_ble=False):
        if not gps.get("valid") or (gps.get("age_seconds") or 0) > WIGLE_FIX_SECONDS:
            return None
        lat, lon = gps.get("latitude"), gps.get("longitude")
        rssi = item.get("rssi")
        if not isinstance(rssi, (int, float)) or lat is None or lon is None:
            return None
        stamp = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(now))
        altitude = gps.get("altMSL", gps.get("altHAE", ""))
        errors = [gps[k] for k in ("epx", "epy") if isinstance(gps.get(k), (int, float))]
        accuracy = round(max(errors), 1) if errors else ""
        if transport == "wifi":
            freq = item.get("frequency")
            return [item["bssid"], _csv_text(item.get("ssid")), wigle_auth(item.get("security")), stamp,
                    item.get("channel") or "", freq or "", int(round(rssi)), round(lat, 7), round(lon, 7),
                    altitude, accuracy, "", "", "WIFI"]
        if item.get("address_type", 1) != 0 and not include_random_ble:
            # Random BLE addresses rotate; WiGLE and privacy are both better without them.
            return None
        manufacturer = item.get("manufacturer") or {}
        mfgr = min(manufacturer) if manufacturer else ""
        return [item["address"], _csv_text(item.get("name")), "Misc [LE]", stamp, 0, "", int(round(rssi)),
                round(lat, 7), round(lon, 7), altitude, accuracy, "", mfgr, "BLE"]

    def write(self, transport, item, gps, now):
        row = self.row(transport, item, gps, now, self.include_random_ble)
        if row is None:
            return False
        self.writer.writerow(row)
        self.file.flush()
        self.rows += 1
        return True

    def close(self):
        try:
            self.file.close()
        except OSError:
            pass


# ------------------------------------------------------------------ alerts
def _tone(path):
    rate = 22050
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        frames = bytearray()
        for freq, length in ((1760, 0.09), (0, 0.04), (2349, 0.12)):
            for i in range(int(rate * length)):
                value = int(9000 * math.sin(2 * math.pi * freq * i / rate)) if freq else 0
                frames += value.to_bytes(2, "little", signed=True)
        out.writeframes(bytes(frames))


class AlertSound:
    def __init__(self):
        self.path = None
        self.player = shutil.which("aplay")

    def play(self):
        if not self.player:
            return False
        try:
            if self.path is None:
                directory = Path(os.environ.get("XDG_RUNTIME_DIR") or tempfile.gettempdir())
                self.path = directory / ("lora-tunnel-alert-%d.wav" % os.getuid())
                _tone(self.path)
            subprocess.Popen([self.player, "-q", str(self.path)], stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, start_new_session=True)
            return True
        except OSError:
            return False


# ------------------------------------------------------------------ incidents
def is_open(security):
    return str(security or "open").strip().lower() in ("open", "", "--", "none")


class SsidConflicts:
    """The same network name heard both open and protected at the same time: a possible evil twin.

    Several access points sharing one name is normal, and WPA2/WPA3 mixes are normal too, so only an
    open copy of a protected network counts, and only once both have been heard on more than one scan.
    """

    def __init__(self, limit=512):
        self.limit = limit
        self.aps = {}
        self.flagged = {}
        self.quiet = {}

    def observe(self, item, now):
        """Returns (open bssid, protected bssid) when this sighting starts a new conflict, else None."""
        bssid, ssid = item.get("bssid", ""), item.get("ssid", "")
        if not ssid or not bssid or int(bssid[:2], 16) & 1:
            return None
        state = (ssid, is_open(item.get("security")))
        ap = self.aps.get(bssid)
        if ap is None or ap["state"] != state or now - ap["last"] > 30:
            if ap is None and len(self.aps) >= self.limit:
                del self.aps[min(self.aps, key=lambda k: self.aps[k]["last"])]
            ap = self.aps[bssid] = {"state": state, "hits": 0, "first": now, "last": now}
        ap["hits"] += 1
        ap["last"] = now
        if ap["hits"] < TWIN_HITS:
            return None
        for other, peer in self.aps.items():
            if other == bssid or peer["hits"] < TWIN_HITS or now - peer["last"] > TWIN_RECENT:
                continue
            if peer["state"][0] != ssid or peer["state"][1] == state[1]:
                continue
            pair = (bssid, other) if state[1] else (other, bssid)
            fresh = pair[0] not in self.flagged or now - self.flagged[pair[0]][1] > TWIN_HOLD
            self.flagged[pair[0]] = (pair[1], now)
            if fresh and now - self.quiet.get(pair, -TWIN_QUIET) >= TWIN_QUIET:
                self.quiet[pair] = now
                return pair
        return None

    def peer(self, bssid, now):
        """The protected network an open AP is copying, while the conflict is current."""
        flagged = self.flagged.get(bssid)
        if flagged and now - flagged[1] <= TWIN_HOLD:
            return flagged[0]
        return None


class AdvertBursts:
    """Many new BLE addresses or payloads of one pairing kind within seconds: possible advert spam.

    BlueZ keeps reporting a device for a while after its last advert, so an address and payload only
    count the first time they appear, not each time the cache is read.
    """

    def __init__(self, memory=120.0, limit=8192):
        self.memory = memory
        self.limit = limit
        self.known = {}
        self.recent = {}
        self.alerted = {}

    def observe(self, group, address, fingerprint, now):
        """Returns (addresses, variants) when the group crosses a threshold and may alert, else None."""
        entries = self.recent.setdefault(group, {})
        for key in [k for k, at in entries.items() if now - at > BURST_WINDOW]:
            del entries[key]
        known = (group, address, fingerprint)
        last = self.known.get(known)
        self.known[known] = now
        if last is not None and now - last <= self.memory:
            return None
        if len(self.known) > self.limit:
            self.known = {k: at for k, at in self.known.items() if now - at <= self.memory}
        entries[(address, fingerprint)] = now
        addresses = len({a for a, _ in entries})
        variants = len({f for _, f in entries})
        churn = group != "tracker" and variants >= BURST_VARIANTS
        if (addresses >= BURST_ADDRESSES or churn) and now - self.alerted.get(group, -BURST_QUIET) >= BURST_QUIET:
            self.alerted[group] = now
            return addresses, variants
        return None


# ------------------------------------------------------------------ engine
def _distance(a, b):
    lat1, lon1, lat2, lon2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((lat2 - lat1) / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin((lon2 - lon1) / 2) ** 2
    return 2 * 6371000 * math.asin(math.sqrt(min(1.0, h)))


class Recon:
    """Runs the scanners, classifies, keeps the live table and writes recon.jsonl."""

    def __init__(self, cfg, gps, wifi=None, ble=None, sound=None):
        self.cfg = dict(cfg)
        self.gps = gps
        self.wifi = wifi if wifi is not None else WifiScanner()
        self.ble = ble if ble is not None else BleScanner()
        self.sound = sound or AlertSound()
        self.lock = threading.Lock()
        self.state = "idle"
        self.errors = {}
        self.devices = {}
        self.alerts = deque(maxlen=50)
        self.unseen_alerts = 0
        self.directory = None
        self.started = 0.0
        self.counters = dict(wifi_scans=0, ble_scans=0, observations=0, classified=0, alerts=0, log_errors=0,
                             wigle_rows=0)
        self._stop = threading.Event()
        self._threads = []
        self._log = None
        self._wigle = None
        self._cache = {}
        self._seen = set()
        self._ssids = SsidConflicts()
        self._bursts = AdvertBursts()
        self.totals = {"wifi": 0, "ble": 0}

    # -- configuration -------------------------------------------------
    def enabled(self, category):
        return category in self.cfg.get("recon_categories", recon.CATEGORIES)

    def display_min(self):
        return recon.CONFIDENCE_BY_KEY.get(self.cfg.get("recon_display_min", "low"), recon.LOW)

    def alert_min(self):
        return recon.CONFIDENCE_BY_KEY.get(self.cfg.get("recon_alert_min", "medium"), recon.MEDIUM)

    # -- lifecycle -------------------------------------------------------
    def start(self, sessions_root):
        if self.state == "running":
            return
        self.errors.clear()
        self.devices.clear()
        self._seen = set()
        self.totals = {"wifi": 0, "ble": 0}
        self.alerts.clear()
        self._cache.clear()
        self._ssids = SsidConflicts()
        self._bursts = AdvertBursts()
        self.unseen_alerts = 0
        self.counters = dict(wifi_scans=0, ble_scans=0, observations=0, classified=0, alerts=0, log_errors=0,
                             wigle_rows=0)
        self.directory = create_session(sessions_root)
        meta = read_metadata(self.directory)
        meta.update(kind="recon", status="capturing", started_at=time.time(),
                    categories=list(self.cfg.get("recon_categories", recon.CATEGORIES)),
                    display_min=self.cfg.get("recon_display_min", "low"),
                    alert_min=self.cfg.get("recon_alert_min", "medium"))
        atomic_json(self.directory / "session.json", meta)
        fd = os.open(str(self.directory / "recon.jsonl"), os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
        self._log = os.fdopen(fd, "a", encoding="utf-8")
        try:
            from . import __version__
            self._wigle = WigleWriter(self.directory / "wigle.csv", __version__,
                                      bool(self.cfg.get("wigle_random_ble", False)))
        except OSError:
            self._wigle = None
            self.errors["storage"] = "WiGLE log could not be created"
        self._stop.clear()
        self.started = time.monotonic()
        self._threads = []
        if self.cfg.get("recon_wifi", True):
            self._threads.append(threading.Thread(target=self._loop, args=(self.wifi, WIFI_INTERVAL, "wifi"),
                                                  daemon=True, name="recon-wifi"))
        if self.cfg.get("recon_ble", True):
            self._threads.append(threading.Thread(target=self._loop, args=(self.ble, BLE_INTERVAL, "ble"),
                                                  daemon=True, name="recon-ble"))
        if not self._threads:
            self.errors["recon"] = "Turn on Wi-Fi or BLE scanning in Settings"
        self.state = "running"
        for thread in self._threads:
            thread.start()

    def stop(self):
        if self.state != "running":
            return
        self.state = "stopping"
        self._stop.set()
        for thread in self._threads:
            thread.join(15)
        for scanner in (self.wifi, self.ble):
            try:
                scanner.close()
            except Exception:
                pass
        with self.lock:
            if self._log is not None:
                try:
                    self._log.close()
                except OSError:
                    self.counters["log_errors"] += 1
                self._log = None
            if self._wigle is not None:
                self._wigle.close()
                self._wigle = None
        try:
            meta = read_metadata(self.directory)
            classified = [d for d in self.devices.values() if d.get("category")]
            meta.update(status="closed", ended_at=time.time(), counters=dict(self.counters),
                        devices=len(self.devices), classified=len(classified),
                        errors=sorted(set(self.errors.values())),
                        by_category={c: sum(1 for d in classified if d["category"] == c) for c in
                                     sorted({d["category"] for d in classified})})
            atomic_json(self.directory / "session.json", meta)
        except (OSError, ValueError):
            pass
        self.state = "idle"

    def elapsed(self):
        return time.monotonic() - self.started if self.state == "running" else 0

    # -- workers ---------------------------------------------------------
    def _loop(self, scanner, interval, transport):
        while not self._stop.is_set():
            began = time.monotonic()
            try:
                results = scanner.scan()
                self.errors.pop(transport, None)
                self.counters[transport + "_scans"] += 1
                for item in results:
                    self.observe(transport, item)
            except ReconUnavailable as exc:
                self.errors[transport] = str(exc)
                self._stop.wait(10)
                continue
            except Exception as exc:  # a scanner bug must not stop the other transport
                self.errors[transport] = "%s scan failed: %s" % (scanner.name, type(exc).__name__)
            self._stop.wait(max(0.2, interval - (time.monotonic() - began)))

    # -- classification and tracking ---------------------------------------
    def classify(self, transport, item):
        if transport == "wifi":
            key = ("wifi", item["bssid"], item.get("ssid", ""))
            if key not in self._cache:
                self._cache[key] = recon.classify_wifi(item["bssid"], item.get("ssid", ""))
            return self._cache[key]
        ad = item.get("raw_ad") or recon.advertisement_from_properties(
            item.get("name"), item.get("uuids", ()), item.get("manufacturer"), item.get("service_data"))
        key = ("ble", item["address"], item.get("address_type", 1), ad)
        if key not in self._cache:
            if len(self._cache) > 8000:
                self._cache.clear()
            self._cache[key] = recon.classify_ble(item["address"], item.get("address_type", 1), ad)
        return self._cache[key]

    def _twin(self, item, result, now):
        self._ssids.observe(item, now)
        peer = self._ssids.peer(item["bssid"], now)
        if peer is None or not self.enabled("eviltwin") or result.get("confidence", recon.NONE) >= recon.MEDIUM:
            return result
        return {"category": "eviltwin", "label": recon.CATEGORY_LABELS["eviltwin"], "confidence": recon.MEDIUM,
                "confidence_name": recon.CONFIDENCE_NAMES[recon.MEDIUM], "vendor": "", "payload_valid": True,
                "evidence": "Open copy of protected network %s with the same name; possible evil twin or guest "
                            "network" % peer, "peer": peer}

    def _check_bursts(self, item, result, now):
        groups = list(result.get("groups", ()))
        if result.get("category") in ("airtag", "tracker", "ibeacon"):
            groups.append("tracker")
        if not groups:
            return
        ad = item.get("raw_ad") or recon.advertisement_from_properties(
            item.get("name"), item.get("uuids", ()), item.get("manufacturer"), item.get("service_data"))
        fingerprint = zlib.crc32(ad)
        for group in groups:
            hit = self._bursts.observe(group, item["address"], fingerprint, now)
            if hit:
                self._burst_incident(group, hit, item.get("rssi"), now)

    def _burst_incident(self, group, counts, rssi, now):
        if not self.enabled("burst"):
            return
        key = ("ble", "BURST " + group.upper())
        evidence = "%d new addresses, %d payloads in %d s; possible advert spam, sender unproven" % (
            counts[0], counts[1], BURST_WINDOW)
        alert = None
        with self.lock:
            device = self.devices.get(key)
            if device is None:
                device = self.devices[key] = {"transport": "ble", "address": key[1], "first_seen": now, "count": 0,
                                              "best_rssi": rssi, "rssi_history": deque(maxlen=60), "places": [],
                                              "alerted_at": 0.0, "logged_at": 0.0, "wigle_at": 0.0,
                                              "follow": False, "incident": group}
            device.update(category="burst", confidence=recon.MEDIUM, raw_category="burst", last_seen=now,
                          rssi=rssi, name=BURST_NAMES[group], label=recon.CATEGORY_LABELS["burst"],
                          evidence=evidence, vendor="")
            device["count"] += 1
            device["rssi_history"].append((now, rssi))
            self.counters["classified"] += 1
            if recon.MEDIUM >= self.alert_min():
                device["alerted_at"] = now
                alert = {"time": now, "transport": "ble", "address": key[1], "category": "burst",
                         "label": recon.CATEGORY_LABELS["burst"], "confidence": recon.MEDIUM, "evidence": evidence,
                         "rssi": rssi, "name": BURST_NAMES[group], "follow": False}
                self.alerts.appendleft(alert)
                self.unseen_alerts += 1
                self.counters["alerts"] += 1
            self._write({"timestamp": now, "transport": "ble", "incident": "burst", "group": group,
                         "addresses": counts[0], "variants": counts[1], "rssi_dbm": rssi,
                         "receiver_gps": self.gps.snapshot(), "alert": bool(alert)})
        if alert and self.cfg.get("recon_sound", True):
            self.sound.play()

    def observe(self, transport, item, now=None):
        now = time.time() if now is None else now
        address = item["bssid"] if transport == "wifi" else item["address"]
        result = self.classify(transport, item)
        if transport == "wifi":
            result = self._twin(item, result, now)
        else:
            self._check_bursts(item, result, now)
        category = result.get("category")
        shown = bool(category) and self.enabled(category) and result["confidence"] >= self.display_min()
        gps = self.gps.snapshot()
        rssi = item.get("rssi")
        key = (transport, address)
        log_record = None
        with self.lock:
            self.counters["observations"] += 1
            device = self.devices.get(key)
            if key not in self._seen and len(self._seen) < 200000:
                self._seen.add(key)
                self.totals[transport] += 1
            if device is None:
                if len(self.devices) >= MAX_DEVICES:
                    oldest = min(self.devices, key=lambda k: self.devices[k]["last_seen"])
                    del self.devices[oldest]
                device = self.devices[key] = {"transport": transport, "address": address, "first_seen": now,
                                              "count": 0, "best_rssi": rssi, "rssi_history": deque(maxlen=60),
                                              "places": [], "alerted_at": 0.0, "logged_at": 0.0, "wigle_at": 0.0,
                                              "follow": False, "category": None, "confidence": 0}
            device["count"] += 1
            device["last_seen"] = now
            device["rssi"] = rssi
            device["rssi_history"].append((now, rssi))
            device["name"] = item.get("ssid") if transport == "wifi" else (item.get("name") or result.get("name", ""))
            for field in ("channel", "frequency", "security", "address_type"):
                if field in item:
                    device[field] = item[field]
            changed = (device["category"], device["confidence"]) != (category if shown else None,
                                                                     result["confidence"] if shown else 0)
            device["category"] = category if shown else None
            device["confidence"] = result["confidence"] if shown else 0
            device["raw_category"] = category
            for field in ("label", "evidence", "vendor", "remote_id", "beacon", "owner", "peer"):
                if result.get(field):
                    device[field] = result[field]
            if isinstance(rssi, (int, float)) and (device["best_rssi"] is None or rssi > device["best_rssi"]):
                device["best_rssi"] = rssi
                if gps.get("valid"):
                    device["best_fix"] = (gps["latitude"], gps["longitude"])
            if gps.get("valid"):
                here = (gps["latitude"], gps["longitude"])
                places = device["places"]
                if not places or now - places[-1][0] >= 60:
                    places.append((now, here[0], here[1]))
                    del places[:-30]
                follow = self._following(places)
                if follow and not device["follow"] and shown:
                    device["follow"] = True
                    changed = True
            if shown and changed:
                self.counters["classified"] += 1
            alert = None
            if shown and (result["confidence"] >= self.alert_min() or device["follow"]) and \
                    now - device["alerted_at"] >= ALERT_REPEAT:
                device["alerted_at"] = now
                alert = {"time": now, "transport": transport, "address": address, "category": category,
                         "label": recon.CATEGORY_LABELS[category], "confidence": result["confidence"],
                         "evidence": result.get("evidence", ""), "rssi": rssi, "name": device.get("name", ""),
                         "follow": device["follow"]}
                self.alerts.appendleft(alert)
                self.unseen_alerts += 1
                self.counters["alerts"] += 1
            if self._wigle is not None and gps.get("valid") and now - device["wigle_at"] >= LOG_INTERVAL:
                try:
                    if self._wigle.write(transport, item, gps, now):
                        device["wigle_at"] = now
                        self.counters["wigle_rows"] += 1
                except (OSError, ValueError):
                    self.counters["log_errors"] += 1
            if changed or alert or now - device["logged_at"] >= LOG_INTERVAL:
                device["logged_at"] = now
                log_record = {"timestamp": now, "transport": transport, "address": address,
                              "address_type": item.get("address_type"), "rssi_dbm": rssi,
                              "name": device.get("name", ""), "receiver_gps": gps,
                              "classification": {k: v for k, v in result.items() if k != "payload_valid"},
                              "shown": shown, "alert": bool(alert), "following": device["follow"]}
                if transport == "wifi":
                    log_record.update(ssid=item.get("ssid", ""), channel=item.get("channel"),
                                      frequency_mhz=item.get("frequency"), security=item.get("security"))
                else:
                    ad = item.get("raw_ad") or recon.advertisement_from_properties(
                        item.get("name"), item.get("uuids", ()), item.get("manufacturer"), item.get("service_data"))
                    log_record["advertisement_hex"] = ad.hex()
                    log_record["advertisement_source"] = "raw" if item.get("raw_ad") else "bluez-properties"
            if log_record is not None:
                self._write(log_record)
        if alert and self.cfg.get("recon_sound", True):
            self.sound.play()
        return device

    @staticmethod
    def _following(places):
        """Seen at several places that span real movement and time: maybe travelling with you."""
        if len(places) < 3:
            return False
        if places[-1][0] - places[0][0] < FOLLOW_MIN_SECONDS:
            return False
        first = (places[0][1], places[0][2])
        far = max(_distance(first, (p[1], p[2])) for p in places)
        return far >= FOLLOW_MIN_METERS

    def _write(self, record):
        if self._log is None:
            return
        try:
            if shutil.disk_usage(self.directory).free < MIN_FREE_BYTES:
                self.errors["storage"] = "Storage reserve reached; Recon log paused"
                return
            self._log.write(json.dumps(record, ensure_ascii=True, allow_nan=False, default=_jsonable) + "\n")
            self._log.flush()
        except (OSError, ValueError):
            self.counters["log_errors"] += 1
            self.errors["storage"] = "Recon log write failed"

    # -- read side ---------------------------------------------------------
    def device_list(self, show_all=False, sort="threat"):
        with self.lock:
            items = [dict(d, rssi_history=list(d["rssi_history"])) for d in self.devices.values()
                     if show_all or d.get("category")]
        now = time.time()
        if sort == "signal":
            items.sort(key=lambda d: d.get("rssi") if d.get("rssi") is not None else -999, reverse=True)
        elif sort == "recent":
            items.sort(key=lambda d: d["last_seen"], reverse=True)
        else:
            items.sort(key=lambda d: (now - d["last_seen"] < 60, d.get("follow", False), d.get("confidence", 0),
                                      recon.PRIORITY.get(d.get("category"), 0), d.get("rssi") or -999),
                       reverse=True)
        return items

    def summary(self):
        with self.lock:
            devices = list(self.devices.values())
        now = time.time()
        active = [d for d in devices if now - d["last_seen"] < 60]
        return {"wifi": sum(1 for d in active if d["transport"] == "wifi"),
                "ble": sum(1 for d in active if d["transport"] == "ble"),
                "flagged": sum(1 for d in active if d.get("category")),
                "strong": sum(1 for d in active if d.get("category") and d.get("confidence", 0) >= recon.STRONG),
                "following": sum(1 for d in devices if d.get("follow")),
                "wifi_total": self.totals["wifi"], "ble_total": self.totals["ble"],
                "wigle_rows": self.counters.get("wigle_rows", 0)}


def _jsonable(value):
    if isinstance(value, (bytes, bytearray)):
        return value.hex()
    if isinstance(value, (set, tuple, deque)):
        return list(value)
    raise TypeError("not serialisable")
