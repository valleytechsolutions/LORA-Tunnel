#!/usr/bin/env python3
"""Differential test: the app's Python Recon classifier against the firmware's C++.

Compiles lib/recon (and lib/opendroneid) with tools/recon_parity_driver.cpp,
feeds both implementations the same fixtures plus seeded random mutations, and
fails on any difference in category, grade, evidence, vendor, Remote ID or
iBeacon fields. Needs g++. Run from cardputer-zero/:
    python3 tools/check_recon_parity.py [--cases N]
"""

import argparse
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parents[1]
REPO = HERE.parent
sys.path.insert(0, str(HERE))

from lora_tunnel_zero import recon  # noqa: E402
from lora_tunnel_zero.recon_vendors import VENDOR_PREFIXES  # noqa: E402

CPP_CATEGORIES = [None, "alpr", "flock", "axon", "ibeacon", "ring", "camera", "drone", "airtag", "tracker",
                  "glasses", "raven", "skimmer", "hacker", "custom"]


def build(directory):
    binary = directory / "recon_parity"
    sources = [HERE / "tools" / "recon_parity_driver.cpp", REPO / "lib" / "recon" / "src" / "TunnelRecon.cpp",
               REPO / "lib" / "opendroneid" / "src" / "opendroneid.c"]
    objects = []
    for source in sources:
        obj = directory / (source.stem + ".o")
        compiler = "gcc" if source.suffix == ".c" else "g++"
        flags = ["-std=c11"] if source.suffix == ".c" else ["-std=c++14"]
        subprocess.run([compiler, *flags, "-O1", "-w", "-I", str(REPO / "lib" / "recon" / "src"),
                        "-I", str(REPO / "lib" / "opendroneid" / "src"), "-c", str(source), "-o", str(obj)],
                       check=True)
        objects.append(str(obj))
    subprocess.run(["g++", *objects, "-o", str(binary), "-lm"], check=True)
    return binary


def ad(kind, data):
    return bytes((len(data) + 1, kind)) + bytes(data)


def odid_basic(text="1581F5FKD229400LZ7XX", id_type=1, ua_type=2):
    raw = text.encode()[:20].ljust(20, b"\x00")
    return bytes((0x02, id_type << 4 | ua_type)) + raw + b"\x00" * 3


def odid_location(lat=37.7749, lon=-122.4194, alt=120.0, speed=12.5, mult=0):
    speed_raw = int(speed / 0.25) if not mult else int((speed - 63.75) / 0.75)
    flags = 2 << 4 | mult  # airborne
    body = struct.pack("<BBBBbiiHHHBBHB", 0x12, flags, 90, min(255, max(0, speed_raw)), 4,
                       int(lat * 1e7), int(lon * 1e7), int((alt + 1000) * 2), int((alt + 1000) * 2),
                       int((50 + 1000) * 2), 0x3b, 0x22, 1200, 0)
    return body.ljust(25, b"\x00")[:25]


def odid_system(lat=37.7701, lon=-122.4101):
    return (struct.pack("<BBii", 0x42, 0x01, int(lat * 1e7), int(lon * 1e7)) + b"\x00" * 16)[:25]


def odid_pack(*messages):
    return bytes((0xF2, 25, len(messages))) + b"".join(messages)


def beacon_frame(ssid=b"", source=b"\x10\x20\x30\x40\x50\x60", extra=b""):
    header = bytes((0x80, 0x00, 0, 0)) + b"\xff" * 6 + source + source + b"\x00\x00"
    fixed = b"\x00" * 8 + b"\x64\x00" + b"\x11\x04"
    return header + fixed + bytes((0, len(ssid))) + ssid + b"\x03\x01\x06" + extra


def fixtures():
    cases = []
    macs = ["10:20:30:40:50:60", "00:25:DF:11:22:33", "00:1D:96:01:02:03", "28:24:FF:01:02:03",
            "82:6B:F2:11:22:33", "02:C0:CA:11:22:33", "00:13:37:11:22:33", "DE:AD:BE:EF:DE:AD",
            "70:C9:4E:11:22:33", "00:04:7D:11:22:33", "AC:9F:C3:11:22:33", "24:6F:28:11:22:33"]
    names = ["", "Flock-12AB", "FS-1A2B3C", "FS Ext Battery", "Penguin-0123456789", "1234567890", "flock noir",
             "Dragon Detector", "Axon Body 3", "AB3-12345", "Ring Doorbell", "Raven", "HC-05", "BT04-A",
             "pwned", "Pineapple_5G", "Ray-Ban Meta", "Spectacles 3", "Flipper Zero", "Biscuit Pro", "my AirTag",
             "SmartTag2", "Pwnagotchi", "deauther", "nyanbox", "halehound", "Hak5 Shark", "ringo", "Oakley Meta HSTN"]
    for mac in macs:
        for name in names:
            cases.append(("W", mac, name.encode()))
    for prefix, *_ in VENDOR_PREFIXES:
        cases.append(("W", "%02X:%02X:%02X:12:34:56" % (prefix >> 16, prefix >> 8 & 255, prefix & 255), b""))
    for mac in macs[:4]:
        for kind in (0, 1, 2, 3):
            cases.append(("B", mac, kind, ad(1, [6]), 0))
    blobs = [
        ad(0xff, [0x4d, 3]) + ad(3, [0x81, 0xfc]),
        ad(0xff, [0x4d, 3]),
        ad(3, [0x6b, 0xfe]),
        ad(0xff, list(b"\x00\x00ECIVEDCWB")),
        ad(7, list(bytes((0x6f, 0x2e, 0x17, 0xdf, 0x14, 0x18, 0xe5, 0x9f, 0xa8, 0x46, 0x32, 0x95, 0x38, 0xbb, 0xcc, 0xe8)))),
        ad(0xff, [0xc8, 0x09, 1]) + ad(9, list(b"Penguin-0123456789")),
        ad(0xff, [0xc8, 0x09, 1]),
        ad(0xff, [0x53, 0x0d]) + ad(3, [0x5f, 0xfd]),
        ad(0xff, [0xc2, 0x03]) + ad(3, [0x45, 0xfe]),
        ad(3, [0x00, 0x31, 0x00, 0x32, 0x00, 0x33, 0x00, 0x34, 0x00, 0x35]),
        ad(3, [0x00, 0x32]),
        ad(3, [0x5a, 0xfd]),
        ad(0x16, [0xaa, 0xfe, 0x40] + [7] * 20),
        ad(3, [0x33, 0xfe]),
        ad(3, [0xed, 0xfe]),
        ad(0xff, [0x4c, 0, 0x12, 0x19] + [1] * 25),
        ad(3, [0xb2, 0xfc]),
        ad(3, [0x81, 0x30]) + ad(0x19, [0x00, 0x86]),
        ad(3, [0x81, 0x30]),
        ad(0xff, [0x4c, 0, 2, 0x15] + list(range(16)) + [0x12, 0x34, 0xab, 0xcd, 0xc5]),
        ad(0xff, [0x4c, 0, 2, 0x15] + list(range(16)) + [0x12, 0x34, 0xab, 0xcd, 0xc5, 9]),
        ad(3, [0x01, 0x11]),
        ad(0xff, [0x53, 0x0d]),
        ad(0xff, [0x0c, 0x06]),
        ad(3, [0xb7, 0xfe]),
        ad(3, [0xaa, 0xfe]),
        ad(3, [0xfa, 0xff]),
        ad(0xff, [0x4c, 0] + [0] * 27),
        ad(0xff, [0x4c, 0, 0x07, 0x19]),
        ad(0x16, [0xfa, 0xff, 0x0d, 3] + list(odid_basic())),
        ad(0x16, [0xfa, 0xff, 0x0d, 3] + list(odid_location())),
        ad(0x16, [0xfa, 0xff, 0x0d, 3] + list(odid_pack(odid_basic(), odid_location(), odid_system()))),
        ad(0x16, [0xfa, 0xff, 0x0d, 3] + list(odid_pack(odid_basic(id_type=3), odid_location(speed=200, mult=1)))),
        ad(0x16, [0xfa, 0xff, 0x0d, 3] + list(odid_basic())[:20]),
        ad(0x21, [0x90, 0x4b, 0x5a, 0xc6, 0x44, 0x5f, 0x26, 0x8e, 0x2b, 0x45, 0x14, 0x71, 1, 0, 4, 0xdd, 1, 0, 0, 0, 0]),
        ad(7, list(bytes((0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f, 0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f)))) +
        ad(9, list(b"Biscuit")),
        ad(7, [0x85, 0x2a, 0x9f, 0x57, 0xc5, 0x2a, 0xed, 0x88, 0x26, 0xc2, 0xf4, 0x12, 0x01, 0x00, 0x19, 0x15]),
        ad(5, [0x81, 0xfc, 0, 0]),
        ad(7, [0xfb, 0x34, 0x9b, 0x5f, 0x80, 0, 0, 0x80, 0, 0x10, 0, 0, 0x6c, 0xfe, 0, 0]),
        ad(7, [1] * 12 + [0x81, 0xfc, 0, 0]),
        ad(9, list(b"Axon")) + bytes((9, 0xff, 1)),
        bytes((3, 3, 1)),
        ad(9, list(b"Flock")) + b"\x00\x00" + ad(3, [0x5a, 0xfd]),
    ]
    for blob in blobs:
        for mac, kind in (("10:20:30:40:50:60", 0), ("10:20:30:40:50:60", 1), ("00:25:DF:11:22:33", 0), ("-", 0)):
            cases.append(("B", mac, kind, blob, 0))
    for name in names:
        cases.append(("B", "10:20:30:40:50:60", 0, ad(9, list(name.encode())) if name else ad(1, [6]), 0))
    # ADV + scan response boundary
    first = ad(1, [6]) + b"\x00"
    cases.append(("B", "10:20:30:40:50:60", 0, first + ad(3, [0x5a, 0xfd]), len(first)))
    cases.append(("B", "10:20:30:40:50:60", 0, ad(1, [6]) + bytes((5, 3)) + ad(3, [0x5a, 0xfd]), 3))
    # Raw Wi-Fi frames
    wpa2 = bytes((48, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 2, 0, 0))
    rid_ie = bytes((221, 3 + 1 + 1 + len(odid_pack(odid_basic(), odid_location())))) + b"\xfa\x0b\xbc\x0d\x00" + \
        odid_pack(odid_basic(), odid_location())
    rid_ie = bytes((221, len(rid_ie) - 2)) + rid_ie[2:]
    peer = b'{"name": "pwnbot", "pwnd_tot": 3}'
    frames = [beacon_frame(b"Flock-1A2B"), beacon_frame(b"HomeWiFi", extra=wpa2),
              beacon_frame(b"Axon-Body", source=bytes.fromhex("0025df112233")),
              beacon_frame(b"x", extra=rid_ie), beacon_frame(b"", source=bytes.fromhex("deadbeefdead"),
                                                            extra=bytes((221, len(peer))) + peer),
              beacon_frame(b"Open", source=bytes.fromhex("001337112233")),
              beacon_frame(b"bad", extra=bytes((48, 3, 1, 0, 0))),
              beacon_frame(b"a" * 33),
              beacon_frame(b"trunc")[:-1],
              bytes((0x40, 0, 0, 0)) + b"\xff" * 6 + bytes.fromhex("70c94e112233") + b"\xff" * 6 + b"\x00\x00" +
              bytes((0, 0, 2, 8, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 12, 1, 0, 127, 1, 0,
                     221, 7, 0x50, 0x6f, 0x9a, 0x16, 3, 1, 3, 45, 1, 0, 191, 1, 0, 221, 7, 0, 0x50, 0xf2, 8, 0, 0, 0)),
              bytes((0xc0, 0, 0, 0)) + b"\xff" * 6 + bytes.fromhex("0025df112233") * 2 + b"\x00\x00\x07\x00",
              bytes((0x88, 0x01, 0, 0)) + b"\x11" * 18 + b"\x00\x00" + b"\x00\x00" + bytes.fromhex("aaaa030000888e") + b"\x00" * 4,
              b"\x80" * 10]
    for frame in frames:
        cases.append(("F", frame))
    return cases


def mutations(seed_cases, count, rng):
    pieces = [c[3] for c in seed_cases if c[0] == "B" and c[3]]
    frames = [c[1] for c in seed_cases if c[0] == "F"]
    out = []
    for _ in range(count):
        choice = rng.random()
        if choice < 0.6:
            blob = b"".join(rng.choice(pieces) for _ in range(rng.randint(1, 3)))
            blob = bytearray(blob)
            for _ in range(rng.randint(0, 3)):
                if blob:
                    blob[rng.randrange(len(blob))] = rng.randrange(256)
            if rng.random() < 0.2 and blob:
                del blob[rng.randrange(len(blob)):]
            mac = "%02X:%02X:%02X:%02X:%02X:%02X" % tuple(rng.randrange(256) for _ in range(6))
            if rng.random() < 0.3:
                prefix = rng.choice(VENDOR_PREFIXES)[0]
                mac = "%02X:%02X:%02X:01:02:03" % (prefix >> 16, prefix >> 8 & 255, prefix & 255)
            adv = rng.randint(0, len(blob)) if rng.random() < 0.2 else 0
            out.append(("B", mac, rng.choice((0, 1, 2, 3)), bytes(blob[:512]), adv))
        elif choice < 0.85:
            frame = bytearray(rng.choice(frames))
            for _ in range(rng.randint(0, 4)):
                if frame:
                    frame[rng.randrange(len(frame))] = rng.randrange(256)
            out.append(("F", bytes(frame)))
        else:
            prefix = rng.choice(VENDOR_PREFIXES)[0] if rng.random() < 0.5 else rng.randrange(1 << 24)
            mac = "%02X:%02X:%02X:%02X:%02X:%02X" % (prefix >> 16, prefix >> 8 & 255, prefix & 255, 1, 2, 3)
            name = bytes(rng.randrange(32, 127) for _ in range(rng.randint(0, 12)))
            out.append(("W", mac, name))
    return out


def encode(case):
    if case[0] == "W":
        return "W %s %s" % (case[1], case[2].hex() or "-")
    if case[0] == "F":
        return "F %s" % (case[1].hex() or "-")
    return "B %s %d %s %d" % (case[1], case[2], case[3].hex() or "-", case[4])


def python_result(case):
    if case[0] == "W":
        r = recon.classify_wifi(case[1], case[2].decode("latin-1"), extras=False)
    elif case[0] == "F":
        r = recon.classify_wifi(None, frame=case[1], extras=False)
    else:
        r = recon.classify_ble(None if case[1] == "-" else case[1], case[2], case[3], case[4], extras=False)
    cat = CPP_CATEGORIES.index(r.get("category"))
    rid = ""
    d = r.get("remote_id")
    if d:
        rid = "%d,%d,%d,%s,%.6f,%.6f,%d,%.6f,%.6f,%d,%.1f,%d,%.2f,%d,%d,%d" % (
            d.get("has_id", False), d.get("has_location", False), d.get("has_operator", False), d.get("id", ""),
            d.get("latitude", 0.0), d.get("longitude", 0.0), d.get("has_operator", False),
            d.get("operator_latitude", 0.0), d.get("operator_longitude", 0.0), d.get("has_altitude", False),
            d.get("altitude_m", 0.0), d.get("has_speed", False), d.get("speed_mps", 0.0), d.get("id_type", 0),
            d.get("aircraft_type", 0), d.get("operator_location_type", 255))
    beacon = ""
    b = r.get("beacon")
    if b:
        beacon = "%s,%d,%d,%d" % (b["uuid"], b["major"], b["minor"], b["measured_power"])
    return "%d|%d|%s|%s|%d|%s|%s" % (cat, r["confidence"], r.get("evidence", "") if cat else "",
                                     r.get("vendor", "") if cat else "", 1 if r["payload_valid"] else 0, rid, beacon)


def normalise_cpp(line):
    parts = line.split("|")
    if parts[0] == "0":
        parts[1], parts[2], parts[3] = "0", "", ""
        parts[5] = parts[6] = ""  # unmatched results carry no reported fields in the app
    return "|".join(parts)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", type=int, default=6000, help="random mutation cases")
    parser.add_argument("--seed", type=int, default=20261121)
    args = parser.parse_args()
    if not shutil.which("g++"):
        print("g++ is required for the Recon parity check", file=sys.stderr)
        return 2
    rng = random.Random(args.seed)
    cases = fixtures()
    cases += mutations(cases, args.cases, rng)
    with tempfile.TemporaryDirectory(prefix="recon-parity-") as temporary:
        binary = build(Path(temporary))
        result = subprocess.run([str(binary)], input="\n".join(encode(c) for c in cases) + "\n",
                                capture_output=True, text=True, check=True)
    expected = [normalise_cpp(line) for line in result.stdout.splitlines()]
    if len(expected) != len(cases):
        print("Driver returned %d results for %d cases" % (len(expected), len(cases)), file=sys.stderr)
        return 1
    failures = 0
    matched = 0
    for case, want in zip(cases, expected):
        got = python_result(case)
        if want.split("|")[0] != "0":
            matched += 1
        if got != want:
            failures += 1
            if failures <= 10:
                print("MISMATCH %s\n  firmware %s\n  app      %s" % (encode(case)[:160], want, got), file=sys.stderr)
    print("Recon parity: %d cases (%d classified), %d mismatches against the firmware classifier"
          % (len(cases), matched, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
