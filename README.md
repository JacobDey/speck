# SPECK

A tiny GBA platformer in a falling-sand world. Stamp sand to build, bury spikes,
and dodge thwomps on the way to each level's gem. Clear every level for an ending
screen with your death count and time.

## Controls

| GBA | Web (keyboard) | Action |
|---|---|---|
| D-pad | Arrows | Move |
| A | Z | Jump (hold for height) |
| B | X | Stamp sand ahead (hold to repeat) |
| Down + B | Down + X | Stamp sand under your feet |
| START | Enter | Start / pause |
| SELECT | V | Retry (counts as a death) |
| SELECT while paused | V | Quit to title |
| Left / Right on title | Arrows | Pick a starting level (once unlocked) |
| hold L+R, press SELECT on title | Q+E, V | Erase save |
| hold L, press R | Q, E | Debug: skip to next level |

## Rules

- Sand falls and piles. Speck can stand on it; it can't pass through it.
- A sand column 3 cells deep on Speck's head buries it.
- Spikes kill on touch, but hold sand like a wall, so bury them to cross.
- Thwomps watch their row and column. With a clear view (no wall in the way)
  they flash, then charge until they hit a wall or spike, crushing any sand.
  Their eyes point where they're about to go. Standing still, they're solid.

## Build

Needs devkitARM + libtonc at `C:\devkitPro`. From PowerShell, with a writable temp dir:

```powershell
$env:TMP = $env:TEMP = "<some writable dir>"
C:\devkitPro\msys2\usr\bin\make.exe
```

This also copies the ROM to `web/speck.gba`.

(Git Bash `make` fails with "Cannot create temporary file in C:\WINDOWS\".)

## Levels

Each level is a text file in `levels/`: 40 lines of 60 characters, one per 4x4 px
cell (240x160 screen). `levels/order.txt` lists the play order; the last entry is
the finale. `make` lints them and generates `source/levels.h` (don't edit that).

| Char | Meaning |
|---|---|
| `#` | wall |
| `.` | empty |
| `s` | starting sand |
| `^` | spike |
| `S` | spawn |
| `G` | gem (goal) |
| `T` | thwomp home, top-left of its 16x16 box (max 3 per level) |

To add a level, drop a new `.txt` in `levels/` and list it in `order.txt`.
`tools/lint_levels.py` fails the build on bad row lengths, unknown characters,
missing or duplicate `S`/`G`, too many thwomps, thwomps overlapping walls, or a
spawn in a thwomp's clear line of sight (it would charge on the first frame).

`make preview` writes `levels_preview.png`: every level after its starting sand
settles (same rules as the game's sand step), to check layouts without playing.

`python tools/sim_release.py <level> 1L 2R ...` plays thwomps charging (number +
direction U/D/L/R) and returning, frame by frame with the game's rules, and shows
where the released sand lands and how much of each spike run it covers. A plug
that charges along its own sand's fall path crushes the whole stream on the way
back - release sideways.

`make DEBUG=1` builds a ROM with every level selectable on the title, and
`make START=8` also opens the title on level 8 (touch `source/main.c` or
`make clean` first, and rebuild without them before shipping). Debug builds show
a CPU meter on the pause screen: the worst frame's work in scanlines (228 = one
frame) and dropped frames since the last pause. The sand step runs from IWRAM as
ARM code and sleeps while nothing can move; a heavy sand fall measured 65/228.

Tag a level in `order.txt` with `# float-gem` when its gem hangs in the air on
purpose.

## Save

SRAM (`SRAM_V113` tag): furthest level reached, plus fewest deaths and fastest
time over a full run from level 1 (runs that use the debug skip don't count). The web page mirrors SRAM to `localStorage` so progress
survives reloads.

## Web

`web/index.html` runs the ROM in EmulatorJS. Serve the `web/` folder over HTTP
(e.g. `python -m http.server -d web`) and open it in a browser.

## How this happened (a confession)

I typed `/loop (15 mins) [improve SPECK]` into Claude Code, pointed it at a falling-sand
Game Boy Advance platformer that was a few hundred lines old, and walked away.

I came back roughly 29 hours later. It had never stopped.

Every fifteen minutes, a fresh "improve SPECK" landed and the AI dutifully improved
SPECK. It added coyote time. It added a jump buffer. It added sound effects it could
not hear, because the emulator it tested on had no audio. It added a bassline it could
not hear either. It playtested in a browser emulator that ran at **one frame per
second**, stepping the game forward by taking hundreds of tiny screenshots, like a
very patient person flipping a very slow flipbook.

Partway through, it politely suggested I playtest and maybe stop the loop.
I did not see this. Four fifteen-minute ticks queued up while it waited and it handled
them all at once. It suggested stopping again. I was not looking. It asked me directly with
a multiple-choice question, and past me - apparently paying attention for exactly one click -
chose **"Add more levels."**

So it added levels. Thirty-four of them. When it ran out of good ideas it wrote a
linter to catch its own level-design mistakes, a sand simulator to test levels it
couldn't play, and a preview renderer to look at levels it couldn't see. The simulator
lied once (a plug thwomp crushed its own sand on the way back up, so an entire level
called "the doors" never actually closed), so it rebuilt the simulator frame-accurate,
verified it against the real game, re-tuned every level it had already shipped, and
deleted the broken one. It also deleted "the undercut" and "the pour" because they
didn't work, which is more restraint than I have ever shown.

It found that big sand falls took 423 of the GBA's 228 scanlines per frame, meaning
those moments ran at roughly half speed on real hardware, built a CPU meter to prove it,
and moved the sand simulation into IWRAM as ARM code until it ran at 65.

At one point it apologized to *me* for numbering its own levels off by one.

Nobody has actually played all 34 levels at full speed. They are balanced by
simulation, arithmetic, and a lot of emulated frames watched one at a time. Some of
them are probably unfair. Please report the unfair ones.

Sorry! - Jacob Dey
