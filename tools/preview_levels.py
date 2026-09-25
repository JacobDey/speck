"""Render every level after its starting sand settles, as one PNG contact sheet.

Run from the speck/ folder: python tools/preview_levels.py [out.png]
Mirrors sand_step() in source/main.c (bottom-up, alternating bias, no corner
squeezing; idle thwomps are solid). Speck, stamping and thwomp AI are not simulated.
"""
import os
import sys

from PIL import Image, ImageDraw

GW, GH, CELL, TWC = 60, 40, 4, 4
EMPTY, WALL, SAND, SPIKE = 0, 1, 2, 3

sys.path.insert(0, os.path.dirname(__file__))
from levels_io import load_levels, settle, thwomp_cells  # noqa: E402

_levels = load_levels()
nlevels = len(_levels)
rows = [r for _, lv in _levels for r in lv]
names = [n for n, _ in _levels]


def rgb15(r, g, b):
    return (r * 255 // 31, g * 255 // 31, b * 255 // 31)


COL = {
    "bg": rgb15(3, 4, 8), "wall": rgb15(14, 12, 10), "wall2": rgb15(20, 16, 12),
    "sand": rgb15(28, 22, 8), "sand2": rgb15(24, 18, 6), "spike": rgb15(31, 31, 31),
    "spike2": rgb15(20, 6, 8), "thwomp": rgb15(22, 8, 10), "gem": rgb15(8, 26, 30),
    "speck": rgb15(18, 31, 20),
}


def render(li):
    m = rows[li * GH:(li + 1) * GH]
    g = [[EMPTY] * GW for _ in range(GH)]
    thwomps, spawn, gem = [], None, None
    for y in range(GH):
        for x in range(GW):
            ch = m[y][x]
            g[y][x] = {"#": WALL, "s": SAND, "^": SPIKE}.get(ch, EMPTY)
            if ch == "T":
                thwomps.append((x, y))
            elif ch == "S":
                spawn = (x, y)
            elif ch == "G":
                gem = (x, y)
    settle(g, set().union(*(thwomp_cells(*t) for t in thwomps[:3])) if thwomps else set())

    im = Image.new("RGB", (GW * CELL, GH * CELL), COL["bg"])
    d = ImageDraw.Draw(im)
    for y in range(GH):
        for x in range(GW):
            c, x0, y0 = g[y][x], x * CELL, y * CELL
            if c == WALL:
                d.rectangle([x0, y0, x0 + 3, y0 + 3], COL["wall" if (x + y) & 1 else "wall2"])
            elif c == SAND:
                d.rectangle([x0, y0, x0 + 3, y0 + 3], COL["sand" if (x * 3 + y) & 1 else "sand2"])
            elif c == SPIKE:
                d.rectangle([x0, y0 + 2, x0 + 3, y0 + 3], COL["spike2"])
                d.point([(x0, y0), (x0, y0 + 1), (x0 + 3, y0), (x0 + 3, y0 + 1)], COL["spike"])
    for tx, ty in thwomps[:3]:
        d.rectangle([tx * CELL, ty * CELL, tx * CELL + 15, ty * CELL + 15], COL["thwomp"])
    if gem:
        gx, gy = gem[0] * CELL - 2, gem[1] * CELL - 2
        d.polygon([(gx + 4, gy), (gx + 8, gy + 4), (gx + 4, gy + 7), (gx, gy + 4)], COL["gem"])
    if spawn:
        sx, sy = spawn[0] * CELL + 2, spawn[1] * CELL
        d.rectangle([sx, sy, sx + 3, sy + 4], COL["speck"])
    return im


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "levels_preview.png"
    cols, scale, pad = 3, 2, 14
    tiles = [render(i) for i in range(nlevels)]
    w, h = GW * CELL * scale, GH * CELL * scale
    nrows = (nlevels + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + pad) + pad, nrows * (h + pad + 12) + pad), (17, 17, 17))
    d = ImageDraw.Draw(sheet)
    for i, im in enumerate(tiles):
        cx = pad + (i % cols) * (w + pad)
        cy = pad + (i // cols) * (h + pad + 12)
        d.text((cx, cy), f"{i + 1}. {names[i]}", fill=(232, 196, 106))
        sheet.paste(im.resize((w, h), Image.NEAREST), (cx, cy + 12))
    sheet.save(out)
    print(f"wrote {out} ({nlevels} levels)")


main()
