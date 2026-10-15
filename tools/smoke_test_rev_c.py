#!/usr/bin/env python3
"""Minimal Rev C smoke test for Turing/TURZX 8.8\" (480x1920)."""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent / "turing-smart-screen-python"
sys.path.insert(0, str(ROOT))

from PIL import Image, ImageDraw, ImageFont

from library.lcd.lcd_comm import Orientation
from library.lcd.lcd_comm_rev_c import LcdCommRevC

COM_PORT = "AUTO"
WIDTH, HEIGHT = 480, 1920


def main() -> int:
    print("Opening Rev C display (8.8\")...")
    lcd = LcdCommRevC(com_port=COM_PORT, display_width=WIDTH, display_height=HEIGHT)
    lcd.InitializeComm()
    lcd.SetBrightness(level=40)
    lcd.SetOrientation(Orientation.LANDSCAPE)

    img = Image.new("RGB", (lcd.get_width(), lcd.get_height()), (12, 18, 32))
    draw = ImageDraw.Draw(img)
    draw.rectangle((40, 40, lcd.get_width() - 40, lcd.get_height() - 40), outline=(0, 180, 255), width=4)
    draw.text((80, 160), "TURZX 8.8 Rev C", fill=(240, 240, 240))
    draw.text((80, 220), "Mac smoke test OK", fill=(0, 220, 140))
    draw.text((80, 280), f"{lcd.get_width()}x{lcd.get_height()}", fill=(180, 200, 220))

    print(f"Pushing {lcd.get_width()}x{lcd.get_height()} test image...")
    lcd.DisplayPILImage(img)
    print("Smoke test succeeded.")
    lcd.closeSerial()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
