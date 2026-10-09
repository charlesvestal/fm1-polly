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
 *   style NAME                   the song's six sections from the style named NAME ("_" for a space; "qy:NAME": the
 *                                QY70 bank's only), as the Style knob sets them
 */
#define GM_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#define __attribute__(x)

/* ------------------------------------------------------------ screen --- */
static uint16_t fb[240 * 240];          /* RGB565, byte-swapped as the panel takes it (gfx.c) */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
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
}
#include "../firmware/src/gfx.c"


/* ---------------------------------------------------------- platform --- */
#include "../firmware/src/app/plat.h"

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
    return v;
}
uint32_t plat_master(void) { return master; }
void plat_leds(uint32_t b, uint32_t k)
{
    lit_btn = b;
    lit_keys = k;
}
static int glow_on = 1;                 /* the device's button glow (plat_glow); the lights printed are the bright ones */
void plat_glow(int on) { glow_on = on; }
void plat_play_red(int red) { (void)red; }      /* the device's PLAY colour: green, red while recording */

#define MQ 256
static uint32_t min_q[MQ], mi_w, mi_r;
static FILE *midi_log;
static uint32_t midi_out_count, clock_out_count, note_on_count;
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

static uint32_t trk_hits[8], trk_last[8];  /* note-ons seen per track since hitreset (arr_track_hit moving) */

static uint32_t cpu_pct;
uint32_t plat_cpu_pct(void) { return cpu_pct; }
uint32_t plat_xruns(void) { return 0; }

/* -------------------------------------------------------------- app --- */
#include "../firmware/src/app/app.h"
#include "../firmware/src/app/project.c"
#include "../firmware/src/app/pages.c"
#include "../firmware/src/app/ui.c"

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
            for (k = 0; k < 8; k++)
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

static int expect(const char *what, const char *val)
{
    int got;
    if (!strcmp(what, "voices"))                  /* the synth's voices sounding */
        got = gm_voices;
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
    else if (!strncmp(what, "msg_is_", 7))        /* msg_is_WORD: the message showing starts with WORD (1 / 0) */
        got = plat_ms() < ui.msg_until && !strncmp(ui.msg[0], what + 7, strlen(what + 7));
    else if (!strcmp(what, "msg"))                /* a message showing */
        got = plat_ms() < ui.msg_until;
    else if (!strcmp(what, "black_lit")) {        /* the black keys lit, bit i = black key i (b0..b10) */
        got = 0;
        for (int i = 0; i < 11; i++)
            got |= (int)(lit_keys >> BLACK_K[i] & 1u) << i;
    } else if (!strcmp(what, "playing"))
        got = arr_playing;
    else if (!strcmp(what, "recording"))
        got = arr_recording;
    else if (!strcmp(what, "template"))           /* the template playing, -1 none */
        got = arr_template_now;
    else if (!strcmp(what, "page"))               /* P_* (pages.c) */
        got = ui.page;
    else if (!strcmp(what, "chord_mode"))
        got = ui.chord;
    else if (!strcmp(what, "tempo"))
        got = proj.tempo;
    else if (!strcmp(what, "track"))              /* the selected track, 1..8 */
        got = proj.trk + 1;
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
    else if (!strncmp(what, "slot", 4) && strstr(what, "_nev")) {   /* slotN_nev: the arranger's slot N's notes (0 Intro, 1 Main A, ..) */
        arr_pattern_copy(atoi(what + 4) % ARR_NSECT, &dbg_pat);
        got = dbg_pat.nev;
    }
    else if (!strncmp(what, "pat", 3) && strstr(what, "_nev")) {   /* patN_nev: user pattern N's notes (0..23) */
        project_pattern_load(atoi(what + 3), &dbg_pat);
        got = dbg_pat.nev;
    } else if (!strncmp(what, "track_hits", 10))  /* track_hitsN (1..8): its note-ons since hitreset */
        got = (int)trk_hits[(atoi(what + 10) - 1) & 7];
    else if (!strncmp(what, "trk", 3) && strstr(what, "_program")) {   /* trkN_program: the playing slot's track N */
        arr_pattern_copy(sect_slot(arr_sect), &dbg_pat);
        got = dbg_pat.trk[(atoi(what + 3) - 1) & 7].program + 1;
    } else if (!strncmp(what, "trk", 3) && strstr(what, "_mute")) {
        arr_pattern_copy(sect_slot(arr_sect), &dbg_pat);
        got = dbg_pat.trk[(atoi(what + 3) - 1) & 7].mute;
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
    } else if (!strncmp(what, "song_chord", 10)) {   /* song_chordN: the selected song's beat N, root*100 + type */
        const chord_t *c = &songs.song[proj.song].chord[atoi(what + 10) & 255];
        got = c->root * 100 + c->type;
    } else if (!strcmp(what, "cur_chord")) {      /* the chord at the Chords cursor */
        const chord_t *c = &songs.song[proj.song].chord[app.cbeat];
        got = c->root * 100 + c->type;
    } else if (!strncmp(what, "song_sect", 9))    /* song_sectN: bar N's section mark */
        got = songs.song[proj.song].sect[atoi(what + 9) & 63];
    else if (!strcmp(what, "song_bars"))
        got = songs.song[proj.song].bars;
    else if (!strncmp(what, "song", 4) && strstr(what, "_style") && what[4] >= '1' && what[4] <= '4')
        got = songs.song[what[4] - '1'].style[atoi(strstr(what, "_style") + 6) % ARR_NSECT];   /* songN_styleI (I: the slot) */
    else if (!strncmp(what, "song", 4) && strstr(what, "_sect") && what[4] >= '1' && what[4] <= '4')
        got = songs.song[what[4] - '1'].sect[atoi(strstr(what, "_sect") + 5) & 63];   /* songN_sectB: bar B's mark */
    else if (!strncmp(what, "song", 4) && strstr(what, "_chord") && what[4] >= '1' && what[4] <= '4') {
        const chord_t *c = &songs.song[what[4] - '1'].chord[atoi(strstr(what, "_chord") + 6) & 255];
        got = c->type < CH_NTYPES ? c->root * 100 + c->type : -1;   /* songN_chordK: beat K, root*100 + type, -1 none */
    } else if (!strncmp(what, "song", 4) && strstr(what, "_bars") && what[4] >= '1' && what[4] <= '4')
        got = songs.song[what[4] - '1'].bars;     /* songN_bars (1..4), as in RAM */
    else if (!strcmp(what, "song_on"))            /* the app gave the arranger the song (not a template) */
        got = app.song_on;
    else if (!strcmp(what, "rec_chords"))
        got = ui.rec_chords;
    else if (!strcmp(what, "cbeat"))              /* the Chords / Arrange cursor, a beat 0.. */
        got = app.cbeat;
    else if (!strcmp(what, "clock"))
        got = proj.clock;
    else if (!strcmp(what, "key"))
        got = proj.key;
    else if (!strcmp(what, "job"))              /* the jobs open */
        got = ui.job;
    else if (!strcmp(what, "rev_type"))
        got = proj.rev_type;
    else if (!strcmp(what, "song0_bars")) {       /* song 0's length in bars, as saved */
        project_songs_load(&dbg_songs);
        got = dbg_songs.song[0].bars;
    } else if (!strcmp(what, "song0_chord0")) {    /* song 0, bar 0 beat 0's chord: root*100 + type */
        project_songs_load(&dbg_songs);
        got = dbg_songs.song[0].chord[0].root * 100 + dbg_songs.song[0].chord[0].type;
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
        } else if (!strcmp(cmd, "hitreset"))
            memset(trk_hits, 0, sizeof trk_hits);
        else if (!strcmp(cmd, "flash_fail"))
            store_fail = atoi(a);
        else if (!strcmp(cmd, "poke_user_songs")) {   /* host-only: songs 1 and 2 on user patterns P01..P06, P07..P12 */
            songs_defaults(&dbg_songs);
            for (int n = 0; n < 2; n++)
                for (int k = 0; k < ARR_NSECT; k++)
                    dbg_songs.song[n].style[k] = (uint16_t)(n * ARR_NSECT + k);
            project_songs_save(&dbg_songs);
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
        else if (!strcmp(cmd, "shot")) {
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
            dbg_songs.song[0].bars = 4;
            dbg_songs.song[0].chord[0].root = 9;      /* A */
            dbg_songs.song[0].chord[0].type = CH_MIN7;
            dbg_songs.song[0].chord[0].bass = CH_NOBASS;
            project_songs_save(&dbg_songs);
        } else if (!strcmp(cmd, "poke_bad_style")) {   /* host-only: songs with pattern numbers past the styles */
            songs_defaults(&dbg_songs);
            dbg_songs.song[0].style[SEC_A - 1] = (uint16_t)(ARR_FACTORY0 + ARR_NSECT * arr_styles());
            dbg_songs.song[1].style[SEC_FILL_AB - 1] = 250;
            dbg_songs.song[1].style[SEC_B - 1] = (uint16_t)(ARR_FACTORY0 + ARR_NSECT * arr_styles() - 1);   /* the last: kept */
            dbg_songs.song[1].style[SEC_INTRO - 1] = ARR_USER_PATTERNS;                  /* past the user patterns */
            dbg_songs.song[1].style[SEC_END - 1] = 0xFFFF;
            project_songs_save(&dbg_songs);
        } else if (!strcmp(cmd, "poke_songs3")) {     /* host-only: songs of format 3 (byte pattern numbers, 24 user) */
            static uint8_t old[8 + ARR_SONGS * SONG3_BYTES];
            uint32_t m = SONGS_MAGIC, f = 3;
            memset(old, 0, sizeof old);
            memcpy(old, &m, 4);
            memcpy(old + 4, &f, 4);
            for (int n = 0; n < ARR_SONGS; n++) {
                uint8_t *g = old + 8 + n * SONG3_BYTES;
                for (int k = 0; k < ARR_NSECT; k++)
                    g[k] = (uint8_t)(n * ARR_NSECT + k);       /* the old defaults: 6n..6n+5 */
                g[ARR_NSECT] = (uint8_t)(n + 1);                /* bars */
                g[ARR_NSECT + 1] = 1;
                g[ARR_NSECT + 2 + 3] = SEC_B;                   /* bar 4: Main B */
                for (int b = 0; b < ARR_SONGBARS * 4; b++) {
                    uint8_t *c = g + ARR_NSECT + 2 + ARR_SONGBARS + 3 * b;
                    c[0] = (uint8_t)(b % 12);
                    c[1] = b == 0 ? CH_MIN7 : CH_NTYPES;
                    c[2] = CH_NOBASS;
                }
            }
            old[8 + 1 * SONG3_BYTES + 2] = 24 + 6 * 2 + 2;       /* song 2's Main B: factory style 3's (Rock B) */
            plat_store_save(OBJ_SONGS, old, sizeof old);
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
    printf("host: %u ms simulated, %u blits, render %.1f%% of real time (host CPU)%s\n", now_ms, blits,
           render_budget_ns_total > 0 ? 100.0 * render_ns_total / render_budget_ns_total : 0.0,
           fails ? ", EXPECTATIONS FAILED" : "");
    return fails ? 1 : 0;
}
#endif /* GM_WEB */
