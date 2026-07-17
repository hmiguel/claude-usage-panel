# Claude Usage Panel — Firmware

Renders a three-screen usage UI on the Waveshare ESP32-S3-Touch-LCD-4B.
**Primary transport: MQTT push via HiveMQ Cloud** (updates land in <1s);
falls back to polling a GitHub Gist over HTTPS every 30s while MQTT is down.

## Screens

1. **Today** — arc gauge with session %, token counts, reset countdown
2. **Week** — 7-day bar chart + weekly % header
3. **Models** — per-model usage bars (Opus / Sonnet / Haiku)

Swipe to switch screens (nav dots at bottom). The top-right LED lights while
Claude is "thinking" — driven by its own MQTT topic for instant updates.

## Data flow

- `claude/usage` — full usage JSON (schema in `mock_usage.json`), published
  **retained** so the panel paints the latest state immediately on connect.
- `claude/thinking` — `"1"`/`"0"`, drives the LED only.
- Broker + credentials live in `include/config.h`.

Publish from a PC with `scripts/publish_usage.py`:

```
pip install paho-mqtt
python scripts/publish_usage.py usage mock_usage.json   # full update (retained)
python scripts/publish_usage.py thinking 1              # LED on
```

## Build & flash

```
pip install platformio
pio run -t upload        # board on COM3, see platformio.ini
pio device monitor       # serial log at 115200
```

## Hardware notes (learned the hard way)

- **There is no AXP2101 on this board** (earlier docs here claimed otherwise).
  Panel power/reset sequencing goes through a TCA9554-compatible I2C IO
  expander (pins 5/6), driven via Arduino_GFX's `Arduino_XCA9554SWSPI`.
- **Backlight = GPIO4, ACTIVE LOW.** Driving it HIGH turns the screen off —
  that one cost us an afternoon.
- Display: ST7701 480×480 RGB-parallel panel (`Arduino_RGB_Display` +
  `st7701_type1_init_operations`); touch: GT911 via SensorLib.
- LVGL renders into a **shadow buffer** in PSRAM; each loop tick copies it to
  the panel with `draw16bitRGBBitmap()`. Rendering directly into the live
  DMA-scanned framebuffer causes faint stale-cache-line artifacts.
- `GFX Library for Arduino` is pinned to **1.4.9** — newer versions require
  Arduino-ESP32 core 3.x (`esp32-hal-periman.h`), which PlatformIO's
  espressif32 platform doesn't ship.
- LVGL is pinned to **9.2.2**, with `scripts/strip_lvgl_arm_asm.py` removing
  ARM-only NEON/Helium assembly that breaks the Xtensa build.

## Files

- `src/board_init.cpp` — panel/touch/backlight bring-up (vendor demo port)
- `src/ui.cpp` — the whole UI; design tokens at the top
- `src/net.cpp` — WiFi + shared JSON parser + HTTPS fallback fetch
- `src/mqtt_client.cpp` — HiveMQ TLS connection, subscriptions, reconnect
- `src/main.cpp` — glue: boot → MQTT push loop (+ HTTP fallback)
- `include/config.h` — WiFi, broker, credentials, topics. Day-to-day edits.
- `scripts/publish_usage.py` — PC-side publisher

## Troubleshooting

- Black screen but board boots: check backlight polarity (GPIO4 LOW = on).
- Faint line artifacts: something is rendering into the live framebuffer
  again — keep the shadow-buffer + `draw16bitRGBBitmap()` pattern.
- MQTT won't connect: check credentials in `config.h`; serial log prints
  `mqtt: connect failed, state=N` (see PubSubClient state codes).
- Fetch fallback fails but curl works: confirm the gist URL has no revision
  hash and WiFi is 2.4 GHz (no 5 GHz radio on the S3).
