/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly on fm1-base: the app's unity build (compiled into fm1-base's, after the platform) and the glue between
 * fm1-base's contract (app.h) and Polly's parts: the project (project.c), the pages (pages.c), the UI (ui.c), the
 * QY70 bank from the host (bank_rx.c) and the instrument (dsp/: the GM synth on Sonivox EAS, the arranger; their own
 * -O2 units). */
#ifdef PLAT_HOST
static int msg_too_wide;                 /* ui.c's say(): messages wider than the header (the simulator fails them) */
static uint32_t note_on_count, click_count;   /* the arranger's MIDI out: note-ons, the click's (host.c) */
#endif
#include "project.c"
#include "pages.c"
#include "ui.c"

_Static_assert(OBJ_NOBJ == 19 && OBJ_PAT0 == 3, "the store map lists every object (APP_STORE_MAP)");

/* the arranger's MIDI out (from the render): a USB-MIDI packet, cable 0, CIN = the status's high nibble; the room it
 * keeps for the note-offs it owes; a new connection (its sounds told again) */
void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
#ifdef PLAT_HOST
    note_on_count += (st & 0xF0u) == 0x90u && d2;
    click_count += st == 0x9Fu && d2;
#endif
    plat_midi_out(st >> 4 | st << 8 | d1 << 16 | d2 << 24);
}
uint32_t arr_midi_out_room(void) { return plat_midi_out_room(TO_USB); }
uint32_t arr_midi_out_link(void) { return plat_midi_out_link(); }

/* the QY70 bank's flash (dsp/qybank.c reads it in place) */
int qb_flash_read(uint32_t off, void *dst, uint32_t n)
{
    const uint8_t *p = plat_data(off), *e = n ? plat_data(off + n - 1u) : p;
    if (!p || !e || e != p + (n ? n - 1u : 0u))
        return -1;
    memcpy(dst, p, n);
    return 0;
}

/* ---- the QY70 bank written from the host (tools/qy70_install.py), cmd 0x40 */
#include "dsp/qybank.h"
#include "bank_rx.c"
static int bk_fetch(uint32_t addr, uint8_t *dst, uint32_t n) { return plat_host_read(addr, dst, n); }
static int bk_erase(uint32_t off)
{
    if (!QB_FLASH_OK(off, QB_SECTOR) || (off & 0xFFFu))
        return -8;
    return plat_data_erase(off);
}
static int bk_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!QB_FLASH_OK(off, n))
        return -8;
    return plat_data_write(off, src, n);
}
static int bk_read(uint32_t off, void *dst, uint32_t n) { return qb_flash_read(off, dst, n); }
static void bk_wait(uint32_t ms)                    /* (the main loop's: the screen stays a moment) */
{
#ifndef PLAT_HOST
    uint32_t t0 = plat_ms();
    while (plat_ms() - t0 < ms)
        ;
#else
    (void)ms;
#endif
}
static void bk_show(uint32_t pct)
{
    char b[8];
    uint32_t i = 0;
    if (pct >= 100u)
        b[i++] = '1';
    if (pct >= 10u)
        b[i++] = (char)('0' + pct / 10u % 10u);
    b[i++] = (char)('0' + pct % 10u);
    b[i++] = '%';
    b[i] = 0;
    if (!pct)
        lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, &FONT_S, "QY70 STYLES", C_WHITE, 1);
    draw_text_box(0, 124, 240, &FONT_S, b, C_HI, 1);
}
static void bk_begin(void)                          /* a bank: playback stops, the transfer at its own pace */
{
    arr_play(0);
    bk_wait(20);                                    /* the render takes the stop, then silence */
    gm_panic();
    plat_host_patience(5000u, 300000u);             /* 5 min at most (it takes some 30 s) */
}
static void bk_done(int code, int begun)            /* the result, to the host (and the screen, once begun) */
{
    uint8_t b[8];
    plat_host_read(code ? BK_FAIL_ADDR | (uint32_t)code : BK_OK_ADDR, b, 8);
    if (!begun)
        return;                                     /* refused at once: nothing stopped, nothing shown */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, &FONT_S, "QY70 STYLES", C_WHITE, 1);
    draw_text_box(0, 124, 240, &FONT_S, code ? "NOT WRITTEN" : "WRITTEN", C_HI, 1);
    ann_say(code ? "QY70 styles not written" : "QY70 styles written", 0, ANN_NOW | (code ? ANN_KEEP : 0u));
    bk_wait(1200);
}
void app_usb_cmd(const uint8_t *d, uint32_t n)
{
    if (n == 15u && d[2] == BK_REQ) {
        uint32_t len = bk_rd32(d + 6), crc = bk_rd32(d + 10);
        int begun = 0;
        plat_host_patience(300u, 3000u);            /* its directory first, playing on: a host that does not answer
                                                     * costs a second, not 15 (bk_begin: then patient) */
        bank_rx(len, crc, &begun);
        if (begun) {                                /* the flash was written: the bank again, the screen again */
            app_bank_changed();
            ui_init();
        } else
            ui_say("QY70 STYLES", "NOT WRITTEN");
    }
}

/* ---- the contract */
static uint8_t started;                             /* the songs and patterns given to the arranger (app_start) */
void app_init(void)
{
    gm_init();
    arr_init();
    project_load();                                 /* defaults when nothing (or another format) is saved */
    project_apply();
}
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12) { gm_render(lr, n, gain_q12); }
void app_panic(void) { gm_panic(); }
void app_ui_init(void)                              /* boot; again after an update that did not happen, a restore */
{
    ui_init();
    if (!started) {
        started = 1;
        app_start();
    }
}
void app_frame(void)
{
    plat_usb_audio_full(proj.usb_full);             /* USB AUDIO: follows the knob, or full */
    ui_frame();
}
void app_input_only(void) { ui_input_only(); }
void app_say(const char *a, const char *b) { ui_say(a, b); }
void app_redraw(void)                               /* every band drawn again at the next frame (pages.c) */
{
    for (uint32_t k = 0; k < 6u; k++)
        ui.sig[k] = ~ui.sig[k];
}
int app_mirror_test(int on) { return app_test(on); }
int app_backup_playing(void) { return arr_playing != 0; }
void app_backup_flush(void) { app_flush(); }
void app_backup_hold(int on) { app_hold_saves(on); }
void app_backup_restored(void) { app_restored(); }

#ifdef PLAT_HOST
#include "host.c"                                   /* the host simulator's part: Polly's checks and commands */
#endif
