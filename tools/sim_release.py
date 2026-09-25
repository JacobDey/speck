"""Show where a level's sand ends up after its thwomps let go of it.

Run from the speck/ folder:
    python tools/sim_release.py <level-name> [thwomp#[U|D|L|R]...]
Settles the starting sand with every idle thwomp solid, then runs each listed
thwomp (1-based, map order) through a charge in the given direction (default D)
and back home, frame by frame like the game: sand falls each frame, the thwomp
moves 4px/frame out and 2px/frame back and crushes sand in its box the whole way.
Repeat a thwomp to release it again, e.g. `2L 1L 2L`. Prints the map (T = thwomp
home, G = gem, S = spawn) and how much of each spike run ends up covered.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from levels_io import GH, GW, SAND, SPIKE, WALL, load_levels, parse, release, settle, thwomp_cells  # noqa: E402


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    levels = dict(load_levels())
    name = sys.argv[1].removesuffix(".txt")
    if name not in levels:
        sys.exit(f"no level '{name}'. Levels: {', '.join(levels)}")
    g, thwomps, spawn, gem = parse(levels[name])
    # args: "2L" = thwomp 2 charges left. A bare number charges down (plugs sit under hoppers).
    picks = []
    for a in sys.argv[2:] or [str(i + 1) for i in range(len(thwomps))]:
        n, d = (a[:-1], a[-1].upper()) if a[-1].isalpha() else (a, "D")
        picks.append((int(n) - 1, d))

    solid = set().union(*(thwomp_cells(*t) for t in thwomps)) if thwomps else set()
    settle(g, solid)
    for i, d in picks:
        box = thwomp_cells(*thwomps[i])
        frames = release(g, thwomps[i], d, frozenset(solid - box))
        print(f"thwomp {i + 1} charges {d}: back home and settled after {frames} frames")

    homes = set().union(*(thwomp_cells(*t) for t in thwomps)) if thwomps else set()
    for y in range(GH):
        line = []
        for x in range(GW):
            if (x, y) == gem:
                line.append("G")
            elif (x, y) == spawn:
                line.append("S")
            elif (x, y) in homes and g[y][x] == 0:
                line.append("T")
            else:
                line.append(".#s^"[g[y][x]])
        print(f"{y:2d} {''.join(line)}")

    # Coverage of each horizontal spike run (sand directly on top = safe footing).
    for y in range(GH):
        x = 0
        while x < GW:
            if g[y][x] == SPIKE:
                x0 = x
                while x < GW and g[y][x] == SPIKE:
                    x += 1
                run = range(x0, x)
                covered = [c for c in run if y > 0 and g[y - 1][c] in (SAND, WALL)]
                print(f"spikes row {y} cols {x0}-{x - 1}: {len(covered)}/{len(run)} covered")
            else:
                x += 1
    if gem and g[gem[1]][gem[0]] == SAND:
        print("WARNING: gem cell ends up inside sand (unreachable)")


main()
