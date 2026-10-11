/* SPDX-License-Identifier: GPL-3.0-only */
/* POLLY on the host: the whole app (UI, project, engine) against a simulated FM-1 driven by a script.
 * Audio goes to a WAV, the screen to PNGs, the lights to text, MIDI out to a log. The device-only
 * parts (HAL, USB, OTA, flash driver) are replaced by plat.h implemented here. (From X0X's x0x_host.)
 *
 *   polly_host SCRIPT [OUTDIR]
 *
 * Script, one command per line (# comments):
 *   wait MS                      run the device for MS milliseconds
 *   press BTN | release BTN | tap BTN     (BTN: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+)
 *   key K down|up | tapkey K     (K: 0..26 = F3..G5, or w0..w15 white keys, b0..b10 black keys)
 *   turn ENC N [MS]              (ENC: SELECT ALGO PRESET K1 K2 K3 K4; N detents, MS apart, default 80)
 *   spin ENC N                   N detents at once, exactly N (no acceleration)
 *   master N                     MASTER pot 0..4096
 *   midi B0 B1 B2                incoming USB MIDI message (hex bytes)
 *   wav FILE | wavstop           start / stop recording the output
 *   peakreset                    restart the output peak (expect peak_db_min / peak_db_max)
 *   shot FILE.png                save the screen
 *   leds                         print the lit buttons and keys
 *   expect WHAT VALUE            check state: exit 1 on mismatch; VALUE "<N" / ">N" is a bound
 *   reboot                       re-run boot from the simulated flash (persistence test)
 *   clock N [MS]                 N MIDI clocks in (F8), MS apart (default 21: 119 bpm)
 *   flash_fail 0|1               every flash save fails (1) or works (0)
 *   hitreset                     restart the tracks' note-on counts (expect track_hitsN) and notes_out
 *   bank FILE                    a QY70 style bank image (tools/qy70_codec.py --image) written into the flash, as
 *                                tools/qy70_install.py writes it on the device; mounted, the song reloaded.
 *                                FILE "out:NAME": NAME in OUTDIR
 *   backup FILE                  a backup over USB (firmware/src/backup.c's image: the project, the songs, the 16
 *                                slots) written to FILE, as tools/polly_backup.py writes it (stopped: the unsaved saved
 *                                first). FILE "out:NAME": NAME in OUTDIR
 *   restore FILE                 a backup put back as the device does once SEL answers RESTORE FROM USB?: checked
 *                                whole, every object replaced, everything reloaded; refused while playing (STOP
 *                                FIRST) or when damaged: nothing changes (expect restore_rc: 0, 10 playing, 5 damaged)
 *   mirror FILE|off|refresh|nokeep|unplug   the screen and panel mirrored over USB (firmware/src/mirror.c) as a
 *                                viewer asks for it, the device's SysEx into FILE (in OUTDIR) at USB's pace; nokeep: the
 *                                viewer's keep-alive stops; unplug: the cable out
 *   mirror_rate N                that pace, bytes a millisecond (48: a 64-byte USB-MIDI packet a 1 ms frame)
 *   fbdump FILE                  the screen's pixels raw (240 x 240 RGB565, high byte first) into FILE in OUTDIR
 *   style NAME                   the song's six sections from the style named NAME ("_" for a space; "qy:NAME": the
 *                                QY70 bank's only), as the Style knob sets them
 */
#define GM_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#define __attribute__(x)

/* ------------------------------------------------------------ screen --- */
static uint16_t fb[240 * 240];          /* RGB565, byte-swapped as the panel takes it (gfx.c) */
static void lcd_sync(void) {}
/* screen mirroring over USB (firmware/src/mirror.c), as lcd.c copies each blit and fill to it */
static uint8_t mirror_on;
static void mirror_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px);
static void mirror_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
    if (mirror_on && w && h && x < 240u && y < 240u)
        mirror_fill(x, y, x + w > 240u ? 240u - x : w, y + h > 240u ? 240u - y : h, c);
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = sc;
}
static uint32_t blits;
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    blits++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = px[j * w + i];
    if (mirror_on)
        mirror_blit(x, y, w, h, px);
}
#include "../firmware/src/gfx.c"


/* ---------------------------------------------------------- platform --- */
#include "../firmware/src/app/plat.h"
#define MRQ 4096u                       /* the device's mirror ring (usb.c) */
#include "../firmware/src/mirror.c"

static uint32_t now_ms, held_btn, held_keys, master = 2048;
static int32_t enc_acc[NE];
static uint32_t lit_btn, lit_keys;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int role)
{
    int32_t v = enc_acc[role];
    enc_acc[role] = 0;
    if (v)
        mirror_enc(role, v);
    return v;
}
uint32_t plat_master(void) { return master; }
void plat_leds(uint32_t b, uint32_t k)
{
    lit_btn = b;
    lit_keys = k;
    mirror_leds(b, k);
}
static int glow_on = 1;                 /* the device's button glow (plat_glow); the lights printed are the bright ones */
void plat_glow(int on) { glow_on = on; }
static uint32_t dim_keys;
void plat_keys_dim(uint32_t keys) { dim_keys = keys; }
void plat_play_red(int red) { (void)red; }      /* the device's PLAY colour: green, red while recording */

#define MQ 256
static uint32_t min_q[MQ], mi_w, mi_r;
static FILE *midi_log;
static uint32_t midi_out_count, clock_out_count, note_on_count, click_count;
int plat_midi_in(uint32_t *pkt)
{
    if (mi_r == mi_w)
        return 0;
    *pkt = min_q[mi_r++ % MQ];
    return 1;
}
void plat_midi_out(uint32_t pkt)
{
    midi_out_count++;
    if (((pkt >> 8) & 0xF0) == 0x90 && (pkt >> 24))
        note_on_count++;
    if (((pkt >> 8) & 0xFF) == 0x9F && (pkt >> 24))
        click_count++;
    if (((pkt >> 8) & 0xFF) == 0xF8) {
        clock_out_count++;
        return;
    }
    if (midi_log)
        fprintf(midi_log, "%u ms: %02X %02X %02X\n", now_ms, (pkt >> 8) & 0xFF, (pkt >> 16) & 0xFF, (pkt >> 24) & 0xFF);
}
/* the engine's MIDI out, as the device's glue sends it (USB-MIDI packet, cable 0) */
void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { plat_midi_out(st >> 4 | st << 8 | d1 << 16 | d2 << 24); }
/* the host's MIDI out is logged at once and never fills: always the device's whole ring free (the engine's
 * limits are tested in tests/host/arr_test.c, against a ring it fills and drains) */
uint32_t plat_midi_out_room(void) { return 64u; }
uint32_t arr_midi_out_room(void) { return plat_midi_out_room(); }
uint32_t plat_midi_out_link(void) { return 0u; }
uint32_t arr_midi_out_link(void) { return plat_midi_out_link(); }

/* flash: one buffer per object, kept across a simulated reboot. store_len starts at -1 (absent) for
 * every object (a GNU range initialiser, so a new object added to plat.h is covered automatically;
 * a plain {-1, -1} would leave the rest at 0, which reads as "present"). */
static uint8_t store[OBJ_NOBJ][PLAT_STORE_MAX];
static int store_len[OBJ_NOBJ] = { [0 ... OBJ_NOBJ - 1] = -1 };
static uint32_t store_writes, store_tries;
static int store_fail;                  /* flash_fail 1: every save fails (a flash error) */

int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    int n;
    if (obj >= OBJ_NOBJ || (n = store_len[obj]) < 0)
        return -1;
    if ((uint32_t)n > max)
        n = (int)max;
    memcpy(dst, store[obj], (size_t)n);
    return n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    store_tries++;
    if (obj >= OBJ_NOBJ || len > PLAT_STORE_MAX || store_fail)
        return -1;
    memcpy(store[obj], src, len);
    store_len[obj] = (int)len;
    store_writes++;
    return 0;
}

/* the flash the QY70 style bank lives in (qybank.h): erased, until a `bank FILE` writes an image into it */
static uint8_t sim_flash[0x100000];
static int sim_flash_ready;
int qb_flash_read(uint32_t off, void *dst, uint32_t n)
{
    if (!sim_flash_ready) {
        memset(sim_flash, 0xFF, sizeof sim_flash);
        sim_flash_ready = 1;
    }
    if (off > sizeof sim_flash || n > sizeof sim_flash - off)
        return -1;
    memcpy(dst, sim_flash + off, n);
    return 0;
}

static uint32_t trk_hits[10], trk_last[10];  /* note-ons seen per track since hitreset (arr_track_hit moving; 10: ARR_ALLTRK) */

static uint32_t cpu_pct;
uint32_t plat_cpu_pct(void) { return cpu_pct; }
uint32_t plat_xruns(void) { return 0; }

/* -------------------------------------------------------------- app --- */
static int msg_too_wide;                 /* ui.c's say(): messages wider than the header */
#include "../firmware/src/app/app.h"
#include "../firmware/src/app/project.c"
#include "../firmware/src/app/pages.c"
#include "../firmware/src/app/ui.c"

/* ------------------------------------------------- mirroring over USB --- */
/* the device's mirror ring, drained at USB's pace (mr_rate bytes a millisecond; 48: one 64-byte packet of
 * USB-MIDI a 1 ms frame) into a file of SysEx (`mirror FILE`); the SysEx ring's frames (the input reports, the
 * hellos) go first, between frames, as usb.c sends them. The viewer's keep-alive every 2 s. */
static uint8_t hmr_q[MRQ], hctl[1024];
static uint32_t hmr_w, hmr_r, hctl_n, mr_rate = 48, mr_mid, mr_keep = 1, mr_keep_t, mr_link_n = 1;
static FILE *mr_out;
static uint32_t mr_free(void) { return MRQ - (hmr_w - hmr_r); }
static void mr_put(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        hmr_q[(hmr_w + i) % MRQ] = p[i];
    hmr_w += n;
}
static int mr_link(void) { return mr_out ? (int)mr_link_n : -1; }
static int mr_busy(void) { return !mr_out; }
static int mr_ctl_send(const uint8_t *p, uint32_t n)
{
    if (!mr_out || hctl_n + n > sizeof hctl)
        return -1;
    memcpy(hctl + hctl_n, p, n);
    hctl_n += n;
    return 0;
}
static void mr_redraw(void)
{
    for (uint32_t k = 0; k < 5u; k++)
        ui.sig[k] = ~ui.sig[k];
}
static void host_mirror_ms(void)                 /* one millisecond of USB */
{
    uint32_t budget = mr_rate;
    if (!mr_out)
        return;
    if (mirror_on && mr_keep && now_ms - mr_keep_t >= 2000u) {
        mr_keep_t = now_ms;
        mirror_cmd(1);
    }
    for (;;) {
        if (!mr_mid && hctl_n) {
            fwrite(hctl, 1, hctl_n, mr_out);
            budget = budget > hctl_n ? budget - hctl_n : 0;
            hctl_n = 0;
        }
        if (hmr_r == hmr_w || !budget)
            break;
        {
            uint8_t b = hmr_q[hmr_r++ % MRQ];
            fputc(b, mr_out);
            mr_mid = b != 0xF7u;
            budget--;
        }
    }
}

/* ------------------------------------------- backup / restore over USB --- */
/* backup.c's image, made and taken from the simulated store (the device's side and its safety: tests/backup_test.c) */
static int restore_rc = -1;
static uint32_t sim_crc32(uint32_t c, const uint8_t *p, size_t n)
{
    c = ~c;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}
static void sim_wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t sim_rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t sim_obj(int i) { return i == 0 ? OBJ_PROJ : i == 1 ? OBJ_SONGS : (uint32_t)(OBJ_PAT0 + i - 2); }
static uint8_t sim_img[16 + 18 * (8 + PLAT_STORE_MAX) + 4];
static int sim_backup(const char *path)
{
    size_t n = 16;
    FILE *f;
    int playing = arr_playing != 0;
    if (!playing)
        app_flush();
    memcpy(sim_img, "PLYB", 4);
    sim_img[4] = 1, sim_img[5] = 0, sim_img[6] = 18, sim_img[7] = 0;
    sim_wr32(sim_img + 8, (uint32_t)playing);
    sim_wr32(sim_img + 12, sim_crc32(0, sim_img, 12));
    for (int i = 0; i < 18; i++) {
        uint32_t o = sim_obj(i), len = store_len[o] > 0 ? (uint32_t)store_len[o] : 0;
        uint8_t *r = sim_img + n;
        r[0] = (uint8_t)o, r[1] = 0, r[2] = (uint8_t)len, r[3] = (uint8_t)(len >> 8);
        memcpy(r + 8, store[o], len);
        sim_wr32(r + 4, sim_crc32(0, r + 8, len));
        n += 8 + len;
    }
    sim_wr32(sim_img + n, sim_crc32(0, sim_img, n));
    n += 4;
    if (!(f = fopen(path, "wb")))
        return -1;
    fwrite(sim_img, 1, n, f);
    fclose(f);
    return 0;
}
static int sim_restore(const char *path)
{
    size_t n, pos = 16;
    FILE *f = fopen(path, "rb");
    if (!f)
        return 1;
    n = fread(sim_img, 1, sizeof sim_img, f);
    fclose(f);
    if (arr_playing)
        return 10;                                /* STOP FIRST */
    if (n < 16 + 18 * 8 + 4 || memcmp(sim_img, "PLYB", 4) || sim_img[4] != 1 || sim_img[6] != 18 ||
        sim_rd32(sim_img + 12) != sim_crc32(0, sim_img, 12))
        return 3;
    for (int i = 0; i < 18; i++) {                /* all checked before anything changes */
        const uint8_t *r = sim_img + pos;
        uint32_t len = r[2] | r[3] << 8;
        if (pos + 8 + len + 4 > n || r[0] != sim_obj(i) || r[1] || len > PLAT_STORE_MAX)
            return 4;
        if (sim_rd32(r + 4) != sim_crc32(0, r + 8, len))
            return 5;
        pos += 8 + len;
    }
    if (pos + 4 != n || sim_rd32(sim_img + pos) != sim_crc32(0, sim_img, pos))
        return 6;
    pos = 16;
    for (int i = 0; i < 18; i++) {
        const uint8_t *r = sim_img + pos;
        uint32_t o = sim_obj(i), len = r[2] | r[3] << 8;
        memcpy(store[o], r + 8, len);
        store_len[o] = len ? (int)len : -1;       /* (an empty object: none, as storage.c's st_load) */
        pos += 8 + len;
    }
    gm_panic();
    app_restored();
    return 0;
}

/* ---------------------------------------------------------- outputs --- */
static void png_write(const char *path)
{
    /* RGB PNG with stored (uncompressed) deflate blocks: no zlib needed */
    static uint8_t raw[240 * (1 + 240 * 3)];
    FILE *f = fopen(path, "wb");
    uint32_t crc_t[256], i, j, k, n = 0, a = 1, b = 0, pos, len = sizeof raw;
    uint8_t hdr[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_t[i] = c;
    }
    for (j = 0; j < 240; j++) {
        raw[n++] = 0;
        for (i = 0; i < 240; i++) {
            uint16_t p = fb[j * 240 + i];
            p = (uint16_t)((p >> 8) | (p << 8));
            raw[n++] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
            raw[n++] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
            raw[n++] = (uint8_t)((p & 31) * 255 / 31);
        }
    }
    for (i = 0; i < len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
#define PUT32(v) do { uint8_t q_[4] = {(uint8_t)((v) >> 24), (uint8_t)((v) >> 16), (uint8_t)((v) >> 8), (uint8_t)(v)}; fwrite(q_, 1, 4, f); } while (0)
    fwrite(hdr, 1, 8, f);
    {
        uint8_t ih[17] = {'I', 'H', 'D', 'R', 0, 0, 0, 240, 0, 0, 0, 240, 8, 2, 0, 0, 0};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(13u);
        fwrite(ih, 1, 17, f);
        for (i = 0; i < 17; i++)
            c = crc_t[(c ^ ih[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    {
        uint32_t nblk = (len + 65534) / 65535, zlen = 2 + len + nblk * 5 + 4, c = 0xFFFFFFFFu;
        uint8_t *z = malloc(zlen + 4), *q = z;
        memcpy(q, "IDAT", 4);
        q += 4;
        *q++ = 0x78;
        *q++ = 0x01;
        for (pos = 0; pos < len;) {
            uint32_t m = len - pos > 65535 ? 65535 : len - pos;
            *q++ = pos + m >= len ? 1 : 0;
            *q++ = (uint8_t)m;
            *q++ = (uint8_t)(m >> 8);
            *q++ = (uint8_t)~m;
            *q++ = (uint8_t)(~m >> 8);
            memcpy(q, raw + pos, m);
            q += m;
            pos += m;
        }
        *q++ = (uint8_t)(b >> 8);
        *q++ = (uint8_t)b;
        *q++ = (uint8_t)(a >> 8);
        *q++ = (uint8_t)a;
        PUT32(zlen);
        fwrite(z, 1, zlen + 4, f);
        for (i = 0; i < zlen + 4; i++)
            c = crc_t[(c ^ z[i]) & 255] ^ (c >> 8);
        PUT32(~c);
        free(z);
    }
    {
        uint8_t ie[4] = {'I', 'E', 'N', 'D'};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(0u);
        fwrite(ie, 1, 4, f);
        for (i = 0; i < 4; i++)
            c = crc_t[(c ^ ie[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    fclose(f);
}

static FILE *wav;
static uint32_t wav_frames;
static float peak_out;
static void wav_open(const char *path)
{
    static const uint8_t h[44] = {0};
    wav = fopen(path, "wb");
    if (wav)
        fwrite(h, 1, 44, wav);
    wav_frames = 0;
}
static void wav_close(void)
{
    uint32_t bytes = wav_frames * 4, v;
    if (!wav)
        return;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav);
    v = 36 + bytes;
    fwrite(&v, 4, 1, wav);
    fwrite("WAVEfmt ", 1, 8, wav);
    v = 16;
    fwrite(&v, 4, 1, wav);
    {
        uint16_t fmt[2] = {1, 2};
        uint32_t r[2] = {44100, 44100 * 4};
        uint16_t al[2] = {4, 16};
        fwrite(fmt, 2, 2, wav);
        fwrite(r, 4, 2, wav);
        fwrite(al, 2, 2, wav);
    }
    fwrite("data", 1, 4, wav);
    fwrite(&bytes, 4, 1, wav);
    fclose(wav);
    wav = 0;
}

/* --------------------------------------------------------- the device --- */
static double audio_due_ms, render_ns_total, render_budget_ns_total;
static uint32_t ui_due;
static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

static void boot(void)
{
    gm_init();
    arr_init();
    project_load();
    project_apply();
    lcd_fill(0, 0, 240, 240, 0);
    ui_init();
    app_start();
}

static void run_ms(uint32_t ms)
{
    static int32_t blk[512];
    uint32_t i, k;
    for (i = 0; i < ms; i++) {
        now_ms++;
        while (audio_due_ms <= (double)now_ms) {      /* 256-frame blocks, as the device renders them */
            double t0 = now_ns(), dt;
            gm_render(blk, 256, master);
#ifdef GM_WEB
            web_audio(blk, 256);
#endif
            dt = now_ns() - t0;
            render_ns_total += dt;
            render_budget_ns_total += 256.0 * 1e9 / 44100.0;
            cpu_pct = (uint32_t)(100.0 * dt / (256.0 * 1e9 / 44100.0));
            for (k = 0; k < 256; k++) {
                float l = (float)blk[2 * k] / 8388608.0f, r = (float)blk[2 * k + 1] / 8388608.0f;
                int16_t s[2];
                if (fabsf(l) > peak_out)
                    peak_out = fabsf(l);
                if (fabsf(r) > peak_out)
                    peak_out = fabsf(r);
                s[0] = (int16_t)(l * 32767.0f);
                s[1] = (int16_t)(r * 32767.0f);
                if (wav) {
                    fwrite(s, 2, 2, wav);
                    wav_frames++;
                }
            }
            audio_due_ms += 256.0 * 1000.0 / 44100.0;
            for (k = 0; k < ARR_ALLTRK; k++)
                if (arr_track_hit[k] != trk_last[k]) {
                    trk_last[k] = arr_track_hit[k];
                    trk_hits[k]++;
                }
        }
        if (now_ms >= ui_due) {
            ui_due = now_ms + 16;
            ui_frame();
        } else {
            ui_input_only();
        }
        mirror_step();
        host_mirror_ms();
    }
}

/* ---------------------------------------------------------- script --- */
static const char *const BTN_N[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                      "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC_N[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};

static int find_name(const char *const *t, int n, const char *s)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(t[i], s))
            return i;
    return -1;
}

/* WHITE_K, BLACK_K: the app's (ui.c) */
static int key_of(const char *s)
{
    if (s[0] == 'w')
        return WHITE_K[atoi(s + 1) & 15];
    if (s[0] == 'b')
        return BLACK_K[atoi(s + 1) % 11];
    return atoi(s);
}

/* host-only debug buffers for poke_pattern and the pat0_ / song0_ expects (Step 4): static, as
 * pattern_t and songs_t are too big for a stack frame. */
static pattern_t dbg_pat;
static songs_t dbg_songs;
static loops_part_t dbg_loops;

static int expect(const char *what, const char *val)
{
    int got;
    if (!strcmp(what, "voices"))                  /* the synth's voices sounding */
        got = gm_voices;
    else if (!strcmp(what, "clicks_out"))         /* the click's note-ons out (channel 16) since hitreset */
        got = (int)click_count;
    else if (!strcmp(what, "click"))              /* Setup's CLICK: 0 off, 1 rec, 2 on */
        got = proj.click;
    else if (!strcmp(what, "notes_out"))          /* note-ons out (MIDI out on: every note played) since hitreset */
        got = (int)note_on_count;
    else if (!strcmp(what, "level"))              /* the output's peak, percent */
        got = (int)(gm_level * 100.0f);
    else if (!strcmp(what, "ch"))
        got = proj.ch + 1;
    else if (!strncmp(what, "program", 7))        /* programN: channel N's program (1..128) */
        got = proj.program[(atoi(what + 7) - 1) & 15] + 1;
    else if (!strcmp(what, "dirty"))              /* anything not saved */
        got = unsaved();
    else if (!strcmp(what, "chord_root"))         /* the chord now: root 0..11 (C = 0), type CH_* */
        got = (int)(arr_chord_now & 0xFFu);
    else if (!strcmp(what, "chord_type"))
        got = (int)(arr_chord_now >> 8 & 0xFFu);
    else if (!strcmp(what, "sect"))               /* SEC_INTRO 1, SEC_A 2, SEC_B 3, Fill AB 4, Fill BA 5, SEC_END 6 */
        got = arr_sect;
    else if (!strcmp(what, "next_sect"))          /* the section waiting for the bar, 0 none (7: Fill, AB or BA) */
        got = arr_next_sect;
    else if (!strncmp(what, "msg2_is_", 8)) {     /* msg2_is_WORDS: its second part starts with WORDS ("_": a space) */
        char w[24];
        int i;
        for (i = 0; i < 23 && what[8 + i]; i++)
            w[i] = what[8 + i] == '_' ? ' ' : what[8 + i];
        w[i] = 0;
        got = plat_ms() < ui.msg_until && !strncasecmp(ui.msg[1], w, strlen(w));   /* (messages are upper case) */
    } else if (!strncmp(what, "songtext_is_", 12)) {   /* songtext_is_WORDS: HOME's header style text ("_": a space) */
        char w[32], t[32];
        int i;
        for (i = 0; i < 31 && what[12 + i]; i++)
            w[i] = what[12 + i] == '_' ? ' ' : what[12 + i];
        w[i] = 0;
        song_style_text(t);
        got = !strcmp(t, w);
        if (!got)
            printf("  (song text: \"%s\")\n", t);
    } else if (!strncmp(what, "tag_is_", 7) || !strncmp(what, "footer_is_", 10)) {
        /* tag_is_WORDS / footer_is_WORDS: the tag over the knobs, the footer, exactly ("_": a space, "|": a dot) */
        char w[64], t[64];
        int i, f = what[0] == 'f', o = f ? 10 : 7;
        for (i = 0; i < 63 && what[o + i]; i++)
            w[i] = what[o + i] == '_' ? ' ' : what[o + i];
        w[i] = 0;
        if (f)
            page_footer(t);
        else
            page_tag(t);
        got = !strcmp(t, w);
        if (!got)
            printf("  (%s: \"%s\")\n", f ? "footer" : "tag", t);
    } else if (!strncmp(what, "teal_", 5)) {      /* teal_X_Y: the screen's pixel X, Y is K_TEAL (a mark drawn) */
        int x = atoi(what + 5), y = strchr(what + 5, '_') ? atoi(strchr(what + 5, '_') + 1) : 0;
        uint16_t p = fb[(y & 255) % 240 * 240 + (x & 255) % 240], t = K_TEAL;
        got = p == t || p == (uint16_t)(t >> 8 | t << 8);
    } else if (!strncmp(what, "msgtext_is_", 11)) {   /* msgtext_is_WORDS: the message, both parts, exactly ("_": a space) */
        char w[64], t[64];
        int i;
        for (i = 0; i < 63 && what[11 + i]; i++)
            w[i] = what[11 + i] == '_' ? ' ' : what[11 + i];
        w[i] = 0;
        snprintf(t, sizeof t, "%s%s%s", ui.msg[0], ui.msg[1][0] ? " " : "", ui.msg[1]);
        got = plat_ms() < ui.msg_until && !strcmp(t, w);
        if (!got)
            printf("  (message: \"%s\")\n", t);
    } else if (!strcmp(what, "home_chord_root") || !strcmp(what, "home_chord_type")) {   /* the big chord HOME shows */
        chord_t c = arr_chord_unpack(home_chord());
        got = what[11] == 'r' ? c.root : c.type;
    } else if (!strcmp(what, "home_shown"))       /* HOME held long enough: the loops and sections on screen */
        got = home_shown();
    else if (!strncmp(what, "lit_", 4)) {         /* lit_BTN: that button's light (1 / 0), as last set */
        int b = find_name(BTN_N, NB, what + 4);
        got = b >= 0 && (lit_btn >> b & 1u);
    } else if (!strncmp(what, "msg_is_", 7))      /* msg_is_WORD: the message showing starts with WORD (1 / 0) */
        got = plat_ms() < ui.msg_until && !strncasecmp(ui.msg[0], what + 7, strlen(what + 7));
    else if (!strcmp(what, "undo_steps"))         /* the undo journal's steps kept (done and undone) */
        got = uj.n;
    else if (!strcmp(what, "undo_at"))            /* how many of them are done (undo takes back the last) */
        got = uj.cur;
    else if (!strcmp(what, "msg"))                /* a message showing */
        got = plat_ms() < ui.msg_until;
    else if (!strcmp(what, "black_lit")) {        /* the black keys lit, bit i = black key i (b0..b10) */
        got = 0;
        for (int i = 0; i < 11; i++)
            got |= (int)(lit_keys >> BLACK_K[i] & 1u) << i;
    } else if (!strcmp(what, "black_dim")) {      /* the black keys lit dim, bit i = black key i (SONG: muted tracks) */
        got = 0;
        for (int i = 0; i < 11; i++)
            got |= (int)(dim_keys >> BLACK_K[i] & 1u) << i;
    } else if (!strcmp(what, "playing"))
        got = arr_playing;
    else if (!strcmp(what, "rec_wait"))           /* REC waiting for its bar line (or PLAY) */
        got = arr_rec_wait;
    else if (!strcmp(what, "rec_pass"))           /* recording a track: its pass, 1.. (from the bar it started on) */
        got = arr_rec_bars / (app.hdr[sect_slot(arr_sect)].bars ? app.hdr[sect_slot(arr_sect)].bars : 1) + 1;
    else if (!strcmp(what, "rec_shown"))          /* SEQ / EDIT show the track being recorded as bars */
        got = trk_rec_shown();
    else if (!strncmp(what, "xlo", 3) || !strncmp(what, "xhi", 3) || !strncmp(what, "xkey", 4)) {
        /* xloN / xhiN / xkeyN: the selected section's track N's range and high key (12 off) */
        const xtrack_t *x = &app.trk[sect_slot(arr_sect)][(atoi(what + (what[1] == 'k' ? 4 : 3)) - 1) & 7].x;
        got = what[1] == 'k' ? x->hikey : what[1] == 'l' ? x->lo : x->hi;
    } else if (!strncmp(what, "type", 4))           /* typeN: the selected section's track N's type, XT_* (1 Parallel) */
        got = app.trk[sect_slot(arr_sect)][(atoi(what + 4) - 1) & 7].x.type;
    else if (!strcmp(what, "recording"))
        got = arr_recording;
    else if (!strcmp(what, "page"))               /* P_* (pages.c) */
        got = ui.page;
    else if (!strcmp(what, "tempo"))              /* the song's */
        got = csong()->tempo;
    else if (!strcmp(what, "mode"))               /* 0 FREE, 1 LOOP, 2 SONG */
        got = arr_mode;
    else if (!strcmp(what, "loop_now"))           /* 1..16, 0 none */
        got = arr_loop_now + 1;
    else if (!strcmp(what, "loop_beat"))
        got = arr_loop_beat;
    else if (!strcmp(what, "override"))           /* a chord from the keys holds until the source's next change */
        got = arr_override;
    else if (!strcmp(what, "rec_chords"))         /* REC writes chords (HOME, LOOP) */
        got = ui.rec_chords;
    else if (!strcmp(what, "take"))               /* REC on HOME in FREE: a new loop's take, 0 off, 1 waiting, 2 on */
        got = ui.take;
    else if (!strcmp(what, "cbeat"))              /* the chord cursor, a beat of the loop, 0.. */
        got = app.cbeat;
    else if (!strcmp(what, "app_loop"))           /* the loop the LOOP screen shows, 1..16 */
        got = app.loop + 1;
    else if (!strcmp(what, "row"))                /* the song's row playing, 1.. */
        got = arr_row + 1;
    else if (!strcmp(what, "mutes"))              /* the engine's muted tracks now, bit t track t + 1 */
        got = arr_mutes;
    else if (!strncmp(what, "loop_bars", 9))      /* loop_barsN: loop N's bars (0: empty) */
        got = sloops.loop[(atoi(what + 9) - 1) & 15].bars;
    else if (!strncmp(what, "loop_beats", 10))    /* loop_beatsN: loop N's beats a bar */
        got = sloops.loop[(atoi(what + 10) - 1) & 15].beats;
    else if (!strncmp(what, "loop_ch", 7)) {      /* loop_chN_B: loop N's beat B, root*100 + type, -1 "." */
        int l = (atoi(what + 7) - 1) & 15, b = strrchr(what, '_') ? atoi(strrchr(what, '_') + 1) : 0;
        uint16_t v = sloops.loop[l].ch[b % ARR_LOOP_BEATS];
        got = v == LC_SAME ? -1 : lc_unpack(v).root * 100 + lc_unpack(v).type;
    } else if (!strncmp(what, "loop_name_i", 11))   /* loop_name_iN: loop N's name, the NAME list index (8: "Loop N" / custom) */
        got = loop_name_index(&sloops.loop[(atoi(what + 11) - 1) & 15]);
    else if (!strncmp(what, "loop_name", 9) && (what[9] >= '0' && what[9] <= '9')) {
        /* loop_nameN VAL: loop N's name, exactly (its own string, not "_is_"'s 0/1: "Loop 10" has a space) */
        const loop_t *l = &sloops.loop[(atoi(what + 9) - 1) & 15];
        char nm[9];
        int i, ok;
        for (i = 0; i < 8 && l->name[i]; i++)
            nm[i] = l->name[i];
        nm[i] = 0;
        ok = !strcmp(nm, val);
        printf("%s loop_name%d == \"%s\" (got \"%s\")\n", ok ? "  ok" : "FAIL", atoi(what + 9), val, nm);
        return !ok;
    }
    else if (!strcmp(what, "cstep"))              /* the cursor's step: 0 beat, 1 half, 2 bar */
        got = app.cstep;
    else if (!strcmp(what, "rows"))             /* the song's rows */
        got = csong()->nrows;
    else if (!strncmp(what, "row_loop", 8))       /* row_loopN: row N's loop, 1..16 */
        got = csong()->row[(atoi(what + 8) - 1) % ARR_ROWS].loop + 1;
    else if (!strncmp(what, "row_rep", 7))
        got = csong()->row[(atoi(what + 7) - 1) % ARR_ROWS].rep;
    else if (!strncmp(what, "row_sect", 8))       /* ROW_A 0, ROW_A_FILL 1, ROW_B 2, ROW_B_FILL 3 */
        got = csong()->row[(atoi(what + 8) - 1) % ARR_ROWS].sect;
    else if (!strncmp(what, "row_mute", 8))
        got = csong()->row[(atoi(what + 8) - 1) % ARR_ROWS].mute;
    else if (!strncmp(what, "row_key", 7))
        got = csong()->row[(atoi(what + 7) - 1) % ARR_ROWS].key;
    else if (!strcmp(what, "cursor"))             /* SONG's cursor row, 1.. (the END row: rows + 1) */
        got = app.row + 1;
    else if (!strcmp(what, "song_intro"))         /* the song's INTRO, ENDING, AT END (0 stop, 1 again) */
        got = csong()->intro;
    else if (!strcmp(what, "song_ending"))
        got = csong()->ending;
    else if (!strcmp(what, "song_at_end"))
        got = csong()->at_end;
    else if (!strcmp(what, "song_rec"))           /* REC on SONG: rows written from HOME + keys */
        got = ui.song_rec;
    else if (!strcmp(what, "row_follow"))         /* SONG's cursor follows the row playing */
        got = ui.row_follow;
    else if (!strcmp(what, "transpose"))          /* the song's, -6..+5 */
        got = csong()->transpose;
    else if (!strcmp(what, "track"))              /* the selected track, 1..8 */
        got = proj.trk + 1;
    else if (!strcmp(what, "mirror_on"))          /* the screen mirrored over USB */
        got = mirror_on;
    else if (!strcmp(what, "mirror_drops"))       /* frames that did not fit the ring (their rows dirty) */
        got = (int)mr.drops;
    else if (!strcmp(what, "mirror_redraws"))     /* the UI asked to draw again (dirty rows, a refresh) */
        got = (int)mr.redraws;
    else if (!strcmp(what, "mirror_dirty")) {     /* rows the viewer has wrong now */
        got = 0;
        for (uint32_t r = 0; r < 240u; r++)
            got += mr_bit(mr.dirty, r);
    } else if (!strcmp(what, "mirror_inputs"))    /* input reports sent */
        got = (int)mr.inputs;
    else if (!strcmp(what, "restore_rc"))         /* the last restore: 0 done, 10 playing (STOP FIRST), else damaged */
        got = restore_rc;
    else if (!strcmp(what, "store_writes"))
        got = (int)store_writes;
    else if (!strcmp(what, "store_tries"))        /* saves tried, failed ones too */
        got = (int)store_tries;
    else if (!strcmp(what, "bar"))                /* the pattern's bar playing, 0.. */
        got = arr_bar;
    else if (!strcmp(what, "ext"))                /* following MIDI clock in */
        got = arr_ext;
    else if (!strcmp(what, "song"))               /* the selected song, 1..4 */
        got = proj.song + 1;
    else if (!strcmp(what, "bank_count"))         /* the QY70 bank's styles */
        got = qb_count();
    else if (!strcmp(what, "bank_status"))        /* qb_status(): styles, 0 none, -1 damaged, -2 old */
        got = qb_status();
    else if (!strncmp(what, "slot", 4) && strstr(what, "_bank"))    /* slotN_bank: 1 plays a QY70 style's section */
        got = app.slot_buf[atoi(what + 4) % ARR_NSECT] >= 0;
    else if (!strncmp(what, "slot", 4) && strstr(what, "_trk") && (strstr(what, "_note") || strstr(what, "_tick") ||
                                                                strstr(what, "_count"))) {
        /* slotS_trkT_noteI / _tickI: the arranger's slot S's track T (1..8), its event I (0..): pitch, tick (-1 none);
         * slotS_trkT_count: its notes */
        const char *q = strstr(what, "_trk") + 4, *f = strchr(q, '_');
        int t = (atoi(q) - 1) & 7, i = f ? atoi(f + 5) : 0;
        const arr_trk_t *tr;
        arr_pattern_copy(atoi(what + 4) % ARR_NSECT, &dbg_pat);
        tr = &dbg_pat.trk[t];
        if (f && !strncmp(f, "_count", 6))
            got = tr->count;
        else if (i < 0 || i >= tr->count)
            got = -1;
        else
            got = f && !strncmp(f, "_note", 5) ? dbg_pat.ev[tr->first + i].note : dbg_pat.ev[tr->first + i].tick;
    } else if (!strncmp(what, "slot", 4) && strstr(what, "_nev")) {   /* slotN_nev: the arranger's slot N's notes (0 Intro, 1 Main A, ..) */
        arr_pattern_copy(atoi(what + 4) % ARR_NSECT, &dbg_pat);
        got = dbg_pat.nev;
    }
    else if (!strncmp(what, "pat", 3) && strstr(what, "_nev")) {   /* patN_nev: user pattern N's notes (0..23) */
        project_pattern_load(atoi(what + 3), &dbg_pat);
        got = dbg_pat.nev;
    } else if (!strncmp(what, "track_hits", 10))  /* track_hitsN (1..10: 9, 10 LD1, LD2): its note-ons since hitreset */
        got = (int)trk_hits[(unsigned)(atoi(what + 10) - 1) % ARR_ALLTRK];
    else if (!strcmp(what, "lead_notes"))         /* the song's lead notes (both tracks) */
        got = leads.n;
    else if (!strcmp(what, "loops_slot"))         /* the song's loops' slot, 1..16, 0 none (the defaults) */
        got = app.lpslot + 1;
    else if (!strncmp(what, "lead_slot", 9))      /* lead_slotK (1, 2): part K's slot, 1..16, 0 none */
        got = app.lslot[(atoi(what + 9) - 1) & 1] + 1;
    else if (!strcmp(what, "lead_dirty"))         /* the lead parts unsaved (bit k part k + 1) */
        got = app.ldirty;
    else if (!strcmp(what, "lead_rowtick"))       /* the row playing's place, ticks (arr_row_tick) */
        got = (int)arr_row_tick;
    else if (!strcmp(what, "lead_sel"))           /* the lead track selected: 1 LD1, 2 LD2, 0 none (a pattern's track) */
        got = app.lead;
    else if (!strncmp(what, "lead_count", 10))    /* lead_countK_R: lead K's (1, 2) notes in row R (1..; 0: all rows) */
        got = leads_count(&leads, atoi(strrchr(what, '_') + 1) - 1, (what[10] - '1') & 1);
    else if (!strncmp(what, "lsnd", 4) && strchr(what, '_')) {
        /* lsndK_prog / _chan / _vol / _pan / _rev / _cho / _bright / _vel: lead K's (1, 2) sound, the song's (prog 1..128, chan 1..16);
         * engsndK_prog: the engine's copy (leads.trk) */
        const lead_trk_t *l = &csong()->lead[(what[4] - '1') & 1];
        const char *f = strchr(what, '_') + 1;
        got = !strcmp(f, "prog") ? l->program + 1 : !strcmp(f, "chan") ? l->chan + 1 : !strcmp(f, "vol") ? l->vol
            : !strcmp(f, "pan") ? l->pan : !strcmp(f, "rev") ? l->rev : !strcmp(f, "cho") ? l->cho
            : !strcmp(f, "bright") ? (l->bright ^ 64) & 127 : !strcmp(f, "vel") ? l->vel : -1;
    } else if (!strncmp(what, "engsnd", 6) && strstr(what, "_prog"))
        got = leads.trk[(what[6] - '1') & 1].program + 1;
    else if (!strncmp(what, "row_lmute", 9))      /* row_lmuteN: row N's lead mutes, bit 0 LD1, bit 1 LD2 */
        got = csong()->row[(atoi(what + 9) - 1) % ARR_ROWS].lmute;
    else if (!strncmp(what, "lead", 4) && strchr(what, '_') && what[4] >= '0' && what[4] <= '9') {
        /* leadI_row / _pos / _note / _root / _trk / _len / _vel: lead note I (1..) of the list; -1 none */
        int i = atoi(what + 4) - 1;
        const char *f = strchr(what, '_') + 1;
        const lead_ev_t *e = i >= 0 && i < leads.n ? lead_at_c(&leads, (uint32_t)i) : 0;
        got = !e ? -1 : !strcmp(f, "row") ? e->row + 1 : !strcmp(f, "pos") ? (int)lead_pos(e) : !strcmp(f, "note") ? e->note
            : !strcmp(f, "root") ? lead_root(e) : !strcmp(f, "trk") ? lead_trk(e) + 1 : !strcmp(f, "len") ? (int)lead_len(e)
            : !strcmp(f, "vel") ? e->vel : -1;
    } else if (!strncmp(what, "slot_kind", 9)) {  /* slot_kindN (1..16): SLOT_NONE 0, PATTERN 1, LEAD 2, OTHER 3, LOOPS 4 */
        slot_info_t si;
        got = project_slot_info((atoi(what + 9) - 1) & 15, &si, &dbg_pat);
    }
    else if (!strncmp(what, "trk", 3) && strstr(what, "_program")) {   /* trkN_program: the playing slot's track N */
        arr_pattern_copy(sect_slot(arr_sect), &dbg_pat);
        got = dbg_pat.trk[(atoi(what + 3) - 1) & 7].program + 1;
    } else if (!strncmp(what, "trk", 3) && strstr(what, "_mute"))   /* trkN_mute: track N muted (the song's) */
        got = trk_muted(atoi(what + 3) - 1);
    else if (!strncmp(what, "ptrk", 4) && strstr(what, "_mute")) {   /* ptrkN_mute: the playing slot's pattern's own */
        arr_pattern_copy(sect_slot(arr_sect), &dbg_pat);
        got = dbg_pat.trk[(atoi(what + 4) - 1) & 7].mute;
    } else if (!strncmp(what, "trk", 3) && strstr(what, "_bright")) {   /* trkN_bright / _reso: CC 74 / 71 */
        arr_pattern_copy(sect_slot(arr_sect), &dbg_pat);
        got = arr_bright(&dbg_pat.trk[(atoi(what + 3) - 1) & 7]);
    } else if (!strncmp(what, "trk", 3) && strstr(what, "_reso")) {
        arr_pattern_copy(sect_slot(arr_sect), &dbg_pat);
        got = arr_reso(&dbg_pat.trk[(atoi(what + 3) - 1) & 7]);
    } else if (!strncmp(what, "trk", 3) && strstr(what, "_vol")) {
        arr_pattern_copy(sect_slot(arr_sect), &dbg_pat);
        got = dbg_pat.trk[(atoi(what + 3) - 1) & 7].vol;
    } else if (!strncmp(what, "slot", 4) && strstr(what, "_bars")) {
        arr_pattern_copy(atoi(what + 4) % ARR_NSECT, &dbg_pat);
        got = dbg_pat.bars;
    } else if (!strncmp(what, "song", 4) && strstr(what, "_mute") && what[4] >= '1' && what[4] <= '4')
        got = songs.song[what[4] - '1'].mute;     /* songN_mute: its muted tracks, bit t track t + 1 */
    else if (!strncmp(what, "song", 4) && strstr(what, "_sty") && !strstr(what, "_style") && what[4] >= '1' &&
             what[4] <= '4')
        got = songs.song[what[4] - '1'].sty;      /* songN_sty: its style, the Intro's pattern number (32: Pop) */
    else if (!strncmp(what, "song", 4) && strstr(what, "_style") && what[4] >= '1' && what[4] <= '4')
        got = songs.song[what[4] - '1'].style[atoi(strstr(what, "_style") + 6) % ARR_NSECT];   /* songN_styleI (I: the slot) */
    else if (!strncmp(what, "song", 4) && strstr(what, "_rows") && what[4] >= '1' && what[4] <= '4')
        got = songs.song[what[4] - '1'].nrows;    /* songN_rows (1..4), as in RAM */
    else if (!strncmp(what, "song", 4) && strstr(what, "_tempo") && what[4] >= '1' && what[4] <= '4')
        got = songs.song[what[4] - '1'].tempo;    /* songN_tempo (1..4), as in RAM */
    else if (!strcmp(what, "clock"))
        got = proj.clock;
    else if (!strcmp(what, "quant"))              /* the record grid: ARR_Q_8 0, 8T 1, 16 2, 16T 3, 32 4, off 5 */
        got = proj.quant;
    else if (!strcmp(what, "grid_bar"))           /* the Grid knob's bar, as drawn: its fill */
        got = grid_fill();
    else if (!strcmp(what, "slots_list"))         /* the SLOTS list on screen */
        got = lst.open && lst.slots;
    else if (!strncmp(what, "slot_shown", 10))    /* slot_shownN (1..16): its kind as SLOTS shows it (0 free, 1 pattern,
                                                   * 2 lead part, 4 loops) */
        got = slot_kind[(atoi(what + 10) - 1) & 15];
    else if (!strcmp(what, "list_open"))          /* SEL's list on screen */
        got = lst.open;
    else if (!strcmp(what, "list_sel"))           /* the list's row picked, 0.. */
        got = lst.sel;
    else if (!strcmp(what, "card"))               /* a help card on screen: its button + 1 (B_*), 0 none */
        got = ui.card;
    else if (!strcmp(what, "asking"))             /* a question on screen (SEL yes, HOME no) */
        got = ui.ask;
    else if (!strcmp(what, "name_cur"))           /* the name's letter edited, 0..7 */
        got = ui.name_cur;
    else if (!strcmp(what, "rev_type"))
        got = proj.rev_type;
    else if (!strcmp(what, "song0_tempo")) {      /* song 0's tempo, as saved */
        project_songs_load(&dbg_songs);
        got = dbg_songs.song[0].tempo;
    } else if (!strcmp(what, "song0_rows")) {      /* song 0's rows, as saved */
        project_songs_load(&dbg_songs);
        got = dbg_songs.song[0].nrows;
    } else if (!strcmp(what, "loop9_ch0")) {       /* song 1's loop 9's beat 0, as saved: root*100 + type, -1 "." */
        int8_t sl;
        project_loops_load(0, &dbg_loops, &sl);
        got = dbg_loops.loop[8].ch[0] == LC_SAME ? -1 : lc_unpack(dbg_loops.loop[8].ch[0]).root * 100 +
                                                            lc_unpack(dbg_loops.loop[8].ch[0]).type;
    } else if (!strcmp(what, "peak_db_max") || !strcmp(what, "peak_db_min")) {
        double db = 20.0 * log10(peak_out > 1e-9f ? peak_out : 1e-9f);
        int bad = what[8] == 'm' && what[9] == 'a' ? db > atof(val) : db < atof(val);
        printf("%s peak %.1f dBFS (%s %s)\n", bad ? "FAIL" : "  ok", db, what + 8, val);
        return bad;
    } else {
        printf("FAIL unknown expect %s\n", what);
        return 1;
    }
    if (val[0] == '<' || val[0] == '>') {
        int lim = (int)strtol(val + 1, 0, 0);
        if (val[0] == '<' ? got >= lim : got <= lim) {
            printf("FAIL expect %s %s, got %d\n", what, val, got);
            return 1;
        }
        printf("  ok %s == %d (%s)\n", what, got, val);
        return 0;
    }
    if (got != (int)strtol(val, 0, 0)) {
        printf("FAIL expect %s == %s, got %d\n", what, val, got);
        return 1;
    }
    printf("  ok %s == %d\n", what, got);
    return 0;
}

#ifndef GM_WEB
int main(int argc, char **argv)
{
    FILE *sc;
    char line[512], out[400];
    const char *dir = argc > 2 ? argv[2] : ".";
    int lineno = 0, fails = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: polly_host SCRIPT [OUTDIR]\n");
        return 2;
    }
    sc = fopen(argv[1], "r");
    if (!sc) {
        perror(argv[1]);
        return 2;
    }
    snprintf(out, sizeof out, "%s/midi_out.txt", dir);
    midi_log = fopen(out, "w");
    boot();
    while (fgets(line, sizeof line, sc)) {
        char cmd[32] = {0}, a[256] = {0}, b[64] = {0}, c[64] = {0};
        lineno++;
        if (line[0] == '#' || sscanf(line, "%31s %255s %63s %63s", cmd, a, b, c) < 1)
            continue;
        if (!strcmp(cmd, "wait"))
            run_ms((uint32_t)atoi(a));
        else if (!strcmp(cmd, "press") || !strcmp(cmd, "release") || !strcmp(cmd, "tap")) {
            int i = find_name(BTN_N, NB, a);
            if (i < 0) {
                printf("line %d: no button %s\n", lineno, a);
                return 2;
            }
            if (cmd[0] != 'r')
                held_btn |= 1u << i;
            if (cmd[0] == 't')
                run_ms(40);
            if (cmd[0] != 'p')
                held_btn &= ~(1u << i);
            run_ms(cmd[0] == 't' ? 40 : 2);
        } else if (!strcmp(cmd, "key") || !strcmp(cmd, "tapkey")) {
            int k = key_of(a);
            if (cmd[0] == 't') {
                held_keys |= 1u << k;
                run_ms(60);
                held_keys &= ~(1u << k);
                run_ms(30);
            } else {
                if (!strcmp(b, "down"))
                    held_keys |= 1u << k;
                else
                    held_keys &= ~(1u << k);
                run_ms(2);
            }
        } else if (!strcmp(cmd, "strum")) {          /* strum A B [MS]: a finger across the white keys */
            int x = atoi(a), y = atoi(b), gap = c[0] ? atoi(c) : 25, s = y >= x ? 1 : -1;
            for (;; x += s) {
                held_keys |= 1u << WHITE_K[x & 15];
                run_ms((uint32_t)gap);
                held_keys &= ~(1u << WHITE_K[x & 15]);
                if (x == y)
                    break;
            }
            run_ms(2);
        } else if (!strcmp(cmd, "turn")) {
            int e = find_name(ENC_N, NE, a), n = atoi(b), s = n > 0 ? 1 : -1;
            uint32_t gap = c[0] ? (uint32_t)atoi(c) : 80u;
            if (e < 0) {
                printf("line %d: no encoder %s\n", lineno, a);
                return 2;
            }
            for (; n; n -= s) {
                enc_acc[e] += s;
                run_ms(gap);
            }
        } else if (!strcmp(cmd, "spin")) {
            int e = find_name(ENC_N, NE, a);
            if (e < 0) {
                printf("line %d: no encoder %s\n", lineno, a);
                return 2;
            }
            enc_acc[e] += atoi(b);
            accel_off = 1;
            run_ms(1);
            accel_off = 0;
        } else if (!strcmp(cmd, "clock")) {        /* clock N [MS]: N MIDI clocks in (F8), MS apart (default 21) */
            int n = atoi(a), gap = b[0] ? atoi(b) : 21;
            for (; n > 0; n--) {
                min_q[mi_w++ % MQ] = 0x0Fu | 0xF8u << 8;
                run_ms((uint32_t)gap);
            }
        } else if (!strcmp(cmd, "bank")) {          /* bank FILE: an image into the bank's flash, as the device writes it */
            static uint8_t img[0x50000];
            FILE *bf;
            if (!strncmp(a, "out:", 4)) {
                snprintf(out, sizeof out, "%s/%s", dir, a + 4);
                bf = fopen(out, "rb");
            } else
                bf = fopen(a, "rb");
            long n;
            uint8_t z[1];
            if (!bf) {
                printf("line %d: no bank image %s\n", lineno, a);
                return 2;
            }
            n = (long)fread(img, 1, sizeof img, bf);
            fclose(bf);
            qb_flash_read(0, z, 0);                       /* (the flash erased first) */
            memset(sim_flash + QB_DIR, 0xFF, QB_SECTOR);
            for (uint32_t i = 0; i < QB_NSECT; i++)
                memset(sim_flash + qb_sector(i), 0xFF, QB_SECTOR);
            for (long i = QB_SECTOR; i < n; i++)
                sim_flash[qb_phys((uint32_t)(i - QB_SECTOR))] = img[i];
            if (n >= (long)QB_SECTOR)
                memcpy(sim_flash + QB_DIR, img, QB_SECTOR);  /* the directory last */
            app_bank_changed();
            run_ms(1);
        } else if (!strcmp(cmd, "backup") || !strcmp(cmd, "restore")) {
            if (!strncmp(a, "out:", 4))
                snprintf(out, sizeof out, "%s/%s", dir, a + 4);
            else
                snprintf(out, sizeof out, "%s", a);
            if (cmd[0] == 'b') {
                if (sim_backup(out)) {
                    printf("line %d: cannot write %s\n", lineno, out);
                    return 2;
                }
            } else {                                       /* (the device's message after it: main_fm1.c) */
                restore_rc = sim_restore(out);
                ui_say(restore_rc == 10 ? "STOP FIRST" : "RESTORE FROM USB",
                       !restore_rc ? "RESTORED" : restore_rc == 10 ? "then restore" : "NOT RESTORED");
            }
            run_ms(1);
        } else if (!strcmp(cmd, "backup_damage")) {  /* host-only: a byte of a backup file changed (at its middle) */
            FILE *bf;
            long n;
            snprintf(out, sizeof out, "%s/%s", dir, !strncmp(a, "out:", 4) ? a + 4 : a);
            if ((bf = fopen(out, "r+b"))) {
                fseek(bf, 0, SEEK_END);
                n = ftell(bf);
                fseek(bf, n / 2, SEEK_SET);
                int c = fgetc(bf);
                fseek(bf, n / 2, SEEK_SET);
                fputc(c ^ 1, bf);
                fclose(bf);
            }
        } else if (!strcmp(cmd, "bank_damage")) {  /* host-only: a byte of the bank's style N changed in the flash */
            const uint8_t *e = sim_flash + QB_DIR + QB_DIR_HEAD + QB_DIR_ENTRY * (uint32_t)atoi(a);
            uint32_t off = e[8] | e[9] << 8 | (uint32_t)e[10] << 16;
            sim_flash[qb_phys(off + 30)] ^= 0x10;
        } else if (!strcmp(cmd, "poke_full_patterns")) {   /* host-only: all 16 user patterns saved with a note, none
                                                             * played by a song */
            pattern_empty(&dbg_pat);
            dbg_pat.nev = 1;
            dbg_pat.trk[4].first = 0;
            dbg_pat.trk[4].count = 1;
            dbg_pat.ev[0].len = ARR_PPQ;
            dbg_pat.ev[0].note = 60;
            dbg_pat.ev[0].vel = 100;
            for (int i = 0; i < ARR_USER_PATTERNS; i++)
                project_pattern_save(i, &dbg_pat);
        } else if (!strcmp(cmd, "style")) {         /* style NAME: all six sections from it (style_all) */
            int qy = !strncmp(a, "qy:", 3), i, found = -1;
            char *nm = a + (qy ? 3 : 0);
            for (char *c = nm; *c; c++)
                if (*c == '_')
                    *c = ' ';
            for (i = qy ? arr_styles() : 0; i < styles_n() && found < 0; i++)
                if (!strcmp(style_label(i), nm))
                    found = i;
            if (found < 0) {
                printf("line %d: no style %s\n", lineno, nm);
                return 2;
            }
            style_all(found);
            run_ms(20);
        } else if (!strcmp(cmd, "hitreset")) {
            memset(trk_hits, 0, sizeof trk_hits);
            click_count = 0;
        }
        else if (!strcmp(cmd, "flash_fail"))
            store_fail = atoi(a);
        else if (!strcmp(cmd, "poke_user_songs")) {   /* host-only: songs 1 and 2 on user patterns P01..P06, P07..P12 */
            songs_defaults(&dbg_songs);
            for (int n = 0; n < 2; n++)
                for (int k = 0; k < ARR_NSECT; k++)
                    dbg_songs.song[n].style[k] = (uint16_t)(n * ARR_NSECT + k);
            project_songs_save(&dbg_songs);
        } else if (!strcmp(cmd, "poke_lead_rec")) {   /* host-only: poke_lead_rec K: lead K (1, 2) selected and armed (the keys
                                                       * play it, a song's rows record it; 0: back to the track, off), as
                                                       * ALGORITHM past C4 and REC do */
            int k = atoi(a);
            app.lead = (uint8_t)(k >= 1 && k <= 2 ? k : 0);
            arr_record(k >= 1 && k <= 2 ? ARR_LD1 + k - 1 : proj.trk, k >= 1 && k <= 2);
            run_ms(20);
        } else if (!strcmp(cmd, "poke_row")) {        /* host-only: poke_row L: a row of loop L (1..16) at the song's end
                                                       * (until the SONG screen writes rows) */
            songs_write(1);
            row_tap(csong(), csong()->nrows, (atoi(a) - 1) & 15);
            songs_write(0);
        } else if (!strcmp(cmd, "poke_share")) {      /* host-only: song 1 plays one user pattern as Main A and Main B */
            songs_defaults(&dbg_songs);
            for (int k = 0; k < ARR_NSECT; k++)
                dbg_songs.song[0].style[k] = (uint16_t)k;
            dbg_songs.song[0].style[SEC_B - 1] = dbg_songs.song[0].style[SEC_A - 1];
            project_songs_save(&dbg_songs);
        } else if (!strcmp(cmd, "master"))
            master = (uint32_t)atoi(a);
        else if (!strcmp(cmd, "midi")) {
            uint32_t s = (uint32_t)strtoul(a, 0, 16), d1 = (uint32_t)strtoul(b, 0, 16), d2 = (uint32_t)strtoul(c, 0, 16);
            uint32_t cin = s >= 0xF0 ? 0x0F : s >> 4;
            min_q[mi_w++ % MQ] = cin | s << 8 | d1 << 16 | d2 << 24;
        } else if (!strcmp(cmd, "wav")) {
            snprintf(out, sizeof out, "%s/%s", dir, a);
            wav_open(out);
        } else if (!strcmp(cmd, "wavstop"))
            wav_close();
        else if (!strcmp(cmd, "peakreset"))
            peak_out = 0.0f;
        else if (!strcmp(cmd, "mirror")) {          /* mirror FILE | off | refresh | nokeep: the screen mirrored over
                                                       * USB (firmware/src/mirror.c) into FILE (SysEx), as a viewer asks */
            if (!strcmp(a, "off"))
                mirror_cmd(0);
            else if (!strcmp(a, "refresh"))
                mirror_cmd(2);
            else if (!strcmp(a, "nokeep"))            /* the viewer stops its keep-alive (gone without "off") */
                mr_keep = 0;
            else if (!strcmp(a, "unplug")) {          /* the cable out (once what was queued went out): no host */
                if (mr_out) {
                    uint32_t r0 = mr_rate;
                    mr_rate = MRQ + sizeof hctl;
                    host_mirror_ms();
                    mr_rate = r0;
                    fclose(mr_out);
                }
                mr_out = 0;
                mr_mid = 0;
                mr_link_n++;
            } else {
                snprintf(out, sizeof out, "%s/%s", dir, !strncmp(a, "out:", 4) ? a + 4 : a);
                if (mr_out)
                    fclose(mr_out);
                if (!(mr_out = fopen(out, "wb"))) {
                    printf("line %d: cannot write %s\n", lineno, out);
                    return 2;
                }
                mr_keep = 1;
                mr_keep_t = now_ms;
                mirror_cmd(2);
            }
            run_ms(1);
        } else if (!strcmp(cmd, "mirror_rate"))   /* mirror_rate N: USB's pace for the mirror, bytes a millisecond */
            mr_rate = (uint32_t)atoi(a);
        else if (!strcmp(cmd, "fbdump")) {         /* fbdump FILE: the screen's 240 x 240 pixels as stored (RGB565, the
                                                     * panel's byte order: high byte first) */
            FILE *f;
            snprintf(out, sizeof out, "%s/%s", dir, a);
            if (!(f = fopen(out, "wb"))) {
                printf("line %d: cannot write %s\n", lineno, out);
                return 2;
            }
            fwrite(fb, 2, 240 * 240, f);
            fclose(f);
        } else if (!strcmp(cmd, "shot")) {
            snprintf(out, sizeof out, "%s/%s", dir, a);
            png_write(out);
        } else if (!strcmp(cmd, "leds")) {
            int i;
            printf("  leds: buttons");
            for (i = 0; i < NB; i++)
                if (lit_btn >> i & 1u)
                    printf(" %s", BTN_N[i]);
            printf(" | white ");
            for (i = 0; i < 16; i++)
                printf("%c", (lit_keys >> WHITE_K[i] & 1u) ? '#' : '.');
            printf(" black ");
            for (i = 0; i < 11; i++)
                printf("%c", (lit_keys >> BLACK_K[i] & 1u) ? '#' : '.');
            printf("\n");
        } else if (!strcmp(cmd, "expect"))
            fails += expect(a, b);
        else if (!strcmp(cmd, "poke_pattern")) {     /* host-only debug (Step 4): a known pattern and song */
            pattern_empty(&dbg_pat);
            dbg_pat.nev = 3;
            dbg_pat.trk[3].first = 0;
            dbg_pat.trk[3].count = 3;
            dbg_pat.ev[0].tick = 0;
            dbg_pat.ev[0].len = ARR_PPQ;
            dbg_pat.ev[0].note = 36;
            dbg_pat.ev[0].vel = 100;
            dbg_pat.ev[1].tick = ARR_PPQ;
            dbg_pat.ev[1].len = ARR_PPQ;
            dbg_pat.ev[1].note = 40;
            dbg_pat.ev[1].vel = 100;
            dbg_pat.ev[2].tick = 2 * ARR_PPQ;
            dbg_pat.ev[2].len = ARR_PPQ;
            dbg_pat.ev[2].note = 43;
            dbg_pat.ev[2].vel = 100;
            project_pattern_save(SEC_A - 1, &dbg_pat);    /* song 1's Main A (user pattern 2) */
            songs_defaults(&dbg_songs);
            dbg_songs.song[0].tempo = 100;
            dbg_songs.song[0].nrows = 2;
            project_songs_save(&dbg_songs);
            {
                chord_t am7 = {9, CH_MIN7, CH_NOBASS};          /* song 1's L9's first beat: A m7 (its loops in P16) */
                int8_t sl = 15;
                loops_default(dbg_loops.loop);
                loop_write(&dbg_loops.loop[8], 0, am7);
                project_loops_save(0, &dbg_loops, &sl);
            }
        } else if (!strcmp(cmd, "poke_bad_style")) {   /* host-only: songs with pattern numbers past the styles */
            songs_defaults(&dbg_songs);
            dbg_songs.song[0].style[SEC_A - 1] = (uint16_t)(ARR_FACTORY0 + ARR_NSECT * arr_styles());
            dbg_songs.song[1].style[SEC_FILL_AB - 1] = 250;
            dbg_songs.song[1].style[SEC_B - 1] = (uint16_t)(ARR_FACTORY0 + ARR_NSECT * arr_styles() - 1);   /* the last: kept */
            dbg_songs.song[1].style[SEC_INTRO - 1] = ARR_USER_PATTERNS;                  /* past the user patterns */
            dbg_songs.song[1].style[SEC_END - 1] = 0xFFFF;
            project_songs_save(&dbg_songs);
        } else if (!strcmp(cmd, "poke_muted_pattern")) {   /* host-only: P01 saved with C1 muted (patterns held their
                                                             * mutes before they were the song's), song 1's Main A */
            pattern_empty(&dbg_pat);
            dbg_pat.nev = 1;
            dbg_pat.trk[4].first = 0;
            dbg_pat.trk[4].count = 1;
            dbg_pat.ev[0].len = ARR_PPQ;
            dbg_pat.ev[0].note = 60;
            dbg_pat.ev[0].vel = 100;
            dbg_pat.trk[4].mute = 1;
            project_pattern_save(0, &dbg_pat);
            songs_defaults(&dbg_songs);
            dbg_songs.song[0].style[SEC_A - 1] = 0;
            project_songs_save(&dbg_songs);
        } else if (!strcmp(cmd, "poke_bad_pattern")) {   /* host-only debug: an invalid nev, straight into storage */
            pattern_empty(&dbg_pat);
            dbg_pat.nev = ARR_NEV + 1;
            project_pattern_save(SEC_A - 1, &dbg_pat);
        } else if (!strcmp(cmd, "reboot")) {
            memset(&proj, 0, sizeof proj);
            boot();
        } else if (!strcmp(cmd, "echo"))
            printf("%s", line + 5);
        else {
            printf("line %d: unknown command %s\n", lineno, cmd);
            return 2;
        }
    }
    wav_close();
    if (midi_log)
        fclose(midi_log);
    fails += msg_too_wide;
    if (mr.frames) {
        printf("mirror: %u frames, %u bytes (%u rects, %u rows sent, %u rows skipped, %u drops, %u redraws, %u inputs; "
               "%u pixels blitted)\n", mr.frames, mr.bytes, mr.rects, mr.rows, mr.skipped, mr.drops, mr.redraws,
               mr.inputs, mr.px);
    }
    if (mr_out)
        fclose(mr_out);
    printf("host: %u ms simulated, %u blits, render %.1f%% of real time (host CPU)%s\n", now_ms, blits,
           render_budget_ns_total > 0 ? 100.0 * render_ns_total / render_budget_ns_total : 0.0,
           fails ? ", EXPECTATIONS FAILED" : "");
    return fails ? 1 : 0;
}
#endif /* GM_WEB */
