#!/usr/bin/env python3
"""Publish usage data / thinking state to the panel via HiveMQ Cloud.

Credentials are read from include/config.h (gitignored) — no secrets here.

Usage:
  pip install paho-mqtt
  python publish_usage.py usage mock_usage.json    # publish full usage JSON (retained)
  python publish_usage.py thinking 1               # thinking LED on
  python publish_usage.py thinking 0               # thinking LED off
"""

import json
import os
import re
import ssl
import sys
from pathlib import Path

import paho.mqtt.client as mqtt

CONFIG_H = Path(__file__).resolve().parent.parent / "include" / "config.h"
ENV_FILE = Path(__file__).resolve().parent / ".env"

CONFIG_KEYS = ("MQTT_HOST", "MQTT_PORT", "MQTT_USER", "MQTT_PASS",
               "MQTT_TOPIC_USAGE", "MQTT_TOPIC_THINKING")


def _parse_env_file(path: Path) -> dict:
    cfg = {}
    if not path.exists():
        return cfg
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        cfg[k.strip()] = v.strip().strip('"').strip("'")
    return cfg


def read_config() -> dict:
    """Priority: environment variables > scripts/.env > include/config.h.
    The .env path lets the scripts run standalone on machines without the
    firmware tree's (gitignored) config.h."""
    cfg = {}
    if CONFIG_H.exists():
        text = CONFIG_H.read_text(encoding="utf-8")
        for m in re.finditer(r'^#define\s+(\w+)\s+(?:"([^"]*)"|(\d+))', text, flags=re.MULTILINE):
            cfg[m.group(1)] = m.group(2) if m.group(2) is not None else int(m.group(3))
    cfg.update(_parse_env_file(ENV_FILE))
    for k in CONFIG_KEYS:
        if k in os.environ:
            cfg[k] = os.environ[k]
    return cfg


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    cfg = read_config()
    missing = [k for k in ("MQTT_HOST", "MQTT_PORT", "MQTT_USER", "MQTT_PASS") if k not in cfg]
    if missing:
        print(f"missing in {CONFIG_H}: {', '.join(missing)}")
        sys.exit(1)

    mode, arg = sys.argv[1], sys.argv[2]

    if mode == "usage":
        payload = Path(arg).read_text(encoding="utf-8")
        json.loads(payload)  # validate before publishing
        topic, retain = cfg.get("MQTT_TOPIC_USAGE", "claude/usage"), True
    elif mode == "thinking":
        payload = "1" if arg in ("1", "true", "on") else "0"
        topic, retain = cfg.get("MQTT_TOPIC_THINKING", "claude/thinking"), False
    else:
        print(f"unknown mode: {mode}")
        sys.exit(1)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(cfg["MQTT_USER"], cfg["MQTT_PASS"])
    client.tls_set(tls_version=ssl.PROTOCOL_TLS_CLIENT)
    client.connect(cfg["MQTT_HOST"], int(cfg["MQTT_PORT"]), keepalive=30)
    client.loop_start()

    info = client.publish(topic, payload, qos=1, retain=retain)
    info.wait_for_publish(timeout=10)
    client.loop_stop()
    client.disconnect()

    print(f"published to {topic} (retain={retain}): {payload[:80]}{'...' if len(payload) > 80 else ''}")


if __name__ == "__main__":
    main()
