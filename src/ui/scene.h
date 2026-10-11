/* scene.h - the world behind the stage: a sunset drive. A sky from violet
 * dusk to an orange horizon, stars, a striped sun, a skyline with lit
 * windows, and a grid of neon that runs toward whoever is looking.
 *
 * After the first of the Sonulab pedal's three scenes (its ui/synth.c), cut
 * down to what a Cortex-M4 draws between two looks at its controls: whole
 * numbers only, one pass over the rows. It does not listen to the guitar:
 * the SHARC says nothing about the sound while it plays.
 *
 * scene_draw(t) fills the back buffer for time t in milliseconds; UI_YIELD() is called once a row. Include after fb.h. */
#ifndef UI_SCENE_H
#define UI_SCENE_H

#ifndef UI_YIELD
#define UI_YIELD() ((void)0)
#endif

#define SCENE_HZ  126                   /* the horizon's row */
#define SCENE_CX  (FB_W / 2)
#define SUN_CY    92
#define SUN_R     50
#define GRID_K    3000                  /* depth of a ground row: GRID_K / rows under the horizon */
#define GRID_CELL 30
#define N_STARS   48
#define N_BLDG    26

static uint16_t scene_sky[SCENE_HZ];
static struct { uint16_t x; uint8_t y, ph; } scene_star[N_STARS];
static struct { int16_t x; uint8_t w, h; uint32_t seed; } scene_bldg[N_BLDG];
static int scene_ready;

static uint16_t scene_mix(int r0, int g0, int b0, int r1, int g1, int b1, int k, int of) {
    return RGB565(r0 + (r1 - r0) * k / of, g0 + (g1 - g0) * k / of, b0 + (b1 - b0) * k / of);
}
/* a toward b, k parts in 16 */
static uint16_t scene_fade(uint16_t a, uint16_t b, int k) {
    int ar = a >> 11, ag = a >> 5 & 63, ab = a & 31, br = b >> 11, bg = b >> 5 & 63, bb = b & 31;
    return (uint16_t)((ar + (br - ar) * k / 16) << 11 | (ag + (bg - ag) * k / 16) << 5 | (ab + (bb - ab) * k / 16));
}
static uint32_t scene_rnd(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }
/* A sine by parabolas: phase 0..1023 a turn, -256..256. */
static int scene_sin(int ph) {
    int x = ph & 511, y = x * (512 - x) >> 8;
    return ph & 512 ? -y : y;
}
static int scene_isqrt(int v) {
    int r = 0;
    while ((r + 1) * (r + 1) <= v) ++r;
    return r;
}

static void scene_init(void) {
    for (int y = 0; y < SCENE_HZ; ++y) {            /* dark at the top, through wine, to a hot horizon */
        int k = y * 100 / SCENE_HZ;
        scene_sky[y] = k < 55 ? scene_mix(14, 4, 36, 150, 30, 70, k, 55) : scene_mix(150, 30, 70, 255, 120, 30, k - 55, 45);
    }
    uint32_t s = 0x5EEDu;
    for (int i = 0; i < N_STARS; ++i) {
        scene_star[i].x = (uint16_t)(scene_rnd(&s) % FB_W);
        scene_star[i].y = (uint8_t)(STAGE_HDR_H + scene_rnd(&s) % 50);
        scene_star[i].ph = (uint8_t)scene_rnd(&s);
    }
    int x = -12;
    for (int i = 0; i < N_BLDG; ++i) {
        scene_bldg[i].x = (int16_t)x;
        scene_bldg[i].w = (uint8_t)(10 + scene_rnd(&s) % 16);
        scene_bldg[i].h = (uint8_t)(6 + scene_rnd(&s) % 22);
        scene_bldg[i].seed = scene_rnd(&s);
        x += scene_bldg[i].w + (int)(scene_rnd(&s) % 5);
    }
    scene_ready = 1;
}

static void scene_draw(uint32_t t) {
    if (!scene_ready) scene_init();
    int sway = scene_sin((int)(t * 1024u / 18000u % 1024u));        /* a slow swing of the view, 18 s */
    int sun_cx = SCENE_CX + sway * 4 / 256, city = sway * 8 / 256;
    int stripe = (int)(t / 160u);                                   /* the sun's bars drift down */
    int dist = (int)(t * 60u / 1000u);                              /* how far the road has run */

    for (int y = 0; y < FB_H; ++y) {
        uint16_t *row = FB_BACK + y * FB_W;
        if (y < SCENE_HZ) {
            uint16_t sky = scene_sky[y];
            for (int x = 0; x < FB_W; ++x) row[x] = sky;
            int dy = y - SUN_CY;
            if (dy > -SUN_R && dy < SUN_R) {                        /* the sun: gold above, orange below, cut in bars */
                int p = dy + 20, gap = p > 0 && (p + stripe) % 10 < 1 + p / 11;
                if (!gap) {
                    int hw = scene_isqrt(SUN_R * SUN_R - dy * dy), x0 = sun_cx - hw, x1 = sun_cx + hw;
                    uint16_t c = scene_mix(255, 225, 80, 255, 90, 40, dy + SUN_R, 2 * SUN_R);
                    if (x0 < 0) x0 = 0;
                    if (x1 > FB_W) x1 = FB_W;
                    for (int x = x0; x < x1; ++x) row[x] = c;
                }
            }
        } else if (y < SCENE_HZ + 2) {
            for (int x = 0; x < FB_W; ++x) row[x] = RGB565(255, 170, 60);          /* the horizon's glow */
        } else {
            int d = y - SCENE_HZ, fog = d < 24 ? 4 + d / 2 : 16;                   /* lines fade toward the horizon */
            uint16_t ground = scene_fade(RGB565(60, 14, 60), RGB565(22, 6, 40), d > 32 ? 16 : d / 2);
            for (int x = 0; x < FB_W; ++x) row[x] = ground;
            /* a line across where a cell's edge falls inside this row's depth */
            int za = GRID_K * 16 / (2 * d + 1) * 2 + dist * 16, zb = GRID_K * 16 / (2 * d - 1) * 2 + dist * 16;
            if (za / (GRID_CELL * 16) != zb / (GRID_CELL * 16)) {
                uint16_t c = scene_fade(ground, RGB565(255, 90, 210), fog);
                for (int x = 0; x < FB_W; ++x) row[x] = c;
            } else {                                                                /* the lines toward the eye, fanning out */
                uint16_t c = scene_fade(ground, RGB565(255, 60, 200), fog);
                int step = 56 * 256 * d / (FB_H - SCENE_HZ), cx = (SCENE_CX - city) * 256;
                for (int k = -(cx / step) - 1; ; ++k) {
                    int x = (cx + k * step) >> 8;
                    if (x >= FB_W) break;
                    if (x >= 0) row[x] = c;
                    if (d > 44 && x + 1 >= 0 && x + 1 < FB_W) row[x + 1] = c;
                }
            }
        }
        UI_YIELD();
    }

    for (int i = 0; i < N_STARS; ++i) {                             /* stars, each at its own twinkle */
        int v = 110 + 100 * (256 + scene_sin((int)(t * 1024u / 2700u % 1024u) + scene_star[i].ph * 4)) / 512;
        int x = scene_star[i].x - city / 4;
        uint16_t *p = FB_BACK + scene_star[i].y * FB_W + x;
        if (x >= 0 && x < FB_W && *p == scene_sky[scene_star[i].y]) *p = RGB565(v, v, v);      /* not in front of the sun */
    }
    for (int i = 0; i < N_BLDG; ++i) {                              /* the skyline: dark towers, windows from each one's seed */
        int x0 = scene_bldg[i].x - city, w = scene_bldg[i].w, h = scene_bldg[i].h;
        fb_rect(x0, SCENE_HZ - h, w, h, RGB565(24, 4, 36));
        fb_rect(x0, SCENE_HZ - h, w, 1, RGB565(120, 160, 255));
        uint32_t s = scene_bldg[i].seed;
        for (int wy = SCENE_HZ - h + 3; wy < SCENE_HZ - 2; wy += 4)
            for (int wx = x0 + 2; wx < x0 + w - 2; wx += 3)
                if (scene_rnd(&s) % 5 < 2) fb_rect(wx, wy, 1, 2, RGB565(140, 190, 255));
    }
}

#endif
