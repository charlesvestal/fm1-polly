/* SPDX-License-Identifier: GPL-3.0-only */
/* Screen mirroring (firmware/src/mirror.c) on the host: its frames decoded here as the viewer decodes them
 * (web/mirror/index.html), against the screen it mirrors.
 *   - the pixel code's round trip on bands drawn with gfx.c (the fonts, rects, lines, partial blits, fills) and on
 *     the simulator's real screens (fbdump files given as arguments: tests/scenarios/mirror.polly), with the bytes
 *     each band and screen takes (the compression);
 *   - rows the viewer has are not sent again; a ring too small: frames dropped, rows dirty, the UI asked to draw
 *     again until the viewer has it all; a refresh sends everything;
 *   - the input reports: the panel's state, encoder turns added up between reports, 50 a second at most, one every
 *     500 ms anyway; off by request, by a new connection, by silence.
 *   mirror_test [SCREEN.bin ...] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#define GM_HOST 1
#define __attribute__(x)
static uint16_t dev[240 * 240];                       /* the device's screen (as stored: the panel's byte order) */
static uint8_t mirror_on;
static void mirror_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px);
static void mirror_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++)
            dev[(y + j) * 240u + x + i] = px[j * w + i];
    if (mirror_on)
        mirror_blit(x, y, w, h, px);
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    for (uint32_t j = y; j < y + h; j++)
        for (uint32_t i = x; i < x + w; i++)
            dev[j * 240u + i] = (uint16_t)(c >> 8 | c << 8);
    if (mirror_on)
        mirror_fill(x, y, w, h, c);
}
#include "../../firmware/src/gfx.c"
#include "../../firmware/src/app/plat.h"

static uint32_t now, btn, keys, master = 2048;
uint32_t plat_ms(void) { return now; }
uint32_t plat_buttons(void) { return btn; }
uint32_t plat_keys(void) { return keys; }
uint32_t plat_master(void) { return master; }

#define MRQ 4096u
#include "../../firmware/src/mirror.c"

/* the ring (its room set by the test), the stream as the host receives it */
static uint32_t ring_used, ring_cap = MRQ, link_id = 1, redraws, busy;
static uint8_t ring[1 << 20], stream[1 << 22];
static uint32_t ring_n, stream_n;
static uint32_t mr_free(void) { return ring_cap > ring_used ? ring_cap - ring_used : 0; }
static void mr_put(const uint8_t *p, uint32_t n)
{
    memcpy(ring + ring_n, p, n);
    ring_n += n;
    ring_used += n;
}
static int mr_link(void) { return (int)link_id; }
static int mr_busy(void) { return (int)busy; }
static int mr_ctl_send(const uint8_t *p, uint32_t n)       /* (before anything in the ring, as usb.c sends it) */
{
    memcpy(stream + stream_n, p, n);
    stream_n += n;
    return 0;
}
static void (*redraw_fn)(void);
static void mr_redraw(void)
{
    redraws++;
    if (redraw_fn)
        redraw_fn();
}
static void drain_some(uint32_t limit)               /* a slow link: whole frames, about limit bytes */
{
    uint32_t n = 0;
    while (n < ring_n && n < limit) {
        while (ring[n] != 0xF7)
            n++;
        n++;
    }
    memcpy(stream + stream_n, ring, n);
    stream_n += n;
    memmove(ring, ring + n, ring_n - n);
    ring_n -= n;
    ring_used -= n;
}
static void drain(void)
{
    memcpy(stream + stream_n, ring, ring_n);
    stream_n += ring_n;
    ring_n = ring_used = 0;
}

/* ------------------------------------------------------- the viewer --- */
static uint16_t view[240 * 240];                      /* RGB565 */
static uint32_t dec_pos, dec_rects, dec_fills, dec_bad, dec_hello, dec_on, seq_next = 0x100;
static uint8_t inp[32];
static uint32_t n_inp;
static int32_t enc_sum[7];
static uint8_t body[1 << 16];

static uint32_t unpack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)(*in++ & 0x7Fu) << nb;
        nb += 7u;
        if (nb >= 8u) {
            out[o++] = (uint8_t)acc;
            acc >>= 8;
            nb -= 8u;
        }
    }
    return o;
}

static int decode_rect(const uint8_t *b, uint32_t n)
{
    uint32_t x = b[0], y = b[1], w = b[2], h = b[3], i = 5, p = 0, total = w * h;
    uint16_t cache[64] = {0}, cur = 0;
#define PUT(c)                                                                                                     \
    do {                                                                                                           \
        if (p >= total)                                                                                            \
            return -1;                                                                                             \
        view[(y + p / w) * 240u + x + p % w] = (c);                                                                \
        p++;                                                                                                       \
    } while (0)
    if (!w || !h || x + w > 240u || y + h > 240u)
        return -1;
    while (i < n) {
        uint32_t op = b[i++], k;
        if (op < 0x80u) {
            for (k = 0; k <= op; k++)
                PUT(cur);
        } else if (op < 0xC0u) {
            cur = cache[op & 63u];
            PUT(cur);
        } else {
            if (i + 2u > n)
                return -1;
            cur = (uint16_t)(b[i] << 8 | b[i + 1]);
            i += 2;
            cache[MR_HASH(cur)] = cur;
            for (k = 0; k <= (op & 63u); k++)
                PUT(cur);
        }
    }
    return p == total ? 0 : -1;
}

/* everything new in the stream */
static void decode(void)
{
    while (dec_pos < stream_n) {
        uint32_t s = dec_pos, e = s, n, len, sum = 0;
        if (stream[s] != 0xF0) {
            dec_bad++;
            dec_pos++;
            continue;
        }
        while (e < stream_n && stream[e] != 0xF7)
            e++;
        if (e == stream_n)
            return;
        dec_pos = e + 1;
        n = unpack7(stream + s + 1, e - s - 1, body);
        if (n < 7 || body[0] != 0 || body[1] != 0x59) {
            dec_bad++;
            continue;
        }
        len = body[3] | body[4] << 8 | (uint32_t)body[5] << 16;
        for (uint32_t i = 0; i < len; i++)
            sum += body[6 + i];
        if (len + 7 != n || (uint8_t)~sum != body[6 + len]) {
            dec_bad++;
            continue;
        }
        const uint8_t *b = body + 6;
        if (body[2] == MR_RECT || body[2] == MR_FILL) {
            if (seq_next < 0x100 && b[4] != seq_next)
                dec_bad++;                            /* a frame missing */
            seq_next = (b[4] + 1u) & 255u;
        }
        if (body[2] == MR_RECT) {
            if (decode_rect(b, len))
                dec_bad++;
            dec_rects++;
        } else if (body[2] == MR_FILL) {
            uint16_t c = (uint16_t)(b[5] << 8 | b[6]);
            for (uint32_t j = b[1]; j < (uint32_t)b[1] + b[3]; j++)
                for (uint32_t i = b[0]; i < (uint32_t)b[0] + b[2]; i++)
                    view[j * 240u + i] = c;
            dec_fills++;
        } else if (body[2] == MR_INPUT && len == MR_NIN) {
            memcpy(inp, b, MR_NIN);
            for (int k = 0; k < 7; k++)
                enc_sum[k] += (int8_t)b[6 + k];
            n_inp++;
        } else if (body[2] == MR_REQ && len == 4) {
            dec_hello++;
            dec_on = b[1];
        } else
            dec_bad++;
    }
}

static int fails;
#define CHECK(c, ...)                                                                                              \
    do {                                                                                                           \
        if (!(c)) {                                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                            \
            printf(__VA_ARGS__);                                                                                   \
            printf("\n");                                                                                          \
            fails++;                                                                                               \
        }                                                                                                          \
    } while (0)

static uint32_t diff_rows(void)                       /* rows the viewer has wrong */
{
    uint32_t n = 0;
    for (uint32_t r = 0; r < 240; r++)
        for (uint32_t i = 0; i < 240; i++)
            if (view[r * 240 + i] != (uint16_t)(dev[r * 240 + i] >> 8 | dev[r * 240 + i] << 8)) {
                n++;
                break;
            }
    return n;
}

/* the UI's five bands (pages.c), from a screen */
static const uint32_t BAND_Y[6] = {0, 24, 156, 168, 224, 240};
static uint16_t shown[240 * 240];                     /* the screen the "UI" draws (a dump, as stored) */
static void blit_band(uint32_t k) { lcd_blit(0, BAND_Y[k], 240, BAND_Y[k + 1] - BAND_Y[k], shown + BAND_Y[k] * 240); }
static void blit_all(void)
{
    for (uint32_t k = 0; k < 5; k++)
        blit_band(k);
}

static void on(void)
{
    link_id++;
    mirror_cmd(2);
}

/* ------------------------------------------------------------ tests --- */
static void test_drawn(void)
{
    uint32_t b0;
    ring_cap = 1 << 20;
    on();
    redraw_fn = 0;
    palette_set(1);
    lcd_fill(0, 0, 240, 240, RGB(250, 244, 232));
    cv_begin(240, 24, RGB(242, 120, 72));
    cv_text(6, 2, &FONT_M, "SONG 1  Pop 8", C_WHITE);
    cv_blit(0, 0);
    cv_begin(240, 132, RGB(250, 244, 232));
    cv_text(10, 10, &FONT_L, "Am7", RGB(72, 58, 48));
    cv_text(10, 60, &FONT_S, "Main A  bar 3  120 BPM", RGB(150, 136, 118));
    cv_text(10, 90, &FONT_XS, "abcdefghijklmnopqrstuvwxyz 0123456789", RGB(38, 166, 154));
    cv_rect(150, 10, 80, 40, RGB(196, 230, 222));
    cv_line(0, 131, 239, 0, RGB(242, 120, 72));
    for (int i = 0; i < 240; i++)
        cv_pset(i, 120 + (i % 7), (uint16_t)(i * 997u));  /* noise: every pixel a new colour */
    cv_blit(0, 24);
    draw_text_box(20, 170, 200, &FONT_B, "a partial blit", RGB(255, 184, 70), 1);
    lcd_fill(30, 200, 50, 20, RGB(10, 200, 30));
    b0 = stream_n;
    drain();
    decode();
    CHECK(diff_rows() == 0, "drawn: %u rows differ", diff_rows());
    CHECK(dec_bad == 0, "drawn: %u bad frames", dec_bad);
    printf("drawn bands: %u bytes in the ring for the screen above (fills, header, main with a noisy band, a partial "
           "blit)\n", stream_n - b0);
}

/* the dumps are the simulator's fb as stored (little-endian words, the panel's byte order): read as is */
static void test_screen(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint32_t sz[5], full, b0, i, t;
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    if (!f || fread(shown, 2, 240 * 240, f) != 240 * 240) {
        printf("FAIL cannot read %s\n", path);
        fails++;
        if (f)
            fclose(f);
        return;
    }
    fclose(f);
    ring_cap = 1 << 20;
    on();
    for (full = 0, i = 0; i < 5; i++) {
        b0 = ring_n;
        blit_band(i);
        sz[i] = ring_n - b0;
        full += sz[i];
    }
    drain();
    decode();
    CHECK(diff_rows() == 0, "%s: %u rows differ", name, diff_rows());
    printf("%-10s screen %5u B (%4.1f : 1 against 115200 B raw): header %u, main %u, tag %u, knobs %u, footer %u\n", name,
           full, 115200.0 / full, sz[0], sz[1], sz[2], sz[3], sz[4]);
    b0 = ring_n;                                      /* the same again: nothing sent */
    blit_all();
    CHECK(ring_n == b0, "%s: a screen drawn again the same sent %u bytes", name, ring_n - b0);
    {   /* the cost: hashing and coding a whole screen, on this machine */
        clock_t c0 = clock();
        for (t = 0; t < 20; t++) {
            memset(mr.known, 0, sizeof mr.known);
            ring_n = ring_used = 0;
            blit_all();
        }
        ring_n = ring_used = 0;
        seq_next = 0x100;                             /* (those frames thrown away) */
        printf("%-10s coding: %.1f ns a pixel on this machine\n", "",
               (double)(clock() - c0) / CLOCKS_PER_SEC * 1e9 / (20.0 * 57600.0));
        memset(mr.known, 0, sizeof mr.known);
        blit_all();
        drain();
        decode();
    }
}

/* two screens: the bytes the change from one to the other takes (rows the viewer has: skipped) */
static void test_change(const char *a, const char *b)
{
    FILE *f;
    uint32_t b0;
    const char *paths[2] = {a, b};
    for (int k = 0; k < 2; k++) {
        f = fopen(paths[k], "rb");
        if (!f || fread(shown, 2, 240 * 240, f) != 240 * 240) {
            if (f)
                fclose(f);
            return;
        }
        fclose(f);
        if (!k) {
            ring_cap = 1 << 20;
            on();
            blit_all();
            drain();
            decode();
        }
    }
    b0 = ring_n;
    blit_all();
    printf("change %s -> %s: %u B\n", strrchr(a, '/') ? strrchr(a, '/') + 1 : a,
           strrchr(b, '/') ? strrchr(b, '/') + 1 : b, ring_n - b0);
    drain();
    decode();
    CHECK(diff_rows() == 0, "change: %u rows differ", diff_rows());
}

static void test_drops(void)
{
    uint32_t i, rounds = 0, d0 = mr.drops;
    for (i = 0; i < 240 * 240; i++)                   /* a busy screen: text-like noise on every row */
        shown[i] = (uint16_t)((i * 2654435761u) >> 20 & 7u ? 0x1F00u : (i * 40503u) & 0xFFFFu);
    ring_cap = MRQ;                                   /* the device's ring, a slow link: most of it does not fit */
    on();
    redraw_fn = blit_all;
    blit_all();
    CHECK(mr.drops > d0, "drops: none");
    CHECK(diff_rows() > 0, "drops: the viewer has it all already?");
    for (rounds = 0; rounds < 400; rounds++) {        /* the ring drains, the UI draws again, until all is there */
        uint32_t dirty = 0;
        drain_some(300);                              /* 300 bytes a round of 20 ms: 15 kB/s */
        decode();
        now += 20;
        mirror_step();
        for (i = 0; i < 240; i++)
            dirty += mr_bit(mr.dirty, i);
        if (!dirty && !ring_n)
            break;
    }
    drain();
    decode();
    CHECK(diff_rows() == 0, "drops: %u rows still differ after %u rounds", diff_rows(), rounds);
    printf("drops: a busy screen at 15 kB/s: %u frames dropped, the screen whole after %u ms\n", mr.drops - d0,
           rounds * 20);
    /* busy (a backup streaming): nothing goes, the rows dirty, then sent */
    busy = 1;
    ring_cap = 1 << 20;
    shown[5 * 240 + 7] ^= 0x0F0Fu;
    blit_all();
    CHECK(ring_n == 0, "busy: %u bytes sent", ring_n);
    CHECK(mr_bit(mr.dirty, 5), "busy: row 5 not dirty");
    busy = 0;
    now += 100;
    mirror_step();
    drain();
    decode();
    CHECK(diff_rows() == 0, "busy: %u rows differ after", diff_rows());
    /* a refresh: everything again */
    {
        uint32_t b0 = stream_n, h0 = dec_hello;
        mirror_cmd(2);
        now += 100;
        mirror_step();
        drain();
        decode();
        CHECK(dec_hello == h0 + 1 && dec_on == 1, "refresh: no hello");
        CHECK(stream_n - b0 > 20000, "refresh: only %u bytes", stream_n - b0);
        CHECK(diff_rows() == 0, "refresh: %u rows differ", diff_rows());
    }
    redraw_fn = 0;
}

static void test_input(void)
{
    uint32_t n0;
    ring_cap = 1 << 20;
    on();
    now += 1000;
    btn = 1u << B_SEL;
    keys = (1u << 5) | (1u << 26);
    master = 3000;
    mirror_leds(1u << B_PLAY | 1u << 15, 1u << 9);
    memset(enc_sum, 0, sizeof enc_sum);
    mirror_step();
    decode();
    n0 = n_inp;
    CHECK(n_inp >= 1, "input: no report");
    CHECK((inp[0] | inp[1] << 8) == 1u << B_SEL, "input: buttons %x", inp[0] | inp[1] << 8);
    CHECK((uint32_t)(inp[2] | inp[3] << 8 | inp[4] << 16 | (uint32_t)inp[5] << 24) == ((1u << 5) | (1u << 26)), "input: keys");
    CHECK((inp[13] | inp[14] << 8) == 3000, "input: master");
    CHECK((inp[15] | inp[16] << 8) == (1u << B_PLAY | 1u << 15), "input: lit buttons");
    CHECK(inp[17] == 0 && inp[18] == 2, "input: lit keys");
    mirror_enc(EN_K1, 2);                             /* turns added up: one report, 50 a second at most */
    now += 5;
    mirror_step();
    mirror_enc(EN_K1, 3);
    mirror_enc(EN_PRESET, -1);
    now += 5;
    mirror_step();
    decode();
    CHECK(n_inp == n0, "input: a report within 20 ms");
    now += 15;
    mirror_step();
    decode();
    CHECK(n_inp == n0 + 1, "input: %u reports, not 1", n_inp - n0);
    CHECK(enc_sum[EN_K1] == 5 && enc_sum[EN_PRESET] == -1, "input: K1 %d PRESET %d", enc_sum[EN_K1], enc_sum[EN_PRESET]);
    mirror_enc(EN_SELECT, 300);                       /* past an int8: in several reports, none lost */
    for (int i = 0; i < 5; i++) {
        now += 20;
        mirror_step();
    }
    decode();
    CHECK(enc_sum[EN_SELECT] == 300, "input: SELECT %d of 300", enc_sum[EN_SELECT]);
    n0 = n_inp;
    master = 3010;                                    /* MASTER's jitter: no report */
    now += 100;
    mirror_step();
    decode();
    CHECK(n_inp == n0, "input: a report for MASTER's jitter");
    now += 500;                                       /* one every 500 ms anyway */
    mirror_step();
    decode();
    CHECK(n_inp == n0 + 1, "input: no report after 500 ms");
    btn = 0;
    now += 20;
    mirror_step();
    decode();
    CHECK(n_inp == n0 + 2 && !(inp[0] | inp[1]), "input: the button let go not reported");
}

static void test_off(void)
{
    ring_cap = 1 << 20;
    on();
    CHECK(mirror_on, "off: not on");
    mirror_cmd(0);
    decode();
    CHECK(!mirror_on && dec_on == 0, "off: still on");
    mirror_cmd(1);                                    /* on again (a keep-alive turns it on) */
    CHECK(mirror_on && mr.ask_redraw, "off: on again without a redraw");
    now += MR_TIMEOUT_MS - 100;
    mirror_cmd(1);                                    /* the keep-alive */
    now += MR_TIMEOUT_MS - 100;
    mirror_step();
    CHECK(mirror_on, "off: off despite the keep-alive");
    now += 200;
    mirror_step();
    CHECK(!mirror_on, "off: still on after %u ms of silence", MR_TIMEOUT_MS + 100);
    mirror_cmd(1);
    link_id++;                                        /* a new connection (unplugged, plugged again) */
    mirror_step();
    CHECK(!mirror_on, "off: still on for a new connection");
    btn = 1;                                          /* off: no frames, no reports */
    {
        uint32_t s0 = stream_n;
        lcd_fill(0, 0, 240, 240, 0);
        mirror_step();
        drain();
        CHECK(stream_n == s0, "off: %u bytes sent", stream_n - s0);
    }
}

int main(int argc, char **argv)
{
    test_drawn();
    for (int i = 1; i < argc; i++)
        test_screen(argv[i]);
    if (argc > 2)
        for (int i = 2; i < argc; i++)
            test_change(argv[i - 1], argv[i]);
    test_drops();
    test_input();
    test_off();
    decode();
    CHECK(dec_bad == 0, "%u bad frames in the stream", dec_bad);
    printf("%s: %u bytes decoded (%u rects, %u fills, %u input reports)\n", fails ? "FAILED" : "ok", stream_n,
           dec_rects, dec_fills, n_inp);
    return fails ? 1 : 0;
}
