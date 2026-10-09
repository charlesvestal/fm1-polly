/* SPDX-License-Identifier: GPL-3.0-only */
/* The arranger (arr.c) playing QY70 bank styles, against the recording GM stub (gm_stub.c): styles written here by
 * hand (qb_style_t, as qybank.c decodes them; no ROM): phrases repeated to fill their sections, controllers sent
 * on the track's channel (synth and MIDI out) and reset at a section change and a stop, the transposer leaving
 * them alone, the chord held in the Intro and the Ending, a style switched while playing, a section of more notes
 * than a pattern holds. Then, with a bank image (argv[1], optional: the synthetic one), every style's sections
 * played a few bars each: no stuck notes, nothing left bent. 120 BPM: a beat is 22050 frames, a tick 45.9375. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arr.h"
#include "gm_stub.h"
#include "templates.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define BEAT 22050u
#define BAR (4u * BEAT)
#define TICKF (BEAT / 480.f)

static stub_msg_t out_log[STUB_MAX];
static int out_n;
void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (out_n < STUB_MAX) {
        stub_msg_t m = {stub_frame, (uint8_t)st, (uint8_t)d1, (uint8_t)d2};
        out_log[out_n++] = m;
    }
}
static uint32_t room = 1000;                  /* MIDI out's free slots (0: full) */
uint32_t arr_midi_out_room(void) { return room; }
uint32_t arr_midi_out_link(void) { return 0; }
static uint8_t flash[0x100000];
int qb_flash_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > sizeof flash || n > sizeof flash - off)
        return -1;
    memcpy(dst, flash + off, n);
    return 0;
}

static void reset(void)
{
    room = 1000;
    arr_init();
    stub_n = 0;
    stub_frame = 0;
    out_n = 0;
}
static void run_to(uint32_t frame)
{
    static int32_t buf[2 * 256];
    while (stub_frame < frame)
        gm_render(buf, 256, 4096);
}
static int count(const stub_msg_t *log, int n, int from, uint32_t f0, uint32_t f1, int st, int d1)
{
    int c = 0;
    for (int i = from; i < n; i++)
        if (log[i].frame >= f0 && log[i].frame < f1 && log[i].st == st && (d1 < 0 || log[i].d1 == d1) &&
            ((st & 0xF0) != 0x90 || log[i].d2))
            c++;
    return c;
}
static int last_val(int st, int d1, uint32_t f1, int data2)   /* the last message's value before frame f1, -1 none */
{
    int v = -1;
    for (int i = 0; i < stub_n; i++)
        if (stub_log[i].frame < f1 && stub_log[i].st == st && (d1 < 0 || stub_log[i].d1 == d1))
            v = data2 ? stub_log[i].d2 : stub_log[i].d1;
    return v;
}
static chord_t ch(int root, int type) { chord_t c = {(uint8_t)root, (uint8_t)type, CH_NOBASS}; return c; }

/* ---- a hand-made style ---- */
static qb_style_t A, B, D;
static pattern_t P;
static int phrase(qb_style_t *s, int typ, int drum, int length)
{
    qy_phrase_t *p = &s->ph[s->nph];
    memset(p, 0, sizeof *p);
    p->typ = (uint8_t)typ;
    p->drum = (uint8_t)drum;
    p->lo = 0;
    p->hi = 127;
    p->hikey = 12;
    p->length = (uint16_t)length;
    p->note0 = s->nnotes;
    p->ctl0 = s->nctl;
    return s->nph++;
}
static void note(qb_style_t *s, int t, int gate, int key, int vel)
{
    qy_note_t n = {(uint16_t)t, (uint16_t)gate, (uint8_t)key, (uint8_t)vel};
    s->note[s->nnotes++] = n;
    s->ph[s->nph - 1].nnotes++;
}
static void ctl(qb_style_t *s, int t, int kind, int v)
{
    qy_ctl_t c = {(uint16_t)t, (uint8_t)kind, (uint8_t)v};
    s->ctl[s->nctl++] = c;
    s->ph[s->nph - 1].nctl++;
}
static void style_new(qb_style_t *s, const char *name)
{
    memset(s, 0, sizeof *s);
    strcpy(s->name, name);
    s->tempo10 = 1200;
    s->beats = 4;
    memset(s->use, 0xFF, sizeof s->use);
    for (int k = 0; k < 6; k++)
        s->bars[k] = 1;
    for (int t = 0; t < 8; t++) {
        s->trk[t].chan = t < 3 ? 9 : (uint8_t)(t - 2);
        s->trk[t].drum = t < 3;
        s->trk[t].vol = 100;
        s->trk[t].pan = 64;
        s->trk[t].vscale = 128;
    }
}
static void use(qb_style_t *s, int k, int t, int ph)
{
    s->use[k][t] = (uint8_t)ph;
    s->prog[k][t] = s->trk[t].prog;
    s->tmask |= (uint8_t)(1u << t);
}
/* A: Main A 2 bars: D1 a 1-bar phrase (kicks on 1 and 3), BA a 1-bar bass phrase (C2 on 1; a bend down and back:
 * 64, 40, 64 on beats 1, 2, 3), C1 a 2-bar Chord 1 phrase (E4 held; expression 30, then 100 on bar 2). Main B 1 bar:
 * D1's phrase only. Intro 2 bars, Ending 1 bar: C1 a 1-bar phrase of E4 on each beat */
static void make_a(void)
{
    int k, b, c, e;
    style_new(&A, "Alpha");
    A.bars[SEC_INTRO - 1] = 2;
    A.bars[SEC_A - 1] = 2;
    k = phrase(&A, XT_BYPASS, 1, 1920);
    note(&A, 0, 60, 36, 100);
    note(&A, 960, 60, 36, 90);
    b = phrase(&A, XT_BASS, 0, 1920);
    A.ph[b].lo = 28;
    A.ph[b].hi = 55;
    note(&A, 0, 400, 36, 100);
    ctl(&A, 0, 0, 64);
    ctl(&A, 480, 0, 40);
    ctl(&A, 960, 0, 64);
    c = phrase(&A, XT_CHORD1, 0, 3840);
    A.ph[c].lo = 48;
    A.ph[c].hi = 84;
    note(&A, 0, 3600, 64, 80);
    ctl(&A, 0, 2, 30);
    ctl(&A, 960, 5, 30);                          /* brightness down (CC 74) on beat 3 */
    ctl(&A, 1920, 2, 100);
    e = phrase(&A, XT_CHORD1, 0, 1920);
    A.ph[e].lo = 48;
    A.ph[e].hi = 84;
    for (int i = 0; i < 4; i++)
        note(&A, i * 480, 200, 64, 90);
    A.trk[3].prog = 33;
    use(&A, SEC_A - 1, 0, k);
    use(&A, SEC_A - 1, 3, b);
    use(&A, SEC_A - 1, 4, c);
    use(&A, SEC_B - 1, 0, k);
    use(&A, SEC_INTRO - 1, 4, e);
    use(&A, SEC_END - 1, 4, e);
}
/* B: Main A 1 bar, D2 a snare (38) on every beat */
static void make_b(void)
{
    int k;
    style_new(&B, "Beta");
    k = phrase(&B, XT_BYPASS, 1, 1920);
    for (int i = 0; i < 4; i++)
        note(&B, i * 480, 60, 38, 100);
    use(&B, SEC_A - 1, 1, k);
}
/* D: Main A 2 bars, D1 a 1-bar phrase of 400 hi-hats (every 4.8 ticks: 4 or 5), velocities 100 and 40 by turns:
 * 800 as it plays, more than a pattern's 600 */
static void make_d(void)
{
    int k;
    style_new(&D, "Dense");
    D.bars[SEC_A - 1] = 2;
    k = phrase(&D, XT_BYPASS, 1, 1920);
    for (int i = 0; i < 400; i++)
        note(&D, i * 1920 / 400, 30, 42, i & 1 ? 40 : 100);
    use(&D, SEC_A - 1, 0, k);
}
static void load(int slot, const qb_style_t *s, int k)
{
    arr_bank_pattern(s, k, &P);
    arr_load_bank(slot, s, k, &P);
}
static void load_all(const qb_style_t *s)
{
    for (int k = 0; k < ARR_NSECT; k++)
        load(k, s, k);
}

static void t_repeat_and_controllers(void)
{
    uint32_t f0, f1;
    reset();
    make_a();
    load_all(&A);
    arr_midi_out_on(1);
    arr_set_tempo(120);
    arr_section(SEC_A);
    arr_play(1);
    run_to(2 * BAR + 100);
    /* 1-bar phrases twice over the 2 bars, the 2-bar one once */
    CHECK(count(stub_log, stub_n, 0, 0, 2 * BAR, 0x99, 36) == 4, "D1's kicks: 2 a bar, repeated (%d)",
          count(stub_log, stub_n, 0, 0, 2 * BAR, 0x99, 36));
    CHECK(count(stub_log, stub_n, 0, 0, 2 * BAR, 0x91, 36) == 2, "BA's C2: once a bar");
    CHECK(count(stub_log, stub_n, 0, 0, 2 * BAR, 0x92, 64) == 1, "C1's E4: once in 2 bars");
    /* the bend (channel 2, 0xE1): 64, 40, 64 each bar, as MSBs, on their beats */
    CHECK(count(stub_log, stub_n, 0, 0, 2 * BAR - 300, 0xE1, 0) == 6, "BA's bend: 3 a bar, 6 (%d)",
          count(stub_log, stub_n, 0, 0, 2 * BAR - 300, 0xE1, -1));
    f0 = BEAT - 300;
    f1 = BEAT + 10;
    CHECK(last_val(0xE1, 0, f1, 1) == 40 && last_val(0xE1, 0, f0, 1) == 64, "bend down on beat 2 (%d)", last_val(0xE1, 0, f1, 1));
    CHECK(last_val(0xE1, 0, BAR + 2 * BEAT + 10, 1) == 64, "and back on beat 3, in the repeat too");
    CHECK(last_val(0xB2, 11, BAR - 300, 1) == 30 && last_val(0xB2, 11, BAR + 10, 1) == 100, "C1's expression: 30, then 100 on bar 2");
    CHECK(last_val(0xB2, 74, 2 * BEAT - 300, 1) == 64 && last_val(0xB2, 74, 2 * BEAT + 10, 1) == 30,
          "C1's brightness: the track's 64, then the phrase's 30 on beat 3");
    CHECK(count(out_log, out_n, 0, 0, 2 * BAR - 300, 0xE1, 0) == 6 && count(out_log, out_n, 0, 0, 2 * BAR - 300, 0xB2, 11) == 2,
          "the controllers go to MIDI out too");
    /* the transposer leaves them alone: over G, the bass plays G, the bend's values stay */
    arr_set_chord(ch(7, CH_MAJ));
    run_to(3 * BAR + 100);
    CHECK(count(stub_log, stub_n, 0, 2 * BAR, 3 * BAR, 0x91, 43) == 1, "over G: the bass's C2 plays G2");
    CHECK(last_val(0xE1, 0, 2 * BAR + BEAT + 10, 1) == 40, "the bend as written (40)");
    /* stopped: the moved channels reset (bend centre, modulation 0, expression 127, pressure 0, sustain off,
     * pan the track's), the drums' channel left alone */
    {
        int n0 = stub_n, o0 = out_n;
        arr_play(0);
        run_to(stub_frame + 512);
        CHECK(count(stub_log, stub_n, n0, 0, ~0u, 0xE1, 0) == 1 && last_val(0xE1, 0, ~0u, 1) == 64, "stop: bend centred");
        CHECK(last_val(0xB2, 11, ~0u, 1) == 127 && last_val(0xB2, 1, ~0u, 1) == 0 && last_val(0xD2, -1, ~0u, 0) == 0 &&
              last_val(0xB2, 64, ~0u, 1) == 0 && last_val(0xB2, 10, ~0u, 1) == 64 && last_val(0xB2, 74, ~0u, 1) == 64 &&
              last_val(0xB2, 71, ~0u, 1) == 64, "stop: C1's channel reset (brightness, resonance 64 too)");
        CHECK(count(stub_log, stub_n, n0, 0, ~0u, 0xE9, -1) == 0, "the drums' channel: nothing to reset");
        CHECK(count(out_log, out_n, o0, 0, ~0u, 0xE1, 0) == 1, "... MIDI out too");
        n0 = stub_n;
        arr_play(0);
        run_to(stub_frame + 512);
        CHECK(count(stub_log, stub_n, n0, 0, ~0u, 0xE1, -1) == 0, "reset once, not at every stop");
    }
}

/* a track's brightness and resonance (the Mixer's tone page) go with its sounds, and are what a reset returns to */
static void t_tone(void)
{
    reset();
    make_a();
    arr_bank_pattern(&A, SEC_A - 1, &P);
    P.trk[4].bright = arr_xor64(100);
    P.trk[4].reso = arr_xor64(20);
    arr_load_bank(SEC_A - 1, &A, SEC_A - 1, &P);
    arr_section(SEC_A);
    arr_play(1);
    run_to(BAR - 300);
    CHECK(last_val(0xB2, 74, 300, 1) == 100 && last_val(0xB2, 71, 300, 1) == 20, "the track's tone sent at the start");
    arr_play(0);
    run_to(stub_frame + 512);
    CHECK(last_val(0xB2, 74, ~0u, 1) == 100 && last_val(0xB2, 71, ~0u, 1) == 20, "the reset: back to the track's tone");
    P.trk[4].bright = 0;
    CHECK(arr_bright(&P.trk[4]) == 64, "0 stored: the neutral 64 (patterns saved before)");
}

static void t_section_change(void)
{
    uint32_t sw = 2 * BAR;
    reset();
    make_a();
    load_all(&A);
    arr_section(SEC_A);
    arr_play(1);
    run_to(BAR + 100);
    arr_section(SEC_B);                           /* Main B from the next bar line (bar 3) */
    run_to(sw + 3 * BAR / 4);
    CHECK(arr_sect == SEC_B, "Main B");
    CHECK(count(stub_log, stub_n, 0, sw - 256, sw + 256, 0xE1, 0) == 1 && last_val(0xE1, 0, sw + 256, 1) == 64,
          "the bend reset at the section change");
    CHECK(last_val(0xB2, 11, sw + 256, 1) == 127, "the expression reset at the section change");
    CHECK(count(stub_log, stub_n, 0, sw + 256, sw + BAR, 0xE1, -1) == 0, "Main B sends no bends");
    CHECK(count(stub_log, stub_n, 0, sw, sw + BAR, 0x99, 36) == 2, "Main B's kicks");
}

static void t_chord_hold(void)
{
    reset();
    make_a();
    load_all(&A);
    arr_template(0);                              /* Pop: C G Am F, a bar each */
    arr_section(SEC_INTRO);
    arr_play(1);
    run_to(BAR / 2);
    CHECK(arr_sect == SEC_INTRO && arr_chord_unpack(arr_chord_now).root == 0, "the Intro over C");
    run_to(BAR + BAR / 2);
    CHECK(arr_sect == SEC_INTRO && arr_chord_unpack(arr_chord_now).root == 0, "its second bar: C held (the template "
          "would be on G), root %d", arr_chord_unpack(arr_chord_now).root);
    run_to(2 * BAR + BAR / 2);
    CHECK(count(stub_log, stub_n, 0, 0, 2 * BAR, 0x92, 64) == 8, "C1's E4 on every beat of the Intro, as over C (%d)",
          count(stub_log, stub_n, 0, 0, 2 * BAR, 0x92, -1));
    CHECK(arr_sect == SEC_A && arr_chord_unpack(arr_chord_now).root == 0, "Main A: the template's first bar, C");
    run_to(3 * BAR + BAR / 2);
    CHECK(arr_chord_unpack(arr_chord_now).root == 7, "then G");
    /* a chord the player sets changes it at once, in the Ending too; the song's and the template's do not */
    arr_section(SEC_END);
    run_to(4 * BAR + BAR / 2);
    CHECK(arr_sect == SEC_END && arr_chord_unpack(arr_chord_now).root == 9, "the Ending over the bar's chord, Am (%d)",
          arr_chord_unpack(arr_chord_now).root);
    arr_set_chord(ch(2, CH_MIN7));
    run_to(4 * BAR + 3 * BAR / 4);
    CHECK(arr_chord_unpack(arr_chord_now).root == 2, "Dm7 from the keys, at once");
    run_to(5 * BAR + 100);
    CHECK(!arr_playing, "the Ending played once: stopped");
    /* a song: the chord on the Intro's first beat, held; its chord track again from Main A */
    {
        static song_t g;
        reset();
        load_all(&A);
        memset(&g, 0, sizeof g);
        g.bars = 4;
        g.loop = 1;
        for (int i = 0; i < ARR_SONGBARS * 4; i++)
            g.chord[i] = ch(0, CH_NTYPES);
        g.chord[0] = ch(5, CH_MAJ);               /* F, bar 1 */
        g.chord[4] = ch(7, CH_MAJ);               /* G, bar 2 (in the Intro: held F) */
        g.chord[8] = ch(9, CH_MIN);               /* Am, bar 3 (Main A) */
        g.sect[0] = SEC_INTRO;
        g.sect[2] = SEC_A;
        arr_song(&g, 1);
        arr_play(1);
        run_to(BAR + BAR / 2);
        CHECK(arr_sect == SEC_INTRO && arr_chord_unpack(arr_chord_now).root == 5, "song: the Intro holds F (%d)",
              arr_chord_unpack(arr_chord_now).root);
        run_to(2 * BAR + BAR / 2);
        CHECK(arr_sect == SEC_A && arr_chord_unpack(arr_chord_now).root == 9, "song: Main A plays the chord track (Am)");
    }
    {   /* a chord the song changed during the Intro, none at Main A's first beat: the song's latest, not the held one */
        static song_t g;
        reset();
        load_all(&A);
        memset(&g, 0, sizeof g);
        g.bars = 4;
        g.loop = 1;
        for (int i = 0; i < ARR_SONGBARS * 4; i++)
            g.chord[i] = ch(0, CH_NTYPES);
        g.chord[0] = ch(5, CH_MAJ);               /* F, bar 1 */
        g.chord[5] = ch(7, CH_MAJ);               /* G, bar 2 beat 2 (held F) */
        g.sect[0] = SEC_INTRO;
        g.sect[2] = SEC_A;
        arr_song(&g, 1);
        arr_play(1);
        run_to(BAR + BAR / 2);
        CHECK(arr_chord_unpack(arr_chord_now).root == 5, "song: F held through the Intro");
        run_to(2 * BAR + BAR / 4);
        CHECK(arr_sect == SEC_A && arr_chord_unpack(arr_chord_now).root == 7, "song: Main A on the G the song moved to "
              "during the Intro (%d)", arr_chord_unpack(arr_chord_now).root);
    }
}

/* MIDI out full at the stop: the resets owed are sent once it has room; a track muted mid-bend: reset at once */
static void t_reset_owed(void)
{
    int o0, n0;
    reset();
    make_a();
    load_all(&A);
    arr_midi_out_on(1);
    arr_section(SEC_A);
    arr_play(1);
    run_to(BEAT + BEAT / 2);                      /* the bass bent down (40) */
    CHECK(last_val(0xE1, 0, ~0u, 1) == 40, "bent down");
    room = 0;
    o0 = out_n;
    arr_play(0);
    run_to(stub_frame + 2048);
    CHECK(count(out_log, out_n, o0, 0, ~0u, 0xE1, 0) == 0, "MIDI out full: no reset sent yet");
    room = 1000;
    run_to(stub_frame + 1024);
    CHECK(count(out_log, out_n, o0, 0, ~0u, 0xE1, 0) == 1 && count(out_log, out_n, o0, 0, ~0u, 0xB2, 11) == 1,
          "with room: the bend and expression resets go out (%d)", count(out_log, out_n, o0, 0, ~0u, 0xE1, 0));
    run_to(stub_frame + 2048);
    CHECK(count(out_log, out_n, o0, 0, ~0u, 0xE1, 0) == 1, "... once");
    /* mute */
    reset();
    load_all(&A);
    arr_section(SEC_A);
    arr_play(1);
    run_to(BEAT + BEAT / 2);
    n0 = stub_n;
    arr_track_mute(3, 1);
    run_to(stub_frame + 512);
    CHECK(count(stub_log, stub_n, n0, 0, ~0u, 0xE1, 0) == 1 && last_val(0xE1, 0, ~0u, 1) == 64,
          "the bass muted mid-bend: its channel's bend centred at once");
}

static void t_switch_while_playing(void)
{
    uint32_t sw;
    int n0;
    reset();
    make_a();
    make_b();
    load_all(&A);
    arr_section(SEC_A);
    arr_play(1);
    run_to(BAR + BEAT + BEAT / 2);                /* bar 2, between beats 2 and 3 */
    sw = stub_frame;
    n0 = stub_n;
    load(SEC_A - 1, &B, SEC_A - 1);               /* another style, while playing */
    memset(A.note, 0, sizeof A.note);             /* the old one no longer played: never read again */
    memset(A.ph, 0, sizeof A.ph);
    A.nph = 0;
    run_to(2 * BAR + BAR / 2);
    CHECK(count(stub_log, stub_n, n0, sw, 2 * BAR + BAR / 2, 0x99, 38) == 4, "Beta's snares from the next beat, in "
          "step with the bars (beats 3, 4 of bar 2; bar 3 beats 1, 2: %d)", count(stub_log, stub_n, n0, sw, 2 * BAR + BAR / 2, 0x99, 38));
    CHECK(count(stub_log, stub_n, n0, sw, ~0u, 0x99, 36) == 0 && count(stub_log, stub_n, n0, sw, ~0u, 0x99, 0) == 0,
          "nothing more of Alpha");
    CHECK(count(stub_log, stub_n, n0, sw, sw + 256, 0xE1, 0) == 1, "Alpha's bend reset at the switch");
    CHECK(arr_sect == SEC_A && arr_bar == 0, "Beta's 1-bar Main A: playing its bar 1 (%d)", arr_bar);
}

static void t_dense(void)
{
    int dropped, loud = 0;
    reset();
    make_d();
    dropped = arr_bank_pattern(&D, SEC_A - 1, &P);
    for (int i = 0; i < P.nev; i++)
        loud += P.ev[i].vel == 100;
    CHECK(P.nev == ARR_NEV && dropped == 200 && loud == 400, "as a pattern: 600 of 800, the quietest dropped (%d %d %d)",
          P.nev, dropped, loud);
    CHECK(P.trk[0].count == ARR_NEV && P.bars == 2 && P.beats == 4, "... on D1, 2 bars of 4");
    arr_load_bank(SEC_A - 1, &D, SEC_A - 1, &P);
    arr_section(SEC_A);
    arr_play(1);
    run_to(2 * BAR - 50);
    CHECK(count(stub_log, stub_n, 0, 0, 2 * BAR, 0x99, 42) == 800, "played: all 800 (%d)",
          count(stub_log, stub_n, 0, 0, 2 * BAR, 0x99, 42));
}

/* a bank image's styles (tools/qy70_codec.py --image), each section a few bars over a template: no stuck notes,
 * no channel left bent after the stop */
static qb_style_t S;
static int t_image(const char *path)
{
    FILE *f = fopen(path, "rb");
    static uint8_t img[0x50000];
    long n;
    int styles;
    if (!f)
        return -1;
    n = (long)fread(img, 1, sizeof img, f);
    fclose(f);
    memset(flash, 0xFF, sizeof flash);
    memcpy(flash + QB_DIR, img, QB_SECTOR);
    for (long i = QB_SECTOR; i < n; i++)
        flash[qb_phys((uint32_t)(i - QB_SECTOR))] = img[i];
    styles = qb_mount();
    CHECK(styles > 0, "the image mounts (%d)", styles);
    for (int i = 0; i < styles; i++) {
        int ons, offs, bends = 0;
        CHECK(qb_decode(i, &S) == 0, "style %d decodes", i);
        reset();
        load_all(&S);
        arr_template(1);
        arr_section(SEC_A);
        arr_play(1);
        run_to(4 * BAR);
        arr_section(SEC_FILL);
        run_to(7 * BAR);
        arr_section(SEC_END);
        run_to(20 * BAR);
        arr_play(0);
        run_to(stub_frame + 20000);
        ons = offs = 0;
        for (int j = 0; j < stub_n; j++) {
            if ((stub_log[j].st & 0xF0) == 0x90 && stub_log[j].d2)
                ons++;
            if ((stub_log[j].st & 0xF0) == 0x80 || ((stub_log[j].st & 0xF0) == 0x90 && !stub_log[j].d2))
                offs++;
        }
        for (int c = 0; c < 16; c++)
            bends += last_val(0xE0 | c, 0, ~0u, 1) >= 0 && last_val(0xE0 | c, 0, ~0u, 1) != 64;
        CHECK(stub_n < STUB_MAX, "%s: the log held it all", qb_name(i));
        CHECK(ons > 0 && ons == offs, "%s: %d note-ons, %d note-offs", qb_name(i), ons, offs);
        CHECK(!bends && !arr_playing, "%s: stopped, nothing left bent", qb_name(i));
    }
    return styles;
}

int main(int argc, char **argv)
{
    t_repeat_and_controllers();
    t_section_change();
    t_tone();
    t_chord_hold();
    t_reset_owed();
    t_switch_while_playing();
    t_dense();
    if (argc > 1)
        printf("bank image: %d styles played\n", t_image(argv[1]));
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
