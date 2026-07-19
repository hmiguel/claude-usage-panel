#!/usr/bin/env python3
"""Compile real Claude Code usage from local transcripts and publish to the panel.

Reads ~/.claude/projects/**/*.jsonl (written by Claude Code itself), computes
session/week/per-model usage, and publishes the panel's usage JSON to MQTT.

No credentials or network calls beyond the MQTT broker (config from
scripts/.env ??? copy .env.example). Limits are estimated against configurable
budgets ??? calibrate SESSION_BUDGET / WEEK_BUDGET below by comparing with
Claude Code's /usage screen.

Publishes are skipped when nothing meaningful changed since the last one
(compared via a cache file in the system temp dir) ??? pass --force to
publish regardless.

Usage:
  python collect_usage.py --dry-run     # print JSON, don't publish
  python collect_usage.py               # collect + publish once (retained)
  python collect_usage.py --watch 60    # republish every 60s
"""

import json
import os
import ssl
import sys
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
from datetime import datetime, timedelta, timezone
from pathlib import Path

from publish_usage import read_config  # shared .env config loader

# --- Tunables -----------------------------------------------------------
# Weighted tokens: cache reads are far cheaper than fresh tokens, so weight
# them down instead of counting raw.
CACHE_READ_WEIGHT = 0.1
# Fallback budgets in weighted tokens, used only when the OAuth usage API is
# unavailable. The API returns real utilization percentages and reset times.
DEFAULT_SESSION_BUDGET = 9_000_000    # per 5h session window
DEFAULT_WEEK_BUDGET = 100_000_000     # per rolling 7-day window
SESSION_HOURS = 5
# -----------------------------------------------------------------------

PROJECTS_DIR = Path.home() / ".claude" / "projects"
ENV_FILE = Path(__file__).with_name(".env")
CLAUDE_CONFIG_DIR = Path(os.environ.get("CLAUDE_CONFIG_DIR", "")) if os.environ.get("CLAUDE_CONFIG_DIR") else Path.home() / ".claude"
CLAUDE_CREDENTIALS = CLAUDE_CONFIG_DIR / ".credentials.json"
API_URL_USAGE = "https://api.anthropic.com/api/oauth/usage"
OAUTH_BETA = "oauth-2025-04-20"
USER_AGENT = "claude-code/2.1.85"


def read_local_env() -> dict[str, str]:
    values = {}
    if not ENV_FILE.exists():
        return values
    for line in ENV_FILE.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip("\"'")
    return values


def configured_budget(name: str, default: int) -> int:
    env = read_local_env()
    raw = os.environ.get(name) or env.get(name)
    if raw is None:
        return default
    try:
        value = int(raw.replace("_", ""))
    except ValueError:
        return default
    return value if value > 0 else default



def read_access_token() -> str | None:
    try:
        creds = json.loads(CLAUDE_CREDENTIALS.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return creds.get("claudeAiOauth", {}).get("accessToken") or None


# The OAuth usage endpoint isn't reliable under frequent polling (seen
# failing several calls out of six, ~4s apart) — cache the last successful
# reading and reuse it on failure instead of silently falling back to the
# local budget estimate, which uses an unrelated accounting scale and made
# published percentages alternate between two unrelated series.
API_CACHE_FILE = Path(tempfile.gettempdir()) / "claude-usage-panel.api.json"
API_CACHE_MAX_AGE = timedelta(minutes=30)


def _load_cached_api_usage() -> dict | None:
    try:
        cached = json.loads(API_CACHE_FILE.read_text(encoding="utf-8"))
        fetched_at = datetime.fromisoformat(cached["fetched_at"])
    except (OSError, json.JSONDecodeError, KeyError, ValueError):
        return None
    if datetime.now(timezone.utc) - fetched_at > API_CACHE_MAX_AGE:
        return None
    return cached["usage"]


def _save_cached_api_usage(usage: dict) -> None:
    payload = {"fetched_at": datetime.now(timezone.utc).isoformat(), "usage": usage}
    try:
        API_CACHE_FILE.write_text(json.dumps(payload), encoding="utf-8")
    except OSError:
        pass


def fetch_oauth_usage() -> dict | None:
    token = read_access_token()
    if not token:
        return None
    req = Request(
        API_URL_USAGE,
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
            "User-Agent": USER_AGENT,
            "anthropic-beta": OAUTH_BETA,
        },
    )
    try:
        with urlopen(req, timeout=10) as resp:
            usage = json.loads(resp.read().decode("utf-8"))
    except (HTTPError, URLError, TimeoutError, OSError, json.JSONDecodeError) as exc:
        cached = _load_cached_api_usage()
        if cached is not None:
            print(f"  (usage API fetch failed: {exc}; reusing last known reading)")
            return cached
        print(f"  (usage API fetch failed: {exc}; no cached reading, using local estimate)")
        return None
    _save_cached_api_usage(usage)
    return usage


def parse_api_datetime(value: str | None) -> datetime | None:
    if not value:
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


def usage_window(usage: dict | None, key: str) -> dict | None:
    if not usage:
        return None
    window = usage.get(key)
    return window if isinstance(window, dict) else None


def usage_pct(window: dict | None) -> int | None:
    if not window:
        return None
    value = window.get("utilization")
    if value is None:
        return None
    try:
        return max(0, min(100, round(float(value))))
    except (TypeError, ValueError):
        return None


def usage_reset(window: dict | None) -> datetime | None:
    if not window:
        return None
    return parse_api_datetime(window.get("resets_at"))


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
    best: dict = {}  # (message.id, requestId) -> (ts, fam, tokens)
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
                    try:
                        ts = datetime.fromisoformat(ts_str.replace("Z", "+00:00"))
                    except ValueError:
                        continue
                    tok = weighted_tokens(usage)
                    # A message can span several JSONL lines carrying
                    # progressively-updated usage ??? keep the largest snapshot
                    # per (message, request), not the first.
                    key = (msg.get("id"), d.get("requestId"))
                    prev = best.get(key)
                    if prev is None or tok > prev[2]:
                        best[key] = (ts, fam, tok)
        except OSError:
            continue
    return sorted(best.values(), key=lambda e: e[0])


def current_session(entries, now: datetime):
    """5h session windows anchored at the exact first message after the
    previous window expires (matches Claude's own /usage reset times).
    Returns (tokens, resets_at) for the window containing `now`."""
    block_start = None
    block_tokens = 0.0
    for ts, _fam, tok in entries:
        if block_start is None or ts >= block_start + timedelta(hours=SESSION_HOURS):
            block_start = ts
            block_tokens = 0.0
        block_tokens += tok
    if block_start is not None and now < block_start + timedelta(hours=SESSION_HOURS):
        return block_tokens, block_start + timedelta(hours=SESSION_HOURS)
    return 0.0, now + timedelta(hours=SESSION_HOURS)


def build_payload() -> dict:
    now = datetime.now(timezone.utc)
    entries = load_entries()
    real_usage = fetch_oauth_usage()

    session_tokens, session_resets = current_session(entries, now)
    session_budget = configured_budget("SESSION_BUDGET", DEFAULT_SESSION_BUDGET)
    week_budget = configured_budget("WEEK_BUDGET", DEFAULT_WEEK_BUDGET)

    # Weekly %: rolling last-7-days window (Claude's weekly limit is a
    # rolling window anchored per-account, e.g. "resets Tue 11:59" ??? not a
    # calendar week). The Mon-Sun bars stay calendar-based as a visual.
    local_now = now.astimezone()
    monday = (local_now - timedelta(days=local_now.weekday())).replace(
        hour=0, minute=0, second=0, microsecond=0
    )
    week_resets = monday + timedelta(days=7)
    rolling_start = now - timedelta(days=7)

    daily = [0.0] * 7
    week_tokens = 0.0
    fam_tokens: dict[str, float] = {}
    for ts, fam, tok in entries:
        lts = ts.astimezone()
        if lts >= monday:
            daily[lts.weekday()] += tok
        if ts >= rolling_start:
            week_tokens += tok
            fam_tokens[fam] = fam_tokens.get(fam, 0) + tok

    max_day = max(daily) or 1.0

    models = [
        {"name": fam, "pct": round(tok / week_tokens * 100) if week_tokens else 0}
        for fam, tok in sorted(fam_tokens.items(), key=lambda kv: -kv[1])
    ][:3]

    def iso_utc(dt: datetime) -> str:
        return dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    five_hour = usage_window(real_usage, "five_hour")
    seven_day = usage_window(real_usage, "seven_day")
    session_pct = usage_pct(five_hour)
    week_pct = usage_pct(seven_day)
    session_reset = usage_reset(five_hour) or session_resets
    week_reset = usage_reset(seven_day) or week_resets
    if session_pct is None:
        session_pct = min(100, round(session_tokens / session_budget * 100))
    if week_pct is None:
        week_pct = min(100, round(week_tokens / week_budget * 100))

    return {
        "updated_at": iso_utc(now),
        "today": {
            "session_used_pct": session_pct,
            "session_resets_at": iso_utc(session_reset),
        },
        "week": {
            "used_pct": week_pct,
            "resets_at": iso_utc(week_reset),
            # Bars are relative to the busiest day so the chart stays readable.
            "daily_pct": [round(d / max_day * 100) for d in daily],
        },
        "models": models,
        "thinking": False,  # LED is driven by the claude/thinking topic, not this
    }


CACHE_FILE = Path(tempfile.gettempdir()) / "claude-usage-panel.last.json"


def comparison_key(payload: dict) -> str:
    """Canonical form of the payload with volatile fields removed, so
    'nothing actually changed' runs can skip the publish. updated_at is
    always fresh, and an idle session's resets_at drifts with the clock."""
    p = json.loads(json.dumps(payload))
    p.pop("updated_at", None)
    if p.get("today", {}).get("session_used_pct", 0) == 0:
        p["today"]["session_resets_at"] = "idle"
    return json.dumps(p, sort_keys=True)


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

    force = "--force" in args

    while True:
        payload = build_payload()
        key = comparison_key(payload)
        prev = CACHE_FILE.read_text(encoding="utf-8") if CACHE_FILE.exists() else None
        if key == prev and not force:
            print(f"{datetime.now():%H:%M:%S} unchanged, skipped")
        else:
            publish(payload)
            CACHE_FILE.write_text(key, encoding="utf-8")
            print(f"{datetime.now():%H:%M:%S} published: session {payload['today']['session_used_pct']}%, "
                  f"week {payload['week']['used_pct']}%")
        force = False  # --force applies to the first iteration only
        if not interval:
            break
        time.sleep(interval)


if __name__ == "__main__":
    main()




