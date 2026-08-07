#!/usr/bin/env python3
"""Generate 1-bit 10x10 Flipper icons for Meridian from ASCII bitmaps.

'#' = foreground (black / on), anything else = background (white / off).
fbt thresholds PNGs to 1-bit, where dark pixels become 'on'.
"""
from PIL import Image
import os

OUT = os.path.join(os.path.dirname(__file__), "icons")
os.makedirs(OUT, exist_ok=True)

GLYPHS = {
    # App mark: a globe cut by its meridian. The line runs past the sphere at
    # both ends, because the reference is the line, not the world.
    "meridian_10px": [
        "....#.....",
        ".##.#.##..",
        "#...#...#.",
        "#...#...#.",
        "#...#...#.",
        "#...#...#.",
        "#...#...#.",
        ".##.#.##..",
        "....#.....",
        "..........",
    ],
    # A satellite: body, two panels, and a downlink.
    "sat_10px": [
        "#.####.#..",
        "#.####.#..",
        ".##..##...",
        "..####....",
        "..####....",
        "...##.....",
        "..#..#....",
        ".#....#...",
        "#......#..",
        "..........",
    ],
    # The alarm: a crosshair that has been pulled off centre.
    "spoof_10px": [
        "..#.......",
        "..#..###..",
        "..#.#...#.",
        "###.#...#.",
        "..#.#...#.",
        "..#..###..",
        "..........",
        "#..#..#..#",
        "..........",
        "..........",
    ],
}


def render(name, rows):
    img = Image.new("1", (10, 10), 1)  # 1 = white
    px = img.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row[:10]):
            if ch == "#":
                px[x, y] = 0
    path = os.path.join(OUT, f"{name}.png")
    img.save(path)
    print(f"wrote {path}")


if __name__ == "__main__":
    for name, rows in GLYPHS.items():
        assert len(rows) == 10, f"{name}: expected 10 rows, got {len(rows)}"
        render(name, rows)
