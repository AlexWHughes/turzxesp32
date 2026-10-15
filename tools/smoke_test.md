# Mac smoke test — TURZX 8.8" Rev C

## Confirmed device fingerprint

- `UsbMonitor` `1a86:ca88` serial `CT88INCH` (wake)
- `Gadget Serial` `0525:a4a7` (data)
- Protocol: **Revision C**, 480×1920 portrait

## Run

```bash
cd tools
python3 -m venv turing-smart-screen-python/.venv
source turing-smart-screen-python/.venv/bin/activate
pip install pyserial pillow numpy pyyaml
# clone once:
# git clone --depth 1 https://github.com/mathoudebine/turing-smart-screen-python.git turing-smart-screen-python
python smoke_test_rev_c.py
```

Expected log snippet:

```text
Display ID returned: chs_88inch.dev1_rom1.90
HW sub-revision detected: SubRevision.REV_8INCH
Smoke test succeeded.
```

## Result (2026-10-15)

Smoke test **passed** on this Mac against the connected 8.8" panel.
