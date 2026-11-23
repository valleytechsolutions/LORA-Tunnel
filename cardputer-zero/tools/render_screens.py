#!/usr/bin/env python3
"""Render every screen with the synthetic receiver into PNGs (320x170, plus @2x).

All traffic is synthetic. Output defaults to screenshots/. Run from cardputer-zero/:
    python3 tools/render_screens.py [--out DIR] [--seconds N]
"""

import argparse
from pathlib import Path
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))

from lora_tunnel_zero import __version__, config as configuration  # noqa: E402
from lora_tunnel_zero.demo import DemoBle, DemoRadio, DemoWifi  # noqa: E402
from lora_tunnel_zero.recon_scan import Recon  # noqa: E402
from lora_tunnel_zero.engine import Engine  # noqa: E402
from lora_tunnel_zero.gnss import GPS  # noqa: E402
from lora_tunnel_zero.updates import UpdateChecker  # noqa: E402
from lora_tunnel_zero.ui import recon_screens, screens  # noqa: E402
from lora_tunnel_zero.ui.app import App  # noqa: E402
from lora_tunnel_zero.ui.display import PNGDisplay  # noqa: E402
from lora_tunnel_zero.ui.keys import ScriptedInput  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=str(HERE / "screenshots"))
    parser.add_argument("--seconds", type=float, default=8)
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp())
    cfg = dict(configuration.DEFAULTS, region="US", mode="both", backend="demo", dwell_seconds=30,
               tx_enabled=True, node_number=0x4c7a11d3, node_name="Valley Rover", node_short="VTR",
               meshcore_hashtags=["#wardrive"])
    gps = GPS(use_uart=False)
    gps.update({"class": "TPV", "mode": 3, "lat": 37.7790, "lon": -122.4170, "time": "2026-11-21T05:00:00Z",
                "satellites": 9})
    engine = Engine(cfg, radio_factory=lambda c, p: DemoRadio(p, rate=10), gps=gps)
    display = PNGDisplay(out / "_frame.png")

    class QuietSound:
        def play(self):
            return False
    recon_engine = Recon(cfg, gps, wifi=DemoWifi(), ble=DemoBle(), sound=QuietSound())
    app = App(display, ScriptedInput(), engine, cfg, config_path=work / "config.json",
              data_root=work, splash=False, recon=recon_engine)

    def shot(name, screen=None, modal=None, alert=False, top=None):
        recon_engine.unseen_alerts = 1 if alert else 0
        gps.update({"class": "TPV", "mode": 3, "lat": 37.7790, "lon": -122.4170, "satellites": 9})
        app.stack = [screens.Home()]
        if screen is not None:
            app.stack.append(screen)
        if modal is not None:
            app.stack.append(modal)
        img = app.compose()
        if top is not None:
            screen.top = top
            img = app.compose()
        img.save(out / (name + ".png"))
        img.resize((640, 340), resample=0).save(out / (name + "@2x.png"))
        print("wrote", name)

    app.stack = [screens.Home(), screens.Splash()]
    img = app.compose()
    img.save(out / "splash.png")
    img.resize((640, 340), resample=0).save(out / "splash@2x.png")
    shot("wardrive-ready", screens.Wardrive())
    shot("recon-ready", recon_screens.Recon())
    app.toggle_capture()          # starts LoRa, Wi-Fi and BLE wardriving together
    engine.hop(0)
    time.sleep(args.seconds / 2)
    engine.hop(1)
    time.sleep(args.seconds / 2)
    shot("home")
    shot("wardrive-live", screens.Wardrive())
    shot("nodes", screens.Nodes())
    first = engine.node_list()[0]
    shot("node-detail", screens.NodeDetail(first["protocol"], first["id"]))
    shot("packets", screens.Packets())
    decoded = next(item for item in engine.recent if item["text"])
    shot("packet-detail", screens.PacketDetail(decoded))
    mapped = [n for n in engine.node_list() if n.get("neighbors")]
    if mapped:
        shot("node-mesh", screens.NodeDetail(mapped[0]["protocol"], mapped[0]["id"]), top=9)
    trace = next((item for item in engine.recent if item["kind"] == "traceroute"), None)
    if trace is not None:
        shot("packet-traceroute", screens.PacketDetail(trace), top=4)
    while engine.active.protocol != "meshtastic":
        engine.hop(1)
    engine.send("meshtastic", "LongFast", "Rolling through downtown, anyone copy?")
    time.sleep(2.5)
    shot("messages", screens.Messages())
    shot("compose", screens.Messages(), screens.Compose(app.send_targets(), ("meshtastic", "LongFast")))
    app.screen  # noqa: B018
    app.stack[-1:] = []
    # Walk the demo receiver across town so the 'following' check has movement to judge.
    for step in range(12):
        gps.update({"class": "TPV", "mode": 3, "lat": 37.7790 + step * 0.0011, "lon": -122.4170 + step * 0.0006,
                    "satellites": 9})
        for device in recon_engine.ble.scan():
            recon_engine.observe("ble", device, now=time.time() - 660 + step * 60)
    recon_screen = recon_screens.Recon()
    shot("recon-live", recon_screen)
    top = recon_engine.device_list()[0]
    shot("recon-detail", recon_screens.ReconDetail(top["transport"], top["address"]))
    drone = next(d for d in recon_engine.device_list() if d.get("category") == "drone")
    shot("recon-drone", recon_screens.ReconDetail(drone["transport"], drone["address"]))
    shot("recon-finder", recon_screens.Finder(top["transport"], top["address"]))
    recon_engine.alerts[0]["time"] = time.time()
    shot("recon-alert", screens.Wardrive(), alert=True)
    recon_engine.stop()
    engine.stop()
    shot("sessions", screens.Sessions())
    shot("settings", screens.Settings())
    shot("region-picker", screens.Settings(), screens.Picker(
        "LoRa region", [(c, r.label, c) for c, r in list(__import__("lora_tunnel_zero.profiles",
                                                                     fromlist=["REGIONS"]).REGIONS.items())[:8]],
        "US"))
    current = __version__

    def offline_fetch(url, timeout=8.0, no_cache=False):
        if "registry" in url:
            return {"apps": [{"share_code": "LORA", "version": current, "download": {"package": "lora-tunnel"}}]}
        return [{"tag_name": "cardputer-zero-v" + current, "draft": False, "prerelease": False}]
    app.updates = UpdateChecker(offline_fetch)
    app.updates.start()
    app.updates.wait(5)
    updates_screen = screens.Updates()
    updates_screen.started = True
    shot("updates", updates_screen)
    shot("about", screens.About())
    shot("help", screens.Help())
    (out / "_frame.png").unlink(missing_ok=True)


if __name__ == "__main__":
    main()
