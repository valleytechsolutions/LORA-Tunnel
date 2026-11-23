"""Recon screens: live counter-surveillance list, device details and a finder."""

import time

from .. import recon
from . import icons
from .gfx import (AMBER, BG, CONTENT_BOTTOM, CYAN, FAINT, GREEN, GREEN_INK, LINE, MUTED, RED, SURFACE,
                  SURFACE_2, TEXT, W, ago, bearing, clock, compass, distance_m, font, human_distance, rssi_color)
from .screens import TOP, Details, ListMixin, Screen, empty_state, fmt_rssi, gps_text

CATEGORY_COLOR = {
    "flock": RED, "alpr": RED, "axon": RED, "ring": AMBER, "camera": AMBER, "glasses": AMBER, "drone": CYAN,
    "airtag": (255, 120, 200), "tracker": (255, 120, 200), "ibeacon": MUTED, "raven": AMBER, "skimmer": AMBER,
    "hacker": (180, 140, 255), "fleet": MUTED, "listener": MUTED, "fox": CYAN, "eviltwin": AMBER, "burst": AMBER,
}
GRADE_COLOR = {recon.LOW: MUTED, recon.MEDIUM: AMBER, recon.STRONG: RED}


def category_pill(c, x, y, category, width=None):
    label = recon.CATEGORY_SHORT.get(category, "?")
    color = CATEGORY_COLOR.get(category, MUTED)
    f = font("bold", 7)
    w = width or int(c.text_width(label, f)) + 8
    c.rect((x, y, x + w, y + 11), fill=color, radius=3)
    c.text_center(x + w / 2, y + 2, label, f, BG)
    return w


def grade_dots(c, x, y, confidence):
    color = GRADE_COLOR.get(confidence, FAINT)
    for i in range(3):
        c.rect((x + i * 5, y, x + i * 5 + 3, y + 7), fill=color if i < confidence else LINE, radius=1)
    return 15


def device_title(device):
    name = device.get("name") or ""
    if device["transport"] == "wifi":
        return name or "(hidden SSID)"
    return name or device.get("vendor") or device.get("label") or device["address"]


class Recon(Screen, ListMixin):
    title = "RECON"
    refresh = 0.5
    VISIBLE = 6
    SORTS = ("threat", "signal", "recent")

    def __init__(self):
        self.index = self.top = 0
        self.show_all = False
        self.sort = 0

    def hints(self, app):
        r = app.recon
        if r.state == "running":
            return [("Space", "Stop"), ("Enter", "Details"), ("A", "All" if not self.show_all else "Flagged"),
                    ("S", self.SORTS[self.sort].title()), ("Esc", "Home")]
        return [("Space", "Start"), ("C", "Categories"), ("Esc", "Home")]

    def render(self, c, app):
        app.recon.unseen_alerts = 0
        if app.recon.state == "running":
            self.live(c, app)
        else:
            self.ready(c, app)

    def ready(self, c, app):
        cfg, r = app.cfg, app.recon
        y = TOP
        c.rect((4, y, W - 4, CONTENT_BOTTOM - 30), fill=SURFACE, radius=6)
        icons.eye(c.draw, W - 44, y + 8, 30, LINE)
        enabled = [k for k in recon.CATEGORIES if k in cfg.get("recon_categories", recon.CATEGORIES)]
        wifi = getattr(r.wifi, "interface", None)
        rows = [
            ("Wi-Fi", ("AP beacons on %s" % wifi if wifi else "Demo access points" if not hasattr(r.wifi, "interface")
                       else "No Wi-Fi interface") if cfg.get("recon_wifi", True) else "Off",
             TEXT if cfg.get("recon_wifi", True) else FAINT),
            ("BLE", "Passive advertisements (BlueZ)" if cfg.get("recon_ble", True) else "Off",
             TEXT if cfg.get("recon_ble", True) else FAINT),
            ("Watching", "%d of %d categories" % (len(enabled), len(recon.CATEGORIES)), TEXT),
            ("Show", "%s and stronger" % recon.CONFIDENCE_NAMES[r.display_min()].lower(), TEXT),
            ("Alert", "%s and stronger%s" % (recon.CONFIDENCE_NAMES[r.alert_min()].lower(),
                                             ", sound on" if cfg.get("recon_sound", True) else ""), TEXT),
        ]
        gps = app.engine.gps.snapshot()
        rows.append(("GPS", gps_text(gps) + ("  (needed for 'following' checks)" if not gps["valid"] else ""),
                     GREEN if gps["valid"] else AMBER))
        for i, (label, value, color) in enumerate(rows):
            yy = y + 6 + i * 13
            c.text((12, yy), label, font("ui", 9), MUTED)
            c.text((70, yy), value, font("mono", 9), color, width=W - 120)
        by = CONTENT_BOTTOM - 25
        c.rect((4, by, W - 4, CONTENT_BOTTOM - 3), fill=GREEN, radius=6)
        c.text_center(W // 2, by + 6, "SPACE  ·  START RECON", font("display", 11), GREEN_INK)

    def live(self, c, app):
        r = app.recon
        summary = r.summary()
        y = TOP
        tiles = [("FLAGGED", summary["flagged"], AMBER if summary["flagged"] else TEXT),
                 ("STRONG", summary["strong"], RED if summary["strong"] else TEXT),
                 ("WI-FI", summary["wifi"], TEXT), ("BLE", summary["ble"], TEXT)]
        cw = (W - 8 - 9) / 4
        for i, (label, value, color) in enumerate(tiles):
            x = 4 + i * (cw + 3)
            c.rect((x, y, x + cw, y + 27), fill=SURFACE, radius=5)
            c.text((x + 7, y + 3), label, font("bold", 7), MUTED)
            c.text((x + 7, y + 11), str(value), font("display", 13), color)
        c.text_right(W - 10, y + 3, clock(r.elapsed()), font("mono", 8), FAINT)
        y += 31
        errors = sorted(set(r.errors.values()))
        if errors:
            c.text((8, y), errors[0], font("ui", 8), AMBER, width=W - 16)
            y += 11
        devices = r.device_list(self.show_all, self.SORTS[self.sort])
        if not devices:
            label = "Nothing flagged nearby" if not self.show_all else "No devices heard yet"
            c.text_center(W // 2, y + 18, label, font("bold", 11), TEXT)
            c.text_center(W // 2, y + 34, "Scanning Wi-Fi beacons and BLE adverts. Flagged devices appear here.",
                          font("ui", 8), MUTED)
            if summary["wifi"] or summary["ble"]:
                c.text_center(W // 2, y + 47, "A shows every device heard (%d)." % (summary["wifi"] + summary["ble"]),
                              font("ui", 8), FAINT)
            return
        visible = max(1, (CONTENT_BOTTOM - y) // 16)
        self.clamp(len(devices), visible)
        now = time.time()
        for row, device in enumerate(devices[self.top:self.top + visible]):
            yy = y + row * 16
            stale = now - device["last_seen"] > 60
            if self.top + row == self.index:
                c.rect((4, yy, W - 8, yy + 15), fill=SURFACE_2, radius=3)
                c.rect((4, yy + 2, 5, yy + 13), fill=GREEN)
            x = 9
            if device.get("category"):
                x += category_pill(c, x, yy + 2, device["category"], width=46) + 4
            else:
                c.text((x, yy + 3), "—", font("mono", 8), FAINT)
                x += 50
            c.text((x, yy + 3), "W" if device["transport"] == "wifi" else "B", font("monob", 8),
                   CYAN if device["transport"] == "wifi" else (150, 160, 255))
            x += 10
            title = device_title(device)
            if device.get("follow"):
                title = "FOLLOWING · " + title
            c.text((x, yy + 2), title, font("ui", 10), RED if device.get("follow") else (FAINT if stale else TEXT),
                   width=W - x - 98)
            if device.get("category"):
                grade_dots(c, W - 96, yy + 4, device.get("confidence", 0))
            c.signal_bars(W - 76, yy + 3, None if stale else device.get("rssi"))
            c.text_right(W - 34, yy + 3, fmt_rssi(device.get("rssi")), font("mono", 9),
                         FAINT if stale else rssi_color(device.get("rssi")))
            c.text_right(W - 12, yy + 3, ago(now - device["last_seen"]), font("mono", 8), MUTED)
        self.scrollbar(c, len(devices), visible, y0=y)

    def handle(self, key, app):
        r = app.recon
        devices = r.device_list(self.show_all, self.SORTS[self.sort]) if r.state == "running" else []
        if key == "SPACE":
            app.toggle_recon()
        elif r.state == "running" and self.move(key, len(devices), self.VISIBLE):
            pass
        elif key == "ENTER" and devices:
            device = devices[min(self.index, len(devices) - 1)]
            app.push(ReconDetail(device["transport"], device["address"]))
        elif key == "A" and r.state == "running":
            self.show_all = not self.show_all
            self.index = self.top = 0
        elif key == "S" and r.state == "running":
            self.sort = (self.sort + 1) % len(self.SORTS)
            self.index = self.top = 0
        elif key == "C" and r.state != "running":
            app.push(Categories())
        else:
            return super().handle(key, app)
        return True


class ReconDetail(Details):
    refresh = 1.0

    def __init__(self, transport, address):
        super().__init__("DEVICE")
        self.key = (transport, address)

    def hints(self, app):
        device = app.recon.devices.get(self.key)
        if device is not None and device.get("incident"):
            return [("↑↓", "Scroll"), ("Esc", "Back")]
        return [("F", "Find it"), ("↑↓", "Scroll"), ("Esc", "Back")]

    def rows(self, app):
        device = app.recon.devices.get(self.key)
        if device is None:
            return [("", "This device is no longer in the live table.", MUTED)]
        now = time.time()
        if device.get("incident"):
            return [("Category", recon.CATEGORY_LABELS[device["category"]], CATEGORY_COLOR["burst"]),
                    ("Kind", device.get("name", "")), ("", device.get("evidence", ""), MUTED),
                    ("Signal", "%s dBm" % fmt_rssi(device.get("rssi")), rssi_color(device.get("rssi"))),
                    ("Seen", "%d times, last %s ago, first %s ago" % (device["count"], ago(now - device["last_seen"]),
                                                                       ago(now - device["first_seen"]))),
                    ("", "Many addresses at once can be a crowd or advert spam; the sender is not identified.",
                     FAINT)]
        rows = []
        if device.get("category"):
            rows.append(("Category", recon.CATEGORY_LABELS[device["category"]],
                         CATEGORY_COLOR.get(device["category"], TEXT)))
            rows.append(("Evidence", recon.CONFIDENCE_NAMES[device["confidence"]],
                         GRADE_COLOR.get(device["confidence"], TEXT)))
            if device.get("evidence"):
                rows.append(("", device["evidence"], MUTED))
            if device.get("vendor"):
                rows.append(("Vendor", device["vendor"]))
            if device["category"] == "airtag" and device.get("owner"):
                rows.append(("Owner", device["owner"], CATEGORY_COLOR["airtag"]))
            if device["category"] == "eviltwin" and device.get("peer"):
                rows.append(("Protected", device["peer"]))
        else:
            rows.append(("Category", "Not flagged", MUTED))
        if device.get("follow"):
            rows.append(("Following", "Seen at several places you've been over 10+ minutes", RED))
        if device["transport"] == "wifi":
            rows += [("SSID", device.get("name") or "(hidden)"), ("BSSID", device["address"]),
                     ("Channel", "%s  ·  %s MHz  ·  %s" % (device.get("channel") or "?", device.get("frequency") or "?",
                                                           device.get("security") or "?"))]
        else:
            kind = {0: "public", 1: "random", 2: "public identity", 3: "random identity"}.get(device.get("address_type"), "?")
            rows += [("Name", device.get("name") or "—"), ("Address", "%s (%s)" % (device["address"], kind))]
            if device.get("address_type") in (1, 3):
                rows.append(("", "Random addresses rotate; this row may split over time.", FAINT))
        rows.append(("Signal", "%s dBm now, best %s dBm" % (fmt_rssi(device.get("rssi")), fmt_rssi(device.get("best_rssi"))),
                     rssi_color(device.get("rssi"))))
        rows.append(("Seen", "%d times, last %s ago, first %s ago" % (device["count"], ago(now - device["last_seen"]),
                                                                       ago(now - device["first_seen"]))))
        if device.get("best_fix"):
            rows.append(("Best RX at", "%.5f, %.5f" % device["best_fix"]))
        beacon = device.get("beacon")
        if beacon:
            u = beacon["uuid"]
            rows += [("UUID", "%s-%s-%s-%s-%s" % (u[:8], u[8:12], u[12:16], u[16:20], u[20:])),
                     ("Major/minor", "%d / %d  ·  1 m power %d dBm" % (beacon["major"], beacon["minor"],
                                                                     beacon["measured_power"]))]
        rid = device.get("remote_id")
        if rid:
            if rid.get("has_id"):
                rows.append(("Drone ID", rid["id"], CYAN))
            gps = app.engine.gps.snapshot()
            here = (gps["latitude"], gps["longitude"]) if gps["valid"] else None
            if rid.get("has_location"):
                there = (rid["latitude"], rid["longitude"])
                rows.append(("Aircraft", "%.5f, %.5f (self-reported)" % there, CYAN))
                if here:
                    rows.append(("", "%s %s from you" % (human_distance(distance_m(here, there)),
                                                        compass(bearing(here, there))), CYAN))
                extra = []
                if rid.get("has_altitude"):
                    extra.append("alt %.0f m" % rid["altitude_m"])
                if rid.get("has_speed"):
                    extra.append("%.1f m/s" % rid["speed_mps"])
                if extra:
                    rows.append(("", ", ".join(extra), MUTED))
            if rid.get("has_operator"):
                there = (rid["operator_latitude"], rid["operator_longitude"])
                kind = {0: "takeoff point", 1: "live operator", 2: "fixed point"}.get(rid.get("operator_location_type"),
                                                                                    "operator")
                rows.append(("Operator", "%.5f, %.5f (%s)" % (there[0], there[1], kind), AMBER))
                if here:
                    rows.append(("", "%s %s from you" % (human_distance(distance_m(here, there)),
                                                        compass(bearing(here, there))), AMBER))
        rows.append(("", "A match is broadcast evidence, not proof of recording, intent or ownership.", FAINT))
        return rows

    def handle(self, key, app):
        device = app.recon.devices.get(self.key)
        if key == "F" and not (device and device.get("incident")):
            app.push(Finder(*self.key))
            return True
        return super().handle(key, app)


class Finder(Screen):
    """Big signal meter for walking towards a device."""

    title = "FIND"
    refresh = 0.25
    chrome = True

    def __init__(self, transport, address):
        self.key = (transport, address)
        self.peak = None

    def hints(self, app):
        return [("Esc", "Back"), ("R", "Reset peak")]

    def render(self, c, app):
        device = app.recon.devices.get(self.key)
        if device is None:
            empty_state(c, "eye", "Device lost", "It hasn't been heard for a while.")
            return
        now = time.time()
        history = [(t, v) for t, v in device["rssi_history"] if v is not None and now - t < 30]
        rssi = device.get("rssi") if now - device["last_seen"] < 15 else None
        if rssi is not None and (self.peak is None or rssi > self.peak):
            self.peak = rssi
        y = TOP
        title = device_title(device)
        x = 8
        if device.get("category"):
            x += category_pill(c, 8, y + 1, device["category"]) + 6
        c.text((x, y), title, font("bold", 11), TEXT, width=W - x - 8)
        y += 18
        c.text((10, y), fmt_rssi(rssi), font("display", 40), rssi_color(rssi) if rssi is not None else FAINT)
        c.text((10 + c.text_width(fmt_rssi(rssi), font("display", 40)) + 4, y + 26), "dBm", font("bold", 10), MUTED)
        trend = "—"
        color = MUTED
        if len(history) >= 4:
            half = len(history) // 2
            old = sum(v for _, v in history[:half]) / half
            new = sum(v for _, v in history[half:]) / (len(history) - half)
            if new - old > 2:
                trend, color = "WARMER", GREEN
            elif old - new > 2:
                trend, color = "COLDER", AMBER
            else:
                trend = "STEADY"
        c.text_right(W - 10, y + 4, trend, font("display", 14), color)
        c.text_right(W - 10, y + 24, "peak %s dBm" % fmt_rssi(self.peak), font("mono", 9), MUTED)
        beacon = device.get("beacon")
        estimate = recon.distance_from_rssi(rssi, beacon["measured_power"] if beacon else -59)
        if estimate is not None:
            c.text_right(W - 10, y + 36, "≈ %s (rough)" % human_distance(estimate), font("mono", 9), FAINT)
        y += 54
        level = 0 if rssi is None else max(0.0, min(1.0, (rssi + 110) / 70))
        c.progress((10, y, W - 10, y + 10), level, fg=rssi_color(rssi) if rssi is not None else LINE)
        y += 16
        gh = CONTENT_BOTTOM - y - 2
        c.rect((10, y, W - 10, y + gh), fill=SURFACE, radius=4)
        if history:
            span = 30.0
            for i in range(len(history) - 1):
                (t0, v0), (t1, v1) = history[i], history[i + 1]
                x0 = 12 + (W - 24) * (1 - (now - t0) / span)
                x1 = 12 + (W - 24) * (1 - (now - t1) / span)
                y0 = y + gh - 2 - (gh - 4) * max(0, min(1, (v0 + 110) / 70))
                y1 = y + gh - 2 - (gh - 4) * max(0, min(1, (v1 + 110) / 70))
                c.line((x0, y0, x1, y1), fill=GREEN, width=2)
        c.text((14, y + 2), "LAST 30 s", font("bold", 7), FAINT)

    def handle(self, key, app):
        if key == "R":
            self.peak = None
            return True
        return super().handle(key, app)


class Categories(Screen, ListMixin):
    title = "RECON CATEGORIES"
    VISIBLE = 8

    def __init__(self):
        self.index = self.top = 0

    def hints(self, app):
        return [("Enter", "On/Off"), ("A", "All on"), ("Esc", "Back")]

    def render(self, c, app):
        enabled = set(app.cfg.get("recon_categories", recon.CATEGORIES))
        keys = list(recon.CATEGORIES)
        self.clamp(len(keys), self.VISIBLE)
        for row, key in enumerate(keys[self.top:self.top + self.VISIBLE]):
            y = TOP + row * 16
            selected = self.top + row == self.index
            if selected:
                c.rect((4, y, W - 8, y + 15), fill=SURFACE_2, radius=3)
                c.rect((4, y + 2, 5, y + 13), fill=GREEN)
            category_pill(c, 10, y + 2, key, width=56)
            c.text((72, y + 2), recon.CATEGORY_LABELS[key], font("ui", 10), TEXT if key in enabled else FAINT,
                   width=180)
            on = key in enabled
            c.rect((W - 44, y + 3, W - 18, y + 12), fill=GREEN if on else SURFACE_2, radius=5)
            c.draw.ellipse((W - 30 if on else W - 43, y + 4, W - 21 if on else W - 34, y + 11),
                           fill=GREEN_INK if on else MUTED)
        self.scrollbar(c, len(keys), self.VISIBLE)

    def handle(self, key, app):
        keys = list(recon.CATEGORIES)
        if self.move(key, len(keys), self.VISIBLE):
            return True
        enabled = list(app.cfg.get("recon_categories", recon.CATEGORIES))
        if key in ("ENTER", "SPACE"):
            name = keys[self.index]
            enabled = [k for k in keys if (k in enabled) != (k == name)]
            app.save_config(recon_categories=enabled)
            return True
        if key == "A":
            app.save_config(recon_categories=keys)
            return True
        return super().handle(key, app)


def alert_banner(c, app):
    """Overlay for a fresh Recon alert, shown on every screen except Recon itself."""
    r = getattr(app, "recon", None)
    if r is None or not r.alerts or not r.unseen_alerts:
        return
    alert = r.alerts[0]
    if time.time() - alert["time"] > 10:
        return
    strong = alert["confidence"] >= recon.STRONG or alert.get("follow")
    fill, edge = ((70, 12, 14), RED) if strong else ((60, 44, 8), AMBER)
    y0 = TOP - 2
    c.rect((4, y0, W - 4, y0 + 28), fill=fill, outline=edge, radius=6)
    blink = int(time.monotonic() * 3) % 2 == 0
    icons.alert(c.draw, 10, y0 + 5, 18, edge if blink else (fill[0] * 2, fill[1] * 2, fill[2] * 2))
    title = ("FOLLOWING?  " if alert.get("follow") else "") + alert["label"]
    c.text((34, y0 + 3), title, font("bold", 10), TEXT, width=W - 110)
    detail = "%s  ·  %s" % (alert.get("name") or alert["address"], recon.CONFIDENCE_NAMES[alert["confidence"]])
    c.text((34, y0 + 15), detail, font("ui", 8), (230, 220, 210), width=W - 110)
    c.text_right(W - 12, y0 + 3, "%s dBm" % fmt_rssi(alert.get("rssi")), font("monob", 9), edge)
    c.text_right(W - 12, y0 + 15, "R to view" if r.unseen_alerts < 2 else "+%d · R" % (r.unseen_alerts - 1),
                 font("mono", 8), MUTED)
