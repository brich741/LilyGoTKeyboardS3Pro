#!/usr/bin/env python3
"""Generate the starter icons into firmware/data/icons/<id>.565.

Format: 128x128, RGB565, big-endian per pixel (what the panel and the web UI use).
Run:  python tools/make_icons.py        (needs Pillow; a TTF font is optional)
Also writes tools/icons_preview.png (contact sheet) for a quick look.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "firmware", "data", "icons")
SIZE = 128
BG = (14, 17, 22)

RED, GREEN, ORANGE, BLUE, WHITE, GREY, CYAN = (
    (255, 69, 58), (52, 199, 89), (255, 159, 10), (64, 156, 255),
    (240, 242, 245), (140, 146, 156), (50, 215, 255))


def font(px, bold=True):
    for name in (["arialbd.ttf", "segoeuib.ttf", "DejaVuSans-Bold.ttf"] if bold else ["arial.ttf", "segoeui.ttf", "DejaVuSans.ttf"]):
        try:
            return ImageFont.truetype(name, px)
        except OSError:
            continue
    return ImageFont.load_default()


def canvas():
    img = Image.new("RGB", (SIZE, SIZE), BG)
    return img, ImageDraw.Draw(img)


def label(d, text, color=GREY):
    f = font(17)
    w = d.textlength(text, font=f)
    d.text(((SIZE - w) / 2, 104), text, font=f, fill=color)


def mic(color, name):
    img, d = canvas()
    d.rounded_rectangle((47, 14, 81, 66), radius=17, fill=color)
    d.arc((33, 38, 95, 88), 0, 180, fill=color, width=6)
    d.line((64, 88, 64, 98), fill=color, width=6)
    d.line((48, 99, 80, 99), fill=color, width=6)
    return img


def check():
    img, d = canvas()
    d.line((30, 66, 54, 90, 98, 36), fill=GREEN, width=13, joint="curve")
    label(d, "Accept")
    return img


def stop():
    img, d = canvas()
    d.rounded_rectangle((34, 22, 94, 82), radius=10, fill=RED)
    label(d, "Stop")
    return img


def clear():
    img, d = canvas()
    d.ellipse((30, 12, 98, 80), outline=ORANGE, width=8)
    d.line((50, 32, 78, 60), fill=ORANGE, width=8)
    d.line((78, 32, 50, 60), fill=ORANGE, width=8)
    label(d, "/clear", ORANGE)
    return img


def esc():
    img, d = canvas()
    f = font(42)
    w = d.textlength("ESC", font=f)
    d.text(((SIZE - w) / 2, 28), "ESC", font=f, fill=RED)
    label(d, "Escape")
    return img


def enter():
    img, d = canvas()
    d.line((96, 22, 96, 62, 36, 62), fill=BLUE, width=10, joint="curve")
    d.polygon([(30, 62), (54, 44), (54, 80)], fill=BLUE)
    label(d, "Enter")
    return img


def play():
    img, d = canvas()
    d.polygon([(44, 18), (44, 88), (100, 53)], fill=GREEN)
    label(d, "Play")
    return img


def vol():
    img, d = canvas()
    d.polygon([(22, 40), (42, 40), (66, 20), (66, 86), (42, 66), (22, 66)], fill=WHITE)
    d.arc((60, 34, 90, 72), -50, 50, fill=WHITE, width=6)
    d.arc((60, 22, 108, 84), -50, 50, fill=WHITE, width=6)
    label(d, "Volume")
    return img


def win():
    img, d = canvas()
    for x, y in ((24, 16), (68, 16), (24, 58), (68, 58)):
        d.rectangle((x, y, x + 36, y + 36), fill=BLUE)
    label(d, "Win")
    return img


def spark():
    import math
    img, d = canvas()
    cx, cy = 64, 52
    pts = []
    for i in range(16):
        r = 42 if i % 2 == 0 else 14
        a = math.pi * i / 8 - math.pi / 2
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    d.polygon(pts, fill=ORANGE)
    label(d, "Claude")
    return img


def remote():
    img, d = canvas()
    d.rounded_rectangle((14, 20, 86, 70), radius=6, outline=CYAN, width=6)
    d.line((40, 82, 60, 82), fill=CYAN, width=6)
    d.rounded_rectangle((84, 38, 112, 90), radius=6, fill=CYAN)
    d.rounded_rectangle((89, 45, 107, 80), radius=2, fill=BG)
    label(d, "/rc", CYAN)
    return img


ICONS = {
    "mic-red": lambda: mic(RED, "Hold"),
    "mic-green": lambda: mic(GREEN, "Live"),
    "check": check,
    "stop": stop,
    "clear": clear,
    "esc": esc,
    "enter": enter,
    "play": play,
    "vol": vol,
    "win": win,
    "spark": spark,
    "remote": remote,
}


def rgb565_be(img):
    out = bytearray()
    for r, g, b in img.convert("RGB").getdata():
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out += bytes((v >> 8, v & 0xFF))
    return bytes(out)


def main():
    os.makedirs(OUT, exist_ok=True)
    sheet = Image.new("RGB", (SIZE * 6, SIZE * 2), BG)
    for n, (name, fn) in enumerate(ICONS.items()):
        img = fn()
        data = rgb565_be(img)
        assert len(data) == SIZE * SIZE * 2
        with open(os.path.join(OUT, name + ".565"), "wb") as f:
            f.write(data)
        sheet.paste(img, ((n % 6) * SIZE, (n // 6) * SIZE))
    sheet.save(os.path.join(ROOT, "tools", "icons_preview.png"))
    print(f"wrote {len(ICONS)} icons to {OUT}")


if __name__ == "__main__":
    sys.exit(main())
