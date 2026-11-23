"""Recon extras on the CardputerZero: registry prefixes, BLE company IDs, Find My owner state, evil twins,
advert bursts and the categories they add."""

import json
from pathlib import Path
import tempfile
import unittest

from lora_tunnel_zero import config, recon
from lora_tunnel_zero.demo import DemoBle, DemoWifi
from lora_tunnel_zero.gnss import GPS
from lora_tunnel_zero.recon_extras import EXTRA_PREFIXES
from lora_tunnel_zero.recon_scan import AdvertBursts, Recon, SsidConflicts
from lora_tunnel_zero.recon_vendors import VENDOR_PREFIXES

PUBLIC = "10:20:30:40:50:60"


def ad(kind, data):
    return bytes((len(data) + 1, kind)) + bytes(data)


class Sound:
    plays = 0

    def play(self):
        self.plays += 1


def make(**changes):
    cfg = dict(config.DEFAULTS, **changes)
    sound = Sound()
    return Recon(cfg, GPS(use_uart=False), wifi=DemoWifi(), ble=DemoBle(), sound=sound), sound


class ClassifierExtrasTests(unittest.TestCase):
    def test_extra_prefixes_are_new_sorted_and_unique(self):
        keys = [row[0] for row in EXTRA_PREFIXES]
        self.assertEqual(keys, sorted(set(keys)))
        self.assertFalse(set(keys) & {row[0] for row in VENDOR_PREFIXES})

    def test_fleet_dashcam_speaker_and_glasses_prefixes(self):
        cases = {"00:30:44:01:02:03": ("fleet", recon.LOW), "00:A0:D5:01:02:03": ("fleet", recon.LOW),
                 "00:25:42:01:02:03": ("camera", recon.MEDIUM), "2C:42:05:01:02:03": ("camera", recon.MEDIUM),
                 "00:0E:58:01:02:03": ("listener", recon.MEDIUM), "98:DA:92:01:02:03": ("glasses", recon.LOW)}
        for mac, expected in cases.items():
            result = recon.classify_wifi(mac, "")
            self.assertEqual((result["category"], result["confidence"]), expected, mac)
            self.assertIsNone(recon.classify_wifi(mac, "", extras=False)["category"], mac)

    def test_extras_never_use_random_or_local_addresses(self):
        self.assertIsNone(recon.classify_wifi("02:30:44:01:02:03", "")["category"])
        self.assertIsNone(recon.classify_ble("00:0E:58:01:02:03", 1, ad(9, b"x"))["category"])
        self.assertEqual(recon.classify_ble("00:0E:58:01:02:03", 0, ad(9, b"x"))["category"], "listener")

    def test_extras_never_weaken_a_firmware_match(self):
        axon = ad(0xff, [0x4d, 3]) + ad(3, [0x81, 0xfc])
        self.assertEqual(recon.classify_ble("00:0E:58:01:02:03", 0, axon)["category"], "axon")

    def test_sonos_and_flipper_company_ids(self):
        sonos = recon.classify_ble(PUBLIC, 1, ad(0xff, [0xa7, 0x05, 1, 2]))
        self.assertEqual((sonos["category"], sonos["vendor"]), ("listener", "Sonos"))
        flipper = recon.classify_ble(PUBLIC, 1, ad(0xff, [0x29, 0x0e, 1]))
        self.assertEqual((flipper["category"], flipper["confidence"]), ("hacker", recon.MEDIUM))
        self.assertIsNone(recon.classify_ble(PUBLIC, 1, ad(0xff, [0x29, 0x0e, 1]), extras=False)["category"])

    def test_find_my_away_from_owner(self):
        separated = recon.classify_ble(PUBLIC, 1, ad(0xff, [0x4c, 0, 0x12, 0x19] + [0] * 25))
        self.assertEqual((separated["category"], separated["owner"]), ("airtag", "away from owner"))
        service_only = recon.classify_ble(PUBLIC, 1, ad(3, [0x44, 0xfd]))
        self.assertEqual(service_only["category"], "airtag")
        self.assertNotIn("owner", service_only)

    def test_fox_beacon_has_its_own_category(self):
        fox = ad(0x21, bytes(recon._FOX) + bytes([1, 0, 0, 0, 0]))
        self.assertEqual(recon.classify_ble(PUBLIC, 1, fox)["category"], "fox")
        self.assertIsNone(recon.classify_ble(PUBLIC, 1, fox, extras=False)["category"])

    def test_pairing_groups(self):
        self.assertEqual(recon.classify_ble(PUBLIC, 1, ad(0xff, [0x4c, 0, 0x07, 0x19, 1]))["groups"], ("apple",))
        self.assertEqual(recon.classify_ble(PUBLIC, 1, ad(0x16, [0x2c, 0xfe, 1, 2, 3]))["groups"], ("fastpair",))
        self.assertEqual(recon.classify_ble(PUBLIC, 1, ad(0xff, [6, 0, 3, 0]))["groups"], ("swiftpair",))


class IncidentTests(unittest.TestCase):
    def test_open_copy_of_protected_network_is_flagged(self):
        conflicts = SsidConflicts()
        home = {"bssid": "AA:BB:CC:00:00:01", "ssid": "Home", "security": "WPA2/3"}
        twin = {"bssid": "AA:BB:CC:00:00:02", "ssid": "Home", "security": "open"}
        found = None
        for scan in range(3):
            for item in (home, twin):
                found = conflicts.observe(item, 100.0 + scan * 8) or found
        self.assertEqual(found, (twin["bssid"], home["bssid"]))
        self.assertEqual(conflicts.peer(twin["bssid"], 130.0), home["bssid"])
        self.assertIsNone(conflicts.peer(home["bssid"], 130.0))
        self.assertIsNone(conflicts.peer(twin["bssid"], 400.0))

    def test_mesh_and_mixed_security_are_normal(self):
        conflicts = SsidConflicts()
        aps = [{"bssid": "AA:BB:CC:00:00:0%d" % i, "ssid": "Office", "security": s}
               for i, s in enumerate(("WPA2/3", "WPA2/3", "WPA", "WEP"))]
        for scan in range(5):
            for item in aps:
                self.assertIsNone(conflicts.observe(item, scan * 8.0))

    def test_hidden_networks_are_ignored(self):
        conflicts = SsidConflicts()
        for scan in range(4):
            conflicts.observe({"bssid": "AA:BB:CC:00:00:01", "ssid": "", "security": "WPA2/3"}, scan * 8.0)
            self.assertIsNone(conflicts.observe({"bssid": "AA:BB:CC:00:00:02", "ssid": "", "security": "open"},
                                                scan * 8.0))

    def test_burst_needs_many_new_addresses(self):
        bursts = AdvertBursts()
        hits = [bursts.observe("apple", "AA:00:00:00:00:%02X" % i, 7, 10.0 + i * 0.2) for i in range(12)]
        self.assertEqual(hits[:11], [None] * 11)
        self.assertEqual(hits[11], (12, 1))
        self.assertIsNone(bursts.observe("apple", "AA:00:00:00:01:00", 7, 13.0))

    def test_cached_devices_do_not_count_again(self):
        bursts = AdvertBursts()
        for poll in range(30):
            for i in range(11):
                self.assertIsNone(bursts.observe("apple", "AA:00:00:00:00:%02X" % i, 7, float(poll)))

    def test_trackers_need_addresses_not_payload_churn(self):
        bursts = AdvertBursts()
        hits = [bursts.observe("tracker", "AA:00:00:00:00:01", i, 1.0 + i * 0.1) for i in range(30)]
        self.assertEqual(hits, [None] * 30)
        bursts = AdvertBursts()
        hits = [bursts.observe("fastpair", "AA:00:00:00:00:01", i, 1.0 + i * 0.1) for i in range(30)]
        self.assertEqual(hits[23], (1, 24))


class EngineExtrasTests(unittest.TestCase):
    def test_evil_twin_shows_and_alerts(self):
        r, sound = make()
        r.directory = None
        home = {"bssid": "AA:BB:CC:00:00:01", "ssid": "Home", "security": "WPA2/3", "rssi": -50}
        twin = {"bssid": "AA:BB:CC:00:00:02", "ssid": "Home", "security": "open", "rssi": -40}
        for scan in range(3):
            for item in (home, twin):
                r.observe("wifi", item, now=1000.0 + scan * 8)
        listed = r.device_list()
        self.assertEqual([(d["address"], d["category"]) for d in listed], [(twin["bssid"], "eviltwin")])
        self.assertEqual(listed[0]["peer"], home["bssid"])
        self.assertEqual(r.alerts[0]["category"], "eviltwin")
        self.assertEqual(sound.plays, 1)

    def test_evil_twin_can_be_turned_off(self):
        r, _ = make(recon_categories=["flock"])
        r.directory = None
        for scan in range(3):
            r.observe("wifi", {"bssid": "AA:BB:CC:00:00:01", "ssid": "Home", "security": "WPA2/3"}, now=scan * 8.0)
            r.observe("wifi", {"bssid": "AA:BB:CC:00:00:02", "ssid": "Home", "security": "open"}, now=scan * 8.0)
        self.assertEqual(r.device_list(), [])

    def test_advert_burst_becomes_one_incident(self):
        r, sound = make()
        r.directory = Path(tempfile.mkdtemp())
        r._log = open(r.directory / "recon.jsonl", "a", encoding="utf-8")
        for i in range(14):
            r.observe("ble", {"address": "5A:00:00:00:00:%02X" % i, "address_type": 1, "rssi": -55,
                              "raw_ad": ad(0xff, [0x4c, 0, 0x0f, 5, i])}, now=50.0 + i * 0.1)
        r._log.close()
        bursts = [d for d in r.device_list() if d["category"] == "burst"]
        self.assertEqual(len(bursts), 1)
        self.assertEqual(bursts[0]["name"], "Apple pop-up adverts")
        self.assertEqual(len([a for a in r.alerts if a["category"] == "burst"]), 1)
        lines = [json.loads(x) for x in (r.directory / "recon.jsonl").read_text().splitlines()]
        self.assertTrue(any(x.get("incident") == "burst" for x in lines))
        self.assertGreaterEqual(sound.plays, 1)


class ConfigTests(unittest.TestCase):
    def test_new_installs_leave_noisy_kinds_off(self):
        cats = config.DEFAULTS["recon_categories"]
        self.assertIn("eviltwin", cats)
        self.assertIn("burst", cats)
        for key in recon.OFF_BY_DEFAULT:
            self.assertNotIn(key, cats)

    def test_upgrade_turns_on_new_kinds_once(self):
        path = Path(tempfile.mkdtemp()) / "config.json"
        path.write_text(json.dumps({"recon_categories": ["flock", "axon"]}))
        path.chmod(0o600)
        loaded = config.load(path)
        self.assertEqual(loaded["recon_categories"], ["flock", "axon", "eviltwin", "burst"])
        config.save(dict(loaded, recon_categories=["flock"]), path)
        self.assertEqual(config.load(path)["recon_categories"], ["flock"])


if __name__ == "__main__":
    unittest.main()


class UIExtrasTests(unittest.TestCase):
    def test_incident_and_twin_details_render(self):
        from lora_tunnel_zero.demo import DemoRadio
        from lora_tunnel_zero.engine import Engine
        from lora_tunnel_zero.ui import recon_screens
        from lora_tunnel_zero.ui.app import App
        from lora_tunnel_zero.ui.display import PNGDisplay
        from lora_tunnel_zero.ui.keys import ScriptedInput
        e = Engine(dict(config.DEFAULTS, region="US", backend="demo"),
                   radio_factory=lambda c, p: DemoRadio(p), gps=GPS(use_uart=False))
        root = Path(tempfile.mkdtemp())
        r, _ = make()
        r.directory = None
        app = App(PNGDisplay(root / "f.png"), ScriptedInput(), e, e.cfg, config_path=root / "c.json",
                  data_root=root, splash=False, recon=r)
        for scan in range(3):
            r.observe("wifi", {"bssid": "AA:BB:CC:00:00:01", "ssid": "Home", "security": "WPA2/3"}, now=scan * 8.0)
            r.observe("wifi", {"bssid": "AA:BB:CC:00:00:02", "ssid": "Home", "security": "open"}, now=scan * 8.0)
        for i in range(12):
            r.observe("ble", {"address": "5A:00:00:00:00:%02X" % i, "address_type": 1, "rssi": -55,
                              "raw_ad": ad(0x16, [0x2c, 0xfe, i])}, now=30.0 + i * 0.1)
        r.observe("ble", {"address": "5A:3C:11:9D:20:7F", "address_type": 1, "rssi": -70,
                          "raw_ad": ad(0xff, [0x4c, 0, 0x12, 0x19] + [0] * 25)}, now=31.0)
        for key, expected in ((("wifi", "AA:BB:CC:00:00:02"), "Protected"), (("ble", "BURST FASTPAIR"), "Kind"),
                              (("ble", "5A:3C:11:9D:20:7F"), "Owner")):
            detail = recon_screens.ReconDetail(*key)
            self.assertIn(expected, [row[0] for row in detail.rows(app)])
            app.push(detail)
            app.compose()
            consumed = detail.handle(ScriptedInput(["F"]).queue[0], app)
            self.assertEqual(isinstance(app.screen, recon_screens.Finder), key[1] != "BURST FASTPAIR")
            if consumed and isinstance(app.screen, recon_screens.Finder):
                app.pop()
            app.pop()
        app.push(recon_screens.Categories())
        for _ in range(17):
            app.screen.handle(ScriptedInput(["DOWN"]).queue[0], app)
        app.compose()
        self.assertEqual(app.screen.index, 17)
