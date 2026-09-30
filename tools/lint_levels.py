"""Check the level maps in levels/ for authoring mistakes.

Run from the speck/ folder: python tools/lint_levels.py [level_dir]
Exits non-zero on errors; warnings are printed but don't fail the build.
"""
import os
import sys

GW, GH, CELL, TW_CELLS = 60, 40, 4, 4  # thwomp box is 16x16 px = 4x4 cells
MAXTH = 3

sys.path.insert(0, os.path.dirname(__file__))
from levels_io import load_levels  # noqa: E402

_levels = load_levels(sys.argv[1] if len(sys.argv) > 1 else "levels", with_tags=True)
nlevels = len(_levels)
rows = [r for _, lv, _ in _levels for r in lv]
names = [n for n, _, _ in _levels]
tags = [t for _, _, t in _levels]

errors, warnings = [], []


def err(lv, msg):
    errors.append(f"level {lv} ({names[lv - 1]}): {msg}")


def warn(lv, msg):
    warnings.append(f"level {lv} ({names[lv - 1]}): {msg}")


if len(rows) != nlevels * GH:
    errors.append(f"expected {nlevels}x{GH} rows, found {len(rows)}")
    nlevels = len(rows) // GH

for li in range(nlevels):
    lv = li + 1
    g = rows[li * GH:(li + 1) * GH]
    for y, r in enumerate(g):
        if len(r) != GW:
            err(lv, f"row {y} is {len(r)} chars, want {GW}")
        bad = set(r) - set("#.sSTG^")
        if bad:
            err(lv, f"row {y} has unknown chars {sorted(bad)}")
    if errors:
        continue

    find = lambda ch: [(x, y) for y in range(GH) for x in range(GW) if g[y][x] == ch]
    spawns, gems, ths = find("S"), find("G"), find("T")
    if len(spawns) != 1:
        err(lv, f"needs exactly one S, found {len(spawns)}")
    if len(gems) != 1:
        err(lv, f"needs exactly one G, found {len(gems)}")
    if len(ths) > MAXTH:
        err(lv, f"{len(ths)} thwomps, max {MAXTH} (extras are ignored)")

    solid = lambda x, y: not (0 <= x < GW and 0 <= y < GH) or g[y][x] in "#^"

    for tx, ty in ths:
        if any(solid(tx + i, ty + j) for i in range(TW_CELLS) for j in range(TW_CELLS)):
            err(lv, f"thwomp at ({tx},{ty}) overlaps wall/spike/border")

    if spawns:
        sx, sy = spawns[0]
        if not any(solid(sx, y) for y in range(sy + 1, GH)):
            warn(lv, f"spawn ({sx},{sy}) has no floor below")

        # Speck drops from the spawn to the first floor below; judge where it comes to rest.
        land = next((y for y in range(sy + 1, GH) if solid(sx, y) or g[y][sx] == "s"), GH)
        rest = land - 1                 # Speck's center row once standing
        # Resting inside a thwomp's view = it charges on frame one.
        for tx, ty in ths:
            in_col = tx <= sx < tx + TW_CELLS
            in_row = ty <= rest < ty + TW_CELLS
            if not (in_col or in_row):
                continue
            if in_col:
                ys = range(ty + TW_CELLS, rest + 1) if rest > ty else range(rest, ty)
                cells = [(x, y) for y in ys for x in range(tx, tx + TW_CELLS)]
            else:
                xs = range(tx + TW_CELLS, sx + 1) if sx > tx else range(sx, tx)
                cells = [(x, y) for x in xs for y in range(ty, ty + TW_CELLS)]
            if not any(g[y][x] == "#" for x, y in cells):
                err(lv, f"spawn ({sx},{sy}) rests at row {rest} in clear view of thwomp at ({tx},{ty})")

    if gems:
        gx, gy = gems[0]
        # Gem art hangs ~1 cell low, so allow one empty row before the floor.
        # A ceiling just above (gem tucked in a notch, reached by jumping) is also fine.
        floor = any(gy + d < GH and g[gy + d][gx] in "#^s" for d in (1, 2))
        roof = any(gy - d >= 0 and g[gy - d][gx] == "#" for d in (1, 2, 3))
        if not (floor or roof or "float-gem" in tags[li]):
            warn(lv, f"gem ({gx},{gy}) floats (nothing under it) - fine if intended")

for w in warnings:
    print("warning:", w)
for e in errors:
    print("error:", e)
print(f"levels: {nlevels} checked, {len(errors)} errors, {len(warnings)} warnings")
sys.exit(1 if errors else 0)
