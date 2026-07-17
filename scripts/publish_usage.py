#!/usr/bin/env python3
"""Publish usage data / thinking state to the panel via HiveMQ Cloud.

Config: copy scripts/.env.example to scripts/.env (gitignored) and fill in
broker credentials. Real environment variables override .env. Standalone —
works without the firmware tree.

Usage:
  pip install paho-mqtt
  python publish_usage.py usage mock_usage.json    # publish full usage JSON (retained)
  python publish_usage.py thinking 1               # thinking LED on
  python publish_usage.py thinking 0               # thinking LED off
"""

import json
import os
import ssl
import sys
from pathlib import Path

import paho.mqtt.client as mqtt

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
    """Config comes from scripts/.env (copy .env.example), overridable by
    real environment variables. Fully standalone — no firmware tree needed."""
    cfg = _parse_env_file(ENV_FILE)
    for k in CONFIG_KEYS:
        if k in os.environ:
            cfg[k] = os.environ[k]
    missing = [k for k in ("MQTT_HOST", "MQTT_PORT", "MQTT_USER", "MQTT_PASS") if k not in cfg]
    if missing:
        sys.exit(f"missing config {', '.join(missing)} — copy {ENV_FILE.parent / '.env.example'} "
                 f"to {ENV_FILE} and fill it in")
    return cfg


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    cfg = read_config()  # exits with a helpful message if incomplete

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
