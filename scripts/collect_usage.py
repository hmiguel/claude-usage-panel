#!/usr/bin/env python3
"""Compile real Claude Code usage from local transcripts and publish to the panel.

Reads ~/.claude/projects/**/*.jsonl (written by Claude Code itself), computes
session/week/per-model usage, and publishes the panel's usage JSON to MQTT.

No credentials or network calls beyond the MQTT broker (config from
include/config.h). Limits are estimated against configurable budgets —
calibrate SESSION_BUDGET / WEEK_BUDGET below by comparing with Claude Code's
/usage screen.

Usage:
  python collect_usage.py --dry-run     # print JSON, don't publish
  python collect_usage.py               # collect + publish once (retained)
  python collect_usage.py --watch 60    # republish every 60s
"""

import json
import ssl
import sys
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path

from publish_usage import read_config  # shared config.h parser

# --- Tunables -----------------------------------------------------------
# Weighted tokens: cache reads are far cheaper than fresh tokens, so weight
# them down instead of counting raw.
CACHE_READ_WEIGHT = 0.1
# Budgets in weighted tokens. Calibrate against Claude Code's /usage screen:
# if /usage says 40% but the panel says 20%, halve the budget.
SESSION_BUDGET = 10_000_000  # per 5h session block
WEEK_BUDGET = 40_000_000     # per calendar week
SESSION_HOURS = 5
# -----------------------------------------------------------------------

PROJECTS_DIR = Path.home() / ".claude" / "projects"


def weighted_tokens(usage: dict) -> float:
    return (
        usage.get("input_tokens", 0)
        + usage.get("output_tokens", 0)
        + usage.get("cache_creation_input_tokens", 0)
        + usage.get("cache_read_input_tokens", 0) * CACHE_READ_WEIGHT
    )


def model_family(model: str) -> str | None:
    m = (model or "").lower()
    for fam in ("opus", "sonnet", "haiku", "fable"):
        if fam in m:
            return fam.capitalize()
    return None


def load_entries(max_age_days: float = 8.0) -> list[tuple[datetime, str, float]]:
    """Returns deduped (timestamp, family, weighted_tokens) tuples."""
    cutoff_mtime = time.time() - max_age_days * 86400
    entries = []
    seen = set()
    for path in PROJECTS_DIR.glob("*/*.jsonl"):
        try:
            if path.stat().st_mtime < cutoff_mtime:
                continue
            with open(path, encoding="utf-8") as f:
                for line in f:
                    if '"assistant"' not in line:
                        continue
                    try:
                        d = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if d.get("type") != "assistant":
                        continue
                    msg = d.get("message") or {}
                    usage = msg.get("usage")
                    ts_str = d.get("timestamp")
                    if not usage or not ts_str:
                        continue
                    fam = model_family(msg.get("model"))
                    if fam is None:  # skips "<synthetic>" etc.
                        continue
                    key = (msg.get("id"), d.get("requestId"))
                    if key in seen:
                        continue
                    seen.add(key)
                    try:
                        ts = datetime.fromisoformat(ts_str.replace("Z", "+00:00"))
                    except ValueError:
                        continue
                    entries.append((ts, fam, weighted_tokens(usage)))
        except OSError:
            continue
    entries.sort(key=lambda e: e[0])
    return entries


def floor_to_hour(ts: datetime) -> datetime:
    return ts.replace(minute=0, second=0, microsecond=0)


def current_session(entries, now: datetime):
    """ccusage-style 5h blocks: a block starts at the first message's hour and
    lasts SESSION_HOURS; later messages open new blocks. Returns (tokens,
    resets_at) for the block containing `now`, or (0, now+5h) if idle."""
    block_start = None
    block_tokens = 0.0
    for ts, _fam, tok in entries:
        if block_start is None or ts >= block_start + timedelta(hours=SESSION_HOURS):
            block_start = floor_to_hour(ts)
            block_tokens = 0.0
        block_tokens += tok
    if block_start is not None and now < block_start + timedelta(hours=SESSION_HOURS):
        return block_tokens, block_start + timedelta(hours=SESSION_HOURS)
    return 0.0, floor_to_hour(now) + timedelta(hours=SESSION_HOURS)


def build_payload() -> dict:
    now = datetime.now(timezone.utc)
    entries = load_entries()

    session_tokens, session_resets = current_session(entries, now)

    # Calendar week (local time), Monday..Sunday — matches the UI bar labels.
    local_now = now.astimezone()
    monday = (local_now - timedelta(days=local_now.weekday())).replace(
        hour=0, minute=0, second=0, microsecond=0
    )
    week_resets = monday + timedelta(days=7)

    daily = [0.0] * 7
    fam_tokens: dict[str, float] = {}
    for ts, fam, tok in entries:
        lts = ts.astimezone()
        if lts < monday:
            continue
        daily[lts.weekday()] += tok
        fam_tokens[fam] = fam_tokens.get(fam, 0) + tok

    week_tokens = sum(daily)
    max_day = max(daily) or 1.0

    models = [
        {"name": fam, "pct": round(tok / week_tokens * 100) if week_tokens else 0}
        for fam, tok in sorted(fam_tokens.items(), key=lambda kv: -kv[1])
    ][:3]

    def iso_utc(dt: datetime) -> str:
        return dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    return {
        "updated_at": iso_utc(now),
        "today": {
            "session_used_pct": min(100, round(session_tokens / SESSION_BUDGET * 100)),
            "session_resets_at": iso_utc(session_resets),
            "tokens_used": round(session_tokens),
            "tokens_limit": SESSION_BUDGET,
        },
        "week": {
            "used_pct": min(100, round(week_tokens / WEEK_BUDGET * 100)),
            "resets_at": iso_utc(week_resets),
            # Bars are relative to the busiest day so the chart stays readable.
            "daily_pct": [round(d / max_day * 100) for d in daily],
        },
        "models": models,
        "thinking": False,  # LED is driven by the claude/thinking topic, not this
    }


def publish(payload: dict) -> None:
    import paho.mqtt.client as mqtt

    cfg = read_config()
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(cfg["MQTT_USER"], cfg["MQTT_PASS"])
    client.tls_set(tls_version=ssl.PROTOCOL_TLS_CLIENT)
    client.connect(cfg["MQTT_HOST"], int(cfg["MQTT_PORT"]), keepalive=30)
    client.loop_start()
    info = client.publish(cfg.get("MQTT_TOPIC_USAGE", "claude/usage"),
                          json.dumps(payload), qos=1, retain=True)
    info.wait_for_publish(timeout=10)
    client.loop_stop()
    client.disconnect()


def main() -> None:
    args = sys.argv[1:]
    if "--dry-run" in args:
        print(json.dumps(build_payload(), indent=2))
        return

    interval = 0
    if "--watch" in args:
        i = args.index("--watch")
        interval = int(args[i + 1]) if i + 1 < len(args) else 60

    while True:
        payload = build_payload()
        publish(payload)
        print(f"{datetime.now():%H:%M:%S} published: session {payload['today']['session_used_pct']}%, "
              f"week {payload['week']['used_pct']}%")
        if not interval:
            break
        time.sleep(interval)


if __name__ == "__main__":
    main()
