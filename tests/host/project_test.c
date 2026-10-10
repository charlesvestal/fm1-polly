/* SPDX-License-Identifier: GPL-3.0-only */
/* project.c: songs format 6 (16 loops, songs of rows, per-song tempo and transpose), defaults, old-format
 * reset, and load's clamping. plat_store_load/save are an in-memory object table (no real flash). */
#include <stdio.h>
#include <string.h>
#include "../../firmware/src/app/app.h"
#include "../../firmware/src/app/project.c"

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
    CHECK(s.format == SONGS_FORMAT && SONGS_FORMAT == 6u, "format 6");
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
}
int main(void)
{
    test_defaults();
    test_old_format();
    test_clamp();
    printf("%d checks, %d failed\n", checks, fails);
    if (fails)
        return 1;
    printf("project test passed\n");
    return 0;
}
