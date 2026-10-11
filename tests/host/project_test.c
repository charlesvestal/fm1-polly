/* SPDX-License-Identifier: GPL-3.0-only */
/* project.c: songs format 7 (16 loops, songs of rows, per-song tempo and transpose, the lead tracks' sounds),
 * defaults, old-format reset, and load's clamping; the shared slots (user patterns and lead parts: what each holds,
 * the free one, a pattern never saved over a lead part) and the lead parts saved and loaded. plat_store_load/save
 * are an in-memory object table (no real flash). */
#include <stdio.h>
#include <string.h>
#include "../../firmware/src/app/app.h"
#include "../../firmware/src/app/project.c"
#include "leads.h"

static uint8_t obj[OBJ_NOBJ][4096];
static int len[OBJ_NOBJ];

int plat_store_load(uint32_t o, void *dst, uint32_t max)
{
    uint32_t n;
    if (o >= OBJ_NOBJ || len[o] <= 0)
        return -1;
    n = (uint32_t)len[o] < max ? (uint32_t)len[o] : max;
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
    CHECK(s.format == SONGS_FORMAT && SONGS_FORMAT == 7u, "format 7");
    CHECK(s.song[2].lead[0].program == 80 && s.song[2].lead[0].chan == 6 && s.song[2].lead[1].program == 48 &&
              s.song[2].lead[1].chan == 7 && s.song[2].lead[1].vol == 100, "the lead tracks: LD1 GM 81 on 7, LD2 GM 49 on 8");
    CHECK(strcmp(s.loop[0].name, "Pop") == 0 && s.loop[4].bars == 12 && strcmp(s.loop[7].name, "Canon") == 0, "L1 Pop, L5 Blues, L8 Canon");
    CHECK(s.loop[8].bars == 0 && strcmp(s.loop[8].name, "Loop 9") == 0 && strcmp(s.loop[15].name, "Loop 16") == 0, "L9..L16 empty");
    CHECK(s.song[0].nrows == 0 && s.song[0].intro && s.song[0].ending && s.song[0].tempo == arr_style_tempo(0), "an empty song at its style's tempo");
}
static void test_old_format(void)
{
    static songs_t s;
    memset(&s, 0, sizeof s);
    s.magic = SONGS_MAGIC;
    s.format = 5;
    project_songs_save(&s);
    CHECK(project_songs_load(&s) != 0 && s.loop[0].bars == 4, "format 5: defaults");
    memset(&s, 0, sizeof s);
    s.magic = SONGS_MAGIC;
    s.format = 6;                                       /* (prerelease: no migration) */
    s.song[0].nrows = 3;
    project_songs_save(&s);
    CHECK(project_songs_load(&s) != 0 && s.song[0].nrows == 0 && s.song[0].lead[0].program == 80, "format 6: defaults");
}
static void test_clamp(void)
{
    static songs_t s;
    char name3[8], name4[8];
    chord_t bad = {15, CH_MAJ, CH_NOBASS};              /* root 15: invalid */

    project_songs_load(&s);
    s.loop[1].bars = 20;                                /* too many bars for its beats: cleared */
    s.loop[2].beats = 9;                                /* out of 2..7: cleared */
    memcpy(name4, s.loop[3].name, 8);                    /* untouched by either clear: kept for comparison */
    s.loop[4].ch[0] = lc_pack(bad);                      /* a chord this firmware can't play: that beat only */
    uint16_t kept4 = s.loop[4].ch[4];

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
    s.song[3].lead[1].program = 200;
    s.song[3].row[4].lmute = 0xFF;
    s.song[3].intro = 5;
    s.song[3].ending = 0;
    s.song[3].at_end = 3;

    project_songs_save(&s);
    project_songs_load(&s);

    memcpy(name3, s.loop[1].name, 8);
    CHECK(s.loop[1].bars == 0 && strncmp(name3, "Loop 2", 8) == 0, "bars too big for its beats: that loop cleared");
    memcpy(name3, s.loop[2].name, 8);
    CHECK(s.loop[2].bars == 0 && strncmp(name3, "Loop 3", 8) == 0, "beats out of 2..7: that loop cleared");
    CHECK(memcmp(s.loop[3].name, name4, 8) == 0 && s.loop[3].bars != 0, "another loop untouched");
    CHECK(s.loop[4].ch[0] == LC_SAME, "a chord this firmware can't play: that beat set to no change");
    CHECK(s.loop[4].ch[4] == kept4, "the loop's other chords kept");

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
    CHECK(project_pattern_save(0, &pb) == 0, "a pattern with a note into slot 1");
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
    CHECK(project_slot_leads(2) && !project_slot_leads(3) && !project_slot_leads(0), "a slot holding leads");
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
int main(void)
{
    test_defaults();
    test_old_format();
    test_clamp();
    test_slots();
    test_leads();
    printf("%d checks, %d failed\n", checks, fails);
    if (fails)
        return 1;
    printf("project test passed\n");
    return 0;
}
