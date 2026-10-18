# TURZX ESP32-S3

PlatformIO firmware for an **ESP32-S3** that drives a [Turing / TURZX 8.8"](https://github.com/mathoudebine/turing-smart-screen-python) **Rev C** panel over USB, serves a control WebUI, speaks MQTT / Home Assistant, and supports Wi‑Fi OTA.

**Repo:** [github.com/AlexWHughes/turzxesp32](https://github.com/AlexWHughes/turzxesp32)

## Features

- TURZX / Turing **Rev C** USB serial protocol (480×1920 · 1920×480, with flip variants)
- Typed widget library (up to 6 slots): MQTT text, MQTT JSON path, world clocks, Open‑Meteo weather, Spotify Now Playing
- Grid or carousel layout, brightness, orientation, and full‑resolution **test pattern** from the WebUI
- Home Assistant MQTT discovery (`turzx/status`)
- HTTP OTA + ArduinoOTA (`turzx-esp32`)
- Captive setup AP when no Wi‑Fi is configured

## Hardware

| Item | Value |
|------|--------|
| MCU | ESP32-S3 with **PSRAM** (recommended: WROOM-1-N16R8 / DevKitC-1) |
| Panel | 8.8" Turing / TURZX |
| Revision | **C** (UART-over-USB) — **not** USB-bulk Rev 1.1 |
| Wake | `1a86:ca88` UsbMonitor / `CT88INCH` |
| Data | `0525:a4a7` Gadget Serial |

### Boards tested

| Board | Result |
|-------|--------|
| [Freenove](https://github.com/Freenove) **ESP32-S3-WROOM** | Works — native USB OTG drives the panel |
| Lonely Binary **ESP32-S3-WROOM01** | Does **not** work for the panel — no voltage on USB OTG (D+/D− only; panel needs its own 5 V, and this board’s OTG path is unsuitable) |

Any ESP32-S3 with PSRAM and a true USB-OTG host port should work. Prefer boards that expose OTG with host-capable wiring (GPIO19/20) and keep the panel on a separate 5 V supply.

## Wiring (no cable swapping)

On **ESP32-S3-DevKitC** the silkscreen has two sockets:

| Silkscreen | Chip behind it | Plug in | Can drive TURZX? |
|------------|----------------|---------|------------------|
| **UART** | CH343 bridge | Mac/PC (flash + serial) | **No** — not USB host |
| **USB** | ESP32-S3 native OTG | TURZX panel **data** | **Yes** — only this port |
| Panel’s other USB-C | — | Wall / PSU | Required — DevKit **USB** rarely supplies 5V |

```text
Mac/PC ──► ESP32  [UART]     flash + logs
Panel  ─► ESP32  [USB]      data only (D+/D−)
PSU    ──► Panel  2nd USB-C  5V power
```

If the panel is on **UART**, the firmware will report `bus=0`. Move it to **USB**, keep power on the panel, press **RESET**.

## Requirements

- [PlatformIO Core](https://platformio.org/install/cli) (`pio`) or the PlatformIO / pioarduino IDE extension
- ESP32-S3 with PSRAM and USB-OTG wiring (GPIO19/20) plus **5V VBUS** for the panel

## Quick start

### 1. Clone

```bash
git clone https://github.com/AlexWHughes/turzxesp32.git
cd turzxesp32
```

### 2. Secrets

`include/secrets.h` is **gitignored** and never leaves your machine.

```bash
cp include/secrets.h.example include/secrets.h
# edit WIFI_SSID / WIFI_PASSWORD (and optional MQTT_* / SPOTIFY_*)
```

**Spotify Now Playing** (optional): create an app in the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard), run an Authorization Code flow once on a computer with scopes `user-read-currently-playing` / `user-read-playback-state`, then put `SPOTIFY_CLIENT_ID`, `SPOTIFY_CLIENT_SECRET`, and `SPOTIFY_REFRESH_TOKEN` in `secrets.h`.

Values apply only when no SSID is stored in flash yet. The WebUI can override Wi‑Fi/MQTT later.

If you skip secrets entirely, first boot opens AP **`Turzx-Setup`** / password **`turzxsetup`** at `http://192.168.4.1/`.

### 3. Build & flash

Default env `esp32-s3` logs and flashes on **USB-UART**; OTG stays free for the panel.

```bash
# Close any serial monitor first
python3 -m platformio run -t upload
python3 -m platformio device monitor
```

| Symptom | Fix |
|---------|-----|
| UART: stub RAM timeout (`0107`) | Default env uses `--no-stub` |
| Erase OK then `0105` on write | Hold **BOOT** for the **entire** upload (CH343 auto-reset). Baud is 115200. |
| Still fails | Hold BOOT → tap RESET → keep holding BOOT → `pio run -t upload` → release after SUCCESS |
| UART won’t flash | One-time OTG flash: `pio run -e esp32-s3-otg -t upload --upload-port /dev/cu.usbmodem*` then use OTA |
| Already on Wi‑Fi | `pio run -e esp32-s3-ota -t upload --upload-port <ip>` |

Expect on UART monitor: `[turzx] STA IP …`, then USB host / panel diag. WebUI is embedded in firmware (no `uploadfs` required).

### 4. Configure

Open `http://<device-ip>/`:

- **Display** — brightness, orientation (portrait / landscape / flipped), grid vs carousel, test pattern
- **Widgets** — slot type + fields (MQTT topic / JSON path / UTC offset); weather uses lat/lon/timezone
- **Connections** — Wi‑Fi / MQTT (collapsed)

Test pattern draws full‑width colour bars, a 1px border, corner marks, and centred `WxH` so you can confirm the panel is showing the full frame.

- HTTP OTA: `http://<device-ip>/update`
- ArduinoOTA hostname: `turzx-esp32`

## Widget types

| Type | Source |
|------|--------|
| MQTT text | Raw MQTT payload |
| MQTT JSON | MQTT payload + simple path (`main.temp`, `item.0.name`) |
| World clock | NTP + per-slot UTC offset (`UTC+10`, `UTC-5:30`) |
| Weather | [Open‑Meteo](https://open-meteo.com/) using WebUI lat/lon/timezone (no API key) |
| Spotify | Spotify Web API Currently Playing (tokens in `secrets.h` only) |

## PlatformIO environments

| Env | Use |
|-----|-----|
| `esp32-s3` (default) | Flash + monitor via CH343 UART; USB-OTG free for the panel |
| `esp32-s3-otg` | One-time flash when UART upload fails (OTG Serial/JTAG) |
| `esp32-s3-ota` | Wireless upload once the board is on Wi‑Fi |

## Project layout

```text
include/       headers + secrets.h.example
src/           firmware (Rev C driver, USB/UART transport, dashboard, widgets, MQTT, WebUI, OTA)
lib/           vendored usb_host_cdc_acm (build sources only)
third_party/   ESP-IDF USB host private headers (patched enum support)
tools/         optional Mac panel smoke test
```

## Mac panel smoke test (optional)

Verify the panel directly from a Mac before flashing firmware — see [tools/smoke_test.md](tools/smoke_test.md). Clone the upstream Python project into `tools/` only when you need that test; it is gitignored and must not be a nested git repo inside this project.

## Protocol reference

Ported from [mathoudebine/turing-smart-screen-python](https://github.com/mathoudebine/turing-smart-screen-python) `library/lcd/lcd_comm_rev_c.py`.

## License

Firmware in this repo is provided as-is for personal / educational use. Vendored components retain their upstream licenses (see `lib/usb_host_cdc_acm/LICENSE`).
