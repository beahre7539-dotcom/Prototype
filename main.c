/* Bloodrain-Chronicles of Bloods
 * لعبة مغامرات Pixel Art بسيطة لجهاز PSP (تعمل على المحاكي PPSSPP)
 * لا تحتاج أي ملفات صور: كل الرسومات مكتوبة داخل الكود.
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

PSP_MODULE_INFO("Bloodrain", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define FB_SIZE (BUF_W * SCR_H * 4)
#define TILE 16
#define MAP_W 30
#define MAP_H 17
#define NUM_SHARDS 3
#define MAX_ENEMIES 2
#define RAIN_N 80
#define RGB(r,g,b) (0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))

/* ---------- زر Home للخروج ---------- */
static int exit_callback(int a, int b, void *c) { sceKernelExitGame(); return 0; }
static int callback_thread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int t = sceKernelCreateThread("cb_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (t >= 0) sceKernelStartThread(t, 0, 0);
}

/* ---------- الشاشة (رسم مزدوج) ---------- */
static u32 *back;
static int draw_idx = 0;

static void flip(void) {
    sceDisplayWaitVblankStart();
    sceDisplaySetFrameBuf((void *)(0x04000000 + draw_idx * FB_SIZE), BUF_W,
                          PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
    draw_idx ^= 1;
    back = (u32 *)(0x44000000 + draw_idx * FB_SIZE);
}

static void rect(int x, int y, int w, int h, u32 c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        u32 *p = back + (y + j) * BUF_W + x;
        for (int i = 0; i < w; i++) p[i] = c;
    }
}

/* ---------- خط بكسلات 3x5 ---------- */
#define GL(a,b,c,d,e) (((a)<<12)|((b)<<9)|((c)<<6)|((d)<<3)|(e))
static const unsigned short FONT_AZ[26] = {
    GL(2,5,7,5,5), GL(6,5,6,5,6), GL(3,4,4,4,3), GL(6,5,5,5,6), GL(7,4,6,4,7),
    GL(7,4,6,4,4), GL(3,4,5,5,3), GL(5,5,7,5,5), GL(7,2,2,2,7), GL(1,1,1,5,2),
    GL(5,5,6,5,5), GL(4,4,4,4,7), GL(5,7,7,5,5), GL(6,5,5,5,5), GL(2,5,5,5,2),
    GL(6,5,6,4,4), GL(2,5,5,6,3), GL(6,5,6,5,5), GL(3,4,2,1,6), GL(7,2,2,2,2),
    GL(5,5,5,5,7), GL(5,5,5,5,2), GL(5,5,7,7,5), GL(5,5,2,5,5), GL(5,5,2,2,2),
    GL(7,1,2,4,7)
};
static const unsigned short FONT_09[10] = {
    GL(7,5,5,5,7), GL(2,6,2,2,7), GL(6,1,2,4,7), GL(6,1,2,1,6), GL(5,5,7,1,1),
    GL(7,4,6,1,6), GL(3,4,7,5,7), GL(7,1,2,2,2), GL(7,5,7,5,7), GL(7,5,7,1,6)
};

static int glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch >= 'A' && ch <= 'Z') return FONT_AZ[ch - 'A'];
    if (ch >= '0' && ch <= '9') return FONT_09[ch - '0'];
    switch (ch) {
        case '-': return GL(0,0,7,0,0);
        case '!': return GL(2,2,2,0,2);
        case ':': return GL(0,2,0,2,0);
        case '/': return GL(1,1,2,4,4);
        case '.': return GL(0,0,0,0,2);
    }
    return 0;
}
static void draw_char(char ch, int x, int y, int s, u32 col) {
    int g = glyph(ch);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++)
            if (g & (1 << (14 - (r * 3 + c)))) rect(x + c * s, y + r * s, s, s, col);
}
static int text_w(const char *t, int s) { int n = (int)strlen(t); return n ? n * 4 * s - s : 0; }
static void text(const char *t, int x, int y, int s, u32 col) {
    for (; *t; t++, x += 4 * s) draw_char(*t, x, y, s, col);
}
static void text_c(const char *t, int y, int s, u32 col) {
    int x = (SCR_W - text_w(t, s)) / 2, o = (s + 1) / 2;
    text(t, x + o, y + o, s, RGB(0, 0, 0));
    text(t, x, y, s, col);
}

/* ---------- سبرايتات Pixel Art بحجم 8x8 (تُكبَّر x2) ---------- */
static u32 pal(char c) {
    switch (c) {
        case 'k': return RGB(18, 10, 16);
        case 'r': return RGB(214, 32, 52);
        case 'R': return RGB(120, 12, 34);
        case 'w': return RGB(236, 228, 214);
        case 'b': return RGB(120, 74, 52);
        case 'B': return RGB(62, 36, 30);
        case 's': return RGB(112, 112, 128);
        case 'S': return RGB(66, 66, 84);
        case 'y': return RGB(238, 196, 156);
        case 'p': return RGB(96, 56, 130);
        case 'P': return RGB(52, 28, 74);
    }
    return RGB(255, 0, 255);
}
typedef const char *const Sprite[8];

static Sprite SPR_HERO_A = { "..BBBB..", ".BbbbbB.", ".ByyyyB.", ".BkyykB.", "..rrrr..", ".bbrrbb.", ".bbbbbb.", ".bb..bb." };
static Sprite SPR_HERO_B = { "..BBBB..", ".BbbbbB.", ".ByyyyB.", ".BkyykB.", "..rrrr..", ".bbrrbb.", ".bbbbbb.", "..bbbb.." };
static Sprite SPR_ENEMY_A = { "..PPPP..", ".PppppP.", "PprpprpP", "PppppppP", "PpwppwpP", "PppppppP", "Pp.pp.pP", "P..pp..P" };
static Sprite SPR_ENEMY_B = { "..PPPP..", ".PppppP.", "PprpprpP", "PppppppP", "PpwppwpP", "PppppppP", ".pPppPp.", "..P..P.." };
static Sprite SPR_SHARD = { "...rr...", "..rwrr..", ".rrwrrR.", ".rrrrRR.", ".rrrRRR.", "..rRRR..", "...RR...", "........" };
static Sprite SPR_WALL = { "ssssSsss", "ssssSsss", "SSSSSSSS", "sSssssss", "sSssssss", "SSSSSSSS", "ssssSsss", "ssssSsss" };
static Sprite SPR_TREE = { "B..BB..B", ".B.BB.B.", "..BBBB..", "...BB...", "...BB...", "...BB...", "..bBBb..", ".bbBBbb." };
static Sprite SPR_GATE = { "SSSSSSSS", "SkSkkSkS", "SkSkkSkS", "SkSrrSkS", "SkSrrSkS", "SkSkkSkS", "SkSkkSkS", "SSSSSSSS" };
static Sprite SPR_GATE_OPEN = { "SSSSSSSS", "SkkkkkkS", "SkkRRkkS", "SkRrrRkS", "SkRrrRkS", "SkkRRkkS", "SkkkkkkS", "SSSSSSSS" };

static void sprite(Sprite sp, int x, int y, int s, int flip) {
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++) {
            char c = sp[j][flip ? 7 - i : i];
            if (c != '.') rect(x + i * s, y + j * s, s, s, pal(c));
        }
}

/* ---------- الخريطة: # جدار | T شجرة | S شظية | G بوابة | P اللاعب | E عدو ---------- */
static const char *MAP_SRC[MAP_H] = {
    "##########" "##########" "##########",
    "#P.......T" ".........#" "......S..#",
    "#........." "..T......#" ".........#",
    "#..###...." ".........#" ".........#",
    "#..#......" ".........#" ".........#",
    "#..#..T..." ".........." ".....T...#",
    "#........." ".........." ".........#",
    "#.....###." "..T......." ".........#",
    "#.....#..." ".........." "....T....#",
    "#.....#..." "...E......" ".........#",
    "#.....#..." ".........." ".........#",
    "#.T......." "......###." ".........#",
    "#........." "......#..." "...T.....#",
    "#.S......." "......#..." ".........#",
    "#........." ".........." "......S..#",
    "#...T....." ".........." ".....E...#",
    "##########" "#####GG###" "##########"
};

/* ---------- حالة اللعبة ---------- */
enum { ST_TITLE, ST_PLAY, ST_WIN, ST_OVER };
typedef struct { int x, y, dx; } Enemy;

static int state = ST_TITLE;
static char map[MAP_H][MAP_W + 1];
static int px, py, start_x, start_y, face, anim, moving;
static int shards, lives, invuln, msg_timer;
static Enemy en[MAX_ENEMIES];
static int n_en;
static unsigned frame;
static struct { int x, y, s; } rain[RAIN_N];

static void reset_game(void) {
    n_en = 0;
    for (int y = 0; y < MAP_H; y++) {
        strcpy(map[y], MAP_SRC[y]);
        for (int x = 0; x < MAP_W; x++) {
            char c = map[y][x];
            if (c == 'P') { start_x = x * TILE; start_y = y * TILE; map[y][x] = '.'; }
            else if (c == 'E' && n_en < MAX_ENEMIES) {
                en[n_en].x = x * TILE; en[n_en].y = y * TILE;
                en[n_en].dx = (n_en & 1) ? -1 : 1;
                n_en++; map[y][x] = '.';
            }
        }
    }
    px = start_x; py = start_y;
    shards = 0; lives = 3; invuln = 0; msg_timer = 0; face = 0; anim = 0;
}

static int solid_at(int x, int y) {
    if (x < 0 || y < 0) return 1;
    int tx = x / TILE, ty = y / TILE;
    if (tx >= MAP_W || ty >= MAP_H) return 1;
    char t = map[ty][tx];
    if (t == '#' || t == 'T') return 1;
    if (t == 'G' && shards < NUM_SHARDS) return 1;
    return 0;
}
static int blocked(int x, int y) {
    return solid_at(x + 4, y + 4) || solid_at(x + 11, y + 4) ||
           solid_at(x + 4, y + 11) || solid_at(x + 11, y + 11);
}

static void init_rain(void) {
    for (int i = 0; i < RAIN_N; i++) {
        rain[i].x = rand() % SCR_W; rain[i].y = rand() % SCR_H; rain[i].s = 4 + rand() % 4;
    }
}
static void update_rain(void) {
    for (int i = 0; i < RAIN_N; i++) {
        rain[i].y += rain[i].s; rain[i].x -= 1;
        if (rain[i].y > SCR_H || rain[i].x < 0) {
            rain[i].x = rand() % (SCR_W + 40); rain[i].y = -(rand() % 20);
        }
    }
}
static void draw_rain(void) {
    for (int i = 0; i < RAIN_N; i++) {
        int near = rain[i].s >= 6;
        rect(rain[i].x, rain[i].y, 1, near ? 4 : 3, near ? RGB(220, 44, 64) : RGB(150, 26, 48));
    }
}

static void update_enemies(void) {
    for (int i = 0; i < n_en; i++) {
        int nx = en[i].x + en[i].dx;
        int ex = nx + (en[i].dx > 0 ? TILE - 1 : 0);
        if (solid_at(ex, en[i].y + 8)) en[i].dx = -en[i].dx;
        else en[i].x = nx;
    }
}

static void update_play(u32 b, int lx, int ly) {
    int dx = 0, dy = 0;
    if ((b & PSP_CTRL_LEFT) || lx < 70) dx = -1;
    else if ((b & PSP_CTRL_RIGHT) || lx > 186) dx = 1;
    if ((b & PSP_CTRL_UP) || ly < 70) dy = -1;
    else if ((b & PSP_CTRL_DOWN) || ly > 186) dy = 1;
    moving = (dx || dy);
    if (dx) face = (dx < 0);
    for (int i = 0; i < 2; i++) {            /* سرعة 2 بكسل في الإطار */
        if (dx && !blocked(px + dx, py)) px += dx;
        if (dy && !blocked(px, py + dy)) py += dy;
    }
    if (moving) anim++;

    int tx = (px + 8) / TILE, ty = (py + 8) / TILE;
    char t = map[ty][tx];
    if (t == 'S') {
        map[ty][tx] = '.'; shards++;
        msg_timer = (shards == NUM_SHARDS) ? 150 : 60;
    }
    if (t == 'G' && shards >= NUM_SHARDS) state = ST_WIN;

    update_enemies();

    if (invuln > 0) invuln--;
    else {
        for (int i = 0; i < n_en; i++) {
            if (abs(px - en[i].x) < 10 && abs(py - en[i].y) < 10) {
                lives--; invuln = 90; px = start_x; py = start_y;
                if (lives <= 0) state = ST_OVER;
                break;
            }
        }
    }
    if (msg_timer > 0) msg_timer--;
}

/* ---------- الرسم ---------- */
static void draw_ground(int tx, int ty) {
    int x = tx * TILE, y = ty * TILE;
    rect(x, y, TILE, TILE, ((tx + ty) & 1) ? RGB(24, 30, 36) : RGB(27, 34, 40));
    unsigned h = (unsigned)tx * 73856093u ^ (unsigned)ty * 19349663u;
    rect(x + (h % 14), y + ((h >> 4) % 14), 2, 1, RGB(44, 70, 56));
    rect(x + ((h >> 8) % 14), y + ((h >> 12) % 14), 1, 2, RGB(36, 54, 50));
    if (((h >> 16) % 9) == 0) {              /* بقعة دم */
        int ox = (h >> 3) % 9, oy = (h >> 6) % 9;
        rect(x + ox + 2, y + oy + 2, 5, 3, RGB(92, 10, 28));
        rect(x + ox + 3, y + oy + 2, 2, 1, RGB(150, 24, 44));
    }
}

static void draw_world(void) {
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            char t = map[ty][tx];
            int x = tx * TILE, y = ty * TILE;
            if (t == '#') { sprite(SPR_WALL, x, y, 2, 0); continue; }
            draw_ground(tx, ty);
            if (t == 'T') sprite(SPR_TREE, x, y, 2, 0);
            else if (t == 'S') {
                int bob = (frame / 12) & 1;
                rect(x + 2, y + 2, 12, 12, RGB(58, 14, 30));
                sprite(SPR_SHARD, x, y - bob, 2, 0);
            }
            else if (t == 'G') sprite(shards >= NUM_SHARDS ? SPR_GATE_OPEN : SPR_GATE, x, y, 2, 0);
        }
    for (int i = 0; i < n_en; i++)
        sprite(((frame / 10) & 1) ? SPR_ENEMY_B : SPR_ENEMY_A, en[i].x, en[i].y, 2, en[i].dx < 0);
    if (!(invuln && ((frame / 4) & 1))) {
        rect(px + 3, py + 14, 10, 2, RGB(10, 12, 16));
        sprite(((anim / 8) & 1) ? SPR_HERO_B : SPR_HERO_A, px, py, 2, face);
    }
    draw_rain();

    char buf[32];
    rect(0, 0, SCR_W, 16, RGB(10, 6, 12));
    sprintf(buf, "SHARDS %d/%d", shards, NUM_SHARDS);
    text(buf, 6, 3, 2, RGB(230, 60, 80));
    sprintf(buf, "LIVES %d", lives);
    text(buf, SCR_W - 6 - text_w(buf, 2), 3, 2, RGB(236, 228, 214));
    if (msg_timer > 0)
        text_c(shards >= NUM_SHARDS ? "THE GATE IS OPEN!" : "BLOOD SHARD FOUND!", 28, 2, RGB(255, 220, 120));
}

static void draw_bg(void) {
    for (int y = 0; y < SCR_H; y += 4)
        rect(0, y, SCR_W, 4, RGB(14 + y / 18, 6, 12 + y / 30));
}

static void draw_title(void) {
    draw_bg();
    sprite(SPR_SHARD, 150, 134, 4, 0);
    sprite(SPR_HERO_A, 216, 118, 6, 0);
    sprite(SPR_ENEMY_A, 298, 134, 4, 0);
    draw_rain();
    text_c("BLOODRAIN", 30, 8, RGB(210, 28, 50));
    text_c("CHRONICLES OF BLOODS", 84, 3, RGB(236, 228, 214));
    if ((frame / 30) & 1) text_c("PRESS START", 190, 3, RGB(255, 220, 120));
    text_c("COLLECT 3 BLOOD SHARDS", 225, 2, RGB(180, 180, 200));
    text_c("STICK OR D-PAD TO MOVE", 245, 2, RGB(180, 180, 200));
}
static void draw_end(int win) {
    draw_bg();
    draw_rain();
    if (win) {
        text_c("YOU ESCAPED!", 70, 6, RGB(236, 228, 214));
        text_c("THE BLOOD RAIN FADES...", 130, 2, RGB(210, 28, 50));
        text_c("TO BE CONTINUED", 165, 3, RGB(255, 220, 120));
    } else {
        text_c("GAME OVER", 70, 7, RGB(210, 28, 50));
        text_c("THE RAIN CLAIMS YOU", 140, 2, RGB(236, 228, 214));
    }
    if ((frame / 30) & 1) text_c("PRESS START", 215, 3, RGB(255, 220, 120));
}

int main(void) {
    setup_callbacks();
    sceDisplaySetMode(0, SCR_W, SCR_H);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    back = (u32 *)0x44000000;
    srand(12345);
    init_rain();
    reset_game();

    SceCtrlData pad;
    u32 prev = 0;
    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        u32 pressed = pad.Buttons & ~prev;
        prev = pad.Buttons;
        int go = pressed & (PSP_CTRL_START | PSP_CTRL_CROSS);
        frame++;
        update_rain();

        switch (state) {
            case ST_TITLE:
                if (go) { reset_game(); state = ST_PLAY; }
                draw_title();
                break;
            case ST_PLAY:
                update_play(pad.Buttons, pad.Lx, pad.Ly);
                draw_world();
                break;
            case ST_WIN:
                if (go) state = ST_TITLE;
                draw_end(1);
                break;
            case ST_OVER:
                if (go) state = ST_TITLE;
                draw_end(0);
                break;
        }
        flip();
    }
    sceKernelExitGame();
    return 0;
}
