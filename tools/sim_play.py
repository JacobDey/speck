"""Replay a scripted input run through a level, frame by frame with the game's rules.

Run from the speck/ folder:
    python tools/sim_play.py <level> R40 RA20 R10 .15 ...

Each token is <keys><frames>: keys from L R A D (or "." for none), held for that
many frames. Prints how the run ended (gem, death and what killed Speck, or out of
input) and on which frame. `-v` prints Speck's position and thwomp states each frame.

Mirrors main.c: Speck's movement/jump/collision, spikes, and thwomp AI (telegraph,
charge, retract, solid while idle). Sand is treated as static wall, so this is only
exact for levels without starting sand and without stamping (e.g. the four thwomp openers).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from levels_io import GH, GW, SPIKE, WALL, SAND, load_levels, parse  # noqa: E402

CELL, FX = 4, 8
GRAV, JUMPV, MAXRUN, ACCEL, FRIC = 48, -720, 2 * 256, 28, 18
COYOTE, JBUF, JUMP_MIN, JUMP_CUT = 6, 6, 4, -200
PW, PH, TW, TH = 4, 5, 16, 16
T_IDLE, T_TELE, T_SLAM, T_RETRACT = range(4)
STATE_NAMES = "idle tele slam retract".split()


class Thwomp:
    def __init__(self, cx, cy):
        self.x = self.hx = cx * CELL
        self.y = self.hy = cy * CELL
        self.state, self.dir, self.timer, self.solid = T_IDLE, 0, 0, 1


class Sim:
    def __init__(self, rows):
        self.g, homes, spawn, gem = parse(rows)
        self.th = [Thwomp(*h) for h in homes]
        self.spawn = spawn
        self.goal_x, self.goal_y = gem[0] * CELL - 2, gem[1] * CELL - 2
        self.x = (spawn[0] * CELL + 2) << FX
        self.y = (spawn[1] * CELL) << FX
        self.vx = self.vy = 0
        self.grounded = self.coyote = self.jbuf = self.jump_t = 0
        self.prev_a = False
        self.frame = 0

    def clone(self):
        """Copy of the run state; the grid is shared (nothing here changes it)."""
        c = object.__new__(Sim)
        c.__dict__.update(self.__dict__)
        c.th = [object.__new__(Thwomp) for _ in self.th]
        for a, b in zip(c.th, self.th):
            a.__dict__.update(b.__dict__)
        return c

    def cell_at_px(self, px, py):
        if px < 0 or py < 0 or px >= GW * CELL or py >= GH * CELL:
            return WALL
        return self.g[py // CELL][px // CELL]

    def in_th_block(self, px, py):
        return any(t.solid and t.x <= px < t.x + TW and t.y <= py < t.y + TH for t in self.th)

    def solid_px(self, px, py):
        return self.cell_at_px(px, py) in (WALL, SAND) or self.in_th_block(px, py)

    # --- thwomps (th_blocked / th_sees / th_update) ---
    def th_hard(self, px, py):
        return self.cell_at_px(px, py) in (WALL, SPIKE)

    def th_blocked(self, tx, ty, d):
        x0, y0, x1, y1 = tx, ty, tx + TW - 1, ty + TH - 1
        if d <= 1:
            yy = y0 - 1 if d == 0 else y1 + 1
            return any(self.th_hard(x, yy) for x in range(x0, x1 + 1, CELL)) or self.th_hard(x1, yy)
        xx = x0 - 1 if d == 2 else x1 + 1
        return any(self.th_hard(xx, y) for y in range(y0, y1 + 1, CELL)) or self.th_hard(xx, y1)

    def th_sees(self, tx, ty, pcx, pcy, d):
        if d <= 1:
            x0, x1 = tx, tx + TW - 1
            y0, y1 = (pcy, ty - 1) if d == 0 else (ty + TH, pcy)
        else:
            y0, y1 = ty, ty + TH - 1
            x0, x1 = (pcx, tx - 1) if d == 2 else (tx + TW, pcx)
        for cy in range(y0 // CELL, y1 // CELL + 1):
            for cx in range(x0 // CELL, x1 // CELL + 1):
                if 0 <= cx < GW and 0 <= cy < GH and self.g[cy][cx] == WALL:
                    return False
        return True

    def th_update(self, t, pcx, pcy):
        if t.state == T_IDLE:
            same_col = t.x <= pcx < t.x + TW
            same_row = t.y <= pcy < t.y + TH
            if same_col or same_row:
                d = (0 if pcy < t.y else 1) if same_col else (2 if pcx < t.x else 3)
                if self.th_sees(t.x, t.y, pcx, pcy, d):
                    t.dir, t.state, t.timer = d, T_TELE, 28
        elif t.state == T_TELE:
            t.timer -= 1
            if t.timer <= 0:
                t.state = T_SLAM
        elif t.state == T_SLAM:
            mx, my = (0, 0, -4, 4)[t.dir], (-4, 4, 0, 0)[t.dir]
            nx, ny = t.x + mx, t.y + my
            if (self.th_blocked(nx, ny, t.dir) or nx < 0 or ny < 0
                    or nx + TW > GW * CELL or ny + TH > GH * CELL):
                t.state, t.timer = T_RETRACT, 20
            else:
                t.x, t.y = nx, ny
        elif t.state == T_RETRACT:
            t.timer -= 1
            t.x += 2 * ((t.x < t.hx) - (t.x > t.hx))
            t.y += 2 * ((t.y < t.hy) - (t.y > t.hy))
            if (t.x, t.y) == (t.hx, t.hy) and t.timer <= 0:
                t.state = T_IDLE

    @staticmethod
    def overlap_th(px, py, tx, ty):
        return not (px + PW <= tx or px >= tx + TW or py + PH <= ty or py >= ty + TH)

    def step(self, keys):
        """One frame. Returns None, 'gem', or a death cause."""
        self.frame += 1
        held_a = "A" in keys
        hit_a = held_a and not self.prev_a
        self.prev_a = held_a
        d = ("R" in keys) - ("L" in keys)
        if d:
            self.vx = max(-MAXRUN, min(MAXRUN, self.vx + d * ACCEL))
        elif self.vx > 0:
            self.vx = max(0, self.vx - FRIC)
        elif self.vx < 0:
            self.vx = min(0, self.vx + FRIC)

        if self.grounded:
            self.coyote = COYOTE
        elif self.coyote > 0:
            self.coyote -= 1
        if hit_a:
            self.jbuf = JBUF
        elif self.jbuf > 0:
            self.jbuf -= 1
        if self.jbuf > 0 and self.coyote > 0:
            self.vy = JUMPV
            self.grounded = self.coyote = self.jbuf = 0
            self.jump_t = 1
        if self.jump_t and self.jump_t > JUMP_MIN and not held_a:
            self.vy = max(self.vy, JUMP_CUT)
            self.jump_t = 0
        self.vy += GRAV
        if self.jump_t:
            self.jump_t += 1
        if self.vy >= 0:
            self.jump_t = 0
        self.vy = min(self.vy, CELL * 256)

        nx = self.x + self.vx
        ppx, ppy = nx >> FX, self.y >> FX
        if self.vx > 0 and (self.solid_px(ppx + PW - 1, ppy) or self.solid_px(ppx + PW - 1, ppy + PH - 1)):
            nx = ((ppx + PW - 1) // CELL * CELL - PW) << FX
            self.vx = 0
        if self.vx < 0 and (self.solid_px(ppx, ppy) or self.solid_px(ppx, ppy + PH - 1)):
            nx = ((ppx // CELL + 1) * CELL) << FX
            self.vx = 0
        self.x = nx

        ny = self.y + self.vy
        ppx, ppy = self.x >> FX, ny >> FX
        self.grounded = 0
        if self.vy > 0 and (self.solid_px(ppx, ppy + PH - 1) or self.solid_px(ppx + PW - 1, ppy + PH - 1)):
            ny = ((ppy + PH - 1) // CELL * CELL - PH) << FX
            self.vy = 0
            self.grounded = 1
        if self.vy < 0 and (self.solid_px(ppx, ppy) or self.solid_px(ppx + PW - 1, ppy)):
            ny = ((ppy // CELL + 1) * CELL) << FX
            self.vy = 0
        self.y = ny
        px, py = self.x >> FX, self.y >> FX

        result = None
        corners = [(px, py), (px + PW - 1, py), (px, py + PH - 1), (px + PW - 1, py + PH - 1)]
        if any(self.cell_at_px(cx, cy) == SPIKE for cx, cy in corners):
            result = "spike"
        for i, t in enumerate(self.th):
            if t.state == T_SLAM and self.overlap_th(px, py, t.x, t.y):
                result = f"thwomp {i}"
        if result is None and not (px + PW <= self.goal_x + 1 or px >= self.goal_x + 7 or
                                   py + PH <= self.goal_y + 1 or py >= self.goal_y + 7):
            result = "gem"

        pcx, pcy = px + PW // 2, py + PH // 2
        for t in self.th:
            self.th_update(t, pcx, pcy)
            t.solid = int(t.state in (T_IDLE, T_TELE) and not self.overlap_th(px, py, t.x, t.y))
        return result


def parse_script(tokens):
    out = []
    for tok in tokens:
        m = re.fullmatch(r"([LRAD.]+)(\d+)", tok)
        if not m:
            sys.exit(f"bad token {tok!r}: want keys (L R A D or .) then a frame count, e.g. RA12")
        out += [m.group(1).replace(".", "")] * int(m.group(2))
    return out


def run(rows, frames, verbose=False):
    """(result, frame, sim) for a list of per-frame key strings."""
    sim = Sim(rows)
    for keys in frames:
        r = sim.step(keys)
        if verbose:
            ths = " ".join(f"T{i}:{STATE_NAMES[t.state]}@{t.x},{t.y}" for i, t in enumerate(sim.th))
            print(f"{sim.frame:4d} {keys:3s} x={sim.x >> FX:3d} y={sim.y >> FX:3d} "
                  f"vx={sim.vx:4d} vy={sim.vy:5d} g={sim.grounded} {ths}")
        if r:
            return r, sim.frame, sim
    return "out of input", sim.frame, sim


def main():
    args = [a for a in sys.argv[1:] if a != "-v"]
    if not args:
        sys.exit(__doc__)
    levels = dict(load_levels())
    name = os.path.splitext(os.path.basename(args[0]))[0]
    if name not in levels:
        sys.exit(f"no level {name!r} in levels/order.txt")
    result, frame, sim = run(levels[name], parse_script(args[1:]), "-v" in sys.argv)
    print(f"{name}: {result} on frame {frame} at x={sim.x >> FX} y={sim.y >> FX}")
    sys.exit(0 if result == "gem" else 1)


if __name__ == "__main__":
    main()
