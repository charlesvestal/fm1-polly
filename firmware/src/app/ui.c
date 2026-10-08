/* SPDX-License-Identifier: GPL-3.0-only */
/* GM UI: a General MIDI sound module. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too.
 *
 *   keys            play the selected channel          OCT- / OCT+   the keys' octave
 *   PRESETS         the channel's program (SELECT: a family of eight at a time)
 *   ALGORITHM       the channel (1..16; 10 is the drums)
 *   KNOB 1-4        the channel's volume, pan, reverb, chorus
 *   PLAY            a demo song (to hear it, and to load the synth)     HOME   all notes off
 *   GLO             lights           SAVE   saves (it also saves by itself, a few seconds after a change)
 * MIDI in (USB, TRS) plays all 16 channels. */
#include "gm_names.h"

static const char *const DRUM_NAME = "Drum kit";

#define K_BG RGB(250, 244, 232)
#define K_PANEL RGB(236, 228, 212)
#define K_LINE RGB(216, 205, 186)
#define K_DIM RGB(150, 136, 118)
#define K_TEXT RGB(72, 58, 48)
#define K_ACCENT RGB(242, 120, 72)
#define K_TEAL RGB(38, 166, 154)
#define K_TEAL_T RGB(196, 230, 222)

static struct {
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t dirty;
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint8_t key_note[NKEYS], key_ch[NKEYS];
    uint8_t held[GM_NCH];              /* notes held on each channel (keys, MIDI in, the demo) */
    uint32_t hit_t[GM_NCH];            /* the last note-on on each channel */
    uint8_t demo;
    uint32_t demo_t, demo_step;
    uint32_t sig[3];
} ui;

static void say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
}
void ui_say(const char *a, const char *b) { say(a, b); }

static char *itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static void mark_dirty(void)
{
    ui.dirty = 1;
    ui.act_t = plat_ms();
}
int ui_dirty(void) { return ui.dirty != 0; }

/* everything played goes through here, so the screen knows what each channel is doing */
static void note(uint32_t ch, uint32_t n, uint32_t vel)
{
    ch &= 15u;
    if (vel) {
        gm_midi(0x90u | ch, n, vel);
        if (ui.held[ch] < 255u)
            ui.held[ch]++;
        ui.hit_t[ch] = plat_ms();
    } else {
        gm_midi(0x80u | ch, n, 0);
        if (ui.held[ch])
            ui.held[ch]--;
    }
}

/* ------------------------------------------------------------- the demo --- */
/* a little four-bar song on five channels, to hear the set and to load the synth: drums, bass, piano
 * chords, strings, a lead; up to about 20 voices at once */
#define DEMO_STEP_MS 125u                       /* sixteenths at 120 */
static const uint8_t DEMO_PROG[5] = {0, 33, 0, 48, 80};   /* (drums), finger bass, piano, strings, square */
static const uint8_t DEMO_CH[5] = {9, 1, 2, 3, 4};
static const int8_t CHORD[4][4] = {{0, 4, 7, 11}, {-3, 0, 4, 7}, {-7, -3, 0, 4}, {-5, -1, 2, 7}};   /* C, Am, F, G */
static uint8_t demo_on[5][8], demo_n[5];

static void demo_off_ch(int part)
{
    for (int i = 0; i < demo_n[part]; i++)
        note(DEMO_CH[part], demo_on[part][i], 0);
    demo_n[part] = 0;
}
static void demo_on_note(int part, int n, int vel)
{
    note(DEMO_CH[part], (uint32_t)n, (uint32_t)vel);
    if (demo_n[part] < 8u)
        demo_on[part][demo_n[part]++] = (uint8_t)n;
}
static void demo_start(int on)
{
    int p;
    for (p = 0; p < 5; p++)
        demo_off_ch(p);
    ui.demo = (uint8_t)on;
    if (!on)
        return;
    for (p = 1; p < 5; p++) {
        gm_midi(0xC0u | DEMO_CH[p], DEMO_PROG[p], 0);
        gm_midi(0xB0u | DEMO_CH[p], 7, p == 4 ? 80 : 100);
    }
    ui.demo_step = 0;
    ui.demo_t = plat_ms();
}
static void demo_tick(void)
{
    uint32_t now = plat_ms();
    while (ui.demo && now - ui.demo_t >= DEMO_STEP_MS) {
        uint32_t s = ui.demo_step++, b = (s / 16u) % 4u, st = s % 16u;
        const int8_t *c = CHORD[b];
        ui.demo_t += DEMO_STEP_MS;
        demo_off_ch(0);                          /* drums: one shots */
        if (st % 4u == 0u)
            demo_on_note(0, 36, 110);
        if (st == 4u || st == 12u)
            demo_on_note(0, 38, 100);
        demo_on_note(0, st % 2u ? 42 : 44, st % 4u == 2u ? 90 : 60);
        if (st % 4u == 0u || st == 10u) {        /* bass */
            demo_off_ch(1);
            demo_on_note(1, 36 + c[0] + (st == 10u ? 7 : 0), 100);
        }
        if (st == 0u || st == 6u || st == 12u) { /* piano: the chord, three times a bar */
            demo_off_ch(2);
            for (int i = 0; i < 4; i++)
                demo_on_note(2, 60 + c[i], 80);
        }
        if (st == 0u) {                          /* strings: the bar long */
            demo_off_ch(3);
            for (int i = 0; i < 4; i++)
                demo_on_note(3, 48 + c[i], 70);
        }
        if (st % 2u == 0u) {                     /* the lead: an arpeggio */
            demo_off_ch(4);
            demo_on_note(4, 72 + c[(st / 2u) % 4u] + (st >= 8u ? 12 : 0), 70);
        }
    }
}

/* ------------------------------------------------------------ controls --- */
static void set_program(int p)
{
    uint32_t c = proj.ch;
    p &= 127;
    if (c == 9) {
        say("CHANNEL 10", "IS THE DRUMS");
        return;
    }
    proj.program[c] = (uint8_t)p;
    gm_midi(0xC0u | c, (uint32_t)p, 0);
    mark_dirty();
}
static void set_cc(uint8_t *a, uint32_t cc, int v)
{
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if (a[proj.ch] == (uint8_t)v)
        return;
    a[proj.ch] = (uint8_t)v;
    gm_midi(0xB0u | proj.ch, cc, (uint32_t)v);
    mark_dirty();
}
static uint8_t accel_off;              /* the host simulator's "spin": exact steps */
static int32_t accel(int role, int32_t s)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (!a || accel_off)
        return s;
    if (a > 1)
        dt /= a;
    m = dt < 15u ? 6u : dt < 30u ? 4u : dt < 60u ? 2u : 1u;
    return s * (int32_t)m;
}

static void keys_off(void)
{
    for (uint32_t i = 0; i < NKEYS; i++)
        if (ui.keys >> i & 1u)
            note(ui.key_ch[i], ui.key_note[i], 0);
    ui.keys = 0;
}

static void button(int b)
{
    switch (b) {
    case B_PLAY:
        demo_start(!ui.demo);
        say(ui.demo ? "DEMO" : "DEMO", ui.demo ? "PLAYING" : "STOPPED");
        break;
    case B_HOME:
        demo_start(0);
        gm_panic();
        memset(ui.held, 0, sizeof ui.held);
        say("ALL NOTES OFF", 0);
        break;
    case B_OCTDN: case B_OCTUP:
        proj.oct = (int8_t)(proj.oct + (b == B_OCTUP ? 1 : -1));
        proj.oct = proj.oct < -3 ? -3 : proj.oct > 3 ? 3 : proj.oct;
        {
            char t[8];
            t[0] = proj.oct < 0 ? '-' : '+';
            t[1] = (char)('0' + (proj.oct < 0 ? -proj.oct : proj.oct));
            t[2] = 0;
            say("OCTAVE", t);
        }
        mark_dirty();
        break;
    case B_GLO:
        proj.leds = (uint8_t)((proj.leds + 1u) % 3u);
        say("LIGHTS", proj.leds == 2 ? "NOTES" : proj.leds ? "ON" : "OFF");
        mark_dirty();
        break;
    case B_SAVE:
        if (project_save() == 0) {
            ui.dirty = 0;
            say("SAVED", 0);
        } else
            say("NOT SAVED", "FLASH ERROR");
        break;
    default: break;
    }
}

static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, t = st & 0xF0u;
        if (st < 0x80u || st >= 0xF0u)
            continue;                            /* clock, sysex: not for the synth */
        if (t == 0x90u)
            note(st, d1, d2);
        else if (t == 0x80u)
            note(st, d1, 0);
        else {
            gm_midi(st, d1, d2);
            if (t == 0xC0u && (st & 15u) != 9u)
                proj.program[st & 15u] = (uint8_t)d1;
            else if (t == 0xB0u) {
                uint32_t c = st & 15u;
                if (d1 == 7)
                    proj.volume[c] = (uint8_t)d2;
                else if (d1 == 10)
                    proj.pan[c] = (uint8_t)d2;
                else if (d1 == 91)
                    proj.reverb[c] = (uint8_t)d2;
                else if (d1 == 93)
                    proj.chorus[c] = (uint8_t)d2;
            }
        }
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            if (i == B_PLAY)
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY)
            button((int)i);
    }
    if ((btn & (1u << B_OCTDN | 1u << B_OCTUP)) == (1u << B_OCTDN | 1u << B_OCTUP))
        ui.btn_used |= 1u << B_OCTDN | 1u << B_OCTUP;   /* both: update mode (main loop), not ours */
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++) {
        if (!(ch >> i & 1u))
            continue;
        if (keys >> i & 1u) {
            int n = 53 + (int)i + 12 * proj.oct;     /* key 0 is F3 */
            n = n < 0 ? 0 : n > 127 ? 127 : n;
            ui.key_note[i] = (uint8_t)n;
            ui.key_ch[i] = proj.ch;
            note(proj.ch, (uint32_t)n, 100);
        } else
            note(ui.key_ch[i], ui.key_note[i], 0);
    }
    midi_in();
    if ((e = plat_enc(EN_PRESET)) != 0)
        set_program(proj.program[proj.ch] + (e > 0 ? 1 : -1));
    if ((e = plat_enc(EN_SELECT)) != 0)
        set_program(((proj.program[proj.ch] / 8) + (e > 0 ? 1 : -1)) * 8);
    if ((e = plat_enc(EN_ALGO)) != 0) {
        keys_off();
        proj.ch = (uint8_t)((proj.ch + (e > 0 ? 1 : 15)) & 15u);
        mark_dirty();
    }
    {
        static uint8_t *const A[4] = {proj.volume, proj.pan, proj.reverb, proj.chorus};
        static const uint8_t CC[4] = {7, 10, 91, 93};
        for (i = 0; i < 4; i++)
            if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
                set_cc(A[i], CC[i], A[i][proj.ch] + accel(EN_K1 + (int)i, e));
                ui.touched = (int8_t)i;
                ui.touch_until = plat_ms() + 1200u;
            }
    }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    demo_tick();
}

/* ------------------------------------------------------------- screen --- */
#define HDR_H 24
#define MAIN_Y 24
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static void draw_header(void)
{
    char t[24], *e;
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(2166136261u, gm_voices | (uint32_t)msg << 8 | (uint32_t)ui.dirty << 9 | (uint32_t)ui.demo << 10),
                      plat_cpu_pct());
    if (msg)
        h = hash(h, ui.msg_until);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(8, 4, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 4, &FONT_B, ui.msg[1], K_ACCENT);
    } else {
        int32_t x = cv_text(8, 4, &FONT_B, "GM", K_TEXT);
        if (ui.demo)
            cv_text(x + 8, 6, &FONT_XS, "DEMO", K_ACCENT);
        if (ui.dirty)
            cv_round(x + 46, 10, 5, 5, 2, K_DIM);
        e = itoa_u(gm_voices, t);
        e[0] = ' ';
        e[1] = 'v';
        e[2] = 0;
        cv_text(150 - text_w(&FONT_S, t), 4, &FONT_S, t, K_TEXT);
        e = itoa_u(plat_cpu_pct(), t);
        e[0] = '%';
        e[1] = 0;
        cv_text(158, 6, &FONT_XS, "CPU", K_DIM);
        cv_text(232 - text_w(&FONT_S, t), 4, &FONT_S, t, K_TEXT);
    }
    cv_blit(0, 0);
}

static void draw_main(void)
{
    uint32_t c = proj.ch, h = hash(2166136261u, c | (uint32_t)proj.program[c] << 4 | (uint32_t)proj.oct << 12), i,
             now = plat_ms();
    for (i = 0; i < GM_NCH; i++)
        h = hash(h, ui.held[i] | (now - ui.hit_t[i] < 150u) << 8 | (uint32_t)proj.program[i] << 9);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    {   /* the channel and its program */
        char t[16], *e;
        e = itoa_u(c + 1u, t + 3);
        (void)e;
        t[0] = 'C';
        t[1] = 'h';
        t[2] = ' ';
        cv_text(10, 4, &FONT_S, t, c == 9 ? K_ACCENT : K_TEAL);
        if (c == 9) {
            cv_text(10, 26, &FONT_L, DRUM_NAME, K_TEXT);
            cv_text(10, 64, &FONT_S, "GM drums: a sound on each key", K_DIM);
        } else {
            uint32_t p = proj.program[c];
            char n[8];
            n[0] = (char)('0' + (p + 1u) / 100u);
            n[1] = (char)('0' + (p + 1u) / 10u % 10u);
            n[2] = (char)('0' + (p + 1u) % 10u);
            n[3] = 0;
            cv_text(10, 26, &FONT_L, n, K_ACCENT);
            if (text_w(&FONT_L, GM_PROGRAM[p]) <= 160)
                cv_text(70, 26, &FONT_L, GM_PROGRAM[p], K_TEXT);
            else
                cv_text(70, 34, &FONT_B, GM_PROGRAM[p], K_TEXT);
            cv_text(10, 64, &FONT_S, GM_FAMILY[p / 8u], K_DIM);
        }
        if (proj.oct) {
            char o[12] = "keys oct ";
            o[9] = proj.oct < 0 ? '-' : '+';
            o[10] = (char)('0' + (proj.oct < 0 ? -proj.oct : proj.oct));
            o[11] = 0;
            cv_text(230 - text_w(&FONT_XS, o), 6, &FONT_XS, o, K_DIM);
        }
    }
    for (i = 0; i < GM_NCH; i++) {               /* the sixteen channels: lit while they play */
        int32_t x = 6 + (int32_t)i * 14, y = 92;
        int on = ui.held[i] || now - ui.hit_t[i] < 150u, sel = i == c;
        char n[4];
        itoa_u(i + 1u, n);
        if (sel)
            cv_round(x - 1, y - 1, 14, 46, 4, K_TEXT);
        cv_round(x, y, 12, 44, 3, on ? (i == 9 ? K_ACCENT : K_TEAL) : (sel ? K_PANEL : K_TEAL_T));
        text_c(x + 6, y + 28, &FONT_XS, n, on ? K_BG : K_DIM);
    }
    cv_blit(0, MAIN_Y);
}

static void draw_knobs(void)
{
    static const char *const NAME[4] = {"Volume", "Pan", "Reverb", "Chorus"};
    const uint8_t *A[4] = {proj.volume, proj.pan, proj.reverb, proj.chorus};
    uint32_t h = hash(2166136261u, proj.ch | (uint32_t)ui.touched << 8), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, A[i][proj.ch]);
    h = hash(h, (uint32_t)now_touch);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    cv_round(4, 2, 232, KNB_H - 6, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int32_t x = (int32_t)i * 60, cx = x + 30, v = A[i][proj.ch], bw = 44 * v / 127;
        int hot = now_touch && ui.touched == (int)i;
        char t[8];
        if (i)
            cv_rect(x, 14, 1, KNB_H - 30, K_LINE);
        text_c(cx, 6, &FONT_XS, NAME[i], hot ? K_TEXT : K_DIM);
        if (i == 1 && v != 64) {                 /* pan: L / C / R */
            t[0] = v < 64 ? 'L' : 'R';
            itoa_u((uint32_t)(v < 64 ? 64 - v : v - 64), t + 1);
        } else if (i == 1) {
            t[0] = 'C';
            t[1] = 0;
        } else
            itoa_u((uint32_t)v, t);
        text_c(cx, 22, &FONT_M, t, hot ? K_ACCENT : K_TEXT);
        cv_round(cx - 22, 50, 44, 6, 3, K_LINE);
        if (bw)
            cv_round(cx - 22, 50, bw < 6 ? 6 : bw, 6, 3, hot ? K_ACCENT : K_TEAL);
    }
    cv_blit(0, KNB_Y);
}

static void leds(void)
{
    uint32_t b = 0, k = 0;
    if (ui.demo)
        b |= 1u << B_PLAY;
    if (proj.leds)
        k = ui.keys;
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

#define AUTOSAVE_QUIET 4000u
static void autosave(void)
{
    if (!ui.dirty || ui.demo || ui.btn || ui.keys || gm_voices || plat_ms() - ui.act_t < AUTOSAVE_QUIET)
        return;
    if (project_save() == 0)
        ui.dirty = 0;
}

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    lcd_fill(0, 0, 240, 240, K_BG);
}

void ui_frame(void)
{
    input();
    autosave();
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }
