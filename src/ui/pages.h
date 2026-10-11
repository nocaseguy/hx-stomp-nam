/* pages.h - the pedal's pages: what a control does on each, and what each
 * shows. Two layers, as on the Sonulab pedal whose UI this follows: the
 * STAGE, where feet work the chain and the knobs its values, and behind
 * ACTION one MENU with the pages that are set by hand.
 *
 *   footswitch 1 2 3    its block on or off, from any page (and back to the stage)
 *   a foot resting      on the stage: choose that block for the knobs
 *   upper knob, PAGE    stage: choose a block; lists: move
 *   upper knob push     stage: the chosen block on or off; lists: open or switch
 *   three knobs         stage: the chosen block's values; push: see the footer
 *   ACTION              the menu, and out of it
 *   VIEW                back
 *   volume knob         output level
 * The lower knob does nothing yet.
 *
 * Include after panel.h, fb.h, draw.h and scene.h. Whoever includes this
 * provides the four hw_ functions below and calls ui_init() once, then
 * ui_control() for each message from the panel and ui_draw() for each frame. */
#ifndef UI_PAGES_H
#define UI_PAGES_H

static void hw_apply(unsigned block);       /* a block's state to the SHARC */
static void hw_level(void);                 /* out_level to the SHARC */
static void hw_rings(void);                 /* the footswitch rings want sending again */
static void hw_loader(void);                /* back to the USB loader; does not return on the unit */

#define UI_MODEL "NAM A2-FULL"              /* the model built into the SHARC image */

enum ui_page { PG_STAGE, PG_MENU, PG_CONFIRM, PG_SYSTEM, PG_HELP };
enum { MI_SCENE, MI_RINGS, MI_RESET, MI_HELP, MI_SYSTEM, MI_LOADER, MI_N };
enum { RINGS_BRIGHT, RINGS_DIM, RINGS_OFF, RINGS_N };

struct ui {
    enum ui_page page;
    uint32_t now;                           /* milliseconds, as of the last ui_draw() */
    uint32_t held;                          /* switches down right now, bit = its SW_ number */
    int lit_block, lit_val; uint32_t lit_at;    /* the value last turned, and when */
    char msg[64]; uint32_t msg_at; int msg_warn, msg_live;
    int menu_cur, menu_first, help_line, confirm;   /* confirm: the menu row being asked about */
    int scene, rings;                       /* settings: the scene shown; RINGS_ */
    int16_t start[BLOCKS][3];               /* the values as they began */
    int upper;                              /* upper-knob steps not yet used */
    /* what the image knows, for the stage's header and SYSTEM */
    int dsp_up, dsp_load, arm_load, fps;    /* loads in per cent, -1 not known */
    uint8_t last[3]; uint32_t messages;     /* the last panel message: kind, control, value; how many so far */
};

/* the unit written after each value on the stage */
static const char *const ui_unit[BLOCKS][3] = { { "", "", "" }, { "", "", "" }, { "ms", "%", "%" } };

static void ui_say(struct ui *u, int warn, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    fb_vfmt(u->msg, sizeof u->msg, f, ap);
    va_end(ap);
    u->msg_at = u->now; u->msg_warn = warn; u->msg_live = 1;
}
static int ui_msg_live(const struct ui *u) {
    return u->msg_live && u->msg[0] && u->now - u->msg_at < draw_fit_time(u->msg);
}

static void ui_init(struct ui *u, int dsp_up) {
    static const struct ui none;
    *u = none;
    u->scene = 1; u->lit_block = -1;
    u->dsp_up = dsp_up; u->dsp_load = u->arm_load = -1;
    for (unsigned b = 0; b < BLOCKS; ++b)
        for (unsigned i = 0; i < 3; ++i) u->start[b][i] = chain[b].val[i].v;
    if (!dsp_up) ui_say(u, 1, "the DSP did not start: no sound");
}

/* ---- what the pages share ---- */
static void ui_toggle(struct ui *u, unsigned b) {
    chain[b].on = !chain[b].on;
    hw_apply(b);
    hw_rings();
    ui_say(u, 0, "%s %s", chain[b].name, chain[b].on ? "on" : "off");
}

static void ui_reset_all(struct ui *u) {
    for (unsigned b = 0; b < BLOCKS; ++b) {
        for (unsigned i = 0; i < chain[b].values; ++i) chain[b].val[i].v = u->start[b][i];
        hw_apply(b);
    }
    ui_say(u, 0, "every value is back where it began");
}

static void ui_set(struct ui *u, unsigned b, unsigned i, int v) {
    struct value *p = &chain[b].val[i];
    v = v > p->max ? p->max : v < p->min ? p->min : v;
    u->lit_block = (int)b; u->lit_val = (int)i; u->lit_at = u->now;
    if (v == p->v) return;
    p->v = (int16_t)v;
    hw_apply(b);
}

/* Upper-knob steps as moves of a cursor: `per` steps a move. */
static int ui_upper(struct ui *u, int steps, int per) {
    u->upper += steps;
    int moves = u->upper / per;
    u->upper -= moves * per;
    return moves;
}

/* ---- STAGE ---- */
static void stage_control(struct ui *u, unsigned kind, unsigned id, int steps, unsigned down) {
    if (kind == K_KNOB && id <= KN_3) {
        if (id < chain[chosen].values) ui_set(u, chosen, id, chain[chosen].val[id].v + steps * chain[chosen].val[id].step);
    } else if (kind == K_KNOB && id == KN_UPPER) {
        int m = ui_upper(u, steps, 4);              /* four steps a block: three boxes are a short walk */
        if (m) chosen = (chosen + (m > 0 ? 1u : BLOCKS - 1u)) % BLOCKS;
    } else if (kind == K_SWITCH && down) {
        switch (id) {
        case SW_KNOB1: case SW_KNOB2: case SW_KNOB3:
            if (id < chain[chosen].values) ui_set(u, chosen, id, u->start[chosen][id]);
            break;
        case SW_PAGE_L: chosen = (chosen + BLOCKS - 1) % BLOCKS; break;
        case SW_PAGE_R: chosen = (chosen + 1) % BLOCKS; break;
        case SW_UPPER:  ui_toggle(u, chosen); break;
        case SW_TOUCH1: case SW_TOUCH2: case SW_TOUCH3: chosen = id - SW_TOUCH1; break;
        case SW_ACTION: u->page = PG_MENU; break;
        }
    }
}

static void stage_draw(struct ui *u) {
    if (u->scene) scene_draw(u->now); else fb_rect(0, 0, FB_W, FB_H, C_BG);

    /* the header: one line. What plays, in the headline face; to its right
     * the two numbers a player glances at, and the way to the menu */
    fb_shade(0, 0, FB_W, STAGE_HDR_H, C_BG);
    if (u->dsp_up) draw_fit_disp(6, 2, 176, C_FG, UI_MODEL);
    else fb_text_disp(6, 2, DISP_TITLE, C_ERR, "NO DSP");
    {
        char t[8];
        if (u->dsp_load < 0) fb_fmt(t, sizeof t, " --"); else fb_fmt(t, sizeof t, "%3d", u->dsp_load);
        fb_text(188, 2, C_DIM, C_NONE, "DSP");
        fb_text(220, 2, u->dsp_load > 97 ? C_ERR : C_FG, C_NONE, "%s%%", t);
        if (volume < 0) fb_fmt(t, sizeof t, " --"); else fb_fmt(t, sizeof t, "%3d", volume);
        fb_text(260, 2, C_DIM, C_NONE, "VOL");
        fb_text(292, 2, C_FG, C_NONE, "%s", t);
        fb_text(220, 14, C_DIM, C_NONE, "ACTION: menu");
    }
    if (ui_msg_live(u)) draw_notice(STAGE_HDR_H, u->msg, u->msg_warn, 1);

    /* the chain: a box a block, in footswitch order, joined left to right */
    const int y = 142, h = 52, w = 92, pitch = 108, x0 = 6;
    fb_rect(0, y + h / 2 - 1, FB_W, 2, C_DIM);
    for (unsigned b = 0; b < BLOCKS; ++b) {
        int x = x0 + (int)b * pitch, on = chain[b].on;
        uint16_t col = on ? block_col[b] : C_DIM;
        if (b == chosen) fb_rect(x, y, w, h, C_SEL);
        else { fb_rect(x, y + h / 2 - 1, w, 2, C_BG); fb_shade(x, y, w, h, C_BG); fb_shade(x, y, w, h, C_BG); }
        fb_rect(x, y, w, 2, col); fb_rect(x, y + h - 2, w, 2, col);
        fb_rect(x, y, 2, h, col); fb_rect(x + w - 2, y, 2, h, col);
        char n[2] = { (char)('1' + b), 0 };
        fb_text_disp(x + 8, y + 5, DISP_TITLE, col, "%s", n);
        fb_text_disp(x + 8 + fb_disp_width(DISP_TITLE, n) + 8, y + 5, DISP_TITLE, on ? C_FG : C_DIM, "%s", chain[b].name);
        fb_text(x + 9, y + 34, on ? C_FG : C_DIM, C_NONE, "%s", on ? "ON" : "OFF");
    }

    /* the chosen block's values, each over its knob, and under them what a
     * push of that knob does: put a changed value back */
    const struct block *k = &chain[chosen];
    const char *key[KEYS] = { "", "", "" };
    for (unsigned i = 0; i < KEYS; ++i) {
        char t[20] = "";
        const struct value *p = &k->val[i];
        if (i < k->values) {
            if (p->tenths && !p->v) fb_fmt(t, sizeof t, "%s 0%s", p->name, ui_unit[chosen][i]);
            else if (p->tenths) fb_fmt(t, sizeof t, "%s %+t%s", p->name, p->v, ui_unit[chosen][i]);
            else fb_fmt(t, sizeof t, "%s %d%s", p->name, p->v, ui_unit[chosen][i]);
            if (p->v != u->start[chosen][i]) key[i] = "reset";
        }
        draw_cell((int)i, FOOTER_Y - 18, t, p->v, p->min, p->max,
                  u->lit_block == (int)chosen && u->lit_val == (int)i && u->now - u->lit_at < 2000);
    }
    draw_keys(key[0], key[1], key[2], -1, 0);
}

/* ---- MENU ---- */
static const char *const menu_label[MI_N] = { "Scene", "Footswitch rings", "Reset all values", "Help", "System", "USB loader" };
static const char *const rings_word[RINGS_N] = { "bright", "dim", "off" };

static void menu_control(struct ui *u, unsigned kind, unsigned id, int steps, unsigned down) {
    int move = kind == K_KNOB && id == KN_UPPER ? ui_upper(u, steps, 1) : 0;
    if (kind == K_SWITCH && down && id == SW_PAGE_L) move = -1;
    if (kind == K_SWITCH && down && id == SW_PAGE_R) move = 1;
    u->menu_cur += move;
    if (u->menu_cur < 0) u->menu_cur = 0;
    if (u->menu_cur >= MI_N) u->menu_cur = MI_N - 1;
    if (kind != K_SWITCH || !down) return;
    if (id == SW_KNOB1 || id == SW_VIEW || id == SW_ACTION) { u->page = PG_STAGE; return; }
    if (id != SW_UPPER && id != SW_KNOB3) return;
    switch (u->menu_cur) {
    case MI_SCENE:  u->scene = !u->scene; break;
    case MI_RINGS:  u->rings = (u->rings + 1) % RINGS_N; hw_rings(); break;
    case MI_RESET: case MI_LOADER: u->confirm = u->menu_cur; u->page = PG_CONFIRM; break;
    case MI_HELP:   u->page = PG_HELP; break;       /* opens where it was left */
    case MI_SYSTEM: u->page = PG_SYSTEM; break;
    }
}

static void menu_draw(struct ui *u) {
    draw_page("MENU", "upper knob: move, push: open");
    const char *val[MI_N] = { u->scene ? "on" : "off", rings_word[u->rings], ">", ">", ">", ">" };
    /* the list moves down under a notice, so the cursor's row is never covered */
    int live = ui_msg_live(u), y0 = BODY_Y + 2 + (live ? 14 : 0), show = (FOOTER_Y - y0) / ROW;
    if (u->menu_cur < u->menu_first) u->menu_first = u->menu_cur;
    if (u->menu_cur >= u->menu_first + show) u->menu_first = u->menu_cur - show + 1;
    for (int i = u->menu_first, y = y0; i < MI_N && i < u->menu_first + show; ++i, y += ROW)
        draw_row(y, i == u->menu_cur, menu_label[i], val[i], val[i][0] != '>' && val[i][1] != 'f');     /* not a way in, not "off" */
    draw_scrollbar(y0, show, MI_N, u->menu_first);
    if (live) draw_notice(BODY_Y, u->msg, u->msg_warn, 0);
    /* the right box says what the push does on this row: a page opens, a setting switches */
    draw_keys("back", "", u->menu_cur == MI_SCENE || u->menu_cur == MI_RINGS ? "switch" : "open", -1, 0);
}

/* ---- CONFIRM: what is about to happen, in words, and one box that does it.
 * It is the box that opened this page; the upper knob never goes ahead. ---- */
static void confirm_control(struct ui *u, unsigned kind, unsigned id, unsigned down) {
    if (kind != K_SWITCH || !down) return;
    if (id == SW_KNOB1 || id == SW_VIEW) u->page = PG_MENU;
    else if (id == SW_ACTION) u->page = PG_STAGE;
    else if (id == SW_KNOB3) {
        u->page = PG_STAGE;
        if (u->confirm == MI_RESET) ui_reset_all(u); else hw_loader();
    }
}

static void confirm_draw(struct ui *u) {
    int y = BODY_Y + 10;
    if (u->confirm == MI_RESET) {
        draw_page("RESET?", "every value of every block");
        fb_text(4, y, C_FG, C_NONE, "Gain, the three bands and the delay"); y += ROW;
        fb_text(4, y, C_FG, C_NONE, "go back to where they began."); y += 2 * ROW;
        fb_text(4, y, C_DIM, C_NONE, "Which blocks are on stays as it is.");
        draw_keys("keep", "", "RESET", 2, (int)(u->now / 400 & 1));
    } else {
        draw_page("USB LOADER?", "hand the pedal back to the computer");
        fb_text(4, y, C_ERR, C_NONE, "The sound stops."); y += 2 * ROW;
        fb_text(4, y, C_FG, C_NONE, "The pedal waits on USB for a firmware"); y += ROW;
        fb_text(4, y, C_FG, C_NONE, "image. To play again: load one, or"); y += ROW;
        fb_text(4, y, C_FG, C_NONE, "switch the pedal off and on.");
        draw_keys("stay", "", "LOADER", 2, (int)(u->now / 400 & 1));
    }
}

/* ---- SYSTEM: what the pedal measures ---- */
static void system_draw(struct ui *u) {
    int y = BODY_Y + 2;
    draw_page("SYSTEM", "what the pedal measures");
#define SYS(label) fb_text(4, y, C_DIM, C_NONE, label)
    SYS("dsp");
    if (!u->dsp_up) fb_text(84, y, C_ERR, C_NONE, "did not start");
    else if (u->dsp_load < 0) fb_text(84, y, C_FG, C_NONE, "running");
    else fb_text(84, y, u->dsp_load > 97 ? C_ERR : C_FG, C_NONE, "running, %d%% of a sample", u->dsp_load);
    y += ROW;
    SYS("model"); fb_text(84, y, C_FG, C_NONE, "%s, 48 kHz", UI_MODEL); y += ROW;
    SYS("chain");
    fb_text(84, y, C_FG, C_NONE, "%3s %-3s %3s %-3s %3s %-3s", chain[0].name, chain[0].on ? "on" : "off",
            chain[1].name, chain[1].on ? "on" : "off", chain[2].name, chain[2].on ? "on" : "off");
    y += ROW;
    SYS("volume");
    if (volume < 0) fb_text(84, y, C_DIM, C_NONE, "not known: turn its knob"); else fb_text(84, y, C_FG, C_NONE, "%d of 100", volume);
    y += ROW;
    SYS("arm");
    if (u->arm_load < 0) fb_text(84, y, C_DIM, C_NONE, "not measured"); else fb_text(84, y, C_FG, C_NONE, "%d%% busy, %d frames/s", u->arm_load, u->fps);
    y += ROW;
    SYS("panel");
    if (!u->messages) fb_text(84, y, C_DIM, C_NONE, "nothing heard yet");
    else fb_text(84, y, C_FG, C_NONE, "%u heard, last %02x %02x %02x", (int)u->messages, u->last[0], u->last[1], u->last[2]);
    y += ROW;
    SYS("screen"); fb_text(84, y, C_FG, C_NONE, "%d x %d, 16 bits", FB_W, FB_H); y += ROW;
    SYS("uptime"); fb_text(84, y, C_FG, C_NONE, "%u s", (int)(u->now / 1000)); y += ROW;
#undef SYS
    draw_keys("back", "", "", -1, 0);
}

/* ---- HELP: the guide, on the pedal. A leading '#' makes a heading. ---- */
static const char *const help[] = {
    "#WHAT THIS IS",
    "A NAM amp model on the HX Stomp's own",
    "DSP, with an equaliser and a delay",
    "after it. This firmware runs from",
    "memory: switch the pedal off and on",
    "and the maker's firmware is back.",
    "",
    "#THE CHAIN",
    "in > 1 AMP > 2 EQ > 3 DLY > volume >",
    "out. Each block is a box on the stage:",
    "its footswitch number in its colour,",
    "its name, ON or OFF. The rings round",
    "the footswitches wear the same",
    "colours: bright is on, dim is off.",
    "",
    "#FEET",
    "A footswitch turns its block on or",
    "off, from any page. A foot resting on",
    "one chooses that block for the knobs.",
    "",
    "#HANDS",
    "Upper knob or PAGE < >: choose a",
    "block. Upper knob push: that block on",
    "or off. The three knobs under the",
    "screen turn the chosen block's values,",
    "written above them; the one just",
    "turned is lit. The boxes at the bottom",
    "say what pushing each knob does here;",
    "an empty box does nothing. On the",
    "stage a push puts a changed value back",
    "where it began. The big knob is the",
    "output volume.",
    "",
    "#VALUES",
    "AMP gain: dB into the model, +-20.",
    "EQ bass, mid, treble: dB at 120 Hz,",
    "700 Hz and 3.5 kHz, +-12. DLY time in",
    "ms, feedback and mix in per cent.",
    "With AMP off the guitar goes past",
    "the model untouched.",
    "",
    "#MENU",
    "ACTION opens it and closes it. Upper",
    "knob: move; push: open or switch.",
    "VIEW or the left knob: back.",
    "Nothing is kept when the power goes:",
    "the values start again as they were.",
    "",
    "#USB LOADER",
    "Menu > USB loader hands the pedal back",
    "to the computer for a new firmware",
    "image. The sound stops.",
};
#define HELP_N ((int)(sizeof help / sizeof help[0]))
#define HELP_ROWS ((FOOTER_Y - BODY_Y - 2) / ROW)

static void help_control(struct ui *u, unsigned kind, unsigned id, int steps, unsigned down) {
    int move = kind == K_KNOB && id == KN_UPPER ? 3 * ui_upper(u, steps, 1) : 0;
    if (kind == K_SWITCH && down && id == SW_PAGE_L) move = -HELP_ROWS;
    if (kind == K_SWITCH && down && id == SW_PAGE_R) move = HELP_ROWS;
    u->help_line += move;
    if (u->help_line > HELP_N - HELP_ROWS) u->help_line = HELP_N - HELP_ROWS;
    if (u->help_line < 0) u->help_line = 0;
}

static void help_draw(struct ui *u) {
    char sub[40];
    fb_fmt(sub, sizeof sub, "upper knob or PAGE to read  (%d%%)",
           HELP_N > HELP_ROWS ? 100 * u->help_line / (HELP_N - HELP_ROWS) : 100);
    draw_page("HELP", sub);
    for (int i = u->help_line, y = BODY_Y + 2; i < HELP_N && i < u->help_line + HELP_ROWS; ++i, y += ROW)
        fb_text(4, y, help[i][0] == '#' ? C_MAG : C_FG, C_NONE, "%s", help[i] + (help[i][0] == '#'));
    draw_scrollbar(BODY_Y + 2, HELP_ROWS, HELP_N, u->help_line);
    draw_keys("back", "", "", -1, 0);
}

/* ---- every page ---- */
/* A message from the panel: kind, control and value as the controller sends
 * them (panel.h). */
static void ui_control(struct ui *u, unsigned kind, unsigned id, unsigned value) {
    int steps = (int8_t)value;
    u->last[0] = (uint8_t)kind; u->last[1] = (uint8_t)id; u->last[2] = (uint8_t)value;
    ++u->messages;
    if (kind == K_VOLUME) {
        volume = value > 100 ? 100 : (int)value;
        out_level = (float)(volume * volume) / 10000.0f;    /* 0 is silent, 50 is the 0.25 it starts at */
        hw_level();
        return;
    }
    if (kind == K_SWITCH && id < 32) u->held = value ? u->held | 1u << id : u->held & ~(1u << id);
    if (kind == K_SWITCH && value && id >= SW_FS1 && id <= SW_FS3) {    /* a foot always does its one thing */
        u->page = PG_STAGE;
        ui_toggle(u, id - SW_FS1);
        return;
    }
    enum ui_page was = u->page;
    switch (u->page) {
    case PG_STAGE:   stage_control(u, kind, id, steps, value); break;
    case PG_MENU:    menu_control(u, kind, id, steps, value); break;
    case PG_CONFIRM: confirm_control(u, kind, id, value); break;
    case PG_HELP:    help_control(u, kind, id, steps, value); /* and its ways out: */
    /* fall through */
    case PG_SYSTEM:
        if (kind == K_SWITCH && value && (id == SW_KNOB1 || id == SW_VIEW)) u->page = PG_MENU;
        if (kind == K_SWITCH && value && id == SW_ACTION) u->page = PG_STAGE;
        break;
    }
    if (u->page != was) {
        u->upper = 0;
        if (u->page != PG_STAGE && was != PG_STAGE) u->msg_live = 0;    /* a notice does not follow to another page; the stage keeps its own */
    }
}

/* The whole screen into the back buffer, for time `now` in milliseconds. */
static void ui_draw(struct ui *u, uint32_t now) {
    u->now = now;
    draw_begin(now, u->held >> SW_KNOB1 & 7u);
    switch (u->page) {
    case PG_STAGE:   stage_draw(u); break;
    case PG_MENU:    menu_draw(u); break;
    case PG_CONFIRM: confirm_draw(u); break;
    case PG_SYSTEM:  system_draw(u); break;
    case PG_HELP:    help_draw(u); break;
    }
}

#endif
