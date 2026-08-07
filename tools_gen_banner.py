#!/usr/bin/env python3
"""Render the Meridian GitHub banner and social-preview card.

The motif is the product in one picture: a constellation of carrier bars, ragged
on the left where the sky is real and collapsing into a flat wall on the right
where a transmitter has taken over. That single silhouette is the whole
detection story, and it is the same geometry views/sky_view.c draws on the
device - so the artwork is the product rather than a picture of it.

Type is auto-fitted to its column rather than hand-tuned, so a wording change
can never quietly overflow into the artwork. Supersampled, then
LANCZOS-downsampled.
"""
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops
import math
import os

OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
BLACK_F = "/System/Library/Fonts/Supplemental/Arial Black.ttf"
MONO = "/System/Library/Fonts/Supplemental/Andale Mono.ttf"
REG = "/System/Library/Fonts/Supplemental/Arial.ttf"

# palette - a night sky, and the two colours of the story
BG_TOP = (6, 9, 16)
BG_BOT = (14, 12, 20)
SKY = (86, 196, 255)  # the honest constellation: cold, high, real
FAKE = (255, 96, 72)  # the transmitter: hot, close, wrong
GOLD = (255, 186, 62)  # the meridian itself
WHITE = (246, 248, 252)
GRAY = (140, 150, 166)
DIM = (38, 46, 60)

SS = 2  # supersample

# The satellite table from helpers/mrd_sim.c, and its two carrier models.
SIM_SKY = [
    (2, 312, 68, -4), (5, 44, 41, 9), (6, 128, 12, 11), (9, 201, 55, -7),
    (12, 75, 23, 12), (17, 249, 34, 8), (19, 156, 77, -3), (23, 18, 8, 13),
    (24, 288, 19, -10), (28, 95, 47, 6), (31, 340, 29, -9),
]
NOISE_CLEAN = [1, -2, 0, 2, -1, 1, -1, 0, 2, -2, 1]
NOISE_SPOOF = [3, -4, 1, 2, -3, 0, 4, -1, 2, -2, 3]


def clean_snr(i):
    elev = SIM_SKY[i][2]
    return 29 + (elev * 22) // 100 + NOISE_CLEAN[i]


def spoof_snr(i):
    elev = SIM_SKY[i][2]
    return (522 - elev // 4 + NOISE_SPOOF[i]) // 10


# ---------------- helpers ----------------


def add(a, b):
    """Additive compositing - light stacks, it does not replace."""
    return ImageChops.add(a, b)


def font(path, px):
    try:
        return ImageFont.truetype(path, int(px))
    except OSError:
        return ImageFont.truetype(BOLD, int(px))


def fit(path, px, text, max_w):
    """Largest size at or under `px` whose `text` fits `max_w`.

    The banner is regenerated whenever the pitch is reworded, and a line that
    silently runs into the artwork is the classic way a generated banner rots.
    """
    size = int(px)
    while size > 8:
        f = font(path, size)
        if f.getbbox(text)[2] <= max_w:
            return f
        size -= 2
    return font(path, 8)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def vgradient(w, h):
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=lerp(BG_TOP, BG_BOT, y / max(1, h - 1)))
    return img


def starfield(w, h, n=140):
    """A faint sky behind everything. Deterministic, so the banner is stable."""
    layer = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(layer)
    seed = 20260806
    for _ in range(n):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        x = (seed >> 8) % w
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        y = (seed >> 8) % h
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        v = 20 + (seed >> 12) % 60
        d.point((x, y), fill=(v, v, int(v * 1.3)))
    return layer


def glow(layer, radius, strength=1.0):
    g = layer.filter(ImageFilter.GaussianBlur(radius))
    if strength != 1.0:
        g = ImageChops.multiply(
            g, Image.new("RGB", g.size, tuple([int(255 * strength)] * 3))
        )
    return g


# ---------------- the motif ----------------


def draw_carrier_wall(w, h, bar_w, gap, base_y, top_y):
    """Eleven honest bars, then eleven flat ones. The picture is the detection.

    Left half is the real sky's C/N0 profile: high satellites loud, low ones
    weak, several decibels of spread. Right half is what one transmitter feeding
    every channel produces. The line between them is where the takeover happens.
    """
    layer = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(layer)
    span = base_y - top_y

    n = len(SIM_SKY)
    total = (2 * n) * (bar_w + gap) - gap
    x = (w - total) // 2

    for i in range(n):
        v = clean_snr(i)
        bh = int(span * v / 55)
        d.rectangle([x, base_y - bh, x + bar_w, base_y], fill=SKY)
        x += bar_w + gap

    split_x = x - gap // 2

    for i in range(n):
        v = spoof_snr(i)
        bh = int(span * v / 55)
        d.rectangle([x, base_y - bh, x + bar_w, base_y], fill=FAKE)
        x += bar_w + gap

    # The baseline, and the moment of takeover.
    d.rectangle([0, base_y, w, base_y + max(1, bar_w // 6)], fill=DIM)
    for y in range(top_y - span // 6, base_y, max(2, bar_w // 2)):
        d.rectangle([split_x, y, split_x + max(1, bar_w // 8), y + bar_w // 3], fill=GOLD)

    return layer, split_x


def draw_globe(d, cx, cy, r, color, width):
    """The mark: a globe cut by its meridian, as helpers/mrd_ui.c draws it."""
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=color, width=width)
    for k in (-1, 1):
        dy = (r * k) // 2
        half = int(math.sqrt(max(0, r * r - dy * dy)))
        d.line([cx - half, cy + dy, cx + half, cy + dy], fill=color, width=width)
    # two meridian ellipses, and the fixed vertical one
    for f in (0.42, 0.78):
        d.ellipse(
            [cx - int(r * f), cy - r, cx + int(r * f), cy + r],
            outline=color,
            width=width,
        )
    d.line([cx, cy - r, cx, cy + r], fill=GOLD, width=width)


# ---------------- banner ----------------


def banner(w=1280, h=440):
    W, H = w * SS, h * SS
    img = vgradient(W, H)
    img = add(img, starfield(W, H, 200))

    bar_w, gap = 30 * SS, 10 * SS
    base_y, top_y = int(H * 0.78), int(H * 0.35)
    bars, split_x = draw_carrier_wall(W, H, bar_w, gap, base_y, top_y)
    img = add(img, glow(bars, 14 * SS, 0.75))
    img = add(img, bars)

    d = ImageDraw.Draw(img)

    pad = 52 * SS
    col_w = W - 2 * pad

    f_title = fit(BLACK_F, 92 * SS, "MERIDIAN", col_w)
    f_tag = fit(BOLD, 30 * SS, "Is anything lying to you about where you are?", col_w)
    f_sub = fit(MONO, 19 * SS, "GPS SPOOFING DETECTOR  .  FLIPPER ZERO", col_w)

    # Title block sits above the bars, centred.
    ty = int(H * 0.13)
    d.text((W // 2, ty), "MERIDIAN", font=f_title, fill=WHITE, anchor="mm")
    d.text(
        (W // 2, ty + int(58 * SS)),
        "Is anything lying to you about where you are?",
        font=f_tag,
        fill=GOLD,
        anchor="mm",
    )

    # Legend under the two halves, naming what the shape means.
    f_leg = font(MONO, 17 * SS)
    d.text(
        (split_x - int(150 * SS), base_y + int(24 * SS)),
        "a real sky",
        font=f_leg,
        fill=SKY,
        anchor="mm",
    )
    d.text(
        (split_x + int(150 * SS), base_y + int(24 * SS)),
        "a transmitter",
        font=f_leg,
        fill=FAKE,
        anchor="mm",
    )
    d.text(
        (W // 2, H - int(26 * SS)),
        "GPS SPOOFING DETECTOR  .  FLIPPER ZERO",
        font=f_sub,
        fill=GRAY,
        anchor="mm",
    )

    out = img.resize((w, h), Image.LANCZOS)
    path = os.path.join(OUT, "banner.png")
    out.save(path)
    print(f"wrote {path}")


# ---------------- social preview ----------------


def social(w=1280, h=640):
    """GitHub's card. Cropped hard on small screens, so everything that matters
    stays near the middle."""
    W, H = w * SS, h * SS
    img = vgradient(W, H)
    img = add(img, starfield(W, H, 240))

    # The globe, large and to the left.
    mark = Image.new("RGB", (W, H), (0, 0, 0))
    md = ImageDraw.Draw(mark)
    cx, cy, r = int(W * 0.22), int(H * 0.52), int(H * 0.27)
    draw_globe(md, cx, cy, r, SKY, 3 * SS)
    img = add(img, glow(mark, 10 * SS, 0.8))
    img = add(img, mark)

    # A compact version of the wall on the right.
    bars = Image.new("RGB", (W, H), (0, 0, 0))
    bd = ImageDraw.Draw(bars)
    bar_w, gap = 13 * SS, 6 * SS
    base_y, top_y = int(H * 0.70), int(H * 0.34)
    span = base_y - top_y
    n = len(SIM_SKY)
    x = int(W * 0.45)
    for i in range(n):
        bh = int(span * clean_snr(i) / 55)
        bd.rectangle([x, base_y - bh, x + bar_w, base_y], fill=SKY)
        x += bar_w + gap
    split_x = x - gap // 2
    for i in range(n):
        bh = int(span * spoof_snr(i) / 55)
        bd.rectangle([x, base_y - bh, x + bar_w, base_y], fill=FAKE)
        x += bar_w + gap
    bd.rectangle([int(W * 0.44), base_y, x, base_y + 2 * SS], fill=DIM)
    for y in range(top_y, base_y, 8 * SS):
        bd.rectangle([split_x, y, split_x + 2 * SS, y + 4 * SS], fill=GOLD)
    img = add(img, glow(bars, 12 * SS, 0.7))
    img = add(img, bars)

    d = ImageDraw.Draw(img)
    pad = 60 * SS
    col_w = W - 2 * pad

    f_title = fit(BLACK_F, 104 * SS, "MERIDIAN", col_w)
    f_tag = fit(BOLD, 34 * SS, "GPS spoofing detector for Flipper Zero", col_w)
    f_sub = fit(MONO, 22 * SS, "11 checks  .  4 independent paths  .  receive only", col_w)

    d.text((W // 2, int(H * 0.16)), "MERIDIAN", font=f_title, fill=WHITE, anchor="mm")
    d.text(
        (W // 2, int(H * 0.16) + int(64 * SS)),
        "GPS spoofing detector for Flipper Zero",
        font=f_tag,
        fill=GOLD,
        anchor="mm",
    )
    d.text(
        (W // 2, H - int(52 * SS)),
        "11 checks  .  4 independent paths  .  receive only",
        font=f_sub,
        fill=GRAY,
        anchor="mm",
    )

    out = img.resize((w, h), Image.LANCZOS)
    path = os.path.join(OUT, "social-preview.png")
    out.save(path)
    print(f"wrote {path}")


if __name__ == "__main__":
    banner()
    social()
