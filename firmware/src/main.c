/* SPDX-License-Identifier: GPL-3.0-only */
/* POLLY firmware for the M-VAVE FM-1: one compilation unit for the platform and the app
 * (Felucca's layout, as X0X: the HAL is header-only, so everything that touches it is here).
 * The instrument (dsp/gm.c, dsp/eas/) is a separate unit compiled at -O2 by tools/build.py; this one is
 * -Os. Order matters. */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "fm1_flash.h"
#include "fm1_xip.h"
#include "fm1_perf.h"

#include "libc.c"
#include "lcd.c"
#include "gfx.c"

static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR) */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

#include "app/plat.h"

#define FELUCCA_OTA 1             /* M-UPGRADE update entry: keeps the web installer working */
#ifndef GM_CDC
#define GM_CDC 0                  /* USB serial console off: one plain MIDI interface */
#endif
#define FELUCCA_CDC GM_CDC
#ifndef GM_UAC
#define GM_UAC 1                  /* USB audio input: the master output, recordable over the cable */
#endif
#define FELUCCA_UAC GM_UAC
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_300"     /* package identity (tools/build.py) */
#endif
#include "usb.c"
#ifndef GM_TRS
#define GM_TRS 1                  /* MIDI IN on the TRS jack */
#endif
#if GM_TRS
#include "midi_uart.c"
#endif

static uint8_t flash_ok;          /* JEDEC id matched at boot */
static void audio_silence(void);
static int st_read(uint32_t off, void *dst, uint32_t n)   /* 256-byte IRQ-off windows: audio keeps up */
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static int st_erase(uint32_t off)
{
    uint32_t took;
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    audio_silence();
    return fl_erase4k(off, &took);
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!FL_STORE_OK(off, n))
        return -8;
    return fl_write(off, src, n);
}
#include "storage.c"

/* the QY70 style bank (dsp/qybank.c, dsp/qydec.c: their own unit) reads the flash here; bank_rx.c writes it */
#include "dsp/qybank.h"
_Static_assert(FL_BANK_OK(QB_R0_LO, QB_R0_HI - QB_R0_LO) && FL_BANK_OK(QB_R1_LO, QB_R1_HI - QB_R1_LO) &&
               FL_BANK_OK(QB_R2_LO, QB_R2_HI - QB_R2_LO) && FL_BANK_OK(QB_R3_LO, QB_R3_HI - QB_R3_LO) &&
               !FL_BANK_OK(QB_R0_LO - 1u, 1u) && !FL_BANK_OK(QB_R0_HI, 1u) && !FL_BANK_OK(QB_R1_LO - 1u, 1u) &&
               !FL_BANK_OK(QB_R1_HI, 1u) && !FL_BANK_OK(QB_R2_LO - 1u, 1u) && !FL_BANK_OK(QB_R2_HI, 1u) &&
               !FL_BANK_OK(QB_R3_LO - 1u, 1u) && !FL_BANK_OK(QB_R3_HI, 1u),
               "the HAL's bank window (FL_BANK_OK) is the bank's four regions, exactly");
_Static_assert(QB_R1_LO == 0x99000u + 16u * 0x2000u, "the bank starts where the 16 user patterns end (storage.c)");
int qb_flash_read(uint32_t off, void *dst, uint32_t n) { return st_read(off, dst, n); }

struct bootguard_s { uint32_t magic, failed, pending; };
extern struct bootguard_s bootguard;
#include "ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    audio_silence();
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, &FONT_S, "UPDATE", C_WHITE, 1);
    if (step < 9u) {
        draw_text_box(0, 124, 240, &FONT_S, STEP[step < 6u ? step : 0], C_HI, 1);
        return;
    }
    draw_text_box(0, 124, 240, &FONT_S, code == 1 ? "DRY RUN OK" : "FAILED", C_HI, 1);
    fm1_delay_ms(1500);
}
static void ota_commit(const uint8_t *parm)
{
    bootguard.pending = 0;                              /* intentional reset */
    bootguard.failed = 0;                               /* a new install starts with a clean slate */
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}

/* the QY70 style bank from the host (tools/qy70_install.py) */
#include "bank_rx.c"
static int bk_fetch(uint32_t addr, uint8_t *dst, uint32_t n) { return ota_read(addr, dst, n); }
static int bk_erase(uint32_t off)
{
    uint32_t took;
    if (!QB_FLASH_OK(off, QB_SECTOR) || (off & 0xFFFu))
        return -8;
    fm1_wdt_feed();                                 /* 64 erases in a row at most */
    audio_silence();
    return fl_erase4k(off, &took);
}
static int bk_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!QB_FLASH_OK(off, n))
        return -8;
    return fl_write(off, src, n);
}
static int bk_read(uint32_t off, void *dst, uint32_t n) { return st_read(off, dst, n); }
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
static void bk_done(int code)                       /* the result, to the host and on the screen */
{
    uint8_t b[8];
    ota_read(code ? BK_FAIL_ADDR | (uint32_t)code : BK_OK_ADDR, b, 8);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, &FONT_S, "QY70 STYLES", C_WHITE, 1);
    draw_text_box(0, 124, 240, &FONT_S, code ? "NOT WRITTEN" : "WRITTEN", C_HI, 1);
    fm1_delay_ms(1200);
}

#include "app/app.h"
#include "app/panel.c"
#include "app/plat_fm1.c"
#include "app/project.c"
#include "app/pages.c"
#include "app/ui.c"
#include "app/main_fm1.c"
