# Claude Usage Panel — Firmware

A three-screen Claude usage dashboard on a Waveshare ESP32-S3-Touch-LCD-4B.
Real usage data is collected locally from Claude Code's own transcript files
and pushed to the panel over MQTT — no API keys, no cloud service, no
credentials leave your machine except the MQTT broker of your choice.

## Screens

1. **Session** — arc gauge with the current 5h session %, token counts, reset countdown
2. **Week** — 7-day bar chart + rolling weekly % header
3. **Models** — per-model usage split (Opus / Sonnet / Haiku / Fable, whichever you used)

Swipe to switch screens (nav dots at bottom). Top-right: a LED that lights
while Claude is "thinking" (its own MQTT topic, for instant updates) and a
WiFi status indicator. Top-left: a 24h clock. The backlight dims after 2
minutes idle and turns off after 15 — any touch wakes it instantly.

## How it works

```
~/.claude/projects/**/*.jsonl  →  collect_usage.py  →  MQTT (HiveMQ Cloud)  →  ESP32 panel
   (Claude Code's own logs)       (runs on your PC)      claude/usage (retained)
                                                          claude/thinking
```

- `scripts/collect_usage.py` parses Claude Code's local transcript files,
  computes session/week/per-model usage, and publishes it retained so the
  panel repaints instantly on connect. Run once, or `--watch 60` to keep it
  live. It skips publishing when nothing changed.
- The session/week **percentages are estimates** against configurable budget
  constants — calibrate them against Claude Code's `/usage` screen (see
  comments at the top of the script).
- If MQTT is unreachable, the firmware falls back to polling a URL
  (`DATA_URL` in config — e.g. a GitHub Gist serving the same JSON schema)
  over HTTPS every 30s.

## Getting started

**1. Hardware:** [Waveshare ESP32-S3-Touch-LCD-4B](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B), USB-C cable.

**2. An MQTT broker.** [HiveMQ Cloud](https://www.hivemq.com/mqtt-cloud-broker/)
has a free tier — create a cluster, then add credentials under *Access
Management*.

**3. Configure the firmware:**

```
cp include/config.example.h include/config.h
```

Edit `include/config.h`: WiFi SSID/password (2.4 GHz only — the S3 has no 5
GHz radio) and your broker host/port/username/password.

**4. Build and flash:**

```
pip install platformio
pio run -t upload      # auto-detects the port; see platformio.ini to override
pio device monitor     # serial log at 115200
```

**5. Configure and run the usage collector** (on the PC where you use Claude Code):

```
cd scripts
pip install paho-mqtt
cp .env.example .env   # fill in the same broker details as config.h
python collect_usage.py --dry-run   # sanity-check the numbers first
python collect_usage.py --watch 60  # then run it for real
```

Compare the panel against Claude Code's `/usage` screen and adjust
`SESSION_BUDGET` / `WEEK_BUDGET` at the top of `collect_usage.py` if the
percentages don't match your plan.

## Files

- `src/board_init.cpp` — panel/touch/backlight bring-up (ported from the vendor demo)
- `src/ui.cpp` — the whole UI; design tokens at the top
- `src/net.cpp` — WiFi + shared JSON parser + HTTPS fallback fetch
- `src/mqtt_client.cpp` — HiveMQ TLS connection, subscriptions, reconnect
- `src/main.cpp` — glue: boot → MQTT push loop (+ HTTP fallback)
- `include/config.h` — WiFi, broker, topics, timezone, backlight timing (gitignored — copy from `config.example.h`)
- `scripts/collect_usage.py` — reads local Claude Code transcripts, publishes real usage
- `scripts/publish_usage.py` — low-level manual publisher (`usage <file.json>` / `thinking 1|0`)
- `scripts/.env` — broker credentials for the scripts above (gitignored — copy from `.env.example`)

## Hardware notes (learned the hard way)

- **There is no AXP2101 on this board** (an early assumption here was wrong).
  Panel power/reset sequencing goes through a TCA9554-compatible I2C IO
  expander (pins 5/6), driven via Arduino_GFX's `Arduino_XCA9554SWSPI`.
- **Backlight = GPIO4, ACTIVE LOW.** Driving it HIGH turns the screen off.
  Firmware drives it via LEDC PWM for the idle-dimming feature.
- Display: ST7701 480×480 RGB-parallel panel (`Arduino_RGB_Display` +
  `st7701_type1_init_operations`); touch: GT911 via SensorLib.
- LVGL renders into a **shadow buffer** in PSRAM; each loop tick copies it to
  the panel with `draw16bitRGBBitmap()`. Rendering directly into the live
  DMA-scanned framebuffer causes faint stale-cache-line artifacts.
- `GFX Library for Arduino` is pinned to **1.4.9** — newer versions require
  Arduino-ESP32 core 3.x (`esp32-hal-periman.h`), which PlatformIO's
  espressif32 platform doesn't ship as of writing.
- LVGL is pinned to **9.2.2**, with `scripts/strip_lvgl_arm_asm.py` (a
  PlatformIO pre-build hook) removing ARM-only NEON/Helium assembly that
  breaks the Xtensa build.

## Troubleshooting

- **Black screen but board boots:** check backlight polarity (GPIO4 LOW = on).
- **Faint line artifacts on screen:** something is rendering into the live
  framebuffer again — keep the shadow-buffer + `draw16bitRGBBitmap()` pattern.
- **MQTT won't connect:** check `scripts/.env` / `include/config.h`
  credentials match your broker; firmware serial log prints
  `mqtt: connect failed, state=N` (see PubSubClient state codes).
- **Panel numbers don't match `/usage`:** recalibrate `SESSION_BUDGET` /
  `WEEK_BUDGET` in `collect_usage.py`.
- **HTTP fallback fails but curl works:** confirm `DATA_URL` has no revision
  hash (for a Gist) and WiFi is 2.4 GHz.

## License

MIT — see [LICENSE](LICENSE).
