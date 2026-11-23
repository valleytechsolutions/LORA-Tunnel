"""Private, validated configuration (~/.config/lora-tunnel/config.json, mode 0600).

Values are validated field by field. Error messages never contain the value,
so channel keys can't leak into the screen or logs.
"""

import base64
import copy
import json
import os
from pathlib import Path
import stat
import tempfile

from . import profiles, recon

MAX_BYTES = 32768
MAX_KEYS = 8

DEFAULTS = {
    "region": "",
    "mode": "meshtastic",
    "preset": "LONG_FAST",
    "slot": 0,
    "meshcore_profile": "AUTO",
    "dwell_seconds": 30,
    "survey_dwell_seconds": 15,
    "backend": "cap",
    "serial_device": "",
    "decode_public": True,
    "meshtastic_keys": [],
    "meshcore_keys": [],
    "meshcore_hashtags": [],
    "data_dir": "",
    # Messaging (transmit is off until the user turns it on)
    "tx_enabled": False,
    "tx_power_dbm": 17,
    "hop_limit": 3,
    "node_number": 0,
    "node_name": "",
    "node_short": "",
    # Wi-Fi/BLE wardriving runs with LoRa capture; WiGLE CSV skips random BLE addresses by default
    "wardrive_wifi_ble": True,
    "wigle_random_ble": False,
    "update_check_start": True,
    # Recon
    "recon_wifi": True,
    "recon_ble": True,
    "recon_sound": True,
    "recon_display_min": "low",
    "recon_alert_min": "medium",
    "recon_categories": [c for c in recon.CATEGORIES if c not in recon.OFF_BY_DEFAULT],
    "recon_categories_known": list(recon.CATEGORIES),
}

_CHOICES = {
    "region": set(profiles.REGIONS) | {""},
    "mode": set(profiles.MODES),
    "preset": set(profiles.PRESETS),
    "backend": {"cap", "lorapipe", "demo"},
    "recon_display_min": {"low", "medium", "high"},
    "recon_alert_min": {"low", "medium", "high"},
}


class ConfigError(ValueError):
    """A message that never includes the rejected value."""


def config_path():
    override = os.environ.get("LORA_TUNNEL_CONFIG")
    if override:
        return Path(override).expanduser()
    return Path.home() / ".config" / "lora-tunnel" / "config.json"


def data_dir(config):
    chosen = config.get("data_dir") or os.environ.get("LORA_TUNNEL_DATA")
    return Path(chosen).expanduser() if chosen else Path.home() / "LoRaTunnel"


def _key(value):
    if not isinstance(value, str) or len(value) > 64:
        raise ConfigError("Channel keys must be base64 text")
    try:
        raw = base64.b64decode(value, validate=True)
    except ValueError as exc:
        raise ConfigError("Channel keys must be base64 text") from exc
    # Meshtastic accepts an empty key, a one-byte default-key index, or AES-128/256.
    if len(raw) not in (0, 1, 16, 32):
        raise ConfigError("Channel keys must decode to 0, 1, 16 or 32 bytes")
    return value


def _name(value, limit=32):
    if not isinstance(value, str) or not 1 <= len(value) <= limit:
        raise ConfigError("Channel names must be 1 to 32 characters")
    if any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise ConfigError("Channel names contain unsupported characters")
    return value


def validate(key, value):
    if key not in DEFAULTS:
        raise ConfigError("Unknown configuration field")
    default = DEFAULTS[key]
    if key in _CHOICES:
        if value not in _CHOICES[key]:
            raise ConfigError("Unsupported choice for " + key)
        return value
    if isinstance(default, bool):
        if type(value) is not bool:
            raise ConfigError(key + " must be true or false")
        return value
    if key in ("slot",):
        if type(value) is not int or not 0 <= value <= 104:
            raise ConfigError("Slot must be 0 (default) to 104")
        return value
    if key == "tx_power_dbm":
        if type(value) is not int or not 2 <= value <= 22:
            raise ConfigError("Transmit power must be 2 to 22 dBm")
        return value
    if key == "hop_limit":
        if type(value) is not int or not 1 <= value <= 7:
            raise ConfigError("Hop limit must be 1 to 7")
        return value
    if key == "node_number":
        if type(value) is not int or not (value == 0 or 4 <= value < 0xFFFFFFFF):
            raise ConfigError("Node number is out of range")
        return value
    if key in ("node_name", "node_short"):
        limit = 4 if key == "node_short" else 32
        if not isinstance(value, str) or len(value) > limit or len(value.encode("utf-8")) > limit * 2:
            raise ConfigError("Name is too long" if key == "node_name" else "Short name is 1 to 4 characters")
        if any(ord(c) < 32 or ord(c) == 127 or c == ":" for c in value):
            raise ConfigError("Names can't contain control characters or ':'")
        return value.strip()
    if key in ("recon_categories", "recon_categories_known"):
        if not isinstance(value, list) or any(v not in recon.CATEGORIES for v in value) or len(set(value)) != len(value):
            raise ConfigError("Unknown Recon category")
        return value
    if key in ("dwell_seconds", "survey_dwell_seconds"):
        if type(value) not in (int, float) or not profiles.MIN_DWELL <= value <= profiles.MAX_DWELL:
            raise ConfigError("Dwell must be 5 to 600 seconds")
        return value
    if key in ("meshtastic_keys", "meshcore_keys"):
        if not isinstance(value, list) or len(value) > MAX_KEYS:
            raise ConfigError("Up to 8 channel keys are allowed")
        result = []
        for item in value:
            if not isinstance(item, dict) or set(item) != {"name", "key"}:
                raise ConfigError("Each channel key needs exactly a name and a key")
            raw = base64.b64decode(_key(item["key"]))
            if key == "meshcore_keys" and len(raw) not in (16, 32):
                raise ConfigError("MeshCore channel secrets are 16 or 32 bytes")
            result.append({"name": _name(item["name"]), "key": item["key"]})
        return result
    if key == "meshcore_hashtags":
        if not isinstance(value, list) or len(value) > MAX_KEYS:
            raise ConfigError("Up to 8 hashtag channels are allowed")
        return [_name(tag if tag.startswith("#") else "#" + tag) for tag in value if isinstance(tag, str)]
    if key == "meshcore_profile":
        names = {"AUTO"} | {row[1] for row in profiles.MESHCORE}
        if value not in names:
            raise ConfigError("Unknown MeshCore profile")
        return value
    if key == "serial_device":
        if value and (not isinstance(value, str) or not value.startswith("/dev/") or ".." in value):
            raise ConfigError("Serial device must be a path under /dev")
        return value
    if key == "data_dir":
        if value and (not isinstance(value, str) or not Path(value).expanduser().is_absolute()):
            raise ConfigError("Data folder must be an absolute path")
        return value
    raise ConfigError("Unsupported configuration field")


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ConfigError("Duplicate configuration field")
        result[key] = value
    return result


def load(path=None):
    path = Path(path) if path is not None else config_path()
    result = copy.deepcopy(DEFAULTS)
    if not path.exists():
        return result
    try:
        fd = os.open(str(path), os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
        with os.fdopen(fd, "rb") as source:
            info = os.fstat(source.fileno())
            if not stat.S_ISREG(info.st_mode):
                raise ConfigError("Configuration must be a regular file")
            if stat.S_IMODE(info.st_mode) & 0o077:
                raise ConfigError("Configuration permissions must be 0600")
            raw = source.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES:
            raise ConfigError("Configuration file is too large")
        stored = json.loads(raw, object_pairs_hook=_unique)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ConfigError("Cannot read the configuration file") from exc
    if not isinstance(stored, dict):
        raise ConfigError("Configuration must be a JSON object")
    for key, value in stored.items():
        result[key] = validate(key, value)
    if "recon_categories" in stored:
        known = stored.get("recon_categories_known", recon.FIRMWARE_CATEGORIES)
        result["recon_categories"] += [c for c in recon.CATEGORIES if c not in known and c not in
                                       recon.OFF_BY_DEFAULT and c not in result["recon_categories"]]
        result["recon_categories_known"] = list(recon.CATEGORIES)
    return result


def save(config, path=None):
    """Validate every field, then replace the file atomically with mode 0600."""
    path = Path(path) if path is not None else config_path()
    clean = {key: validate(key, config.get(key, default)) for key, default in DEFAULTS.items()}
    encoded = (json.dumps(clean, indent=2, ensure_ascii=True) + "\n").encode()
    if len(encoded) > MAX_BYTES:
        raise ConfigError("Configuration file is too large")
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".config-", suffix=".json", dir=str(path.parent))
    try:
        with os.fdopen(fd, "wb") as target:
            os.fchmod(target.fileno(), 0o600)
            target.write(encoded)
            target.flush()
            os.fsync(target.fileno())
        os.replace(temporary, path)
        temporary = None
    except OSError as exc:
        raise ConfigError("Cannot save the configuration file") from exc
    finally:
        if temporary is not None:
            try:
                os.unlink(temporary)
            except OSError:
                pass
    return clean


def decode_keys(config):
    """Return (meshtastic_keys, meshcore_keys) in the decoder's format."""
    import hashlib
    mt = list(config.get("meshtastic_keys", []))
    mc = list(config.get("meshcore_keys", []))
    # MeshCore hashtag channels derive their secret from the name.
    for tag in config.get("meshcore_hashtags", []):
        secret = hashlib.sha256(tag.encode("utf-8")).digest()[:16]
        mc.append({"name": tag, "key": base64.b64encode(secret).decode()})
    return mt, mc
