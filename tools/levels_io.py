"""Load SPECK level maps from levels/ (order.txt lists the play order)."""
import os

GW, GH = 60, 40
LEVEL_DIR = "levels"
EMPTY, WALL, SAND, SPIKE = 0, 1, 2, 3
TWC = 4  # thwomp box is 4x4 cells


def load_order(level_dir=LEVEL_DIR):
    """[(filename, tags)] from order.txt. `name.txt  # tag tag` sets per-level tags."""
    order = []
    with open(os.path.join(level_dir, "order.txt"), encoding="utf-8") as f:
        for line in f:
            entry, _, comment = line.partition("#")
            entry = entry.strip()
            if entry:
                order.append((entry, set(comment.split())))
    return order


def load_levels(level_dir=LEVEL_DIR, with_tags=False):
    """[(name, [40 row strings])] in play order (plus tags if with_tags)."""
    levels = []
    for fn, tags in load_order(level_dir):
        with open(os.path.join(level_dir, fn), encoding="utf-8") as f:
            rows = [r.rstrip("\r\n") for r in f if r.strip()]
        name = os.path.splitext(fn)[0]
        levels.append((name, rows, tags) if with_tags else (name, rows))
    return levels


def parse(rows):
    """Grid of cell codes plus thwomp homes, spawn and gem positions (cells)."""
    g = [[{"#": WALL, "s": SAND, "^": SPIKE}.get(ch, EMPTY) for ch in r] for r in rows]
    find = lambda c: [(x, y) for y, r in enumerate(rows) for x, ch in enumerate(r) if ch == c]
    return g, find("T")[:3], (find("S") or [None])[0], (find("G") or [None])[0]


def thwomp_cells(tx, ty):
    return {(tx + i, ty + j) for i in range(TWC) for j in range(TWC)}


def sand_step(g, blocked=frozenset()):
    """One frame of the game's sand_step(). Returns True if any grain moved."""
    def free(x, y):
        return 0 <= x < GW and 0 <= y < GH and g[y][x] == EMPTY and (x, y) not in blocked

    moved = False
    for y in range(GH - 2, -1, -1):
        left_first = y & 1
        for n in range(GW):
            x = n if left_first else GW - 1 - n
            if g[y][x] != SAND:
                continue
            if free(x, y + 1):
                g[y + 1][x], g[y][x] = SAND, EMPTY
                moved = True
                continue
            d0 = -1 if left_first else 1
            for dx in (d0, -d0):
                if free(x + dx, y + 1) and g[y][x + dx] not in (WALL, SPIKE):
                    g[y + 1][x + dx], g[y][x] = SAND, EMPTY
                    moved = True
                    break
    return moved


def settle(g, blocked=frozenset(), max_steps=1000):
    """Run sand_step until nothing moves. `blocked` = solid (idle) thwomp cells."""
    for _ in range(max_steps):
        if not sand_step(g, blocked):
            return


DIRS = {"U": (0, -1), "D": (0, 1), "L": (-1, 0), "R": (1, 0)}


def _box_px(px, py):
    return {(x, y) for y in range(py // 4, (py + 15) // 4 + 1) for x in range(px // 4, (px + 15) // 4 + 1)}


def release(g, home, direction, others=frozenset(), max_frames=2000):
    """Frame-by-frame thwomp charge in `direction` (U/D/L/R) and return home, as the game
    does it: sand steps first each frame; the thwomp moves 4px/frame out and 2px/frame
    back, crushing sand in its box every frame, and is only solid again once home.
    Returns the number of frames until everything is home and settled."""
    dx, dy = DIRS[direction]
    hx, hy = home[0] * 4, home[1] * 4
    x, y = hx, hy
    state, frames = "slam", 0

    def hard(cx, cy):
        return not (0 <= cx < GW and 0 <= cy < GH) or g[cy][cx] in (WALL, SPIKE)

    def crush():
        for cx, cy in _box_px(x, y):
            if 0 <= cx < GW and 0 <= cy < GH and g[cy][cx] == SAND:
                g[cy][cx] = EMPTY

    while frames < max_frames:
        frames += 1
        moved = sand_step(g, others | (_box_px(x, y) if state == "home" else set()))
        if state == "slam":
            nx, ny = x + 4 * dx, y + 4 * dy
            if any(hard(cx, cy) for cx, cy in _box_px(nx, ny)):
                state = "retract"
            else:
                x, y = nx, ny
                crush()
        elif state == "retract":
            x += (hx > x) * 2 - (hx < x) * 2
            y += (hy > y) * 2 - (hy < y) * 2
            crush()
            if (x, y) == (hx, hy):
                state = "home"
        elif not moved:
            return frames
    return frames
