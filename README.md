# TURZX ESP32-S3

PlatformIO firmware for an **ESP32-S3** that drives a [Turing / TURZX 8.8"](https://github.com/mathoudebine/turing-smart-screen-python) **Rev C** panel over USB, serves a config WebUI, speaks MQTT / Home Assistant, and supports Wi‑Fi OTA.

**Repo:** [github.com/AlexWHughes/turzxesp32](https://github.com/AlexWHughes/turzxesp32)

## Features

- TURZX / Turing **Rev C** USB serial protocol (480×1920 portrait · 1920×480 landscape)
- Live dashboard with up to **6 MQTT widgets**
- Embedded WebUI for Wi‑Fi, MQTT, brightness, orientation, and widgets
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

### 2. Secrets (Wi‑Fi / MQTT defaults)

`include/secrets.h` is **gitignored** and never leaves your machine.

```bash
cp include/secrets.h.example include/secrets.h
# edit WIFI_SSID / WIFI_PASSWORD (and optional MQTT_*)
```

Values apply only when no SSID is stored in flash yet. The WebUI can override them later.

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

Open `http://<device-ip>/` for MQTT widgets, brightness, and orientation.

- HTTP OTA: `http://<device-ip>/update`
- ArduinoOTA hostname: `turzx-esp32`

## PlatformIO environments

| Env | Use |
|-----|-----|
| `esp32-s3` (default) | Flash + monitor via CH343 UART; USB-OTG free for the panel |
| `esp32-s3-otg` | One-time flash when UART upload fails (OTG Serial/JTAG) |
| `esp32-s3-ota` | Wireless upload once the board is on Wi‑Fi |

## Project layout

```text
include/       headers + secrets.h.example
src/           firmware (Rev C driver, USB/UART transport, dashboard, MQTT, WebUI, OTA)
data/          LittleFS WebUI reference copy
lib/           vendored usb_host_cdc_acm
third_party/   ESP-IDF USB host private headers (patched enum support)
tools/         Mac panel smoke test script + notes
```

## Mac panel smoke test (optional)

Verify the panel directly from a Mac before flashing firmware — see [tools/smoke_test.md](tools/smoke_test.md).

## Protocol reference

Ported from [mathoudebine/turing-smart-screen-python](https://github.com/mathoudebine/turing-smart-screen-python) `library/lcd/lcd_comm_rev_c.py`.

## License

Firmware in this repo is provided as-is for personal / educational use. Vendored components retain their upstream licenses (see `lib/usb_host_cdc_acm/LICENSE`).
