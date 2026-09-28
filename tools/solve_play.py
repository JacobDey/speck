"""Search for an input run that reaches a level's gem, using sim_play's rules.

Run from the speck/ folder: python tools/solve_play.py <level> [max_nodes]

Best-first search over inputs held 2 frames at a time (none, L, R, A, LA, RA),
scored by distance to the gem. States are merged at whole-pixel precision to keep
the search small, so the run found is replayed through sim_play exactly before it's
printed. Prints a sim_play script, proving the level is beatable without stamping.
Like sim_play, sand is static.
"""
import heapq
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from levels_io import load_levels  # noqa: E402
from sim_play import FX, Sim, parse_script, run  # noqa: E402

ACTIONS = ["", "R", "L", "A", "RA", "LA"]
CHUNK = 2


def key(s):
    return (s.x >> FX, s.y >> FX, s.vx >> 5, s.vy >> 5, s.grounded, s.coyote > 0, s.jbuf > 0,
            s.jump_t > 0, s.prev_a,
            tuple((t.x, t.y, t.state, t.timer // 2) for t in s.th))


def solve(rows, max_nodes=300000, start=None):
    """Inputs (one per CHUNK frames) from `start` (default: the spawn) to the gem, or None."""
    start = start or Sim(rows)
    gx, gy = start.goal_x + 4, start.goal_y + 4

    def h(s):
        return abs((s.x >> FX) - gx) + abs((s.y >> FX) - gy)

    seen = {key(start)}
    heap = [(h(start), 0, start, ())]
    n = 0
    while heap and n < max_nodes:
        _, _, s, path = heapq.heappop(heap)
        for a in ACTIONS:
            c = s.clone()
            result = None
            for _ in range(CHUNK):
                result = c.step(a)
                if result:
                    break
            if result == "gem":
                return list(path) + [a]
            if result:
                continue
            k = key(c)
            if k in seen:
                continue
            seen.add(k)
            n += 1
            heapq.heappush(heap, (h(c) + len(path) // 2, n, c, path + (a,)))
    return None


def to_script(path):
    out, prev, count = [], None, 0
    for a in path:
        if a == prev:
            count += CHUNK
            continue
        if prev is not None:
            out.append(f"{prev or '.'}{count}")
        prev, count = a, CHUNK
    out.append(f"{prev or '.'}{count}")
    return " ".join(out)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    name = os.path.splitext(os.path.basename(sys.argv[1]))[0]
    levels = dict(load_levels())
    if name not in levels:
        sys.exit(f"no level {name!r} in levels/order.txt")
    path = solve(levels[name], int(sys.argv[2]) if len(sys.argv) > 2 else 300000)
    if path is None:
        print(f"{name}: no run found")
        sys.exit(1)
    script = to_script(path)
    result, frame, _ = run(levels[name], parse_script(script.split()))
    if result != "gem":
        print(f"{name}: search run failed on exact replay ({result} on frame {frame})")
        sys.exit(1)
    print(f"{name}: gem in {frame} frames")
    print(script)


if __name__ == "__main__":
    main()
