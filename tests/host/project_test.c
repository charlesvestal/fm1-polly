/* SPDX-License-Identifier: GPL-3.0-only */
/* project.c: songs format 10 (songs of rows, per-song tempo and transpose, the lead tracks' sounds, and each song's
 * loops packed in the same object), defaults, old-format reset, and load's clamping; the shared slots (user patterns,
 * lead parts and songs' loops that did not fit: what each holds, the free one, a pattern never saved over a lead part
 * or loops), the lead parts saved and loaded, and each song's own loops (the defaults: nothing; edited: packed, per
 * song, the pool shared and moved as a song's grow and shrink; the sizes, typical and worst; past the pool: a slot,
 * let go once they fit again; a power cut between the two writes; damaged bytes). plat_store_load/save are an
 * in-memory object table (no real flash). */
#include <stdio.h>
#include <string.h>
#include "../../app/app.h"
#include "../../app/project.c"
#include "leads.h"

static uint8_t obj[OBJ_NOBJ][4096];
static int len[OBJ_NOBJ];

int plat_store_load(uint32_t o, void *dst, uint32_t max)
{
    uint32_t n;
    if (o >= OBJ_NOBJ || len[o] <= 0)
        return -1;
    if ((n = (uint32_t)len[o]) > max)
        n = max;                                     /* (as fm1-base's storage.c: cut short; project.c refuses it) */
    memcpy(dst, obj[o], n);
    return (int)n;
}
int plat_store_size(uint32_t o) { return o >= OBJ_NOBJ || len[o] <= 0 ? -1 : len[o]; }
int plat_store_head(uint32_t o, void *dst, uint32_t n)
{
    if (o >= OBJ_NOBJ || len[o] <= 0 || (uint32_t)len[o] < n)
        return -1;
    memcpy(dst, obj[o], n);
    return (int)n;
}
int plat_store_save(uint32_t o, const void *src, uint32_t n)
{
    if (o >= OBJ_NOBJ || n > sizeof obj[0])
        return -1;
    memcpy(obj[o], src, n);
    len[o] = (int)n;
    return 0;
}
/* arr.c's MIDI out (normally the app's render glue; unused here: project.c never plays) */
void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
uint32_t arr_midi_out_room(void) { return 64; }
uint32_t arr_midi_out_link(void) { return 0; }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_defaults(void)
{
    static songs_t s;
    CHECK(project_songs_load(&s) != 0, "nothing stored: defaults");
    CHECK(s.format == SONGS_FORMAT && SONGS_FORMAT == 10u && s.plen[0] == 0 && s.plen[3] == 0, "format 10, no loops stored");
    CHECK(s.song[1].lead[0].vel == 100 && s.song[1].lead[1].vel == 100, "the leads recorded at velocity 100");
    CHECK(s.song[2].lead[0].program == 52 && s.song[2].lead[0].chan == 6 && s.song[2].lead[1].program == 48 &&
              s.song[2].lead[1].chan == 7 && s.song[2].lead[1].vol == 100, "the lead tracks: LD1 GM 53 on 7, LD2 GM 49 on 8");
    CHECK(s.song[0].nrows == 0 && s.song[0].intro && s.song[0].ending && s.song[0].tempo == arr_style_tempo(0), "an empty song at its style's tempo");
}
static void test_old_format(void)
{
    static songs_t s;
    memset(&s, 0, sizeof s);
    s.magic = SONGS_MAGIC;
    s.format = 5;
    project_songs_save(&s);
    CHECK(project_songs_load(&s) != 0 && s.song[0].tempo == arr_style_tempo(0), "format 5: defaults");
    memset(&s, 0, sizeof s);
    s.magic = SONGS_MAGIC;
    s.format = 7;                                       /* (prerelease: no migration) */
    s.song[0].nrows = 3;
    project_songs_save(&s);
    CHECK(project_songs_load(&s) != 0 && s.song[0].nrows == 0 && s.song[0].lead[0].program == 52, "format 7: defaults");
    memset(&s, 0, sizeof s);
    s.magic = SONGS_MAGIC;
    s.format = 9;                                       /* (the loops in the slots: their songs reset, tidy lets them go) */
    s.song[0].nrows = 3;
    project_songs_save(&s);
    CHECK(project_songs_load(&s) != 0 && s.song[0].nrows == 0, "format 9: defaults");
    CHECK(plat_store_save(OBJ_SONGS, &s, 944) == 0 && project_songs_load(&s) != 0, "format 9's length (944 bytes): defaults");
}
static void test_clamp(void)
{
    static songs_t s;

    project_songs_load(&s);
    s.song[1].nrows = 40;
    s.song[1].row[0].loop = 30;
    s.song[1].row[0].rep = 0;
    s.song[1].row[0].sect = 7;
    s.song[1].row[1].key = 9;
    s.song[1].row[2].key = -7;
    s.song[1].tempo = 250;
    s.song[1].transpose = 8;

    s.song[2].style[2] = 9999;
    s.song[2].sty = 9999;

    s.song[3].lead[1].chan = 0x9A;
    s.song[3].lead[0].chan = ARR_CLICK_CHAN;           /* channel 16: the click's */
    s.song[3].lead[1].program = 200;
    s.song[3].row[4].lmute = 0xFF;
    s.song[3].intro = 5;
    s.song[3].ending = 0;
    s.song[3].at_end = 3;

    project_songs_save(&s);
    project_songs_load(&s);

    CHECK(s.song[1].nrows == 32, "nrows clamped to ARR_ROWS");
    CHECK(s.song[1].row[0].loop == 0, "a row's loop >= ARR_LOOPS clamped");
    CHECK(s.song[1].row[0].rep == 1, "a row's rep < 1 clamped");
    CHECK(s.song[1].row[0].sect == ROW_A, "a row's sect >= ROW_NSECT clamped to ROW_A");
    CHECK(s.song[1].row[1].key == 0, "a row's key > 5 clamped");
    CHECK(s.song[1].row[2].key == 0, "a row's key < -6 clamped");
    CHECK(s.song[1].tempo == 120, "tempo out of 40..240 defaulted");
    CHECK(s.song[1].transpose == 0, "transpose out of -6..5 defaulted");

    CHECK(song_style_valid(s.song[2].style[2]), "a bad style[k] repaired to a pattern this firmware has");
    CHECK(song_style_base(s.song[2].sty) == s.song[2].sty, "a bad sty repaired to a valid base");

    CHECK(s.song[3].intro == 1, "intro forced to 0/1");
    CHECK(s.song[3].ending == 0, "ending forced to 0/1 (already 0)");
    CHECK(s.song[3].at_end == 1, "at_end forced to 0/1");
    CHECK(s.song[3].lead[1].chan == 10 && s.song[3].lead[1].program == 72, "a lead's channel and program in range");
    CHECK(s.song[3].lead[0].chan == ARR_CHAN_MAX, "a lead stored on channel 16 (the click's) loads on 15");
    CHECK(s.song[3].row[4].lmute == 3, "a row's lead mutes: two bits");
}
/* ------------------------------------------------------------ the shared slots --- */
static pattern_t pb;
static leads_t L;
static void store_reset(void)
{
    memset(len, 0, sizeof len);
}
static void put_lead_part(int slot, int song, int part, int n)
{
    static lead_part_t p;
    memset(&p, 0, sizeof p);
    p.magic = LEAD_MAGIC;
    p.kind = LEAD_KIND;
    p.song = (uint8_t)song;
    p.part = (uint8_t)part;
    p.n = (uint16_t)n;
    for (int i = 0; i < n; i++)
        lead_set(&p.ev[i], i / 50, (uint32_t)(i % 50) * 120u, 120, 60 + i % 12, 100, 0, 0);
    plat_store_save((uint32_t)(OBJ_PAT0 + slot), &p, LEAD_HDR + 9u * (uint32_t)n);
}
static void test_slots(void)
{
    static songs_t s;
    slot_info_t si;
    store_reset();
    project_songs_load(&s);
    CHECK(project_slot_info(0, &si, &pb) == SLOT_NONE && project_slot_free(&s, 0, &pb) == 0, "nothing stored: slot 1 free");
    pattern_empty(&pb);
    pb.ev[0].tick = 0, pb.ev[0].len = 120, pb.ev[0].note = 60, pb.ev[0].vel = 100;
    pb.nev = 1, pb.trk[4].first = 0, pb.trk[4].count = 1;
    pb.trk[5].chan = ARR_CLICK_CHAN;                     /* channel 16: the click's */
    CHECK(project_pattern_save(0, &pb) == 0, "a pattern with a note into slot 1");
    {
        static pattern_t q;
        int ok = 1;
        CHECK(project_pattern_load(0, &q) == 0 && q.trk[5].chan == ARR_CHAN_MAX && q.trk[4].chan == pb.trk[4].chan,
              "a track stored on channel 16 (the click's) loads on 15, the others as stored");
        for (int i = 0; i < arr_styles() * ARR_NSECT; i++)
            if (arr_factory_pattern(ARR_FACTORY0 + i, &q) == 0)
                for (int t = 0; t < ARR_TRACKS; t++)
                    ok &= q.trk[t].chan != ARR_CLICK_CHAN;
        CHECK(ok, "no factory style's track on channel 16 (the click's)");
    }
    pattern_empty(&pb);
    CHECK(project_pattern_save(1, &pb) == 0, "an empty pattern into slot 2");
    put_lead_part(2, 1, 0, 30);                          /* song 2's leads */
    put_lead_part(3, 0, 1, 0);                           /* a lead part erased: free */
    plat_store_save(OBJ_PAT0 + 4, "an old pattern", 15);   /* (an old format: counts free, loads empty) */
    CHECK(project_slot_info(0, &si, &pb) == SLOT_PATTERN && si.n == 1, "slot 1: a pattern, 1 note");
    CHECK(project_slot_info(1, &si, &pb) == SLOT_PATTERN && si.n == 0, "slot 2: a pattern, none");
    CHECK(project_slot_info(2, &si, &pb) == SLOT_LEAD && si.song == 1 && si.part == 0 && si.n == 30, "slot 3: song 2's part 1, 30 notes");
    CHECK(project_slot_info(3, &si, &pb) == SLOT_LEAD && si.n == 0, "slot 4: an empty lead part");
    CHECK(project_slot_info(4, &si, &pb) == SLOT_OTHER, "slot 5: neither");
    CHECK(!project_slot_blank(0, &pb) && project_slot_blank(1, &pb) && !project_slot_blank(2, &pb) && project_slot_blank(3, &pb) &&
              project_slot_blank(4, &pb) && project_slot_blank(9, &pb), "blank: none, empty pattern, empty part, other");
    CHECK(project_slot_free(&s, 0, &pb) == 1, "the first free: the empty pattern");
    s.song[3].style[2] = 1;                              /* a song plays it: taken */
    CHECK(project_slot_free(&s, 0, &pb) == 3, "slot 2 played by a song: the empty lead part's slot");
    CHECK(project_slot_free(&s, 1u << 3 | 1u << 4, &pb) == 5, "slots 4, 5 reserved (the leads' own): slot 6");
    CHECK(project_slot_held(2) && !project_slot_held(3) && !project_slot_held(0), "a slot holding leads");
    CHECK(project_pattern_save(2, &pb) == -2 && project_slot_info(2, &si, &pb) == SLOT_LEAD, "a pattern never over a lead part");
    CHECK(project_pattern_save(3, &pb) == 0, "over an empty lead part: yes");
    CHECK(project_pattern_load(2, &pb) != 0 && pb.nev == 0, "a lead part's slot loads as an empty pattern");
    for (int i = 5; i < ARR_USER_PATTERNS; i++)
        put_lead_part(i, 2, i & 1, 1);
    CHECK(project_slot_free(&s, 0, &pb) == 1 || project_slot_free(&s, 0, &pb) == 3 || project_slot_free(&s, 0, &pb) == 4,
          "still the blank ones");
    CHECK(project_slot_free(&s, 1u << 3 | 1u << 4, &pb) < 0, "none left: NO FREE SLOT (-1)");
}
static void fill_leads(int n)
{
    leads_clear(&L);
    for (int i = 0; i < n; i++) {
        lead_ev_t e;
        lead_set(&e, i / 40, (uint32_t)(i % 40) * 60u, 60, 48 + i % 30, 64 + i % 60, i & 1, i % 12);
        leads_insert(&L, &e, LEAD_MAX, 0);
    }
}
static void test_leads(void)
{
    static leads_t got;
    int8_t slot[2] = {5, 6}, gs[2];
    store_reset();
    fill_leads(500);
    CHECK(L.n == 500 && L.part[1].n == 80, "500 notes: 420 + 80");
    CHECK(project_leads_save(2, &L, slot, 3u) == 0, "both parts saved");
    CHECK(len[OBJ_PAT0 + 5] == (int)(LEAD_HDR + 9u * 420u) && len[OBJ_PAT0 + 6] == (int)(LEAD_HDR + 9u * 80u), "12 + 9 n bytes each");
    CHECK(project_leads_load(1, &got, gs) == -1 && gs[0] < 0 && gs[1] < 0 && got.n == 0, "song 2: none");
    CHECK(project_leads_load(2, &got, gs) == 0 && gs[0] == 5 && gs[1] == 6 && got.n == 500, "song 3: both parts (%d)", got.n);
    CHECK(!memcmp(got.part[0].ev, L.part[0].ev, sizeof L.part[0].ev) && !memcmp(got.part[1].ev, L.part[1].ev, 80 * 9) &&
              got.row0[3] == L.row0[3], "the same notes, indexed");
    /* fewer: part 2 emptied, its slot free again */
    leads_erase(&L, -1, 1);
    CHECK(L.n == 250 && L.part[1].n == 0, "LD2 erased: 250 left");
    CHECK(project_leads_save(2, &L, slot, 3u) == 0 && slot[0] == 5 && slot[1] == -1, "saved: part 2's slot let go");
    CHECK(project_slot_blank(6, &pb) && !project_slot_blank(5, &pb), "slot 7 free, slot 6 holds part 1");
    CHECK(project_leads_load(2, &got, gs) == 0 && got.n == 250 && gs[1] == -1, "loaded again: 250");
    /* a part with notes and no slot */
    fill_leads(10);
    slot[0] = -1;
    CHECK(project_leads_save(3, &L, slot, 1u) == -2, "no slot: -2");
    /* only part 2 found (part 1 lost): its notes become part 1's, in its slot */
    store_reset();
    put_lead_part(9, 1, 1, 30);
    CHECK(project_leads_load(1, &got, gs) == 1 && got.n == 30 && gs[0] == 9 && gs[1] == -1, "part 2 alone: part 1 now, moved (%d %d)",
          gs[0], gs[1]);
    /* a damaged part (its length not its count's) */
    put_lead_part(10, 3, 0, 30);
    len[OBJ_PAT0 + 10] -= 9;
    CHECK(project_leads_load(3, &got, gs) == -1 && got.n == 0, "a part shorter than its count: as none");
    /* CLEAR / COPY SONG: every part of a song free */
    put_lead_part(11, 3, 0, 5);
    put_lead_part(12, 3, 1, 5);
    put_lead_part(13, 1, 0, 5);
    CHECK(project_leads_free(3) == 0 && project_slot_blank(11, &pb) && project_slot_blank(12, &pb) && !project_slot_blank(13, &pb),
          "song 4's parts free, song 2's kept");
}
/* ------------------------------------------------------------ a song's loops --- */
static loops_part_t LP, LG, LD;
/* the first n loops full: 16 bars of 4, a chord on every beat (4 + 64 x 3 = 196 bytes each, packed) */
static void dense(loop_t *t, int n)
{
    loops_default(t);
    for (int l = 0; l < n; l++) {
        t[l].bars = 16;
        t[l].beats = 4;
        for (int b = 0; b < ARR_LOOP_BEATS; b++) {
            chord_t c = {(uint8_t)((b * 5 + l) % 12), (uint8_t)(b % 2 ? CH_MIN : CH_MAJ), CH_NOBASS};
            t[l].ch[b] = lc_pack(c);
        }
    }
}
/* a song of typical edits: n progressions (from L9 on) renamed, each `chords` chords a bar apart */
static void typical(loop_t *t, int n, int chords)
{
    loops_default(t);
    for (int l = 8; l < 8 + n; l++) {
        memcpy(t[l].name, "Verse\0\0\0", 8);
        for (int k = 0; k < chords; k++) {
            chord_t c = {(uint8_t)((k * 7) % 12), CH_MAJ7, CH_NOBASS};
            loop_write(&t[l], k * 4, c);
        }
    }
}
static void test_loops(void)
{
    static songs_t s, s2;
    slot_info_t si;
    int8_t sl, gs;
    int n;
    chord_t am = {9, CH_MIN, CH_NOBASS}, d7 = {2, CH_7, CH_NOBASS}, bad = {15, CH_MAJ, CH_NOBASS};
    store_reset();
    project_songs_load(&s);
    /* the defaults: L1..L8 the factory's, L9..L16 empty; nothing stored, no slot */
    CHECK(project_loops_load(&s, 0, &LP, &sl) == -1 && sl == -1, "nothing stored: the defaults, no slot");
    CHECK(strcmp(LP.loop[0].name, "Pop") == 0 && LP.loop[4].bars == 12 && strcmp(LP.loop[7].name, "Canon") == 0,
          "L1 Pop, L5 Blues, L8 Canon");
    CHECK(LP.loop[8].bars == 0 && strcmp(LP.loop[8].name, "Prog 9") == 0 && strcmp(LP.loop[15].name, "Prog 16") == 0, "L9..L16 empty");
    CHECK(loops_is_default(LP.loop) && loops_pack(LP.loop, 0, ARR_PROG_POOL) == 0, "the defaults pack to nothing");
    CHECK(project_loops_store(&s, 0, &LP, &sl) == 0 && sl == -1 && s.plen[0] == 0, "the defaults stored: nothing");
    /* an edited loop: packed into the songs (L9: its number, 1 bar, 4 beats, 1 change; the chord: 7 bytes), no slot */
    loop_write(&LP.loop[8], 0, am);
    CHECK(project_loops_fit(&s, 0, LP.loop), "L9 with a chord fits");
    CHECK(project_loops_store(&s, 0, &LP, &sl) == 0 && sl == -1 && s.plen[0] == 7, "song 1's: 7 bytes in the songs");
    for (int i = 0; i < ARR_USER_PATTERNS; i++)
        CHECK(len[OBJ_PAT0 + i] == 0, "slot %d untouched by packed loops", i + 1);
    /* song 2: L9 D7 .. A m, L1 renamed (its 4 chords, its name: 4 + 8 + 12): 10 + 24 */
    loops_default(LP.loop);
    loop_write(&LP.loop[8], 0, d7);
    loop_write(&LP.loop[8], 4, am);
    memcpy(LP.loop[0].name, "Verse\0\0\0", 8);
    gs = -1;
    CHECK(project_loops_store(&s, 1, &LP, &gs) == 0 && gs == -1 && s.plen[1] == 34, "song 2's: 34 bytes");
    CHECK(project_songs_save(&s) == 0, "the songs saved (one object: %d bytes)", (int)sizeof s);
    /* each song's loops back from the songs as saved (switching songs: loaded each time) */
    CHECK(project_songs_load(&s2) == 0 && s2.plen[0] == 7 && s2.plen[1] == 34, "loaded: the lengths");
    CHECK(project_loops_load(&s2, 0, &LG, &gs) == 0 && gs == -1 && LG.loop[8].ch[0] == lc_pack(am) && LG.loop[8].bars == 1,
          "song 1: L9 A m");
    CHECK(project_loops_load(&s2, 1, &LG, &gs) == 0 && LG.loop[8].ch[0] == lc_pack(d7) && LG.loop[8].ch[4] == lc_pack(am) &&
              LG.loop[8].bars == 2 && !strncmp(LG.loop[0].name, "Verse", 8), "song 2: L9 D7 A m, L1 Verse");
    CHECK(!memcmp(LG.loop, LP.loop, sizeof LP.loop), "the same 16 loops, byte for byte");
    CHECK(project_loops_load(&s2, 2, &LG, &gs) == -1 && gs == -1 && loops_is_default(LG.loop), "song 3: the defaults");
    /* song 1's grow: song 2's bytes move up, kept; shrink to the defaults: down, kept */
    project_loops_load(&s2, 0, &LG, &gs);
    loop_write(&LG.loop[9], 0, d7);
    CHECK(project_loops_store(&s2, 0, &LG, &gs) == 0 && s2.plen[0] == 14, "song 1's grow: 14");
    CHECK(project_loops_load(&s2, 1, &LD, &gs) == 0 && !memcmp(LD.loop, LP.loop, sizeof LP.loop), "song 2's moved up, whole");
    loops_default(LG.loop);
    CHECK(project_loops_store(&s2, 0, &LG, &gs) == 0 && s2.plen[0] == 0, "song 1's the defaults: nothing");
    CHECK(project_loops_load(&s2, 1, &LD, &gs) == 0 && !memcmp(LD.loop, LP.loop, sizeof LP.loop), "song 2's moved down, whole");
    /* the sizes: typical edits (4 progressions of 4..8 chords, named) take ~100..150 bytes a song: four songs, a fifth
     * of the room; the worst (all 16 full, every beat a chord) 3136, past it */
    typical(LG.loop, 4, 4);
    n = loops_pack(LG.loop, 0, ARR_PROG_POOL);
    CHECK(n == 4 * (12 + 12), "4 progressions of 4 chords, named: %d bytes", n);
    typical(LG.loop, 4, 8);
    n = loops_pack(LG.loop, 0, ARR_PROG_POOL);
    CHECK(n == 4 * (12 + 24) && 4 * n < ARR_PROG_POOL / 4, "4 of 8 chords: %d bytes (x 4 songs: %d of %d)", n, 4 * n, ARR_PROG_POOL);
    typical(LG.loop, 8, 16);
    n = loops_pack(LG.loop, 0, ARR_PROG_POOL);
    CHECK(n == 8 * (12 + 48), "8 of 16 chords: %d bytes", n);
    dense(LG.loop, 16);
    CHECK(loops_pack(LG.loop, 0, ARR_PROG_POOL) == -1 && loops_pack(LG.loop, 0, 4000) == 16 * 196, "all 16 full: 3136, past the room");
    dense(LG.loop, 14);
    CHECK(loops_pack(LG.loop, 0, ARR_PROG_POOL) == 14 * 196, "14 full: 2744, it fits alone");
    /* the fallback: dense progressions do not fit: a slot (-2 without one); the song's bytes leave the pool */
    dense(LG.loop, 16);
    gs = -1;
    CHECK(!project_loops_fit(&s2, 0, LG.loop) && project_loops_store(&s2, 0, &LG, &gs) == -2, "past the room, no slot: -2");
    gs = (int8_t)project_slot_free(&s2, 0, &pb);
    CHECK(gs == 0, "the first free slot");
    CHECK(project_loops_store(&s2, 0, &LG, &gs) == 1 && s2.plen[0] == ARR_PLEN_SLOT && len[OBJ_PAT0] == (int)sizeof(loops_part_t),
          "song 1's in slot 1, whole; the songs say so");
    CHECK(project_slot_info(0, &si, &pb) == SLOT_LOOPS && si.song == 0 && si.n == ARR_LOOPS, "slot 1: song 1's loops");
    CHECK(project_slot_held(0) && !project_slot_blank(0, &pb), "held, never blank");
    CHECK(project_slot_free(&s2, 0, &pb) == 1, "the next free: slot 2");
    CHECK(project_loops_load(&s2, 1, &LD, &gs) == 0 && !memcmp(LD.loop, LP.loop, sizeof LP.loop), "song 2's kept in the pool");
    project_songs_save(&s2);
    project_songs_load(&s2);
    CHECK(project_loops_load(&s2, 0, &LD, &gs) == 0 && gs == 0 && !memcmp(LD.loop, LG.loop, sizeof LG.loop), "song 1's back from slot 1");
    CHECK(project_loops_tidy(&s2) == 0 && project_loops_slot(0) == 0, "tidy: a slot its song says it is in, kept");
    /* the pool shared: song 3 takes 14 full (2744 + song 2's 34: 2778 of 2888); song 4's 15 then do not fit */
    dense(LG.loop, 14);
    gs = -1;
    CHECK(project_loops_store(&s2, 2, &LG, &gs) == 0 && s2.plen[2] == 14 * 196, "song 3's 14 full: packed");
    dense(LG.loop, 1);
    CHECK(!project_loops_fit(&s2, 3, LG.loop), "song 4's one full progression: no room left (110 bytes)");
    loops_default(LG.loop);
    loop_write(&LG.loop[8], 0, am);
    CHECK(project_loops_fit(&s2, 3, LG.loop), "... a small edit still fits");
    /* song 1's packed again (they fit once more): the songs saved first, then tidy lets its slot go */
    loops_default(LG.loop);
    loop_write(&LG.loop[8], 0, am);
    gs = 0;
    CHECK(project_loops_store(&s2, 0, &LG, &gs) == 0 && s2.plen[0] == 7 && project_loops_slot(0) == 0, "packed; slot 1 not yet let go");
    CHECK(project_songs_save(&s2) == 0 && project_loops_tidy(&s2) == 0 && project_loops_slot(0) == -1 && project_slot_blank(0, &pb),
          "saved: slot 1 let go");
    CHECK(project_slot_info(0, &si, &pb) == SLOT_LOOPS && si.n == 0, "slot 1: freed loops");
    /* a power cut after a fallback's slot write, before the songs: they say packed (as before), the slot a stale
     * copy; at start, tidy lets it go */
    dense(LG.loop, 16);
    gs = 1;
    project_loops_store(&s2, 3, &LG, &gs);           /* (song 4's slot written; s2 in RAM says slot, never saved) */
    project_songs_load(&s);
    CHECK(s.plen[3] == 0 && project_loops_slot(3) == 1 && project_loops_tidy(&s) == 0 && project_loops_slot(3) == -1,
          "the songs as saved win; the stale slot let go");
    /* damaged packed bytes (a loop number out of range): that song's the defaults; the others kept */
    project_songs_load(&s2);
    s2.prog[s2.plen[0]] = 0x7F;                      /* (song 2's first byte) */
    CHECK(project_loops_load(&s2, 1, &LD, &gs) == -1 && loops_is_default(LD.loop), "damaged: song 2's the defaults");
    CHECK(project_loops_load(&s2, 0, &LD, &gs) == 0 && LD.loop[8].ch[0] == lc_pack(am), "song 1's kept");
    /* lengths past the pool: every song's the defaults */
    s2.plen[0] = 3000;
    project_songs_save(&s2);
    CHECK(project_songs_load(&s2) == 0 && s2.plen[0] == 0 && s2.plen[1] == 0 && s2.plen[2] == 0, "lengths past the pool: none");
    /* never overwritten by a pattern, never taken by a lead part (a loops slot: song 1's, by the fallback) */
    store_reset();
    project_songs_load(&s);
    dense(LG.loop, 16);
    gs = 0;
    CHECK(project_loops_store(&s, 0, &LG, &gs) == 1, "song 1's in slot 1");
    project_songs_save(&s);
    pattern_empty(&pb);
    CHECK(project_pattern_save(0, &pb) == -2 && project_slot_info(0, &si, &pb) == SLOT_LOOPS, "a pattern never over a song's loops");
    CHECK(project_pattern_load(0, &pb) != 0 && pb.nev == 0, "a loops slot loads as an empty pattern");
    {
        static leads_t got;
        int8_t ls[2];
        CHECK(project_leads_load(0, &got, ls) == -1 && ls[0] == -1, "a loops slot is no lead part");
    }
    for (int i = 1; i < ARR_USER_PATTERNS; i++)
        put_lead_part(i, 2, i & 1, 1);
    CHECK(project_slot_free(&s, 0, &pb) < 0, "the rest held by leads: NO FREE SLOT (-1)");
    CHECK(project_loops_load(&s, 0, &LD, &gs) == 0 && gs == 0 && !memcmp(LD.loop, LG.loop, sizeof LG.loop), "song 1's still there");
    /* COPY SONG of loops that fit: packed into the target; free lets go any slot the target had */
    CHECK(project_loops_free(0) == 0 && project_loops_slot(0) == -1 && project_slot_blank(0, &pb), "song 1's slot freed");
    /* damaged loops in a slot put in range when loaded (loops_fix: that loop, that beat) */
    project_loops_load(&s, 0, &LG, &gs);
    loops_default(LG.loop);
    loop_write(&LG.loop[8], 0, am);
    LG.loop[1].bars = 20;                                /* too many bars for its beats: cleared */
    LG.loop[2].beats = 9;                                /* out of 2..7: cleared */
    LG.loop[4].ch[0] = lc_pack(bad);                     /* a chord this firmware can't play: that beat only */
    uint16_t kept4 = LG.loop[4].ch[4];
    char name4[8];
    memcpy(name4, LG.loop[3].name, 8);
    CHECK(plat_store_save(OBJ_PAT0, &LG, sizeof LG) == 0, "saved as they are, in slot 1");
    s.plen[0] = ARR_PLEN_SLOT;
    project_loops_load(&s, 0, &LG, &gs);
    CHECK(gs == 0, "from slot 1");
    CHECK(LG.loop[1].bars == 0 && strncmp(LG.loop[1].name, "Prog 2", 8) == 0, "bars too big for its beats: that loop cleared");
    CHECK(LG.loop[2].bars == 0 && strncmp(LG.loop[2].name, "Prog 3", 8) == 0, "beats out of 2..7: that loop cleared");
    CHECK(memcmp(LG.loop[3].name, name4, 8) == 0 && LG.loop[3].bars != 0, "another loop untouched");
    CHECK(LG.loop[4].ch[0] == LC_SAME && LG.loop[4].ch[4] == kept4, "a bad chord: that beat no change, the others kept");
    /* packed: the same damage refused whole (loops_unpack: out of range): the defaults */
    LG.loop[1].bars = 20;
    {
        uint8_t b[512];
        int k = loops_pack(LG.loop, b, sizeof b);
        CHECK(k > 0 && loops_unpack(LD.loop, b, k) == -1 && loops_is_default(LD.loop), "packed bars out of range: the defaults");
        CHECK(loops_unpack(LD.loop, b, k - 1) == -1, "cut short: the defaults");
    }
    /* a short (damaged) loops slot: the defaults, -1 */
    len[OBJ_PAT0] -= 2;
    CHECK(project_loops_load(&s, 0, &LG, &gs) == -1 && gs == -1 && loops_is_default(LG.loop), "a damaged slot: the defaults");
}
int main(void)
{
    test_defaults();
    test_old_format();
    test_clamp();
    test_slots();
    test_leads();
    test_loops();
    printf("%d checks, %d failed\n", checks, fails);
    if (fails)
        return 1;
    printf("project test passed\n");
    return 0;
}
