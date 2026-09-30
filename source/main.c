// SPECK — tiny jumper in a falling-sand world. Stamp sand (B) to build, bury
// spikes, and dodge 4-way thwomps on the way to each level's gem.
// Mode 3 bitmap + OBJ, libtonc. DMG sfx + wave-channel bass. SRAM save
// (furthest level, best full-run deaths). Debug: hold L, press R to skip a level.
// Become/transform deferred.
#include <tonc.h>
#include <string.h>

#define GW 60
#define GH 40
#define CELL 4

#define EMPTY 0
#define WALL  1
#define SAND  2
#define SPIKE 3   // kills Speck on touch; solid to sand and thwomps

#define FX 8
#define GRAV   48
#define JUMPV  (-720)
#define MAXRUN (2 * 256)
#define ACCEL  28
#define FRIC   18
#define COYOTE 6   // frames you can still jump after leaving a ledge
#define JBUF   6   // frames an early A press is remembered
#define JUMP_MIN 4 // a jump always rises this many frames (~11px) before letting go of A cuts it
#define JUMP_CUT (-200) // upward speed a released jump is cut to
#define DEATH_FRAMES 24

#define PW 4
#define PH 5

#define TW 16
#define TH 16

#define COL_BG     RGB15(3, 4, 8)
#define COL_WALL   RGB15(14, 12, 10)
#define COL_WALL2  RGB15(20, 16, 12)
#define COL_SAND   RGB15(28, 22, 8)
#define COL_SAND2  RGB15(24, 18, 6)
#define COL_SPIKE  RGB15(31, 31, 31)
#define COL_SPIKE2 RGB15(20, 6, 8)   // red base reads as "danger"

enum { T_IDLE, T_TELE, T_SLAM, T_RETRACT };

static u8 grid[GH][GW];
static int spawn_cx, spawn_cy;
// Thwomps: up to MAXTH per level, one per 'T' in the map.
#define MAXTH 3
typedef struct { int x, y, hx, hy, state, dir, timer, flash, solid; } Thwomp;
static Thwomp th[MAXTH];
static int nth;
static int goal_x, goal_y; // px, top-left of 8x8 gem

// Level chars: # wall, . empty, S spawn, T thwomp home, s starter sand, G goal gem, ^ spike
// Maps live in levels/*.txt (play order in levels/order.txt); levels.h is generated.
#include "levels.h"
static int cur_level;

static inline __attribute__((always_inline)) int in_grid(int cx, int cy) {
    return cx >= 0 && cy >= 0 && cx < GW && cy < GH;
}

static inline u8 cell_at_px(int px, int py) {
    if (px < 0 || py < 0 || px >= GW * CELL || py >= GH * CELL) return WALL;
    return grid[py / CELL][px / CELL];
}

static inline int solid_cell(u8 c) { return c == WALL || c == SAND; }

// A thwomp sitting still (IDLE/TELE) is solid to Speck and sand.
static inline __attribute__((always_inline)) int in_th_block(int px, int py) {
    for (int i = 0; i < nth; i++) {
        const Thwomp *t = &th[i];
        if (t->solid && px >= t->x && px < t->x + TW && py >= t->y && py < t->y + TH)
            return 1;
    }
    return 0;
}

// Speck's box (px); sand won't enter it, so it piles on Speck's head instead.
static int sp_x = -1000, sp_y = -1000;

static inline __attribute__((always_inline)) int cell_hits_speck(int cx, int cy) {
    int x0 = cx * CELL, y0 = cy * CELL;
    return x0 < sp_x + PW && x0 + CELL > sp_x && y0 < sp_y + PH && y0 + CELL > sp_y;
}

// Cell sand may move/stamp into.
static inline __attribute__((always_inline)) int free_cell(int cx, int cy) {
    return grid[cy][cx] == EMPTY && !in_th_block(cx * CELL, cy * CELL) &&
           !cell_hits_speck(cx, cy);
}

static inline int solid_px(int px, int py) {
    return solid_cell(cell_at_px(px, py)) || in_th_block(px, py);
}

static inline __attribute__((always_inline)) COLOR cell_color(int cx, int cy) {
    u8 c = grid[cy][cx];
    if (c == WALL) return ((cx + cy) & 1) ? COL_WALL : COL_WALL2;
    if (c == SAND) return ((cx * 3 + cy) & 1) ? COL_SAND : COL_SAND2;
    return COL_BG;
}

static void plot_spike(int cx, int cy) {
    // Two little teeth, lit tips over a dark base.
    static const char pat[CELL][CELL + 1] = { "a..a", "a..a", "bbbb", "bbbb" };
    int x0 = cx * CELL, y0 = cy * CELL;
    for (int dy = 0; dy < CELL; dy++)
        for (int dx = 0; dx < CELL; dx++) {
            char p = pat[dy][dx];
            m3_plot(x0 + dx, y0 + dy, p == 'a' ? COL_SPIKE : p == 'b' ? COL_SPIKE2 : COL_BG);
        }
}

static void plot_cell(int cx, int cy) {
    if (!in_grid(cx, cy)) return;
    if (grid[cy][cx] == SPIKE) {
        plot_spike(cx, cy);
        return;
    }
    COLOR col = cell_color(cx, cy);
    u32 fill = col | (col << 16);
    u32 *p = (u32 *)&vid_mem[cy * CELL * M3_WIDTH + cx * CELL];
    for (int dy = 0; dy < CELL; dy++, p += M3_WIDTH / 2) {
        p[0] = fill;
        p[1] = fill;
    }
}

static void redraw_all(void) {
    // Build one scanline per grid row, then blit it CELL times (fits in vblank-ish time).
    static COLOR line[M3_WIDTH] ALIGN4;
    for (int y = 0; y < GH; y++) {
        for (int x = 0; x < GW; x++) {
            COLOR col = cell_color(x, y);
            for (int dx = 0; dx < CELL; dx++) line[x * CELL + dx] = col;
        }
        for (int dy = 0; dy < CELL; dy++)
            memcpy32(&vid_mem[(y * CELL + dy) * M3_WIDTH], line, M3_WIDTH / 2);
        for (int x = 0; x < GW; x++)
            if (grid[y][x] == SPIKE) plot_spike(x, y);
    }
}

// Settled sand is skipped until something could move it (see sand_wake()).
static int sand_asleep;
static inline void sand_wake(void) { sand_asleep = 0; }

static void load_level(void) {
    spawn_cx = 10;
    spawn_cy = 30;
    nth = 0;
    goal_x = goal_y = -100;
    for (int y = 0; y < GH; y++) {
        for (int x = 0; x < GW; x++) {
            char ch = levels[cur_level][y][x];
            u8 c = EMPTY;
            if (ch == '#') c = WALL;
            else if (ch == 's') c = SAND;
            else if (ch == '^') c = SPIKE;
            else if (ch == 'S') {
                spawn_cx = x;
                spawn_cy = y;
            } else if (ch == 'G') {
                goal_x = x * CELL - 2;
                goal_y = y * CELL - 2;
            } else if (ch == 'T' && nth < MAXTH) {
                Thwomp *t = &th[nth++];
                memset(t, 0, sizeof(*t));
                t->x = t->hx = x * CELL;
                t->y = t->hy = y * CELL;
                t->state = T_IDLE;
                t->solid = 1; // solid from frame one, before sand's first step (level 8's plug)
            }
            grid[y][x] = c;
        }
    }
    sand_wake();
}

static inline __attribute__((always_inline)) void plot_sand_cell(int cx, int cy) {
    COLOR col = cell_color(cx, cy); // only SAND/EMPTY cells change in sand_step
    u32 fill = col | (col << 16);
    u32 *p = (u32 *)&vid_mem[cy * CELL * M3_WIDTH + cx * CELL];
    for (int dy = 0; dy < CELL; dy++, p += M3_WIDTH / 2) {
        p[0] = fill;
        p[1] = fill;
    }
}

// Hot loop: runs from IWRAM as ARM code (several times faster than Thumb from the cart).
IWRAM_CODE __attribute__((target("arm"), noinline)) static int sand_step(void) {
    int moved = 0;
    // Bottom-up; swap into empty below / diagonal.
    for (int y = GH - 2; y >= 0; y--) {
        // Alternate bias so piles don't always lean one way.
        int left_first = (y & 1);
        for (int n = 0; n < GW; n++) {
            int x = left_first ? n : (GW - 1 - n);
            if (grid[y][x] != SAND) continue;
            if (free_cell(x, y + 1)) {
                grid[y + 1][x] = SAND;
                grid[y][x] = EMPTY;
                plot_sand_cell(x, y);
                plot_sand_cell(x, y + 1);
                moved = 1;
                continue;
            }
            int d0 = left_first ? -1 : 1;
            int d1 = -d0;
            int slid = 0;
            for (int k = 0; k < 2 && !slid; k++) {
                int dx = (k == 0) ? d0 : d1;
                int nx = x + dx;
                // Side cell must be open too, or grains squeeze through wall corners.
                if (in_grid(nx, y + 1) && free_cell(nx, y + 1) && grid[y][nx] != WALL &&
                    grid[y][nx] != SPIKE) {
                    grid[y + 1][nx] = SAND;
                    grid[y][x] = EMPTY;
                    plot_sand_cell(x, y);
                    plot_sand_cell(nx, y + 1);
                    slid = moved = 1;
                }
            }
        }
    }
    return moved;
}

static int fill_sand(int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++)
            if (in_grid(tx, ty) && free_cell(tx, ty)) {
                grid[ty][tx] = SAND;
                plot_cell(tx, ty);
                n++;
                sand_wake();
            }
    return n;
}

// Returns cells placed.
static int stamp_sand(int px, int py, int face, int down) {
    int feet = (py + PH - 1) / CELL;
    if (down) // row just under Speck's feet (mid-air bridging / spike burying)
        return fill_sand(px / CELL, feet + 1, (px + PW - 1) / CELL, feet + 1);
    // 2x2 brush just past Speck's leading edge, at feet height.
    int x0 = (face < 0) ? px / CELL - 2 : (px + PW - 1) / CELL + 1;
    return fill_sand(x0, feet - 1, x0 + 1, feet);
}

#define BURY_DEPTH 3
static int player_buried(int px, int py) {
    // Crushed when a sand column BURY_DEPTH deep rests on Speck's head.
    int cx = (px + PW / 2) / CELL;
    int depth = 0;
    for (int cy = py / CELL - 1; cy >= 0 && grid[cy][cx] == SAND; cy--)
        if (++depth >= BURY_DEPTH) return 1;
    return 0;
}

static int overlap_thwomp(int px, int py, int tx, int ty) {
    return !(px + PW <= tx || px >= tx + TW || py + PH <= ty || py >= ty + TH);
}

static inline int th_hard(u8 c) { return c == WALL || c == SPIKE; }

static int th_blocked(int tx, int ty, int dir) {
    // Probe leading edge for wall (sand is crushable — thwomp eats it).
    int x0 = tx, y0 = ty, x1 = tx + TW - 1, y1 = ty + TH - 1;
    if (dir == 0) { // up
        for (int x = x0; x <= x1; x += CELL)
            if (th_hard(cell_at_px(x, y0 - 1))) return 1;
        return th_hard(cell_at_px(x1, y0 - 1));
    }
    if (dir == 1) { // down
        for (int x = x0; x <= x1; x += CELL)
            if (th_hard(cell_at_px(x, y1 + 1))) return 1;
        return th_hard(cell_at_px(x1, y1 + 1));
    }
    if (dir == 2) { // left
        for (int y = y0; y <= y1; y += CELL)
            if (th_hard(cell_at_px(x0 - 1, y))) return 1;
        return th_hard(cell_at_px(x0 - 1, y1));
    }
    // right
    for (int y = y0; y <= y1; y += CELL)
        if (th_hard(cell_at_px(x1 + 1, y))) return 1;
    return th_hard(cell_at_px(x1 + 1, y1));
}

static int th_sees(int tx, int ty, int pcx, int pcy, int dir) {
    // Walls between thwomp and Speck block its view; sand doesn't (it gets crushed).
    int x0, x1, y0, y1;
    if (dir <= 1) { // vertical: thwomp's column span, rows between
        x0 = tx; x1 = tx + TW - 1;
        if (dir == 0) { y0 = pcy; y1 = ty - 1; }
        else          { y0 = ty + TH; y1 = pcy; }
    } else {        // horizontal: thwomp's row span, cols between
        y0 = ty; y1 = ty + TH - 1;
        if (dir == 2) { x0 = pcx; x1 = tx - 1; }
        else          { x0 = tx + TW; x1 = pcx; }
    }
    for (int cy = y0 / CELL; cy <= y1 / CELL; cy++)
        for (int cx = x0 / CELL; cx <= x1 / CELL; cx++)
            if (in_grid(cx, cy) && grid[cy][cx] == WALL) return 0;
    return 1;
}

static void th_crush_sand(int tx, int ty) {
    sand_wake();
    int x0 = tx / CELL, y0 = ty / CELL;
    int x1 = (tx + TW - 1) / CELL, y1 = (ty + TH - 1) / CELL;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (in_grid(x, y) && grid[y][x] == SAND) {
                grid[y][x] = EMPTY;
                plot_cell(x, y);
            }
}

static void make_speck_tile(void) {
    // 8x8 sprite, ~5px speck in palette index 1 (body) + 2 (eye).
    // Mode 3 framebuffer overlaps charblock 4, so OBJ tiles live in charblock 5 (tile 512+).
    TILE *ot = &tile_mem[5][0];
    memset32(ot, 0, 8);
    // rows as nibbles packed in u32 words (4bpp, 8 pixels per row)
    // Pattern (cols 2-5 used): tiny body
    u32 rows[8] = {
        0x00000000,
        0x00011000, // ...##...
        0x00121100, // ..#o#...  (eye at col 4)
        0x00111100,
        0x00111100,
        0x00100100, // feet
        0x00000000,
        0x00000000,
    };
    memcpy32(ot, rows, 8);

    // Extra frames: walk (feet together), air (feet tucked), blink (eye shut).
    u32 walk[8], air[8], blink[8];
    memcpy32(walk, rows, 8);
    memcpy32(air, rows, 8);
    memcpy32(blink, rows, 8);
    walk[5] = 0x00011000;
    air[4] = 0x01111110;   // limbs out
    air[5] = 0x00000000;
    blink[2] = 0x00111100;
    memcpy32(&tile_mem[5][7], walk, 8);
    memcpy32(&tile_mem[5][8], air, 8);
    memcpy32(&tile_mem[5][9], blink, 8);
}

#define SPR_IDLE  512
#define SPR_WALK  519
#define SPR_AIR   520
#define SPR_BLINK 521

// Set pixel (x,y) of a 16x16 1D-mapped 4bpp sprite (tiles 0,1 / 2,3).
static void spr16_px(TILE *t, int x, int y, u32 c) {
    u32 *row = &t[(y >> 3) * 2 + (x >> 3)].data[y & 7];
    int sh = (x & 7) * 4;
    *row = (*row & ~(0xFu << sh)) | (c << sh);
}

// One thwomp face: body index 3, 2x2 eyes (index 4) offset by (dx,dy) from center.
static void make_thwomp_face(TILE *t, int dx, int dy) {
    for (int i = 0; i < 4; i++)
        for (int r = 0; r < 8; r++) t[i].data[r] = 0x33333333;
    for (int e = 0; e < 2; e++) {
        int ex = (e ? 10 : 4) + dx, ey = 3 + dy;
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++) spr16_px(t, ex + i, ey + j, 4);
    }
}

// OBJ 513: idle face. OBJ 522 + dir*4: eyes glaring toward the charge (up/down/left/right).
#define TH_TILE_IDLE 513
#define TH_TILE_DIR  522
static void make_thwomp_tiles(void) {
    static const s8 ox[4] = { 0, 0, -3, 3 };
    static const s8 oy[4] = { -2, 4, 1, 1 };
    make_thwomp_face(&tile_mem[5][1], 0, 0);
    for (int d = 0; d < 4; d++)
        make_thwomp_face(&tile_mem[5][10 + d * 4], ox[d], oy[d]);
}

// --- dust particles: OBJ 3..3+NDUST-1, tile 518 (2x2 dot, palette 8) ---
#define NDUST 12
typedef struct { int x, y, vx, vy, life, tile; } Dust;
#define P_DUST  518 // tile 6:  dust (palette 8)
#define P_SPECK 538 // tile 26: Speck-yellow bit (palette 1)
#define P_GEM   539 // tile 27: gem sparkle (palette 6)
static Dust dust[NDUST];
static int dust_next;

static void make_dust_tile(void) {
    TILE *ot = &tile_mem[5][6];
    memset32(ot, 0, 8);
    ot->data[0] = 0x00000088;
    ot->data[1] = 0x00000088;
    // Same 2x2 dot in other colors.
    memcpy32(&tile_mem[5][26], ot, 8);
    memcpy32(&tile_mem[5][27], ot, 8);
    tile_mem[5][26].data[0] = tile_mem[5][26].data[1] = 0x00000011;
    tile_mem[5][27].data[0] = tile_mem[5][27].data[1] = 0x00000066;
}

// n particles of `tile` at (px,py), spread sideways, optional upward kick (subpixels).
static void particle_burst(int px, int py, int n, int up, int tile) {
    static const s8 sx[8] = { -3, 3, -2, 2, -4, 4, -1, 1 };
    for (int i = 0; i < n; i++) {
        Dust *d = &dust[dust_next];
        dust_next = (dust_next + 1) % NDUST;
        d->x = px << FX;
        d->y = py << FX;
        d->vx = sx[i & 7] * 40;
        d->vy = -up - (i & 3) * 30;
        d->life = 12 + (i & 3) * 3;
        d->tile = tile;
    }
}

static void dust_burst(int px, int py, int n, int up) { particle_burst(px, py, n, up, P_DUST); }

static void dust_update(OBJ_ATTR *o, int ox, int oy) {
    for (int i = 0; i < NDUST; i++) {
        Dust *d = &dust[i];
        if (d->life > 0) {
            d->life--;
            d->x += d->vx;
            d->y += d->vy;
            d->vy += 20;
            d->vx = d->vx * 7 / 8;
        }
        if (d->life > 0) {
            o[i].attr0 &= ~ATTR0_HIDE;
            o[i].attr2 = d->tile;
            obj_set_pos(&o[i], (d->x >> FX) - ox, (d->y >> FX) - oy);
        } else {
            o[i].attr0 |= ATTR0_HIDE;
        }
    }
}

static void dust_clear(void) {
    for (int i = 0; i < NDUST; i++) dust[i].life = 0;
}

// Respawn safety: sand piled over the spawn would trap Speck (can't move or jump
// inside sand) or re-bury it instantly, looping forever. In Speck's columns, clear
// sand from the feet row upward until a wall/spike roof (sand above that can't fall here).
static void clear_spawn(int px, int py) {
    int x0 = px / CELL, x1 = (px + PW - 1) / CELL, y1 = (py + PH - 1) / CELL;
    int cleared = 0;
    for (int cx = x0; cx <= x1; cx++)
        for (int cy = y1; cy >= 0 && (grid[cy][cx] == SAND || grid[cy][cx] == EMPTY); cy--)
            if (grid[cy][cx] == SAND) {
                grid[cy][cx] = EMPTY;
                plot_cell(cx, cy);
                cleared = 1;
                sand_wake();
            }
    if (cleared) dust_burst(px + PW / 2, py, 6, 100);
}

static void make_gem_tile(void) {
    // 8x8 diamond: index 6 body, 7 glint. Tile 5 in charblock 5 (OBJ 517).
    TILE *ot = &tile_mem[5][5];
    u32 rows[8] = {
        0x00066000,
        0x00677600,
        0x06666760,
        0x66666666,
        0x06666660,
        0x00666600,
        0x00066000,
        0x00000000,
    };
    memcpy32(ot, rows, 8);
}

static int overlap_gem(int px, int py) {
    return !(px + PW <= goal_x + 1 || px >= goal_x + 7 ||
             py + PH <= goal_y + 1 || py >= goal_y + 7);
}

// --- text (tte bitmap, vwf font) ---
static int banner_t; // frames left before the level banner is wiped

static void text_center(int y, const char *str, COLOR ink) {
    POINT16 sz = tte_get_text_size(str);
    tte_set_ink(ink);
    tte_set_pos((M3_WIDTH - sz.x) / 2, y);
    tte_write(str);
}

static void str_int(char *dst, int v);

static void draw_banner(void) {
    char buf[16] = "LEVEL ";
    str_int(buf, cur_level + 1); // two digits from level 10 on
    text_center(64, buf, CLR_WHITE);
    text_center(78, level_names[cur_level], COL_SAND);
}

static void show_banner(void) {
    draw_banner();
    banner_t = 90;
}

static void wait_start(void) {
    do {
        VBlankIntrWait();
        key_poll();
    } while (!key_hit(KEY_START | KEY_A));
}

// --- save: SRAM, byte access only. The tag lets emulators/flashcarts detect SRAM. ---
__attribute__((used)) static const char save_tag[] ALIGN4 = "SRAM_V113";
#define SRAM ((vu8 *)0x0E000000)
static int save_unlocked = 1; // levels selectable on the title (1..NLEVELS)
static int save_best = -1;    // fewest deaths over a full run from level 1; -1 = none
static int save_time = -1;    // fastest full run, in frames; -1 = none

static void save_load_sram(void);

static void save_load(void) {
    save_load_sram();
#ifdef SPECK_UNLOCK_ALL
    save_unlocked = NLEVELS; // debug builds (make DEBUG=1): every level on the title
#endif
}

#define SAVE_VER 2
// Version 1 saves predate the 4 thwomp opening levels: every level number since moved up
// by 4, and full-run records were for a shorter run.
#define V1_SHIFT 4

static void save_write(void);

static void save_load_sram(void) {
    int ver = SRAM[3];
    if (SRAM[0] != 'S' || SRAM[1] != 'P' || SRAM[2] != 'K' || ver < 1 || ver > SAVE_VER) return;
    save_unlocked = SRAM[4];
    if (save_unlocked < 1 || save_unlocked > NLEVELS) save_unlocked = 1;
    int b = SRAM[5] | (SRAM[6] << 8);
    save_best = (b == 0xFFFF) ? -1 : b;
    // Bytes 7-10 were added later; older saves (and fresh SRAM) read 0xFFFFFFFF there.
    u32 t = SRAM[7] | (SRAM[8] << 8) | (SRAM[9] << 16) | ((u32)SRAM[10] << 24);
    save_time = (t == 0xFFFFFFFF || t > 0x7FFFFFFF) ? -1 : (int)t;
    if (ver == 1) {
        save_unlocked = (save_unlocked > 1) ? save_unlocked + V1_SHIFT : 1;
        if (save_unlocked > NLEVELS) save_unlocked = NLEVELS;
        save_best = save_time = -1;
        save_write();
    }
}

static void save_write(void) {
    int b = (save_best < 0) ? 0xFFFF : save_best;
    SRAM[0] = 'S'; SRAM[1] = 'P'; SRAM[2] = 'K'; SRAM[3] = SAVE_VER;
    SRAM[4] = save_unlocked;
    SRAM[5] = b & 0xFF;
    SRAM[6] = b >> 8;
    u32 t = (save_time < 0) ? 0xFFFFFFFF : (u32)save_time;
    for (int i = 0; i < 4; i++) SRAM[7 + i] = (t >> (8 * i)) & 0xFF;
}

// Append decimal v to the string at dst.
static void str_int(char *dst, int v) {
    while (*dst) dst++;
    char num[8];
    int n = 0;
    do { num[n++] = '0' + v % 10; v /= 10; } while (v && n < 7);
    while (n) *dst++ = num[--n];
    *dst = 0;
}

// Append frames as m:ss.
static void str_time(char *dst, int frames) {
    int secs = frames / 60;
    str_int(dst, secs / 60);
    while (*dst) dst++;
    *dst++ = ':';
    *dst++ = '0' + (secs % 60) / 10;
    *dst++ = '0' + secs % 10;
    *dst = 0;
}

static void clear_line(int y) { m3_rect(0, y, M3_WIDTH, y + 12, COL_BG); }

static void ending_screen(int deaths, int frames, int best_deaths, int best_time) {
    char buf[24] = "deaths: ";
    str_int(buf, deaths);
    char tbuf[24] = "time: ";
    str_time(tbuf, frames);

    REG_DISPCNT &= ~DCNT_OBJ;
    REG_BG2X = REG_BG2Y = 0;
    REG_SND3CNT = 0; // cut any held bass note
    m3_fill(COL_BG);
    text_center(50, "the speck got every gem", COL_SAND);
    text_center(70, buf, COL_WALL2);
    text_center(82, tbuf, COL_WALL2);
    if (best_deaths && best_time) text_center(98, "new best: deaths + time!", CLR_WHITE);
    else if (best_deaths) text_center(98, "new best: fewest deaths!", CLR_WHITE);
    else if (best_time) text_center(98, "new best: fastest time!", CLR_WHITE);
    text_center(118, "press START", CLR_WHITE);
    wait_start();
    REG_DISPCNT |= DCNT_OBJ;
}

// Returns the chosen starting level (0-based). Hold L+R and press SELECT to erase the save.
static int title_screen(void) {
  redraw:
    m3_fill(COL_BG);
    text_center(40, "S  P  E  C  K", COL_SAND);
    text_center(72, "A  jump      B  stamp sand", COL_WALL2);
    text_center(84, "hold B: repeat   down+B: below", COL_WALL2);
    text_center(96, "reach the gem - dodge the block", COL_WALL2);
    if (save_best >= 0 || save_time >= 0) {
        char buf[40] = "best: ";
        if (save_best >= 0) {
            str_int(buf, save_best);
            strcat(buf, " deaths");
        }
        if (save_time >= 0) {
            if (save_best >= 0) strcat(buf, "   ");
            str_time(buf, save_time);
        }
        text_center(149, buf, COL_WALL);
    }
    int sel = save_unlocked - 1; // default to the furthest level reached
#ifdef SPECK_START
    sel = SPECK_START - 1;       // debug: make START=n opens the title on level n
#endif
    int t = 0, dirty = 1;
    while (1) {
        VBlankIntrWait();
        key_poll();
        if (key_hit(KEY_START | KEY_A)) break;
        if (key_held(KEY_L) && key_held(KEY_R) && key_hit(KEY_SELECT) &&
            (save_unlocked > 1 || save_best >= 0 || save_time >= 0)) {
            save_unlocked = 1;
            save_best = save_time = -1;
            save_write();
            goto redraw; // best line and level select vanish = erased
        }
        if (save_unlocked > 1) {
            if (key_hit(KEY_LEFT) && sel > 0) { sel--; dirty = 1; }
            if (key_hit(KEY_RIGHT) && sel < save_unlocked - 1) { sel++; dirty = 1; }
            if (dirty) {
                // Arrows only where there's somewhere to go.
                char buf[20];
                strcpy(buf, sel > 0 ? "<  level " : "   level ");
                str_int(buf, sel + 1);
                strcat(buf, sel < save_unlocked - 1 ? "  >" : "   ");
                clear_line(110);
                text_center(110, buf, COL_SAND);
                clear_line(122);
                text_center(122, level_names[sel], COL_WALL2);
                dirty = 0;
            }
        }
        if ((t++ & 31) == 0) text_center(136, "press START", (t & 32) ? COL_BG : CLR_WHITE);
    }
    return sel;
}

// --- sfx: DMG channels, raw register values ---
// CNT: len[0-5] duty[6-7] envstep[8-10] envdir[11] vol[12-15]
// FREQ: rate[0-10] (Hz = 131072 / (2048 - rate)) reset[15]
#define ENV(vol, step) (((vol) << 12) | ((step) << 8))
#define DUTY(d)        ((d) << 6)
#define SWEEP(time, down, shift) (((time) << 4) | ((down) << 3) | (shift))

static void sfx_init(void) {
    REG_SNDSTAT = 0x0080;                    // master on
    REG_SNDDMGCNT = 0xFF77;                  // all DMG ch, L+R, vol 7
    REG_SNDDSCNT = 0x0002;                   // DMG at 100%
}

static void sfx_sq1(u16 sweep, u16 cnt, u16 rate) {
    REG_SND1SWEEP = sweep;
    REG_SND1CNT = cnt;
    REG_SND1FREQ = 0x8000 | rate;
}

static void sfx_sq2(u16 cnt, u16 rate) {
    REG_SND2CNT = cnt;
    REG_SND2FREQ = 0x8000 | rate;
}

// NOISE FREQ: div[0-2] width7[3] shift[4-7]; higher shift = lower rumble
static void sfx_noise(u16 cnt, u16 freq) {
    REG_SND4CNT = cnt;
    REG_SND4FREQ = 0x8000 | freq;
}

#define SFX_JUMP()  sfx_sq1(SWEEP(2, 0, 2), ENV(10, 2) | DUTY(2), 1750)
#define SFX_STAMP() sfx_noise(ENV(9, 1), (2 << 4) | 1)
#define SFX_TELE()  sfx_sq2(ENV(8, 1) | DUTY(1), 1860)
#define SFX_SLAM()  sfx_noise(ENV(15, 4), (6 << 4) | 3)
#define SFX_DIE()   sfx_sq1(SWEEP(3, 1, 3), ENV(12, 4) | DUTY(2), 1880)
#define SFX_GEM()   do { sfx_sq1(SWEEP(2, 0, 3), ENV(11, 5) | DUTY(2), 1923);                          sfx_sq2(ENV(9, 6) | DUTY(2), 1985); } while (0)

// --- music: staccato bassline on the wave channel (ch3), leaving sq1/sq2/noise to sfx ---
// Wave rate: Hz = 65536 / (2048 - rate)
enum { N_F2 = 1297, N_G2 = 1379, N_A2 = 1452, N_C3 = 1547, N_D3 = 1602, N_E3 = 1650 };
static const u16 bassline[32] = {
    N_A2, 0, N_A2, N_E3, N_A2, 0, N_G2, 0,  N_F2, 0, N_F2, N_C3, N_F2, 0, N_G2, 0,
    N_C3, 0, N_C3, N_G2, N_C3, 0, N_D3, 0,  N_E3, 0, N_E3, N_D3, N_C3, 0, N_G2, 0,
};
#define MUS_STEP 8   // frames per step
#define MUS_GATE 6   // frames a note sounds
#define MUS_VOL  0x6000 // 25%

static void music_init(void) {
    // Soft triangle. Wave RAM writes land in the bank not selected for play.
    REG_SND3SEL = 0x0040;          // play bank 1 -> write bank 0
    REG_WAVE_RAM0 = 0x67452301;
    REG_WAVE_RAM1 = 0xEFCDAB89;
    REG_WAVE_RAM2 = 0x98BADCFE;
    REG_WAVE_RAM3 = 0x10325476;
    REG_SND3SEL = 0x0080;          // play bank 0, channel on
    REG_SND3CNT = 0;
}

static void music_tick(int frame) {
    int sub = frame % MUS_STEP;
    u16 note = bassline[(frame / MUS_STEP) % 32];
    if (sub == 0 && note) {
        REG_SND3CNT = MUS_VOL;
        REG_SND3FREQ = 0x8000 | note;
    } else if (sub == MUS_GATE) {
        REG_SND3CNT = 0;
    }
}

// Returns 1 on the frame a slam hits a wall.
static int th_update(Thwomp *t, int pcx, int pcy) {
    if (t->state == T_IDLE) {
        t->flash = 0;
        // Aligned on same row or column with a clear view -> telegraph
        int same_col = (pcx >= t->x && pcx < t->x + TW);
        int same_row = (pcy >= t->y && pcy < t->y + TH);
        if (same_col || same_row) {
            int d = same_col ? ((pcy < t->y) ? 0 : 1) : ((pcx < t->x) ? 2 : 3);
            if (th_sees(t->x, t->y, pcx, pcy, d)) {
                t->dir = d;
                t->state = T_TELE;
                t->timer = 28;
                SFX_TELE();
            }
        }
    } else if (t->state == T_TELE) {
        t->timer--;
        t->flash = (t->timer / 4) & 1;
        if (t->timer <= 0) {
            t->flash = 0;
            t->state = T_SLAM;
        }
    } else if (t->state == T_SLAM) {
        static const s8 mx[4] = { 0, 0, -4, 4 };
        static const s8 my[4] = { -4, 4, 0, 0 };
        int ntx = t->x + mx[t->dir], nty = t->y + my[t->dir];
        if (th_blocked(ntx, nty, t->dir) || ntx < 0 || nty < 0 ||
            ntx + TW > GW * CELL || nty + TH > GH * CELL) {
            static const s8 ix[4] = { TW / 2, TW / 2, 0, TW };
            static const s8 iy[4] = { 0, TH, TH / 2, TH / 2 };
            t->state = T_RETRACT;
            t->timer = 20;
            SFX_SLAM();
            dust_burst(t->x + ix[t->dir], t->y + iy[t->dir], 8, 160);
            return 1;
        }
        t->x = ntx;
        t->y = nty;
        th_crush_sand(t->x, t->y);
    } else if (t->state == T_RETRACT) {
        t->timer--;
        // Ease back home
        if (t->x < t->hx) t->x += 2;
        if (t->x > t->hx) t->x -= 2;
        if (t->y < t->hy) t->y += 2;
        if (t->y > t->hy) t->y -= 2;
        th_crush_sand(t->x, t->y);
        if (t->x == t->hx && t->y == t->hy && t->timer <= 0) t->state = T_IDLE;
    }
    return 0;
}

#ifdef SPECK_UNLOCK_ALL
// Debug CPU meter: scanlines each frame's work took (228 = one full frame) and
// frames dropped (work ran past the next vblank). Shown on the pause screen.
static volatile u32 vbl_count;
static void vbl_isr(void) { vbl_count++; }
static int cpu_max_lines, cpu_drops;
static u32 frame_vbl;
#endif

int main(void) {
    REG_WAITCNT = WS_STANDARD; // faster cart access (3/1 waitstates + prefetch)
    REG_DISPCNT = DCNT_MODE3 | DCNT_BG2 | DCNT_OBJ | DCNT_OBJ_1D;
    // BG2 affine identity; BG2X/Y used for screen shake. Backdrop fills the exposed edge.
    REG_BG2PA = 256; REG_BG2PB = 0; REG_BG2PC = 0; REG_BG2PD = 256;
    pal_bg_mem[0] = COL_WALL;
    irq_init(NULL);
    irq_enable(II_VBLANK);
#ifdef SPECK_UNLOCK_ALL
    irq_add(II_VBLANK, vbl_isr);
#endif

    pal_obj_mem[0] = 0;
    pal_obj_mem[1] = RGB15(18, 31, 20); // speck body: mint, so it reads against yellow sand
    pal_obj_mem[2] = RGB15(4, 4, 8);    // eye
    pal_obj_mem[3] = RGB15(22, 8, 10);  // thwomp
    pal_obj_mem[4] = RGB15(31, 28, 20); // thwomp eye
    pal_obj_mem[5] = RGB15(31, 24, 4);  // telegraph flash
    pal_obj_mem[6] = RGB15(8, 26, 30);  // gem
    pal_obj_mem[7] = RGB15(28, 31, 31); // gem glint
    pal_obj_mem[8] = RGB15(26, 22, 14); // dust
    // Bank 1: same colors, thwomp body swapped for the telegraph flash.
    for (int i = 0; i < 16; i++) pal_obj_bank[1][i] = pal_obj_mem[i];
    pal_obj_bank[1][3] = RGB15(31, 22, 8);

    make_speck_tile();
    make_thwomp_tiles();
    make_gem_tile();
    make_dust_tile();
    sfx_init();
    music_init();

    // OAM: 0 Speck, 1..MAXTH thwomps, then gem, then dust.
    #define OBJ_TH   1
    #define OBJ_GEM  (OBJ_TH + MAXTH)
    #define OBJ_DUST (OBJ_GEM + 1)
    #define NOBJ     (OBJ_DUST + NDUST)
    OBJ_ATTR obj[NOBJ];
    oam_init(oam_mem, 128);
    oam_init(obj, NOBJ);
    // Speck 8x8
    obj_set_attr(&obj[0], ATTR0_SQUARE | ATTR0_4BPP, ATTR1_SIZE_8x8, 512);
    // Thwomp 16x16 starting at tile 1
    for (int i = 0; i < MAXTH; i++)
        obj_set_attr(&obj[OBJ_TH + i], ATTR0_SQUARE | ATTR0_4BPP | ATTR0_HIDE, ATTR1_SIZE_16x16, 513);
    // Goal gem 8x8
    obj_set_attr(&obj[OBJ_GEM], ATTR0_SQUARE | ATTR0_4BPP, ATTR1_SIZE_8x8, 517);
    for (int i = 0; i < NDUST; i++)
        obj_set_attr(&obj[OBJ_DUST + i], ATTR0_SQUARE | ATTR0_4BPP | ATTR0_HIDE, ATTR1_SIZE_8x8, 518);

    tte_init_bmp_default(3);
    save_load();
    cur_level = title_screen();
    int full_run = (cur_level == 0);

    int px, py, x, y;
    int vx = 0, vy = 0;
    int grounded = 0;
    int face = 1; // 1 right, -1 left
    int stamp_cd = 0;
    int coyote = 0, jbuf = 0;
    int jump_t = 0; // frames since the jump started, while it can still be cut short
    int death_t = 0;
    int win_t = 0;
    int deaths = 0;
    int run_frames = 0; // play time this run (pause excluded)
    int frame = 0;
    int shake_t = 0, shake_amp = 0;
    int shake_ox = 0, shake_oy = 0;

    // Build cur_level and put Speck at its spawn.
    #define START_LEVEL() do {                                 load_level();                                          redraw_all();                                          show_banner();                                         dust_clear();                                          stamp_cd = death_t = 0;                                x = (spawn_cx * CELL + 2) << FX;                       y = (spawn_cy * CELL) << FX;                           vx = vy = 0;                                           coyote = jbuf = 0;                                     obj[0].attr0 &= ~ATTR0_HIDE;                       } while (0)

    START_LEVEL();
    px = x >> FX;
    py = y >> FX;

    // First vblank copies OAM before the draw pass runs; start sprites in place.
    obj_set_pos(&obj[0], px - 2, py - 1);
    obj_set_pos(&obj[OBJ_GEM], goal_x, goal_y);

    while (1) {
        VBlankIntrWait();
#ifdef SPECK_UNLOCK_ALL
        frame_vbl = vbl_count;
#endif
        // Video regs/OAM written here, inside vblank. BG2X/Y latch per-frame; writing
        // them mid-frame restarts the affine line counter and tears the bitmap.
        REG_BG2X = shake_ox << 8;
        REG_BG2Y = shake_oy << 8;
        oam_copy(oam_mem, obj, NOBJ);
        if (banner_t > 0) {
            if (--banner_t == 0) redraw_all();
            else if ((banner_t & 7) == 0) draw_banner(); // sand falling through erases it
        }
        key_poll();

        // --- pause ---
        if (key_hit(KEY_START) && win_t == 0 && death_t == 0) {
            redraw_all(); // clear any banner under the text
            REG_DISPCNT &= ~DCNT_OBJ; // sprites would cover the menu text
            text_center(64, "PAUSED", CLR_WHITE);
            text_center(80, "SELECT: quit to title", COL_WALL2);
#ifdef SPECK_UNLOCK_ALL
            {
                char cbuf[40] = "cpu max ";
                str_int(cbuf, cpu_max_lines);
                strcat(cbuf, "/228 lines  drops ");
                str_int(cbuf, cpu_drops);
                text_center(100, cbuf, CLR_WHITE);
                cpu_max_lines = cpu_drops = 0; // measure afresh after resuming
            }
#endif
            int quit = 0;
            do {
                VBlankIntrWait();
                key_poll();
                REG_SND3CNT = 0; // hush the bass while paused
                if (key_hit(KEY_SELECT)) quit = 1;
            } while (!quit && !key_hit(KEY_START));
            banner_t = 0;
            if (quit) {
                shake_ox = shake_oy = shake_t = 0;
                REG_BG2X = REG_BG2Y = 0;
                cur_level = title_screen();
                REG_DISPCNT |= DCNT_OBJ;
                full_run = (cur_level == 0);
                deaths = run_frames = 0;
                START_LEVEL();
            } else {
                redraw_all();
                REG_DISPCNT |= DCNT_OBJ;
            }
            continue;
        }

        // --- sand ---
        sp_x = x >> FX;
        sp_y = y >> FX;
        {
            // Sand can't enter Speck's cells, so Speck moving to other cells may free it.
            static int last_box = -1;
            int box = (sp_x / CELL) | ((sp_x + PW - 1) / CELL) << 6 |
                      (sp_y / CELL) << 12 | ((sp_y + PH - 1) / CELL) << 18;
            if (box != last_box) {
                last_box = box;
                sand_wake();
            }
        }
        if (!sand_asleep) sand_asleep = !sand_step();

        frame++;
        run_frames++;
        music_tick(frame);

        // --- win: gem sparkle, then rebuild the level ---
        if (win_t > 0) {
            win_t--;
            pal_obj_mem[6] = (win_t & 4) ? RGB15(31, 31, 31) : RGB15(8, 26, 30);
            if (win_t == 0) {
                pal_obj_mem[6] = RGB15(8, 26, 30);
                if (cur_level == NLEVELS - 1) {
                    int nb_d = full_run && (save_best < 0 || deaths < save_best);
                    int nb_t = full_run && (save_time < 0 || run_frames < save_time);
                    if (nb_d) save_best = deaths;
                    if (nb_t) save_time = run_frames;
                    if (nb_d || nb_t) save_write();
                    ending_screen(deaths, run_frames, nb_d, nb_t);
                    deaths = 0;
                    run_frames = 0;
                    full_run = 1;
                }
                cur_level = (cur_level + 1) % NLEVELS;
                if (cur_level + 1 > save_unlocked) {
                    save_unlocked = cur_level + 1;
                    save_write();
                }
                START_LEVEL();
            }
            px = x >> FX;
            py = y >> FX;
            goto draw;
        }

        // --- player ---
        if (death_t > 0) {
            // Frozen + blinking, then respawn.
            death_t--;
            obj[0].attr0 = (death_t & 2) ? (obj[0].attr0 | ATTR0_HIDE)
                                         : (obj[0].attr0 & ~ATTR0_HIDE);
            if (death_t == 0) {
                obj[0].attr0 &= ~ATTR0_HIDE;
                x = (spawn_cx * CELL + 2) << FX;
                y = (spawn_cy * CELL) << FX;
                vx = vy = 0;
                coyote = jbuf = 0;
                clear_spawn(x >> FX, y >> FX);
            }
            px = x >> FX;
            py = y >> FX;
            goto thwomp;
        }

        int dir = key_tri_horz();
        if (dir) {
            face = dir;
            vx += dir * ACCEL;
            if (vx > MAXRUN) vx = MAXRUN;
            if (vx < -MAXRUN) vx = -MAXRUN;
        } else {
            if (vx > 0) {
                vx -= FRIC;
                if (vx < 0) vx = 0;
            } else if (vx < 0) {
                vx += FRIC;
                if (vx > 0) vx = 0;
            }
        }

        if (grounded) coyote = COYOTE;
        else if (coyote > 0) coyote--;
        if (key_hit(KEY_A)) jbuf = JBUF;
        else if (jbuf > 0) jbuf--;
        if (jbuf > 0 && coyote > 0) {
            vy = JUMPV;
            SFX_JUMP();
            grounded = 0;
            coyote = jbuf = 0;
            jump_t = 1;
        }
        // Variable height from the *held* state, not the release edge: a quick tap still
        // gives a real hop (JUMP_MIN frames of rise), and a press buffered before landing
        // that was already let go is cut the same way instead of jumping full height.
        if (jump_t && jump_t > JUMP_MIN && !key_held(KEY_A)) {
            if (vy < JUMP_CUT) vy = JUMP_CUT;
            jump_t = 0;
        }

        vy += GRAV;
        if (jump_t) jump_t++;
        if (vy >= 0) jump_t = 0; // past the apex: nothing left to cut
        // Terminal velocity must stay <= CELL px/frame or Speck tunnels through 1-cell platforms.
        if (vy > CELL * 256) vy = CELL * 256;

        // Move X
        int nx = x + vx;
        int ppx = nx >> FX, ppy = y >> FX;
        if (vx > 0 && (solid_px(ppx + PW - 1, ppy) || solid_px(ppx + PW - 1, ppy + PH - 1))) {
            nx = ((ppx + PW - 1) / CELL * CELL - PW) << FX;
            vx = 0;
        }
        if (vx < 0 && (solid_px(ppx, ppy) || solid_px(ppx, ppy + PH - 1))) {
            nx = ((ppx / CELL + 1) * CELL) << FX;
            vx = 0;
        }
        x = nx;

        // Move Y
        int ny = y + vy;
        ppx = x >> FX;
        ppy = ny >> FX;
        grounded = 0;
        if (vy > 0 && (solid_px(ppx, ppy + PH - 1) || solid_px(ppx + PW - 1, ppy + PH - 1))) {
            ny = ((ppy + PH - 1) / CELL * CELL - PH) << FX;
            if (vy >= 3 * 256) dust_burst(ppx + 1, (ny >> FX) + PH - 1, 4, 60);
            vy = 0;
            grounded = 1;
        }
        if (vy < 0 && (solid_px(ppx, ppy) || solid_px(ppx + PW - 1, ppy))) {
            ny = ((ppy / CELL + 1) * CELL) << FX;
            vy = 0;
        }
        y = ny;
        px = x >> FX;
        py = y >> FX;

        // Stamp sand (B)
        if (stamp_cd > 0) stamp_cd--;
        // Hold B to keep stamping; Down+B stamps under your feet.
        if (key_held(KEY_B) && stamp_cd == 0) {
            int down = key_held(KEY_DOWN);
            if (stamp_sand(px, py, face, down)) {
                SFX_STAMP();
                if (down) dust_burst(px + PW / 2, py + PH + 2, 3, 60);
                else dust_burst(px + PW / 2 + face * 5, py + 2, 3, 120);
            }
            stamp_cd = 10;
        }

        // Burial / crush respawn
        int dead = 0;
        if (player_buried(px, py)) dead = 1;
        if (cell_at_px(px, py) == SPIKE || cell_at_px(px + PW - 1, py) == SPIKE ||
            cell_at_px(px, py + PH - 1) == SPIKE || cell_at_px(px + PW - 1, py + PH - 1) == SPIKE)
            dead = 1;
        for (int i = 0; i < nth; i++)
            if (th[i].state == T_SLAM && overlap_thwomp(px, py, th[i].x, th[i].y)) dead = 1;
        if (dead || key_hit(KEY_SELECT)) {
            death_t = DEATH_FRAMES;
            deaths++;
            SFX_DIE();
            particle_burst(px + PW / 2, py + PH / 2, 10, 220, P_SPECK);
            shake_t = 8; shake_amp = 1;
            vx = vy = 0;
        } else if (key_held(KEY_L) && key_hit(KEY_R)) {
            win_t = 1;    // debug: skip to next level
            full_run = 0; // ...and forfeit this run's records
        } else if (overlap_gem(px, py)) {
            win_t = 60;
            SFX_GEM();
            particle_burst(goal_x + 4, goal_y + 4, 12, 260, P_GEM);
            vx = vy = 0;
        }

    thwomp:
        // --- thwomp AI ---
        int pcx = px + PW / 2;
        int pcy = py + PH / 2;
        for (int i = 0; i < nth; i++) {
            if (th_update(&th[i], pcx, pcy)) {
                shake_t = 10;
                shake_amp = 2;
            }
            // Stationary thwomp is a block (unless Speck is already inside it).
            int was_solid = th[i].solid;
            th[i].solid = (th[i].state == T_IDLE || th[i].state == T_TELE) &&
                          !overlap_thwomp(px, py, th[i].x, th[i].y);
            if (th[i].solid != was_solid) sand_wake(); // sand resting on it may now fall
        }

    draw:
        // Draw sprites (offset against BG shake)
        int ox = 0, oy = 0;
        if (shake_t > 0) {
            shake_t--;
            static const s8 jit[8] = { 1, -1, 0, 1, -1, 0, -1, 1 };
            ox = jit[frame & 7] * shake_amp;
            oy = jit[(frame + 3) & 7] * shake_amp;
        }
        shake_ox = ox;
        shake_oy = oy;
        for (int i = 0; i < MAXTH; i++) {
            OBJ_ATTR *o = &obj[OBJ_TH + i];
            if (i >= nth) {
                o->attr0 |= ATTR0_HIDE;
                continue;
            }
            const Thwomp *t = &th[i];
            o->attr0 &= ~ATTR0_HIDE;
            int tile = (t->state == T_TELE || t->state == T_SLAM) ? TH_TILE_DIR + t->dir * 4
                                                                  : TH_TILE_IDLE;
            o->attr2 = tile | ATTR2_PALBANK(t->flash ? 1 : 0); // bank 1 = telegraph flash
            obj_set_pos(o, t->x + t->flash - ox, t->y - oy);
        }
        // Speck: center the 8x8 art on the 4x5 hitbox
        int sx = px - 2, sy = py - 1;
        int spr = SPR_IDLE;
        if (!grounded && death_t == 0) spr = SPR_AIR;
        else if (vx > 64 || vx < -64) spr = ((frame >> 3) & 1) ? SPR_WALK : SPR_IDLE;
        else if ((frame & 127) < 5) spr = SPR_BLINK;
        obj[0].attr2 = spr;
        if (face < 0) obj[0].attr1 |= ATTR1_HFLIP;
        if (face > 0) obj[0].attr1 &= ~ATTR1_HFLIP;
        obj_set_pos(&obj[0], sx - ox, sy - oy);
        dust_update(&obj[OBJ_DUST], ox, oy);
        // Gem bobs gently
        obj_set_pos(&obj[OBJ_GEM], goal_x - ox, goal_y - oy + (((frame >> 4) & 1) ? 1 : 0));
#ifdef SPECK_UNLOCK_ALL
        {
            // Work began right after vblank (line ~160); each extra vblank since then
            // means the frame overran and the next one was dropped.
            u32 extra = vbl_count - frame_vbl;
            int lines = (int)extra * 228 + (REG_VCOUNT + 228 - 160) % 228;
            cpu_drops += extra;
            if (lines > cpu_max_lines) cpu_max_lines = lines;
        }
#endif
    }
}
