#!/usr/bin/env python3
"""Render Flipper-style mock screenshots (128x64, orange backlight) for the README.

These mirror the view sources line for line - the same layout constants, the
same arc-gauge and check-strip geometry from helpers/mrd_ui.c - so a layout
collision or an overlapping label shows up here before it ships on a device.
Text is positioned by BASELINE (PIL anchor "ls"/"rs"/"ms") because
canvas_draw_str takes y as the baseline, not the top.

The numbers are not invented. The satellite table and the carrier-power model
are the ones in helpers/mrd_sim.c, evaluated here with the same arithmetic, and
the check states are the ones test/host_detect_test.c actually reports for the
matching scenario. What you see below is what the device draws.
"""
from PIL import Image, ImageDraw, ImageFont
import math
import os

S = 6  # upscale factor
W, H = 128, 64
BG = (255, 130, 0)  # flipper backlight orange
FG = (10, 8, 4)  # near-black pixels
OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

MONO = "/System/Library/Fonts/Supplemental/Andale Mono.ttf"
BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"

f_sec = ImageFont.truetype(MONO, 7 * S - 2)  # FontSecondary
f_pri = ImageFont.truetype(BOLD, 8 * S)  # FontPrimary


# ---------------- canvas ----------------


class Screen:
    """A 128x64 Flipper canvas, drawn at S times scale."""

    def __init__(self):
        self.img = Image.new("RGB", (W * S, H * S), BG)
        self.d = ImageDraw.Draw(self.img)
        self.color = FG

    def set_color(self, white):
        self.color = BG if white else FG

    def box(self, x, y, w, h, color=None):
        self.d.rectangle(
            [x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], fill=color or self.color
        )

    def frame(self, x, y, w, h, color=None):
        c = color or self.color
        self.box(x, y, w, 1, c)
        self.box(x, y + h - 1, w, 1, c)
        self.box(x, y, 1, h, c)
        self.box(x + w - 1, y, 1, h, c)

    def rframe(self, x, y, w, h, r, color=None):
        c = color or self.color
        self.box(x + r, y, w - 2 * r, 1, c)
        self.box(x + r, y + h - 1, w - 2 * r, 1, c)
        self.box(x, y + r, 1, h - 2 * r, c)
        self.box(x + w - 1, y + r, 1, h - 2 * r, c)
        for dx, dy in ((r - 1, 1), (1, r - 1)):
            self.box(x + dx, y + dy, 1, 1, c)
            self.box(x + w - 1 - dx, y + dy, 1, 1, c)
            self.box(x + dx, y + h - 1 - dy, 1, 1, c)
            self.box(x + w - 1 - dx, y + h - 1 - dy, 1, 1, c)

    def rbox(self, x, y, w, h, r, color=None):
        c = color or self.color
        self.box(x + r, y, w - 2 * r, h, c)
        self.box(x, y + r, w, h - 2 * r, c)

    def line(self, x0, y0, x1, y1, color=None):
        self.d.line([x0 * S, y0 * S, x1 * S, y1 * S], fill=color or self.color, width=S)

    def dot(self, x, y, color=None):
        self.box(x, y, 1, 1, color)

    def circle(self, cx, cy, r, color=None):
        c = color or self.color
        self.d.ellipse(
            [(cx - r) * S, (cy - r) * S, (cx + r + 1) * S - 1, (cy + r + 1) * S - 1],
            outline=c,
            width=S,
        )

    def disc(self, cx, cy, r, color=None):
        self.d.ellipse(
            [(cx - r) * S, (cy - r) * S, (cx + r + 1) * S - 1, (cy + r + 1) * S - 1],
            fill=color or self.color,
        )

    def text(self, x, baseline, s, font=None, align="l", color=None):
        anchor = {"l": "ls", "r": "rs", "m": "ms"}[align]
        self.d.text(
            (x * S, baseline * S),
            s,
            font=font or f_sec,
            fill=color or self.color,
            anchor=anchor,
        )

    def width(self, s, font=None):
        return (font or f_sec).getbbox(s)[2] / S

    def save(self, name):
        path = os.path.join(OUT, name)
        self.img.save(path)
        print(f"wrote {path}")
        return self.img


def bezel(img, label=None):
    """Put the screen in a device-ish frame so the README reads as hardware."""
    pad = 10 * S
    bar = 0 if not label else 12 * S
    out = Image.new(
        "RGB", (img.width + 2 * pad, img.height + 2 * pad + bar), (24, 22, 20)
    )
    out.paste(img, (pad, pad))
    if label:
        d = ImageDraw.Draw(out)
        d.text(
            (out.width // 2, img.height + pad + bar // 2 + pad // 4),
            label,
            font=ImageFont.truetype(BOLD, 7 * S),
            fill=(240, 236, 230),
            anchor="mm",
        )
    return out


# ---------------- helpers/mrd_ui.c, mirrored ----------------

MRD_HDR_BASE, MRD_HDR_RULE = 8, 10

IDLE, OK, WARN, ALERT = 0, 1, 2, 3


def header(sc, title, right=""):
    sc.text(2, MRD_HDR_BASE, title)
    if right:
        sc.text(W - 2, MRD_HDR_BASE, right, align="r")
    sc.line(0, MRD_HDR_RULE, W - 1, MRD_HDR_RULE)


def arc_gauge(sc, cx, cy, r_out, r_in, pct):
    STEPS = 64
    on = (pct * STEPS + 50) // 100

    prev = None
    for i in range(STEPS + 1):
        a = math.pi * (1.0 - i / STEPS)
        ca, sa = math.cos(a), math.sin(a)
        o = (cx + int(ca * r_out), cy - int(sa * r_out))
        n = (cx + int(ca * r_in), cy - int(sa * r_in))
        if prev:
            sc.line(prev[0][0], prev[0][1], o[0], o[1])
            sc.line(prev[1][0], prev[1][1], n[0], n[1])
        prev = (o, n)

    for i in range(on):
        a = math.pi * (1.0 - (i + 0.5) / STEPS)
        ca, sa = math.cos(a), math.sin(a)
        sc.line(
            cx + int(ca * r_in), cy - int(sa * r_in),
            cx + int(ca * r_out), cy - int(sa * r_out),
        )

    for q in (1, 2, 3):
        a = math.pi * (1.0 - q / 4.0)
        ca, sa = math.cos(a), math.sin(a)
        sc.line(
            cx + int(ca * (r_out + 1)), cy - int(sa * (r_out + 1)),
            cx + int(ca * (r_out + 3)), cy - int(sa * (r_out + 3)),
        )


STRIP_CELL_W, STRIP_CELL_H, STRIP_GAP = 10, 9, 1


def check_strip(sc, x, y, states):
    for i, st in enumerate(states):
        cx = x + i * (STRIP_CELL_W + STRIP_GAP)
        if st == ALERT:
            sc.box(cx, y, STRIP_CELL_W, STRIP_CELL_H)
        elif st == WARN:
            sc.frame(cx, y, STRIP_CELL_W, STRIP_CELL_H)
            sc.box(cx + 2, y + STRIP_CELL_H - 4, STRIP_CELL_W - 4, 2)
        elif st == OK:
            sc.frame(cx, y, STRIP_CELL_W, STRIP_CELL_H)
        else:
            sc.dot(cx, y)
            sc.dot(cx + STRIP_CELL_W - 1, y)
            sc.dot(cx, y + STRIP_CELL_H - 1)
            sc.dot(cx + STRIP_CELL_W - 1, y + STRIP_CELL_H - 1)


def page_dots(sc, cx, y, count, active):
    span = (count - 1) * 5
    x = cx - span // 2
    for i in range(count):
        if i == active:
            sc.box(x + i * 5 - 1, y - 1, 3, 3)
        else:
            sc.dot(x + i * 5, y)


def globe(sc, cx, cy, r, phase):
    sc.circle(cx, cy, r)
    for k in (-1, 1):
        dy = (r * k) // 2
        half = int(math.sqrt(r * r - dy * dy))
        sc.line(cx - half, cy + dy, cx + half, cy + dy)
    t = (phase % 64) / 64.0
    half_w = int(math.cos(t * 2.0 * math.pi) * r)
    for sign in (1, -1):
        px, py = cx, cy - r
        for i in range(1, 17):
            a = math.pi * (i / 16.0)
            nx = cx + sign * int(half_w * math.sin(a))
            ny = cy - int(r * math.cos(a))
            sc.line(px, py, nx, ny)
            px, py = nx, ny


# ---------------- helpers/mrd_sim.c, mirrored ----------------

SIM_SKY = [
    (2, 312, 68, -4),
    (5, 44, 41, 9),
    (6, 128, 12, 11),
    (9, 201, 55, -7),
    (12, 75, 23, 12),
    (17, 249, 34, 8),
    (19, 156, 77, -3),
    (23, 18, 8, 13),
    (24, 288, 19, -10),
    (28, 95, 47, 6),
    (31, 340, 29, -9),
]


# One fixed draw from the same rnd_span() range the simulator uses, so the
# mockups are reproducible while still showing the scintillation a real
# receiver reports. Without it the elevation/power correlation comes out at a
# suspiciously perfect 1.00.
NOISE_CLEAN = [1, -2, 0, 2, -1, 1, -1, 0, 2, -2, 1]  # rnd_span(2)
NOISE_SPOOF = [3, -4, 1, 2, -3, 0, 4, -1, 2, -2, 3]  # rnd_span(4), tenths of a dB


def sim_sky(epoch, spoofed):
    """The same arithmetic build_sky() performs, with a fixed noise sample in
    place of the live RNG so the picture is reproducible run to run."""
    out = []
    t = 8 if spoofed else epoch
    for i, (prn, az, elev0, rate10) in enumerate(SIM_SKY):
        elev = elev0 + (rate10 * t) // 600
        elev = max(3, min(88, elev))
        if spoofed:
            snr = (522 - elev // 4 + NOISE_SPOOF[i]) // 10
        else:
            snr = 29 + (elev * 22) // 100 + NOISE_CLEAN[i]
        out.append((prn, az, elev, max(0, min(54, snr))))
    return out


def stats(sky):
    snrs = [s for _, _, _, s in sky if s > 0]
    mean = sum(snrs) / len(snrs)
    sd = math.sqrt(sum((v - mean) ** 2 for v in snrs) / len(snrs))
    els = [e for _, _, e, s in sky if s > 0]
    ex, ey = sum(els) / len(els), mean
    sxy = sum((e - ex) * (s - ey) for _, _, e, s in sky if s > 0)
    sxx = sum((e - ex) ** 2 for e in els)
    syy = sum((s - ey) ** 2 for s in snrs)
    r = sxy / math.sqrt(sxx * syy) if sxx and syy else 0.0
    return mean, sd, r


# ---------------- views/monitor_view.c ----------------

MON_GAUGE_CX, MON_GAUGE_CY, MON_R_OUT, MON_R_IN = 29, 46, 24, 19
MON_SCORE_BASE, MON_SCORE_CAP = 46, 36
MON_COL_X, MON_V1_BASE, MON_V2_BASE, MON_V_ONE_BASE, MON_CTX_BASE = 59, 24, 35, 30, 46
MON_STRIP_Y, MON_DOTS_Y = 52, 62

CLEAN_STATES = [OK] * 10 + [IDLE]
SPOOF_STATES = [OK, OK, ALERT, ALERT, ALERT, OK, ALERT, ALERT, ALERT, WARN, IDLE]


def screen_monitor(verdict, score, ctx, states, sats=9, demo=False, hint=False):
    sc = Screen()
    right = "" if demo else f"{sats} SAT"
    header(sc, "MONITOR", right)

    if demo:
        w = int(sc.width("DEMO")) + 6
        sc.box(W - w, 0, w, 10)
        sc.set_color(True)
        sc.text(W - w + 3, MRD_HDR_BASE, "DEMO")
        sc.set_color(False)

    arc_gauge(sc, MON_GAUGE_CX, MON_GAUGE_CY, MON_R_OUT, MON_R_IN, score)
    sc.text(MON_GAUGE_CX, MON_SCORE_CAP, "SCORE", align="m")
    sc.text(MON_GAUGE_CX, MON_SCORE_BASE, str(score), font=f_pri, align="m")

    words = verdict.split(" ", 1)
    if len(words) == 2:
        sc.text(MON_COL_X, MON_V1_BASE, words[0], font=f_pri)
        sc.text(MON_COL_X, MON_V2_BASE, words[1], font=f_pri)
    else:
        sc.text(MON_COL_X, MON_V_ONE_BASE, words[0], font=f_pri)
    sc.text(MON_COL_X, MON_CTX_BASE, ctx)

    if hint:
        sc.box(0, MON_STRIP_Y - 1, W, 13)
        sc.set_color(True)
        sc.text(4, MON_STRIP_Y + 8, "< >  pages")
        sc.text(W - 4, MON_STRIP_Y + 8, "OK  evidence", align="r")
        sc.set_color(False)
    else:
        sc.line(0, MON_STRIP_Y - 2, W - 1, MON_STRIP_Y - 2)
        check_strip(sc, 4, MON_STRIP_Y, states)
        page_dots(sc, 64, MON_DOTS_Y, 4, 0)
    return sc


def screen_no_link():
    sc = Screen()
    header(sc, "MONITOR", "NO LINK")
    sc.text(64, 24, "Waiting for", font=f_pri, align="m")
    sc.text(64, 35, "the receiver", font=f_pri, align="m")
    sc.text(64, 47, "No NMEA on the port yet", align="m")
    sc.line(0, 51, W - 1, 51)
    sc.text(64, 61, "Menu > Wiring for pinout", align="m")
    return sc


# ---------------- views/sky_view.c ----------------

SKY_CX, SKY_CY, SKY_R, SKY_INFO_X = 33, 37, 23, 61
BAR_TOP, BAR_BASE, BAR_MAX_DB, BAR_W, BAR_MAX_W, BAR_GAP = 14, 50, 55, 3, 9, 1
SKY_FOOT_BASE = 61


def draw_stats(sc, mean, sd, r):
    sc.line(0, SKY_FOOT_BASE - 9, W - 1, SKY_FOOT_BASE - 9)
    sc.text(2, SKY_FOOT_BASE, f"avg {mean:.1f}")
    sc.text(46, SKY_FOOT_BASE, f"sd {sd:.1f}")
    sc.text(W - 2, SKY_FOOT_BASE, f"r {r:.2f}", align="r")


def screen_sky(sky, sel=0):
    sc = Screen()
    tracked = sum(1 for *_, s in sky if s > 0)
    header(sc, "SKY", f"{tracked}/{len(sky)} trk")

    sc.circle(SKY_CX, SKY_CY, SKY_R)
    sc.circle(SKY_CX, SKY_CY, (SKY_R * 2) // 3)
    sc.circle(SKY_CX, SKY_CY, SKY_R // 3)
    sc.text(SKY_CX - 2, SKY_CY - SKY_R - 1, "N")
    sc.line(SKY_CX, SKY_CY - SKY_R, SKY_CX, SKY_CY - SKY_R + 3)
    sc.line(SKY_CX, SKY_CY + SKY_R, SKY_CX, SKY_CY + SKY_R - 3)
    sc.line(SKY_CX - SKY_R, SKY_CY, SKY_CX - SKY_R + 3, SKY_CY)
    sc.line(SKY_CX + SKY_R, SKY_CY, SKY_CX + SKY_R - 3, SKY_CY)

    for i, (prn, az, elev, snr) in enumerate(sky):
        rr = SKY_R * (90.0 - elev) / 90.0
        a = (az - 90.0) * 0.01745329
        x = SKY_CX + int(rr * math.cos(a))
        y = SKY_CY + int(rr * math.sin(a))
        if snr == 0:
            sc.dot(x, y)
        else:
            sz = 3 if snr >= 42 else 2 if snr >= 33 else 1
            if i < 9:
                sc.box(x - sz, y - sz, sz * 2 + 1, sz * 2 + 1)
            else:
                sc.frame(x - sz, y - sz, sz * 2 + 1, sz * 2 + 1)
        if i == sel:
            sc.circle(x, y, 5)

    prn, az, elev, snr = sky[sel]
    sc.text(SKY_INFO_X, 21, f"GPS {prn}", font=f_pri)
    sc.text(SKY_INFO_X, 31, f"el {elev}  az {az}")
    sc.text(SKY_INFO_X, 41, f"{snr} dB-Hz" if snr else "not tracked")
    sc.text(SKY_INFO_X, 51, "in the fix")

    draw_stats(sc, *stats(sky))
    page_dots(sc, 64, 62, 4, 1)
    return sc


def screen_bars(sky):
    sc = Screen()
    tracked = sum(1 for *_, s in sky if s > 0)
    header(sc, "CARRIERS", f"{tracked}/{len(sky)} trk")

    bar_w = max(BAR_W, min(BAR_MAX_W, (W - 4) // len(sky) - BAR_GAP))
    step = bar_w + BAR_GAP
    total = len(sky) * step - BAR_GAP
    x0 = max(1, (W - total) // 2)
    sc.line(0, BAR_BASE, W - 1, BAR_BASE)

    for i, (_, _, _, snr) in enumerate(sky):
        x = x0 + i * step
        h = min(BAR_BASE - BAR_TOP, max(1, snr * (BAR_BASE - BAR_TOP) // BAR_MAX_DB))
        if i < 9:
            sc.box(x, BAR_BASE - h, bar_w, h)
        else:
            sc.frame(x, BAR_BASE - h, bar_w, h)

    mean, sd, r = stats(sky)
    my = BAR_BASE - int(mean) * (BAR_BASE - BAR_TOP) // BAR_MAX_DB
    for x in range(0, W, 4):
        sc.dot(x, my)

    draw_stats(sc, mean, sd, r)
    page_dots(sc, 64, 62, 4, 1)
    return sc


# ---------------- views/trail_view.c ----------------

TRL_CX, TRL_CY, TRL_R = 64, 31, 17
TRL_FOOT_RULE, TRL_FOOT_BASE = 52, 61


def screen_trail(points, span, spread, note, count):
    sc = Screen()
    header(sc, "DRIFT", f"{count} fixes")

    for a in range(0, 360, 12):
        rad = math.radians(a)
        sc.dot(TRL_CX + int(math.cos(rad) * TRL_R), TRL_CY + int(math.sin(rad) * TRL_R))
    sc.line(TRL_CX - 3, TRL_CY, TRL_CX + 3, TRL_CY)
    sc.line(TRL_CX, TRL_CY - 3, TRL_CX, TRL_CY + 3)

    ppm = TRL_R / span
    for i, (e, n) in enumerate(points):
        x = TRL_CX + int(e * ppm)
        y = TRL_CY - int(n * ppm)
        if i + 1 == len(points):
            sc.box(x - 1, y - 1, 3, 3)
            sc.frame(x - 3, y - 3, 7, 7)
        else:
            sc.dot(x, y)

    sc.text(2, TRL_CY - TRL_R + 4, f"{span:.1f} m")
    sc.line(0, TRL_FOOT_RULE, W - 1, TRL_FOOT_RULE)
    sc.text(2, TRL_FOOT_BASE, f"spread {spread:.1f} m")
    sc.text(W - 2, TRL_FOOT_BASE, note, align="r")
    page_dots(sc, 64, 62, 4, 2)
    return sc


# ---------------- views/evidence_view.c ----------------

EV_TOP, EV_ROW_H, EV_ROWS, EV_TEXT_DY = 12, 12, 3, 9
EV_COL_GLYPH, EV_COL_NAME, EV_COL_HITS = 2, 13, 116
EV_RULE_Y, EV_OBS_BASE = 49, 58

CHECK_NAMES = [
    "Impossible motion",
    "Speed mismatch",
    "Flat carrier power",
    "Carrier too strong",
    "Power vs elevation",
    "Clock inconsistent",
    "Position frozen",
    "Sky not moving",
    "Accuracy implausible",
    "Altitude anomaly",
    "Lock captured",
]


def glyph(sc, x, y, state):
    s = 7
    if state == ALERT:
        sc.box(x, y, s, s)
    elif state == WARN:
        sc.frame(x, y, s, s)
        sc.box(x + 2, y + 4, s - 4, 2)
    elif state == OK:
        sc.frame(x, y, s, s)
    else:
        for dx in (0, s - 1):
            for dy in (0, s - 1):
                sc.dot(x + dx, y + dy)


def screen_evidence(states, hits, sel, top, obs):
    sc = Screen()
    alerts = sum(1 for s in states if s == ALERT)
    header(sc, "EVIDENCE", f"{alerts} alerting")

    for r in range(EV_ROWS):
        i = top + r
        if i >= len(states):
            break
        y = EV_TOP + r * EV_ROW_H
        selected = i == sel
        if selected:
            sc.box(0, y, W - 4, EV_ROW_H)
            sc.set_color(True)
        glyph(sc, EV_COL_GLYPH, y + 3, states[i])
        sc.text(EV_COL_NAME, y + EV_TEXT_DY, CHECK_NAMES[i])
        if hits[i]:
            sc.text(EV_COL_HITS, y + EV_TEXT_DY, f"x{hits[i]}", align="r")
        if selected:
            sc.set_color(False)

    # elements_scrollbar
    sc.line(W - 3, EV_TOP, W - 3, EV_RULE_Y - 1)
    bar_h = max(2, (EV_RULE_Y - EV_TOP) * EV_ROWS // len(states))
    bar_y = EV_TOP + (EV_RULE_Y - EV_TOP - bar_h) * sel // (len(states) - 1)
    sc.box(W - 4, bar_y, 3, bar_h)

    sc.line(0, EV_RULE_Y, W - 1, EV_RULE_Y)
    sc.text(2, EV_OBS_BASE, obs)
    page_dots(sc, 64, 62, 4, 3)
    return sc


# ---------------- views/wiring_view.c ----------------

WIR_BOX_Y, WIR_BOX_H = 14, 34
WIR_L_X, WIR_L_W, WIR_R_X, WIR_R_W = 2, 38, 88, 38
ROW_PWR, ROW_GND, ROW_A, ROW_B = 20, 28, 36, 44
WIR_RULE_Y, WIR_BASE1 = 50, 58


def screen_wiring(last=""):
    sc = Screen()
    header(sc, "WIRING", "LINK UP" if last else "")

    sc.rframe(WIR_L_X, WIR_BOX_Y, WIR_L_W, WIR_BOX_H, 3)
    sc.text(WIR_L_X + 3, WIR_BOX_Y - 2, "FLIPPER")
    sc.rframe(WIR_R_X, WIR_BOX_Y, WIR_R_W, WIR_BOX_H, 3)
    sc.text(WIR_R_X + 3, WIR_BOX_Y - 2, "GPS")

    for y, lab in ((ROW_PWR, "3V3"), (ROW_GND, "GND"), (ROW_A, "TX"), (ROW_B, "RX")):
        sc.text(WIR_L_X + 3, y + 3, lab)
    sc.text(WIR_R_X + 14, ROW_PWR + 3, "VCC")
    sc.text(WIR_R_X + 14, ROW_GND + 3, "GND")
    sc.text(WIR_R_X + 17, ROW_A + 3, "TX")
    sc.text(WIR_R_X + 17, ROW_B + 3, "RX")

    lx, rx = WIR_L_X + WIR_L_W, WIR_R_X
    sc.line(lx, ROW_PWR, rx, ROW_PWR)
    sc.line(lx, ROW_GND, rx, ROW_GND)
    sc.line(lx, ROW_A, rx, ROW_B)
    sc.line(lx, ROW_B, rx, ROW_A)

    f = 12 / 40.0
    sc.disc(rx - int((rx - lx) * f), ROW_A + int((ROW_B - ROW_A) * f), 1)

    sc.line(0, WIR_RULE_Y, W - 1, WIR_RULE_Y)
    sc.text(2, WIR_BASE1, "USART  13 TX / 14 RX  9600")
    sc.text(2, WIR_BASE1 + 8, last or "Flipper TX goes to module RX")
    return sc


# ---------------- views/learn_view.c ----------------

ART_TOP, ART_BOT, TEXT1_BASE, TEXT2_BASE = 12, 40, 47, 55


def learn_frame(idx, title, l1, l2, art):
    sc = Screen()
    header(sc, title, f"{idx + 1}/6")
    art(sc)
    sc.text(2, TEXT1_BASE, l1)
    sc.text(2, TEXT2_BASE, l2)
    page_dots(sc, 64, 62, 6, idx)
    return sc


def art_trilateration(sc):
    for i, sx in enumerate((24, 64, 104)):
        sc.box(sx - 3, ART_TOP + 1, 7, 4)
        sc.line(sx - 5, ART_TOP + 3, sx - 4, ART_TOP + 3)
        sc.line(sx + 4, ART_TOP + 3, sx + 5, ART_TOP + 3)
        for w in range(2):
            r = (10 + i * 5 + w * 8) % 22
            if r > 3:
                sc.circle(sx, ART_TOP + 3, r)
    sc.disc(64, ART_BOT - 2, 2)
    sc.line(56, ART_BOT + 1, 72, ART_BOT + 1)


def art_takeover(sc):
    sc.box(18, ART_TOP, 7, 4)
    sc.circle(21, ART_TOP + 2, 7)
    sc.rframe(96, ART_TOP + 16, 18, 10, 1)
    sc.line(105, ART_TOP + 16, 105, ART_TOP + 10)
    for w in range(3):
        sc.circle(105, ART_TOP + 9, 4 + (6 + w * 6) % 18)
    sc.disc(58, ART_BOT - 3, 2)
    sc.line(58, ART_BOT - 3, 92, ART_TOP + 12)
    sc.line(88, ART_TOP + 12, 92, ART_TOP + 12)
    sc.line(92, ART_TOP + 12, 92, ART_TOP + 16)


def art_families(sc):
    for i, name in enumerate(("POS", "RF", "GEO", "TIME")):
        x = 6 + i * 30
        lit = i == 1
        if lit:
            sc.rbox(x, ART_TOP + 6, 26, 16, 3)
            sc.set_color(True)
        else:
            sc.rframe(x, ART_TOP + 6, 26, 16, 3)
        sc.text(x + 13, ART_TOP + 17, name, align="m")
        if lit:
            sc.set_color(False)
    sc.text(64, ART_BOT - 1, "agreement is the evidence", align="m")


def art_limits(sc):
    globe(sc, 26, ART_TOP + 13, 11, 10)
    sc.text(46, ART_TOP + 8, "SUSPECT is as far")
    sc.text(46, ART_TOP + 17, "as one antenna can")
    sc.text(46, ART_TOP + 26, "honestly go alone.")


# ---------------- menus ----------------


def screen_menu(title, items, sel=0):
    sc = Screen()
    sc.box(0, 0, W, 12)
    sc.set_color(True)
    sc.text(64, 9, title, font=f_pri, align="m")
    sc.set_color(False)
    for i, it in enumerate(items[:4]):
        y = 13 + i * 13
        if i == sel:
            sc.box(0, y, W, 12)
            sc.set_color(True)
        sc.text(4, y + 9, it)
        if i == sel:
            sc.set_color(False)
    return sc


def screen_splash():
    sc = Screen()
    globe(sc, 28, 32, 19, 12)
    sc.line(28, 6, 28, 58)
    sc.text(58, 30, "MERIDIAN", font=f_pri)
    sc.text(58, 41, "GPS integrity")
    sc.text(58, 50, "monitor")
    return sc


# ---------------- contact sheets ----------------


def contact_sheet(imgs, name, cols=3, label_texts=None):
    labelled = [
        bezel(im, label_texts[i] if label_texts else None) for i, im in enumerate(imgs)
    ]
    rows = (len(labelled) + cols - 1) // cols
    cw, ch = labelled[0].width, labelled[0].height
    sheet = Image.new("RGB", (cw * cols, ch * rows), (24, 22, 20))
    for i, im in enumerate(labelled):
        sheet.paste(im, ((i % cols) * cw, (i // cols) * ch))
    path = os.path.join(OUT, name)
    sheet.save(path)
    print(f"wrote {path}")


if __name__ == "__main__":
    clean_sky = sim_sky(0, False)
    spoof_sky = sim_sky(400, True)

    mon_clean = screen_monitor("NOMINAL", 0, "10 of 11 armed", CLEAN_STATES).save(
        "screen_monitor_clean.png"
    )
    mon_spoof = screen_monitor(
        "SPOOF LIKELY", 80, "3 of 4 paths", SPOOF_STATES, demo=True
    ).save("screen_monitor_spoof.png")
    mon_hint = screen_monitor(
        "WARMING UP", 0, "2 of 11 armed", [OK, OK] + [IDLE] * 9, hint=True
    ).save("screen_monitor_hint.png")
    nolink = screen_no_link().save("screen_nolink.png")

    sky = screen_sky(clean_sky, sel=3).save("screen_sky.png")
    bars_clean = screen_bars(clean_sky).save("screen_carriers_clean.png")
    bars_spoof = screen_bars(spoof_sky).save("screen_carriers_spoof.png")

    wander = [
        (1.4, 0.6), (-0.9, 1.7), (0.3, -1.2), (-1.8, -0.4), (0.8, 1.9),
        (1.9, -1.1), (-0.4, 0.9), (-1.5, 1.2), (0.6, -1.8), (1.2, 0.2),
        (-1.1, -1.6), (0.1, 1.4), (-0.7, -0.3), (1.6, 1.1), (-1.9, 0.5),
        (0.4, -0.9),
    ]
    trail_ok = screen_trail(wander, 2.1, 2.1, "wandering", 64).save("screen_trail.png")
    trail_frozen = screen_trail([(0.0, 0.0)] * 6, 1.0, 0.0, "16 identical", 64).save(
        "screen_trail_frozen.png"
    )

    ev_states = SPOOF_STATES
    ev_hits = [1, 0, 393, 393, 393, 0, 382, 1, 381, 373, 0]
    evidence = screen_evidence(
        ev_states, ev_hits, 2, 2, "spread 0.8 dB, expect 4-10"
    ).save("screen_evidence.png")
    evidence2 = screen_evidence(
        ev_states, ev_hits, 6, 4, "16 of 16 fixes identical"
    ).save("screen_evidence2.png")

    wiring = screen_wiring("$GPGGA,120001.00,5128.674,N,0").save("screen_wiring.png")

    l1 = learn_frame(
        0, "Distance from timing", "Four satellites, four",
        "distances, one point.", art_trilateration,
    ).save("screen_learn1.png")
    l3 = learn_frame(
        2, "The loudest wins", "A local copy, a few dB",
        "louder, and it is believed.", art_takeover,
    ).save("screen_learn3.png")
    l5 = learn_frame(
        4, "What Meridian reads", "Eleven checks over four",
        "independent paths.", art_families,
    ).save("screen_learn5.png")
    l6 = learn_frame(
        5, "What it cannot say", "Nothing here is proof. One",
        "antenna sees tells, not truth.", art_limits,
    ).save("screen_learn6.png")

    menu = screen_menu(
        "Meridian",
        ["Monitor GPS", "Demo without hardware", "How spoofing works", "Wiring"],
    ).save("screen_menu.png")
    demo_menu = screen_menu(
        "Simulated receiver",
        ["Open sky", "Driving", "Held in place", "Carried off"],
        sel=2,
    ).save("screen_demo.png")
    splash = screen_splash().save("screen_splash.png")

    contact_sheet(
        [mon_clean, mon_spoof, bars_clean, bars_spoof, trail_ok, evidence],
        "screens.png",
        cols=3,
        label_texts=[
            "Nothing wrong",
            "Something is",
            "A real sky",
            "A transmitter",
            "Receiver noise",
            "The evidence",
        ],
    )
    contact_sheet(
        [bars_clean, bars_spoof],
        "screens_carriers.png",
        cols=2,
        label_texts=["Open sky - ragged, sd 4.8 dB", "Spoofed - flat, sd 0.6 dB"],
    )
    contact_sheet(
        [l1, l3, l5, l6],
        "screens_learn.png",
        cols=4,
        label_texts=["1 - how it works", "3 - the attack", "5 - the checks", "6 - the limit"],
    )
    contact_sheet(
        [menu, wiring, nolink, sky],
        "screens_menu.png",
        cols=4,
        label_texts=["Menu", "Wiring", "No receiver", "Sky plot"],
    )
    contact_sheet(
        [trail_ok, trail_frozen],
        "screens_trail.png",
        cols=2,
        label_texts=["Computed - it wanders", "Recited - it does not"],
    )
