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
    project_songs_load(&s);
    s.loop[2].bars = 20;
    s.song[1].nrows = 40;
    s.song[1].row[0].rep = 0;
    s.song[1].row[0].loop = 30;
    project_songs_save(&s);
    project_songs_load(&s);
    CHECK(s.loop[2].bars == 0 && s.song[1].nrows == 32 && s.song[1].row[0].rep == 1 && s.song[1].row[0].loop == 0, "clamped");
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
