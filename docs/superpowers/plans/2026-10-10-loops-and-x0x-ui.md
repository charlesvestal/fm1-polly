# Chord loops, songs of loops, and X0X's UI: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Polly's fixed templates and 64-bar chord-track songs with 16 editable chord loops and songs
that chain them (rows with section, repeat, mutes and key), and move the UI to X0X's conventions (tags,
footer, SELECT pages, HOME + key shortcuts, SEL lists, help cards).

**Architecture:** The engine (`firmware/src/dsp/arr.c`) gets a chord *source* — FREE, a loop, or a song of
rows — read straight from the app's `songs_t` (one table of loops and four songs, one flash object), guarded
by a busy flag and a per-block snapshot of the song's rows. Pure helpers for loops and rows live in a new
`firmware/src/dsp/loops.c` with their own host test. The app (`project.c`, `pages.c`, `ui.c`) is rebuilt page
by page on the new model, then the simulator scenarios and the manual follow.

**Tech Stack:** C99 on the FM-1 (single-precision FPU, no libm, no `double`), host tests in C
(`tests/host/*.c`, `cc`), the host simulator (`host/polly_host.c`, `tests/scenarios/*.polly`), Python tools.

**User decisions (already made):**
- "Both, loops first": make your own progressions (loops), jam with them, then chain them into a song.
- Chord entry "Both": step entry while stopped, live while playing, same REC button.
- Sections: "Each row picks one" — a row is loop, section (A/B), times; fills automatic on a change.
- Live chords over a loop: "Until the next change"; the screen shows the played chord while it holds.
- SELECT turns pages ("you set tempo less often"); tempo on the TEMPO knob and HOME + SELECT.
- REC writes what the keys play on the screen; REC on HOME in FREE starts a new loop ("makes sense").
- Song rows carry their own instrument mutes ("yes").
- The songwriting / UX pass folded in whole, including the per-row KEY ("yes").
- Spec: `docs/superpowers/specs/2026-10-10-loops-and-x0x-ui-design.md` (approved: "go ahead").

**Clarifications made while planning (from the spec, no new decisions):**
- The songs object is format **6** (format 5 is today's; the spec's "format 5" predates that check).
- Live chords sound as played: TRANSPOSE and a row's KEY shift what is *stored* (loops), and writing stores
  minus them, so the screen and the sound always agree (spec principle 1).
- Never flash the FM-1 unless the user asks (memory: flash only on request). Task 12's device check asks first.

---

## File map

| File | Change | Responsibility |
|---|---|---|
| `firmware/src/dsp/arr.h` | modify | new `loop_t`, `row_t`, `song_t`, `songs_t`; the chord source API; screen state |
| `firmware/src/dsp/arr.c` | modify | FREE / LOOP / SONG chord source, override, rows, fills, mutes, intro / ending; templates and the old song path removed |
| `firmware/src/dsp/loops.h`, `loops.c` | create | pure helpers: chord packing, loop length, writing with growth and the 64-beat limit, factory loops, rows (tap, insert, delete, section default), free-recording length |
| `firmware/src/dsp/templates.h/.c` | keep | the 16 factory progressions, now only data for factory loops and LOAD PROGRESSION |
| `firmware/src/app/app.h`, `project.c` | modify | `SONGS_FORMAT 6`, defaults (L1–L8 factory, L9–L16 empty), per-song tempo / transpose; old migrations removed; `proj.key` retired |
| `firmware/src/app/pages.c` | modify | page table, header tag, footer, HOME / LOOP / SONG / STYLE screens, lists, help cards, HOME-held overlay |
| `firmware/src/app/ui.c` | modify | input: SELECT pages, HOME combos, keys by screen, REC by screen, SAVE + white copy, lists, MODE, loop writing, song rows |
| `host/polly_host.c` | modify | new `expect` names (mode, loop, row, …); old ones removed |
| `tests/host/arr_test.c` | modify | tests on the new source API; template / chord-track tests replaced |
| `tests/host/loops_test.c` | create | the helpers |
| `tests/host/style_chords_test.c`, `bank_arr_test.c` | modify | progressions played as loops / songs of rows |
| `tests/run_tests.sh` | modify | build `loops.c` into the arr, timing, gm, filter, style-chords, bank-arr targets; add the loops test |
| `tests/scenarios/*.polly` | modify / create | rewritten for the new buttons; `loops.polly`, `song.polly` new |
| `MANUAL.md`, `docs/img/manual/*` | modify | the new model and pages; labelled panel drawing |

Build of the whole firmware is broken from Task 1 until Task 6 (the app still uses the old API); Tasks 1–4
verify with their host tests only. **Do the work on a branch** (`git switch -c loops` from `polly`) and
merge back when `sh tests/run_tests.sh` and `./build.sh` pass (Task 11).

Common commands (from `fm1-polly/`):

```sh
export PATH=~/github/fm1-parent/bin:$PATH
ARR="cc -std=c99 -Wall -Wextra -Werror -O1 -DARR_TEST -Ifirmware/src/dsp -Itests/host -o build/host/arr_test \
  tests/host/arr_test.c tests/host/gm_stub.c firmware/src/dsp/arr.c firmware/src/dsp/loops.c \
  firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c"
mkdir -p build/host && eval "$ARR" && build/host/arr_test      # the engine test
sh tests/run_tests.sh                                           # everything
./build.sh                                                      # the firmware (size / RAM / pool checks)
```

---

### Task 1: Loop and row helpers (`loops.c`)

**Goal:** Pure, tested helpers for packed chords, loop length and writing, factory loops, and song rows.

**Files:**
- Create: `firmware/src/dsp/loops.h`, `firmware/src/dsp/loops.c`, `tests/host/loops_test.c`
- Modify: `firmware/src/dsp/arr.h` (the data types only, top of the file), `tests/run_tests.sh`

**Acceptance Criteria:**
- [ ] `sizeof(songs_t) <= 3840` (static assert) with 16 loops of 64 beats and 4 songs of 32 rows
- [ ] `loop_write` grows an empty loop bar by bar and refuses past 64 beats
- [ ] `row_tap` adds a row, or adds to REPEAT when the same loop is tapped again, with the section default
- [ ] `loop_from_template` gives the factory progressions as loops (blues: 12 bars; waltz: 3 beats)
- [ ] `loops_test` prints `loops test passed`

**Verify:** `cc -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/src/dsp -o build/host/loops_test tests/host/loops_test.c firmware/src/dsp/loops.c firmware/src/dsp/templates.c firmware/src/dsp/chords.c && build/host/loops_test` → `loops test passed`

**Steps:**

- [ ] **Step 1: The data types in `arr.h`.** Replace the old `song_t` / `songs_t` and `ARR_SONGBARS` with:

```c
#define ARR_SONGS 4
#define ARR_LOOPS 16                     /* L1..L16 */
#define ARR_LOOP_BEATS 64                /* a loop's beats at most (bars x beats) */
#define ARR_ROWS 32                      /* a song's rows at most */
#define LC_SAME 0xFFFFu                  /* a loop's beat with no chord change ("·") */
/* a chord in 16 bits: root 0..11 (bits 0-3), type CH_* (4-8), bass 0..11 or 15 none (9-12) */
static inline uint16_t lc_pack(chord_t c)
{
    return (uint16_t)((c.root & 15u) | (uint32_t)(c.type & 31u) << 4 | (uint32_t)(c.bass < 12u ? c.bass : 15u) << 9);
}
static inline chord_t lc_unpack(uint16_t v)
{
    chord_t c = {(uint8_t)(v & 15u), (uint8_t)(v >> 4 & 31u), (uint8_t)((v >> 9 & 15u) < 12u ? (v >> 9 & 15u) : CH_NOBASS)};
    return c;
}
typedef struct {
    char name[8];
    uint8_t bars, beats;                 /* 0 bars: empty; 2..7 beats a bar; bars * beats <= ARR_LOOP_BEATS */
    uint8_t rsv[2];
    uint16_t ch[ARR_LOOP_BEATS];         /* beat i of the loop: lc_pack, or LC_SAME */
} loop_t;
enum { ROW_A, ROW_A_FILL, ROW_B, ROW_B_FILL, ROW_NSECT };   /* a row's section (+ a fill in its last bar) */
typedef struct {
    uint8_t loop;                        /* 0..ARR_LOOPS-1 */
    uint8_t sect;                        /* ROW_* */
    uint8_t rep;                         /* plays 1..8 times */
    uint8_t mute;                        /* tracks muted, bit t track t */
    int8_t key;                          /* -6..+5 semitones over the song's transpose */
    uint8_t rsv;
} row_t;
typedef struct {
    uint16_t style[ARR_NSECT];           /* the pattern for each section (as before) */
    uint16_t sty;                        /* the song's style (as before) */
    uint8_t nrows;                       /* 0..ARR_ROWS */
    uint8_t intro, ending, at_end;       /* Intro first; Ending last; at the end 0 stop, 1 again */
    uint8_t tempo;                       /* 40..240 */
    int8_t transpose;                    /* -6..+5 */
    uint8_t mute;                        /* the tracks muted in FREE and LOOP */
    uint8_t rsv;
    row_t row[ARR_ROWS];
} song_t;
typedef struct {
    uint32_t magic, format;
    loop_t loop[ARR_LOOPS];
    song_t song[ARR_SONGS];
} songs_t;
static inline int row_main(const row_t *r) { return r->sect >= ROW_B ? SEC_B : SEC_A; }
static inline int row_fill(const row_t *r) { return r->sect == ROW_A_FILL || r->sect == ROW_B_FILL; }
```

Keep `_Static_assert(sizeof(songs_t) <= 3840, ...)`. (16 × 140 + 4 × 214 + 8 = 3104 bytes.) Delete the
engine declarations of `arr_song`, `arr_template`, `arr_set_key`, `arr_template_now`, `arr_template_bar`,
`arr_song_bar`, `arr_song_wait` from `arr.h` in Task 2, not now (`arr.c` still compiles against them until then).

- [ ] **Step 2: Write the failing test** `tests/host/loops_test.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* loops.c: packed chords, loop writing (growth, the 64-beat limit), factory loops, song rows */
#include <stdio.h>
#include <string.h>
#include "arr.h"
#include "loops.h"
#include "templates.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static chord_t ch(int r, int t) { chord_t c = {(uint8_t)r, (uint8_t)t, CH_NOBASS}; return c; }

static void test_pack(void)
{
    chord_t c = {9, CH_MIN7, 4}, d = lc_unpack(lc_pack(c)), e = lc_unpack(lc_pack(ch(0, CH_NONE)));
    CHECK(d.root == 9 && d.type == CH_MIN7 && d.bass == 4, "Am7/E round trip");
    CHECK(e.type == CH_NONE && e.bass == CH_NOBASS, "--- round trip, no bass");
    CHECK(lc_pack(ch(11, CH_DIM)) != LC_SAME, "never LC_SAME");
}
static void test_write(void)
{
    loop_t l;
    loop_clear(&l, 8);
    CHECK(l.bars == 0 && l.beats == 4 && loop_len(&l) == 0 && strcmp(l.name, "Loop 9") == 0, "empty, named by its number");
    CHECK(loop_write(&l, 0, ch(9, CH_MIN)) == 0 && l.bars == 1, "beat 0: one bar");
    CHECK(loop_write(&l, 4, ch(5, CH_MAJ)) == 0 && l.bars == 2, "beat 4: two bars");
    CHECK(loop_write(&l, 13, ch(7, CH_MAJ)) == 0 && l.bars == 4, "beat 13: four bars");
    CHECK(l.ch[1] == LC_SAME && l.ch[4] == lc_pack(ch(5, CH_MAJ)), "the rest no change");
    CHECK(loop_write(&l, 64, ch(0, CH_MAJ)) == -1 && l.bars == 4, "past 64 beats: refused");
    CHECK(loop_chord_at(&l, 6).root == 5 && loop_chord_at(&l, 2).root == 9, "the chord sounding at a beat");
    CHECK(loop_chord_at(&l, 0).root == 9, "beat 0: its own");
    l.ch[0] = LC_SAME;
    CHECK(loop_chord_at(&l, 0).root == 7, "beat 0 without a chord: the loop's last (it goes round)");
    loop_set_beats(&l, 7);
    CHECK(l.beats == 7 && l.bars == 4 && loop_len(&l) == 28, "7 beats: 4 bars fit");
    loop_set_bars(&l, 16);
    CHECK(l.bars == 9, "7 beats: 9 bars at most (%d)", l.bars);
    loop_set_beats(&l, 4);
    loop_set_bars(&l, 16);
    CHECK(l.bars == 16 && loop_len(&l) == 64, "4 beats: 16 bars");
}
static void test_factory(void)
{
    loop_t l;
    static const int L18[8] = {0, 1, 2, 3, 4, 7, 9, 8};   /* L1..L8: Pop, Sensitive, Fifties, ii-V-I, Blues, Minor, Andalusian, Canon */
    for (int i = 0; i < 8; i++)
        CHECK(FACTORY_LOOP[i] == L18[i], "factory loop %d is template %d", i, L18[i]);
    loop_from_template(&l, 4);
    CHECK(l.bars == 12 && l.beats == 4 && strcmp(l.name, "Blues") == 0, "the blues: 12 bars");
    CHECK(lc_unpack(l.ch[4]).root == 5 && l.ch[5] == LC_SAME, "a chord a bar: F7 on bar 2");
    loop_from_template(&l, 15);
    CHECK(l.bars == 4 && l.beats == 3 && lc_unpack(l.ch[6]).type == CH_7, "the waltz in 3: C7 on bar 3");
}
static void test_rows(void)
{
    song_t g;
    loops_song_clear(&g);
    CHECK(g.nrows == 0 && g.intro == 1 && g.ending == 1 && g.at_end == 0 && g.tempo == 120, "an empty song");
    CHECK(row_tap(&g, 0, 8) == 0 && g.nrows == 1 && g.row[0].loop == 8 && g.row[0].rep == 1 && g.row[0].sect == ROW_A,
          "tap L9: row 1, x1, Main A");
    CHECK(row_tap(&g, 0, 8) == 0 && g.nrows == 1 && g.row[0].rep == 2, "L9 again: x2");
    CHECK(row_tap(&g, 1, 9) == 1 && g.nrows == 2 && g.row[1].loop == 9, "L10: row 2");
    g.row[1].sect = ROW_B;
    row_tap(&g, 2, 8);
    row_tap(&g, 3, 9);
    CHECK(g.row[2].sect == ROW_A && g.row[3].sect == ROW_B, "a loop's section from its earlier row");
    g.row[3].rep = 8;
    CHECK(row_tap(&g, 4, 9) == 3 && g.row[3].rep == 8 && g.nrows == 4, "x8 at most");
    CHECK(row_tap(&g, 0, 9) == 0 && g.row[0].loop == 9 && g.row[0].rep == 1, "a tap on an earlier row rewrites it");
    g.row[0].loop = 8;
    CHECK(loop_rows_using(&g, 9) == 2, "L10 in 2 rows");
    row_insert(&g, 1);
    CHECK(g.nrows == 5 && g.row[1].loop == 8 && g.row[2].loop == 9, "insert: a copy of the row before");
    row_delete(&g, 1);
    CHECK(g.nrows == 4 && g.row[1].loop == 9, "delete");
    g.nrows = ARR_ROWS;
    g.row[ARR_ROWS - 1].loop = 0;
    CHECK(row_tap(&g, ARR_ROWS, 3) == -1, "32 rows at most");
}
static void test_free_length(void)
{
    CHECK(free_rec_bars(0, 4) == 1 && free_rec_bars(4, 4) == 1 && free_rec_bars(5, 4) == 2, "rounded up to bars");
    CHECK(free_rec_bars(200, 4) == 16 && free_rec_bars(200, 7) == 9, "64 beats at most");
}
int main(void)
{
    test_pack();
    test_write();
    test_factory();
    test_rows();
    test_free_length();
    printf("%d checks, %d failed\n", checks, fails);
    if (fails)
        return 1;
    printf("loops test passed\n");
    return 0;
}
```

- [ ] **Step 3: Run it to see it fail.** Run the Verify command. Expected: compile error, `loops.h` not found.

- [ ] **Step 4: Write `loops.h`:**

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* Loops and song rows (arr.h's types): pure helpers the app and the tests share. No state. */
#pragma once
#include "arr.h"
extern const uint8_t FACTORY_LOOP[8];            /* L1..L8's factory progressions: TEMPLATES indices */
int loop_len(const loop_t *l);                    /* its beats: bars * beats */
int loop_max_bars(int beats);                     /* ARR_LOOP_BEATS / beats, at most 16 */
void loop_clear(loop_t *l, int i);                /* empty (0 bars of 4), named "Loop <i+1>" */
void loop_from_template(loop_t *l, int t);        /* TEMPLATES[t]: a chord a bar, its beats, its name */
/* chord c on beat b: the loop grows (bar by bar) to hold it. 0 = written, -1 = past ARR_LOOP_BEATS */
int loop_write(loop_t *l, int b, chord_t c);
void loop_unwrite(loop_t *l, int b);              /* beat b back to LC_SAME */
chord_t loop_chord_at(const loop_t *l, int b);    /* the chord sounding on beat b (the latest at or before
                                                   * it, round from the end); CH_NONE type if it has none */
void loop_set_bars(loop_t *l, int bars);          /* 1..loop_max_bars; beats past the end kept, not played */
void loop_set_beats(loop_t *l, int beats);        /* 2..7: the bars shrink to fit 64 beats */
void loops_song_clear(song_t *g);                 /* no rows; Intro and Ending on; stop at the end; 120 BPM */
/* white key `loop` tapped on the SONG screen at row r: r == the last row and its loop is `loop`: REPEAT + 1
 * (8 at most), returns r; else row r is set (a new row at the end), its section the one this loop had in an
 * earlier row, else Main A; returns the row; -1 when full */
int row_tap(song_t *g, int r, int loop);
void row_insert(song_t *g, int r);                /* a copy of row r - 1 (or a default row) at r */
void row_delete(song_t *g, int r);
int loop_rows_using(const song_t *g, int loop);   /* rows of g that play it */
int free_rec_bars(int beats, int beats_a_bar);    /* REC in FREE: the bars the take rounds up to (1..max) */
```

- [ ] **Step 5: Write `loops.c`:**

```c
/* SPDX-License-Identifier: GPL-3.0-only */
#include <string.h>
#include "loops.h"
#include "templates.h"

const uint8_t FACTORY_LOOP[8] = {0, 1, 2, 3, 4, 7, 9, 8};

int loop_len(const loop_t *l) { return l->bars * (l->beats ? l->beats : 4); }
int loop_max_bars(int beats)
{
    int m = ARR_LOOP_BEATS / (beats < 2 ? 2 : beats);
    return m > 16 ? 16 : m;
}
static void name_set(loop_t *l, const char *s)
{
    memset(l->name, 0, sizeof l->name);
    for (int i = 0; i < (int)sizeof l->name && s[i]; i++)
        l->name[i] = s[i];
}
void loop_clear(loop_t *l, int i)
{
    char n[8] = "Loop ";
    memset(l, 0, sizeof *l);
    l->beats = 4;
    if (i >= 9) {
        n[5] = '1';
        n[6] = (char)('0' + (i + 1) % 10);
    } else
        n[5] = (char)('1' + i);
    name_set(l, n);
    for (int b = 0; b < ARR_LOOP_BEATS; b++)
        l->ch[b] = LC_SAME;
}
void loop_from_template(loop_t *l, int t)
{
    const template_t *m = &TEMPLATES[t];
    loop_clear(l, 0);
    name_set(l, m->name);
    l->beats = m->beats;
    l->bars = m->nbars;
    for (int b = 0; b < m->nbars; b++)
        l->ch[b * m->beats] = lc_pack(m->bars[b]);
}
int loop_write(loop_t *l, int b, chord_t c)
{
    int bpb = l->beats ? l->beats : 4;
    if (b < 0 || b >= loop_max_bars(bpb) * bpb)
        return -1;
    if (b >= loop_len(l))
        l->bars = (uint8_t)(b / bpb + 1);
    l->ch[b] = lc_pack(c);
    return 0;
}
void loop_unwrite(loop_t *l, int b)
{
    if (b >= 0 && b < ARR_LOOP_BEATS)
        l->ch[b] = LC_SAME;
}
chord_t loop_chord_at(const loop_t *l, int b)
{
    int n = loop_len(l);
    chord_t none = {0, CH_NONE, CH_NOBASS};
    if (n <= 0)
        return none;
    b %= n;
    for (int k = 0; k < n; k++) {
        int i = (b - k + n) % n;
        if (l->ch[i] != LC_SAME)
            return lc_unpack(l->ch[i]);
    }
    return none;
}
void loop_set_bars(loop_t *l, int bars)
{
    int m = loop_max_bars(l->beats);
    l->bars = (uint8_t)(bars < 1 ? 1 : bars > m ? m : bars);
}
void loop_set_beats(loop_t *l, int beats)
{
    l->beats = (uint8_t)(beats < 2 ? 2 : beats > 7 ? 7 : beats);
    if (l->bars > loop_max_bars(l->beats))
        l->bars = (uint8_t)loop_max_bars(l->beats);
}
void loops_song_clear(song_t *g)
{
    uint16_t style[ARR_NSECT], sty = g->sty;
    memcpy(style, g->style, sizeof style);
    memset(g, 0, sizeof *g);
    memcpy(g->style, style, sizeof style);
    g->sty = sty;
    g->intro = g->ending = 1;
    g->tempo = 120;
}
static row_t row_new(const song_t *g, int r, int loop)
{
    row_t w = {(uint8_t)loop, ROW_A, 1, 0, 0, 0};
    for (int k = r - 1; k >= 0; k--)
        if (g->row[k].loop == loop) {
            w.sect = g->row[k].sect >= ROW_B ? ROW_B : ROW_A;
            break;
        }
    return w;
}
int row_tap(song_t *g, int r, int loop)
{
    int last = g->nrows - 1;
    if ((r == g->nrows || r == last) && last >= 0 && g->row[last].loop == loop) {
        if (g->row[last].rep < 8)              /* the same loop again at the end: REPEAT + 1 */
            g->row[last].rep++;
        return last;
    }
    if (r < 0 || r > g->nrows || r >= ARR_ROWS)
        return -1;
    g->row[r] = row_new(g, r, loop);
    if (r == g->nrows)
        g->nrows++;
    return r;
}
void row_insert(song_t *g, int r)              /* (no memmove in the firmware: by hand) */
{
    if (g->nrows >= ARR_ROWS || r < 0 || r > g->nrows)
        return;
    for (int k = g->nrows; k > r; k--)
        g->row[k] = g->row[k - 1];
    g->row[r] = r > 0 ? g->row[r - 1] : row_new(g, 0, 0);
    g->nrows++;
}
void row_delete(song_t *g, int r)
{
    if (r < 0 || r >= g->nrows)
        return;
    for (int k = r; k < g->nrows - 1; k++)
        g->row[k] = g->row[k + 1];
    g->nrows--;
}
int loop_rows_using(const song_t *g, int loop)
{
    int n = 0;
    for (int r = 0; r < g->nrows; r++)
        n += g->row[r].loop == loop;
    return n;
}
int free_rec_bars(int beats, int bpb)
{
    int bars = (beats + bpb - 1) / bpb, m = loop_max_bars(bpb);
    return bars < 1 ? 1 : bars > m ? m : bars;
}
```

`row_tap`'s rule: the white key lands on the cursor row `r`; when `r` is the last row or the one after it
and the last row plays `loop`, that row's REPEAT goes up; otherwise row `r` is written (appended when
`r == nrows`). The firmware has no `memmove` (commit c9f3cf3): rows are copied by hand.

- [ ] **Step 6: Run the test.** Expected: `loops test passed`.

- [ ] **Step 7: Add it to `tests/run_tests.sh`** after the `chords` line:

```sh
run loops sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/src/dsp -o $OUT/loops_test tests/host/loops_test.c firmware/src/dsp/loops.c firmware/src/dsp/templates.c firmware/src/dsp/chords.c && $OUT/loops_test"
```

and append ` firmware/src/dsp/loops.c` after `firmware/src/dsp/arr.c` on the `arr`, `timing`, `gm`, `filter`,
`style-chords` and `bank-arr` lines. Add `loops.c` to the firmware's source list (`tools/build.py` or the unity
build file that includes `arr.c`: `grep -rn "arr.c" tools/build.py firmware/src/*.c host/build_host.sh web/emu/build.sh`
and add `loops.c` next to each).

- [ ] **Step 8: Commit.**

```bash
git add firmware/src/dsp/loops.h firmware/src/dsp/loops.c firmware/src/dsp/arr.h tests/host/loops_test.c tests/run_tests.sh
git commit -m "Loops and rows: the data, packed chords, writing with growth, factory loops, row taps"
```

---

### Task 2: The engine's chord source: FREE and LOOP

**Goal:** `arr.c` plays chords from FREE (the keys only) or a loop (looping, transposed, a played chord held
until the loop's next change), reading the app's loop table; the template path is removed.

**Files:**
- Modify: `firmware/src/dsp/arr.h`, `firmware/src/dsp/arr.c`
- Test: `tests/host/arr_test.c`

**Acceptance Criteria:**
- [ ] A 4-bar loop plays its chords on their beats and goes round; a 3-beat loop on a 3-beat pattern too
- [ ] `arr_mode_loop(i)` while playing starts loop i at the next bar line, its chord at that downbeat
- [ ] An empty loop (0 bars) changes no chord
- [ ] `arr_set_transpose(2)` plays the loop a tone up, from the next beat
- [ ] A chord from the keys in LOOP sounds until the loop's next chord entry; `arr_override` is 1 meanwhile
- [ ] With the loop table busy on a beat line, the chord comes at the first block after it is released (never skipped)
- [ ] The old template tests are removed and the arr test passes

**Verify:** `eval "$ARR" && build/host/arr_test` → `arr test passed`

**Steps:**

- [ ] **Step 1: The API in `arr.h`.** Remove `arr_song`, `arr_template`, `arr_set_key`, `arr_template_now`,
`arr_template_bar`, `arr_song_bar`, `arr_song_wait` and their comments. Add:

```c
/* ---- the chord source. FREE: the keys' chords only (held: fingered). LOOP: a loop of the app's table goes
 * round, transposed; SONG: rows of a song (Task 3). The app's table and song stay put (they are the app's
 * songs_t): the render reads them on beat and bar lines. The app brackets every write into them with
 * arr_edit(1) / arr_edit(0); a beat line that finds them busy takes its chord at the first block after. */
enum { ARR_FREE, ARR_LOOP, ARR_SONG };
void arr_loops(const loop_t *table);             /* at init: the app's ARR_LOOPS loops */
void arr_edit(int on);
void arr_mode_free(void);
/* loop i: stopped, from PLAY; playing, from the next bar line (its bars count from there) */
void arr_mode_loop(int i);
void arr_set_transpose(int semis);               /* -6..+5: loops and songs (not the keys' chords) */
extern volatile uint8_t arr_mode;                /* ARR_* now (a change asked for: from its bar line) */
extern volatile int8_t arr_loop_now;             /* the loop playing (LOOP, or the song's row), -1 none */
extern volatile uint16_t arr_loop_beat;          /* the beat of that loop sounding (0..len-1) */
extern volatile uint8_t arr_override;            /* a chord from the keys holds until the source's next change */
extern volatile uint32_t arr_beats;              /* beats since PLAY (REC in FREE measures its take with it) */
```

Keep `arr_keys_held` (its comment: "set as the chord; in LOOP and SONG it holds until the source's next
chord change").

- [ ] **Step 2: Write the failing tests** in `tests/host/arr_test.c`. Delete `test_templates`,
`test_template_now`, `test_template_beats` and their calls in `main`; remove `#include "templates.h"`; add
`#include "loops.h"`. Add helpers and tests:

```c
static loop_t LT[ARR_LOOPS];                       /* the app's loop table, here */
static void loops_reset(void)
{
    for (int i = 0; i < ARR_LOOPS; i++)
        loop_clear(&LT[i], i);
    arr_loops(LT);
}
/* loop i: n chords, one a bar of `beats` */
static void loop_bars(int i, int beats, int n, const chord_t *c)
{
    loop_clear(&LT[i], i);
    LT[i].beats = (uint8_t)beats;
    for (int b = 0; b < n; b++)
        loop_write(&LT[i], b * beats, c[b]);
}
static void test_loop(void)
{
    static const chord_t pop[4] = {{0, CH_MAJ, CH_NOBASS}, {7, CH_MAJ, CH_NOBASS}, {9, CH_MIN, CH_NOBASS}, {5, CH_MAJ, CH_NOBASS}};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(2, 4, 4, pop);
    arr_set_transpose(2);                         /* D A Bm G */
    arr_mode_loop(2);
    arr_play(1);
    run_to((uint32_t)(5 * BAR + 512));
    CHECK(root_at(0) == 2 && root_at(BAR) == 9 && root_at(2 * BAR) == 11 && root_at(3 * BAR) == 7, "D A Bm G (%d %d %d %d)",
          root_at(0), root_at(BAR), root_at(2 * BAR), root_at(3 * BAR));
    CHECK(root_at(4 * BAR) == 2 && root_at(5 * BAR) == 9, "round again");
    CHECK(arr_mode == ARR_LOOP && arr_loop_now == 2 && arr_loop_beat == 4, "for the screen (%d %d %d)", arr_mode, arr_loop_now, arr_loop_beat);
    arr_set_transpose(0);
    run_to((uint32_t)(5.5f * BAR + 512));
    CHECK(root_at(5.5f * BAR) == 7, "transpose 0: G (%d)", root_at(5.5f * BAR));
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "loop: offs match");
}
static void test_loop_waltz(void)
{
    static const chord_t w[4] = {{0, CH_MAJ, CH_NOBASS}, {0, CH_MAJ, CH_NOBASS}, {7, CH_7, CH_NOBASS}, {0, CH_MAJ, CH_NOBASS}};
    reset();
    loops_reset();
    load_slots(3);
    loop_bars(0, 3, 4, w);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(4 * 72 * FPT + 512));
    CHECK(root_at(2 * 72 * FPT) == 7 && root_at(3 * 72 * FPT) == 0 && root_at(4 * 72 * FPT) == 0, "a chord a 3-beat bar");
    arr_play(0);
    run_blocks(1);
}
static void test_loop_switch(void)                /* picked while playing: from the next bar line */
{
    static const chord_t a[1] = {{9, CH_MIN, CH_NOBASS}}, f[1] = {{5, CH_MAJ, CH_NOBASS}};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, a);
    loop_bars(1, 4, 1, f);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(1.3f * BAR));
    arr_mode_loop(1);
    run_to((uint32_t)(3 * BAR + 512));
    CHECK(root_at(1.5f * BAR) == 9 && root_at(2 * BAR) == 5, "Am to the bar line, then F (%d %d)", root_at(1.5f * BAR), root_at(2 * BAR));
    arr_mode_loop(4);                              /* an empty loop: the chord stays */
    run_to((uint32_t)(5 * BAR + 512));
    CHECK(root_at(4 * BAR) == 5 && arr_loop_now == 4, "empty loop: F stays");
    arr_mode_free();
    run_to((uint32_t)(6 * BAR + 512));
    CHECK(arr_mode == ARR_FREE && arr_loop_now == -1 && root_at(6 * BAR) == 5, "FREE: the chord stays");
    arr_play(0);
    run_blocks(1);
}
static void test_loop_override(void)              /* a played chord holds until the loop's next change */
{
    static const chord_t c4[2] = {{0, CH_MAJ, CH_NOBASS}, {7, CH_MAJ, CH_NOBASS}};
    static const uint8_t f[] = {53, 57, 60};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 2, c4);                        /* C | G */
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(0.3f * BAR));
    CHECK(arr_keys_held(f, 3) == 1, "F played");
    run_to((uint32_t)(0.9f * BAR));
    CHECK(root_at(0.5f * BAR) == 5 && arr_override == 1, "F holds (%d), override shown", root_at(0.5f * BAR));
    run_to((uint32_t)(1.1f * BAR));
    CHECK(root_at(BAR) == 7 && arr_override == 0, "the loop's G takes over (%d)", root_at(BAR));
    arr_play(0);
    run_blocks(1);
}
static void test_loop_busy(void)                  /* the table busy on a beat line: its chord a block late */
{
    static const chord_t c2[2] = {{0, CH_MAJ, CH_NOBASS}, {7, CH_MAJ, CH_NOBASS}};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 2, c2);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(BAR - 300));
    arr_edit(1);
    run_to((uint32_t)(BAR + 300));
    CHECK(arr_chord_unpack(arr_chord_now).root == 0, "busy: C still");
    arr_edit(0);
    run_blocks(1);
    CHECK(arr_chord_unpack(arr_chord_now).root == 7, "released: G at once");
    arr_play(0);
    run_blocks(1);
}
```

Call them from `main` after `test_live_chords();`. In `test_live_chords`, replace the template part
(`arr_template(0)` …) with a loop: `loop_bars(0, 4, 4, pop); arr_mode_loop(0);` (pop = C G Am F; picked at
~2.5 bars, it starts at the bar line at 3 × BAR with C). The keys' Am at 3.1 bars holds until the loop's next
entry, G at 4 × BAR. Replace the check with
`CHECK(root_at(3.5f * BAR) == 9 && root_at(4 * BAR) == 7, "the keys' Am until the loop's G (%d %d)", root_at(3.5f * BAR), root_at(4 * BAR));`. Add `loops_reset();`
right after every `reset();` in the file (the engine needs a table).

- [ ] **Step 3: Run to see them fail** (compile errors: `arr_mode_loop` undefined, …).

- [ ] **Step 4: Implement in `arr.c`.**

Commands: in the enum replace `C_SONG, C_TEMPLATE, C_KEY` with `C_MODE, C_TRANSPOSE` (C_SONG comes back in
Task 3). State (replace `tmpl`, `key`, `t_origin`'s template meaning, `song*`):

```c
volatile uint8_t arr_mode, arr_override;
volatile int8_t arr_loop_now = -1;
volatile uint16_t arr_loop_beat;
volatile uint32_t arr_beats;
static const loop_t *loops;                        /* the app's table */
static volatile uint8_t edit_busy;                 /* the app is writing the table or the song */
static uint8_t mode, mode_next, lp, lp_next;       /* the source, and one asked for (from its bar line) */
static uint8_t mode_wait;                          /* mode_next waits for the bar line */
static int8_t transp;                              /* -6..+5 */
static uint8_t chord_wait;                         /* a beat line found the table busy: its chord owed */
static uint8_t override;                           /* a chord from the keys holds until the source's next change */
static uint32_t lp_origin;                         /* beat_now where the loop's beat 0 fell */
void arr_loops(const loop_t *t) { loops = t; }
void arr_edit(int on) { BARRIER(); edit_busy = (uint8_t)(on != 0); BARRIER(); }
void arr_mode_free(void) { post(C_MODE, ARR_FREE, 0); }
void arr_mode_loop(int i) { post(C_MODE, ARR_LOOP, (uint32_t)(i >= 0 && i < ARR_LOOPS ? i : 0)); }
void arr_set_transpose(int s) { post(C_TRANSPOSE, (uint32_t)(int32_t)(s < -6 ? -6 : s > 5 ? 5 : s) & 0xFFu, 0); }
static chord_t shifted(chord_t c, int by)          /* a stored chord, transposed */
{
    by = ((by % 12) + 12) % 12;
    c.root = (uint8_t)((c.root + by) % 12u);
    if (c.bass < 12u)
        c.bass = (uint8_t)((c.bass + by) % 12u);
    return c;
}
```

The source's chord on a beat line (replaces `tmpl_chord` and the template branch of `beat_chord`):

```c
/* the loop's chord on this beat: its own entry (a change: it ends an override), or with `owed` (the loop's
 * first beat, after a held section) the one sounding there. 0: nothing to do */
static int loop_beat(const loop_t *l, uint32_t b, int owed, int by, int play)
{
    int n = l->bars * (l->beats ? l->beats : 4);
    uint16_t v;
    if (n <= 0)
        return 0;
    b %= (uint32_t)n;
    arr_loop_beat = (uint16_t)b;
    v = l->ch[b];
    if (v == LC_SAME && !owed)
        return 0;
    if (v == LC_SAME) {                            /* the latest before it, round from the end */
        for (int k = 1; k < n && v == LC_SAME; k++)
            v = l->ch[((int)b - k + n) % n];
        if (v == LC_SAME)
            return 0;
        if (override)                              /* (owed: a played chord still wins until a real change) */
            return 0;
    }
    override = 0;
    chord_change(shifted(lc_unpack(v), by), play);
    return 1;
}
static void source_chord(int play)
{
    if (edit_busy) {
        chord_wait = 1;                            /* the first block after the app's write */
        return;
    }
    chord_wait = 0;
    if (mode == ARR_LOOP)
        loop_beat(&loops[lp], beat_now - lp_origin, beat_now == lp_origin || chord_owed, transp, play);
    chord_owed = 0;
}
```

In `beat_chord(int play)`: keep the early return for the held Intro / Ending; replace the `song_on` / `tmpl`
branches with `source_chord(play);` (the SONG branch is added in Task 3). In `arr_block`, right after
`drain();`, add:

```c
    if (chord_wait && !edit_busy && arr_playing) {
        chord_owed = 1;                            /* the chord sounding now (a change on the missed beat too) */
        source_chord(1);
    }
```

Mode changes: in `drain()`:

```c
        case C_MODE:
            if (a == ARR_LOOP && !loops)
                break;
            mode_next = (uint8_t)a;
            lp_next = (uint8_t)b;
            if (!arr_playing) {                    /* stopped: now; its beat 0 is PLAY's */
                mode = mode_next;
                lp = lp_next;
                mode_wait = 0;
            } else
                mode_wait = 1;                     /* playing: from the next bar line */
            if (a == ARR_FREE && arr_playing) {    /* FREE: at once (the chord stays) */
                mode = ARR_FREE;
                mode_wait = 0;
            }
            override = 0;
            break;
        case C_TRANSPOSE:
            transp = (int8_t)(int8_t)(a & 0xFFu);
            if (arr_playing && mode != ARR_FREE)
                chord_owed = 1, source_chord(1);   /* the chord sounding, moved now */
            break;
        case C_LIVE:
            chord_change(arr_chord_unpack(a), 1);
            override = mode != ARR_FREE;
            break;
```

In `bar_line()`, before the section logic: `if (mode_wait) { mode = mode_next; lp = lp_next; mode_wait = 0; lp_origin = beat_now; chord_owed = 1; }`.
In `transport_start()`: `if (mode_wait) { mode = mode_next; lp = lp_next; mode_wait = 0; } lp_origin = 0; override = 0;`.
In `switch_to`, the `t_origin = beat_now` line goes (a loop's bars count from where it started, not the Intro);
keep `chord_owed = 1` when leaving the Intro / Ending.
In `tick()`, after `beat_now++`: `arr_beats = beat_now;`. In `arr_block` after `sync();`:

```c
    arr_mode = mode_wait ? mode_next : mode;
    arr_loop_now = mode == ARR_LOOP ? (int8_t)lp : -1;
    arr_override = override;
```

`arr_init`: `mode = mode_next = ARR_FREE; mode_wait = 0; transp = 0; override = 0; chord_wait = 0; arr_loop_now = -1;`
(keep `loops` as set: `arr_loops` is called once by the app). Delete `arr_template`, `arr_set_key`,
`tmpl_chord`, `tmpl`, `key`, `t_origin`, `arr_template_now/bar`. Leave the song code compiling for now with
`song_on` forced 0 — it is replaced in Task 3 (delete `arr_song` and `C_SONG` there).

- [ ] **Step 5: Run the arr test.** Expected: `arr test passed`. Fix until it does (existing tests that used
templates were rewritten in Step 2; tests on songs still use the old `S` — leave them failing to *compile*?
No: in this task comment them out of `main` and out of the file with `#if 0 … #endif` around
`test_song`, `test_song_marks_continue`, `test_song_while_playing`, `test_song_busy`, `test_shorten`,
`test_catchup_no_restrike`, `test_catchup_jump`, `test_switch_chord_ends`, `test_intro`, `test_ending`,
`test_fill_fallbacks`; Task 3 ports them).

- [ ] **Step 6: Commit.**

```bash
git add firmware/src/dsp/arr.h firmware/src/dsp/arr.c tests/host/arr_test.c
git commit -m "Engine: the chord source, FREE and LOOP (loops from the app's table, transpose, a played chord until the next change)"
```

---

### Task 3: The engine's SONG source: rows, repeats, fills, mutes, key, intro, ending

**Goal:** A song of rows plays each row's loop REPEAT times in its section, with automatic and forced fills,
the row's mutes (a hand mute lasting to the next row), the row's KEY, the Intro and Ending, and AT END.

**Files:**
- Modify: `firmware/src/dsp/arr.h`, `firmware/src/dsp/arr.c`
- Test: `tests/host/arr_test.c`

**Acceptance Criteria:**
- [ ] Rows L1 ×2 (A), L2 ×1 (B): L1's chords twice in Main A, then Fill AB in the last bar before L2, then Main B with L2's chords
- [ ] A `+ fill` row with the same section next plays a fill in its last bar
- [ ] Row mutes apply at each row's first downbeat; a hand mute (`arr_track_mute`) lasts until the next row; `arr_mutes` reports them
- [ ] A row's KEY +2 plays its loop a tone up (on top of the transpose)
- [ ] Intro on: the Intro plays first over row 1's first chord, then row 1; Ending on: the Ending after the last row, over the last chord, then stop; AT END again: row 1 again, no Intro
- [ ] `arr_mode_song(s, r)` stopped: PLAY starts at row r; playing: row r from the next bar line
- [ ] The song tests from before are ported to rows and pass

**Verify:** `eval "$ARR" && build/host/arr_test` → `arr test passed`

**Steps:**

- [ ] **Step 1: The API in `arr.h`:**

```c
/* SONG: song *s (the app's; it stays put) plays its rows from row r: stopped, from PLAY (with its Intro when
 * r is 0 and Intro is on); playing, from the next bar line (no Intro). Each row plays its loop rep times in its
 * section (Main A or B), its tracks muted as the row says, its chords shifted by the transpose and the row's key.
 * The section changes at the row's first downbeat; a fill plays in a row's last bar when the next row's
 * section differs, or the row asks for one. A section or a mute set by hand lasts until the next row. After the
 * last row: the Ending (on), held on the last chord, then the stop; else at_end again: row 0 (no Intro); else
 * the stop. The engine snapshots the song's header and the rows it needs at each block's start, when not
 * busy (arr_edit): so an edit while it plays takes effect from the next row it reads. */
void arr_mode_song(const song_t *s, int r);
extern volatile uint8_t arr_row, arr_rep;         /* the row playing, and its pass (0..rep-1) */
extern volatile uint8_t arr_mutes;                /* the tracks muted now (rows and hand mutes) */
```

- [ ] **Step 2: Port and add tests.** Replace the `#if 0` block's tests with ports on rows (keep their names
and what they check; the song's bars become rows of one-bar loops). Helper:

```c
static song_t SG;
static void song_rows(int n, const int *loop, const int *sect, const int *rep)
{
    memset(&SG, 0, sizeof SG);
    SG.nrows = (uint8_t)n;
    for (int r = 0; r < n; r++) {
        SG.row[r].loop = (uint8_t)loop[r];
        SG.row[r].sect = (uint8_t)sect[r];
        SG.row[r].rep = (uint8_t)rep[r];
    }
}
```

New tests (add to `main`):

```c
static void test_song_rows(void)
{
    static const chord_t am[2] = {{9, CH_MIN, CH_NOBASS}, {5, CH_MAJ, CH_NOBASS}}, cg[1] = {{0, CH_MAJ, CH_NOBASS}};
    static const int lo[2] = {0, 1}, se[2] = {ROW_A, ROW_B}, re[2] = {2, 1};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 2, am);                        /* L1: Am | F */
    loop_bars(1, 4, 1, cg);                        /* L2: C */
    song_rows(2, lo, se, re);
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(6 * BAR + 512));
    CHECK(root_at(0) == 9 && root_at(BAR) == 5 && root_at(2 * BAR) == 9 && root_at(3 * BAR) == 5, "L1 twice");
    CHECK(slot_at(0) == SA && slot_at(2 * BAR) == SA && slot_at(3 * BAR) == SFA, "A, A, A, Fill AB in the row's last bar (%d)", slot_at(3 * BAR));
    CHECK(slot_at(4 * BAR) == SB && root_at(4 * BAR) == 0, "row 2: Main B over C");
    CHECK(arr_playing == 0, "the Ending, then the stop (Intro / Ending off: the stop after row 2)");
    CHECK(offs_match(stub_log, stub_n), "rows: offs match");
}
static void test_song_forced_fill_mutes_key(void)
{
    static const chord_t c[1] = {{0, CH_MAJ, CH_NOBASS}};
    static const int lo[2] = {0, 0}, se[2] = {ROW_A_FILL, ROW_A}, re[2] = {2, 1};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, c);
    song_rows(2, lo, se, re);
    SG.row[0].mute = 1u << 3;                      /* row 1: the probe's track (3) muted */
    SG.row[1].key = 2;                             /* row 2: a tone up */
    SG.at_end = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    CHECK(arr_mutes == 1u << 3 && root_at(0) == -1, "row 1: track 3 muted (no probe note)");
    arr_track_mute(3, 0);                          /* by hand: on again, until the next row */
    run_to((uint32_t)(1.5f * BAR));
    CHECK(root_at(BAR) == 0 && slot_at(BAR) == SFA, "unmuted by hand; a forced fill in row 1's last bar");
    run_to((uint32_t)(2.5f * BAR));
    CHECK(arr_row == 1 && slot_at(2 * BAR) == SA && root_at(2 * BAR) == 2 && arr_mutes == 0, "row 2: A, D (key +2), no mutes");
    run_to((uint32_t)(3.5f * BAR));
    CHECK(arr_row == 0 && arr_mutes == 1u << 3 && arr_playing, "again: row 1, its mute back");
    arr_play(0);
    run_blocks(1);
}
static void test_song_intro_ending(void)
{
    static const chord_t a[1] = {{9, CH_MIN, CH_NOBASS}}, g[1] = {{7, CH_MAJ, CH_NOBASS}};
    static const int lo[2] = {0, 1}, se[2] = {ROW_A, ROW_A}, re[2] = {1, 1};
    reset();
    loops_reset();
    load_slot(SI, 1, 4);
    load_slots(4);
    loop_bars(0, 4, 1, a);
    loop_bars(1, 4, 1, g);
    song_rows(2, lo, se, re);
    SG.intro = SG.ending = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(5 * BAR + 512));
    CHECK(slot_at(0) == SI && root_at(0) == 9, "the Intro over row 1's Am");
    CHECK(slot_at(BAR) == SA && arr_row == 1, "then row 1 (A)");
    CHECK(slot_at(3 * BAR) == SE && root_at(3 * BAR) == 7, "the Ending over the last chord, G");
    CHECK(arr_playing == 0, "then the stop");
    /* from row 2 while playing: at the next bar line */
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, a);
    loop_bars(1, 4, 1, g);
    song_rows(2, lo, se, re);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_mode_song(&SG, 1);
    run_to((uint32_t)(1.5f * BAR));
    CHECK(arr_mode == ARR_SONG && arr_row == 1 && root_at(BAR) == 7, "row 2 from the bar line");
    arr_play(0);
    run_blocks(1);
}
```

- [ ] **Step 3: Run to see them fail.**

- [ ] **Step 4: Implement.** In `arr.c`, delete `song`, `stage`, `song_busy`, `stage_ready`, `song_on`,
`song_loop`, `song_restart`, `song_len`, `sbar`, `arr_song`, `arr_song_bar`, `arr_song_wait`,
`arr_test_song_busy`, `C_SONG`. Add:

```c
volatile uint8_t arr_row, arr_rep, arr_mutes;
static const song_t *sg;                           /* the app's song (SONG) */
static uint8_t row_from;                           /* the row PLAY or the switch starts at */
static uint8_t rows_live;                          /* the rows' clock runs (not during the Intro, not after the end) */
static uint8_t row;                                /* the row playing */
static uint32_t row_b0;                            /* beat_now at its first downbeat */
/* the song as the render sees it: its header and the rows it needs, taken at a block's start when not busy */
static struct { uint8_t nrows, intro, ending, at_end; row_t cur, next; uint16_t cur_len, next_len; uint8_t ok; } sn;
void arr_mode_song(const song_t *s, int r)
{
    sg = s;
    post(C_MODE, ARR_SONG, (uint32_t)(r < 0 ? 0 : r));
}
static uint16_t row_len(const row_t *w)            /* a row's beats: its loop's (an empty loop: a bar of 4), rep times */
{
    const loop_t *l = &loops[w->loop % ARR_LOOPS];
    int n = l->bars * (l->beats ? l->beats : 4);
    return (uint16_t)((n > 0 ? n : 4) * (w->rep ? w->rep : 1));
}
static void song_snap(void)                        /* arr_block, when the app is not writing */
{
    if (!sg || edit_busy)
        return;
    sn.nrows = sg->nrows;
    sn.intro = sg->intro;
    sn.ending = sg->ending;
    sn.at_end = sg->at_end;
    if (sn.nrows) {
        uint8_t r = row < sn.nrows ? row : 0, n = (uint8_t)(r + 1 < sn.nrows ? r + 1 : 0);
        sn.cur = sg->row[r];
        sn.next = sg->row[n];
        sn.cur_len = row_len(&sn.cur);
        sn.next_len = row_len(&sn.next);
    }
    sn.ok = 1;
}
static void row_mutes(uint8_t m)                   /* a row's mutes: the engine's own (a hand mute lasts to here) */
{
    for (int t = 0; t < ARR_TRACKS; t++) {
        uint8_t on = (uint8_t)(m >> t & 1u);
        if (on && !tk[t].mute) {
            track_off(&tk[t]);
            ctl_reset_mask(1u << (pat[cur].trk[t].chan & 15u));
        }
        tk[t].mute = on;
    }
}
/* the row starting now (at a bar line): its section as the mark, its mutes, its chord owed */
static uint8_t row_start(void)
{
    row_b0 = beat_now;
    rows_live = 1;
    row_mutes(sn.cur.mute);
    chord_owed = 1;
    override = 0;
    return (uint8_t)row_main(&sn.cur);
}
```

Mode switch in `drain`'s `C_MODE` case: for `ARR_SONG`, `row_from = (uint8_t)b`; stopped: `mode = ARR_SONG;
row = row_from; rows_live = 0;` (transport_start starts it); playing: `mode_wait = 1` as for loops. When the
mode leaves SONG (to FREE or LOOP), the app re-sends its mutes (Task 6).

`transport_start()`: before choosing the section,

```c
    if (mode == ARR_SONG) {
        row = row_from;
        rows_live = 0;
        song_snap();                               /* (stopped: the app is not writing) */
        if (sn.nrows && !(sn.intro && row == 0 && !rec_on && !sect_empty(SEC_INTRO)))
            s = row_start();                       /* no Intro: the row's section from beat 0 */
        else if (sn.nrows)
            s = SEC_INTRO;                         /* the Intro holds row 1's first chord (beat_chord below) */
    }
```

(the existing `song.sect[0]` line goes). `bar_line()`: replace the old song block with:

```c
    if (mode == ARR_SONG && sn.nrows) {
        int b = (int)bar_len(&pat[cur]) / ARR_PPQ;
        if (mode_wait_row) {                       /* switched while playing: row_from at this bar line */
            mode_wait_row = 0;
            row = row_from;
            song_snap_row();
            mark = row_start();
        } else if (!rows_live && arr_sect == SEC_INTRO && pos == 0 && !first_tick) {
            mark = row_start();                    /* the Intro played once: row 1 */
        } else if (rows_live && !first_tick && beat_now - row_b0 >= sn.cur_len) {
            if (row + 1 < sn.nrows || sn.at_end) { /* the next row (or row 1 again) */
                row = (uint8_t)(row + 1 < sn.nrows ? row + 1 : 0);
                sn.cur = sn.next;
                sn.cur_len = sn.next_len;
                mark = row_start();
            } else if (sn.ending && !sect_empty(SEC_END)) {
                rows_live = 0;                     /* the Ending: the last chord held */
                mark = SEC_END;
            } else {
                stop_ours();
                ended = 1;
                return 0;
            }
        }
        if (rows_live && mark == SEC_SAME) {       /* the row's last bar: a fill into a different section, or asked */
            uint32_t left = sn.cur_len - (beat_now - row_b0);
            int last = row + 1 >= sn.nrows && !sn.at_end;
            if (left > 0 && left <= (uint32_t)b && !last &&
                (row_fill(&sn.cur) || row_main(&sn.next) != arr_sect))
                mark = arr_sect == SEC_B ? SEC_FILL_BA : SEC_FILL_AB;
        }
        arr_row = row;
        arr_rep = (uint8_t)((beat_now - row_b0) / (sn.cur_len / (sn.cur.rep ? sn.cur.rep : 1)));
    }
```

`mode_wait_row`: a `static uint8_t` set in the mode switch at the bar line (the `if (mode_wait)` block of Task
2: when `mode_next == ARR_SONG`, set `mode = ARR_SONG; mode_wait_row = 1;`). `song_snap_row()` takes `sn.cur`
/ `sn.next` for `row` the same way as `song_snap` (call `song_snap()` — the main loop never runs inside the
render, so `edit_busy` set means the app was interrupted mid-write; then keep the last snapshot). Note: the mark
from a row start must switch even when it equals the section playing after a hand change — `row_start()`'s mark
is compared against `arr_sect` by the existing `if (next == SEC_SAME && mark != arr_sect)`, which is right (the
same section plays on).

The fill's return: the existing rule "a fill's return to its Main after its bar" is overridden by the next row's
mark at that same bar line (mark != fill: switch). Make the existing condition
`if (is_fill(arr_sect) && sec_bars >= 1 && !is_fill(mark))` only apply when `mode != ARR_SONG || !rows_live`.

`source_chord` gets the SONG branch (so the busy retry covers it; `beat_chord` keeps only its Intro / Ending
early return, then calls `source_chord`):

```c
static void source_chord(int play)
{
    if (edit_busy) {
        chord_wait = 1;                            /* the first block after the app's write */
        return;
    }
    chord_wait = 0;
    if (mode == ARR_LOOP)
        loop_beat(&loops[lp], beat_now - lp_origin, beat_now == lp_origin || chord_owed, transp, play);
    else if (mode == ARR_SONG && sn.nrows && (rows_live || arr_sect == SEC_INTRO)) {
        /* a row's loop from its first downbeat; the Intro: row_from's first chord (sn.cur), held */
        arr_loop_now = (int8_t)sn.cur.loop;
        loop_beat(&loops[sn.cur.loop % ARR_LOOPS], rows_live ? beat_now - row_b0 : 0, chord_owed || !rows_live,
                  transp + sn.cur.key, play);
    }                                              /* after the last row: the Ending holds the last chord */
    chord_owed = 0;
}
``` In `arr_block`, call `song_snap()`
before `drain()` each block, and publish `arr_mutes` from `tk[t].mute`:

```c
    { uint8_t m = 0; for (int t = 0; t < ARR_TRACKS; t++) m |= (uint8_t)(tk[t].mute << t); arr_mutes = m; }
```

`arr_loop_now` for SONG: the row's loop (set in the SONG branch); for LOOP as in Task 2; FREE: -1.

- [ ] **Step 5: Port the old song tests** (remove the `#if 0`): each `song_new(n)` + `S.sect[b]` / `S.chord[k]`
becomes loops of one bar and rows of `rep` 1 (a run of equal bars: one row with `rep`); `arr_song(&S, loop)`
becomes `SG.at_end = loop; arr_mode_song(&SG, 0)`; `arr_song(0, 0)` becomes `arr_mode_free()`;
`arr_test_song_busy(1)` becomes `arr_edit(1)`; `arr_song_bar` becomes `arr_row`; `arr_song_wait` checks go
(a switch while playing is `arr_mode == ARR_SONG` with `arr_row` unchanged until the bar line). Song marks that
were SEC_FILL_AB / SEC_FILL_BA / SEC_INTRO / SEC_END on a bar become: a fill → the row before has a `+ fill`
section; Intro / Ending → `SG.intro` / `SG.ending`. Delete a port only if what it checked no longer exists
(`test_song_marks_continue`: marks on every bar — replace with "a row of rep 4 plays its section on, no
restart"). Keep every check on timing, offs matching and catch-up.

- [ ] **Step 6: Run.** Expected: `arr test passed`.

- [ ] **Step 7: Commit.**

```bash
git add firmware/src/dsp/arr.h firmware/src/dsp/arr.c tests/host/arr_test.c
git commit -m "Engine: songs of rows (repeats, fills, row mutes and key, intro, ending, again)"
```

---

### Task 4: The other engine tests on the new API

**Goal:** `style_chords_test` and `bank_arr_test` (and `timing`, `gm`, `filter`, which link `arr.c`) build
and pass with loops and rows.

**Files:**
- Modify: `tests/host/style_chords_test.c`, `tests/host/bank_arr_test.c`, `tests/host/timing_test.c`, `tests/host/gm_test.c`, `tests/host/filter_test.c` (only if they call removed functions)

**Acceptance Criteria:**
- [ ] `style-chords` plays each test progression as a loop (`arr_mode_loop`) over Main A and passes unchanged in what it checks
- [ ] `bank-arr` passes; its `arr_template(0)` / `arr_template(1)` become loops L1 / L2 from `loop_from_template`; its `arr_song(&g, 1)` cases become rows with `at_end = 1`
- [ ] `sh tests/run_tests.sh` shows `ok` for loops, arr, timing, gm, filter, style-chords, bank, bank-arr (the app's targets still fail until Task 6)

**Verify:** `sh tests/run_tests.sh 2>&1 | grep -E "ok|FAIL" | head -20`

**Steps:**

- [ ] **Step 1:** In `style_chords_test.c`, replace the song `S` and its chord track with a loop table: for
progression `g` and `bars`, `loop_clear(&LT[0], 0); LT[0].beats = beats; for b in bars: loop_write(&LT[0], b *
beats, g->c[b % g->n]);` then `arr_loops(LT); arr_mode_loop(0);` instead of `arr_song(&S, 0)`. Keep `S.sect[0] =
SEC_A`'s meaning: the engine starts in Main A without an Intro loaded (it is empty).
- [ ] **Step 2:** In `bank_arr_test.c`: `static loop_t LT[ARR_LOOPS]` with `loop_from_template(&LT[0], 0)` (and
`[1]`), `arr_loops(LT)`, `arr_mode_loop(0)`; the two `song_t g` cases: build `song_t` rows (one row per old
mark region) and `arr_mode_song(&g, 0)` with `g.at_end = 1`.
- [ ] **Step 3:** `grep -n "arr_template\|arr_song\|arr_set_key" tests/host/*.c` → nothing left. Add
`arr_loops(LT)` with an empty table wherever a test calls `arr_init()` and plays (the engine needs a table only
in LOOP / SONG; FREE without one is fine — check `C_MODE` refuses LOOP without a table, it does).
- [ ] **Step 4:** Run the Verify command; expected `ok` on those targets.
- [ ] **Step 5: Commit** `git commit -am "Engine tests: progressions as loops, songs as rows"`.

---

### Task 5: The project: songs format 6, factory loops, per-song tempo and transpose

**Goal:** The saved object holds the 16 loops and 4 songs of rows; defaults give L1–L8 factory and L9–L16
empty; each song keeps tempo and transpose; old formats load as defaults.

**Files:**
- Modify: `firmware/src/app/app.h`, `firmware/src/app/project.c`
- Test: `tests/storage_test.c` (if it builds `project.c`; else a new `tests/host/project_test.c`, see Step 1)

**Acceptance Criteria:**
- [ ] `SONGS_FORMAT 6`; a stored object of format 5 (or any other) loads as the defaults
- [ ] Defaults: L1..L8 = templates 0,1,2,3,4,7,9,8; L9..L16 empty named "Loop 9".."Loop 16"; 4 songs empty with tempo = the first style's tempo, transpose 0, Intro and Ending on
- [ ] `proj.key` and `proj.tempo` are no longer used for playback (the song's are); `project_t` keeps its layout (fields renamed `rsv_key`, `rsv_tempo`) so the project object needs no new format
- [ ] Load validates: loop bars/beats within limits (else that loop cleared), rows' loop < 16, sect < ROW_NSECT, rep 1..8, key -6..5, nrows <= 32 (else clamped)

**Verify:** `sh tests/run_tests.sh 2>&1 | grep -E "storage|project"` → `ok`

**Steps:**

- [ ] **Step 1:** `grep -n "project.c" tests/*.c tests/host/*.c host/*.sh` — if no host test builds
`project.c`, create `tests/host/project_test.c` that includes `firmware/src/app/project.c` with a stub
`plat_store_load/save` (an in-memory object table: `static uint8_t obj[8][4096]; static int len[8];`), and
add it to `run_tests.sh` (`run project sh -c "$CC -std=c99 -O1 -w -Ifirmware/src -Ifirmware/src/app -Ifirmware/src/dsp -Ifirmware/hal -o $OUT/project_test tests/host/project_test.c firmware/src/dsp/loops.c firmware/src/dsp/templates.c firmware/src/dsp/chords.c && $OUT/project_test"`).
Its tests:

```c
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
```

- [ ] **Step 2:** In `app.h`: `#define SONGS_FORMAT 6u` with the comment "6: 16 loops and songs of rows (older:
reset)"; in `project_t` rename `tempo` → `rsv_tempo` and `key` → `rsv_key` (unused; layout unchanged).
- [ ] **Step 3:** In `project.c`: delete `songs_from3`, `songs_from4`, `SONG3_BYTES`, `SONG4_BYTES` and their
asserts. `song_default(g, n)`: `memset`, the style as before (`g->style[k] = ARR_FACTORY0 + k; g->sty =
ARR_FACTORY0;`), then `loops_song_clear(g)` and `g->tempo = (uint8_t)arr_style_tempo(0)`. `songs_defaults(s)`:
magic, format, `for i < 8: loop_from_template(&s->loop[i], FACTORY_LOOP[i]); for i in 8..15: loop_clear(&s->loop[i], i);`
and the four songs. `project_songs_load`: magic / format other than 6 → defaults; then validate as in the
criteria (a loop with `beats` outside 2..7 or `bars > loop_max_bars(beats)` → `loop_clear(&s->loop[i], i)`;
rows clamped field by field; a song's tempo outside 40..240 → 120; transpose outside -6..5 → 0); keep the
existing style validation of `g->style[k]` / `g->sty`.
- [ ] **Step 4:** Run; expected `ok   project` (and `storage`).
- [ ] **Step 5: Commit** `git commit -am "Project: songs format 6 (16 loops, songs of rows, per-song tempo and transpose)"`.

---

### Task 6: The app on the new engine (pages map, keys by screen, MODE, HOME + key)

**Goal:** The firmware builds and runs again: the page table of the spec, SELECT turning pages, the keys'
role set by the page (no CHORD mode), MODE (FREE / LOOP / SONG), HOME held + white / black / SELECT /
PRESETS, the song's tempo / transpose / mutes, and the HOME-held overlay. The new screens are placeholders
until Tasks 8–10.

**Files:**
- Modify: `firmware/src/app/pages.c`, `firmware/src/app/ui.c`, `host/polly_host.c`

**Acceptance Criteria:**
- [ ] `./build.sh` succeeds (size, RAM, pool headroom checks) and `sh host/build_host.sh` builds
- [ ] Pages: HOME → HOME, SONG (HOME again cycles); ARP → LOOP, LOOP 2; SEQ → Pattern, Pattern setup; EDIT → Track, Track keys; LFO → Mixer, Tone; FX → Master FX; ENV → Style (2 pages); GLO → Setup, Panel. SELECT turns the current button's pages
- [ ] The keys play chords on HOME and LOOP, the selected track on SEQ / EDIT, nothing on FX / ENV / GLO; on LFO the black keys pick the track
- [ ] HOME held + white key n: `arr_mode_loop(n)` (+ MODE = LOOP); the lit loop again: FREE; + black key 1..5: Intro, Main A, Main B, Fill, Ending; + SELECT: tempo; + PRESETS: song. Held 400 ms: the overlay with the 16 loops' names ("—" empty) and the five sections
- [ ] HOME's KNOB 4 MODE: FREE / LOOP / SONG calls the engine; leaving SONG re-sends the song's mutes; tempo / transpose come from the song (`arr_set_tempo(g->tempo)`, `arr_set_transpose(g->transpose)`) at boot and on a song change
- [ ] The app calls `arr_loops(songs.loop)` once in `app_start`, and brackets every write into `songs.loop[]` or a playing song's rows with `arr_edit(1)` / `arr_edit(0)`
- [ ] Removed: CHORD mode (`ui.chord`, `set_chord_mode`, `KM_CHORD` toggling by SEL), SEL-held shortcuts and `draw_sel`, the Templates / Copy template / Chords / Arrange pages, `pick_template`, `template_copy`, `key_turn` on Setup

**Verify:** `./build.sh && sh host/build_host.sh && build/host/polly_host tests/scenarios/first.polly build/scenarios/first` (after Step 6 rewrites `first.polly`) → exit 0

**Steps:**

- [ ] **Step 1: Page table** (`pages.c`):

```c
enum { P_HOME, P_SONG, P_LOOP, P_LOOP2, P_PATTERN, P_PATSET, P_TRACK, P_TRACK2, P_MIXER, P_TONE, P_MASTER,
       P_STYLE, P_STYLE2, P_SETUP, P_PANEL, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"Home", "Song", "Loop", "Loop", "Pattern", "Pattern setup", "Track",
                                              "Track keys", "Mixer", "Mixer tone", "Master FX", "Style", "Style",
                                              "Setup", "Panel"};
static const struct { uint8_t btn, first, n; } PAGE_BTN[] = {
    {B_HOME, P_HOME, 2}, {B_ARP, P_LOOP, 2}, {B_SEQ, P_PATTERN, 2}, {B_EDIT, P_TRACK, 2}, {B_LFO, P_MIXER, 2},
    {B_FX, P_MASTER, 1}, {B_ENV, P_STYLE, 2}, {B_GLO, P_SETUP, 2}};
```

Every `P_*` use in `pages.c` / `ui.c` / `host/polly_host.c` follows (`P_SONG` meant the old Song page: now
`P_HOME`; `P_CHORDS` / `P_ARRANGE*` / `P_TEMPLATES` / `P_TCOPY`: deleted with their draw and knob functions;
`P_ARRANGE2/3`'s six-pattern knobs move to `P_STYLE` / `P_STYLE2`, KNOB 4 the style).

- [ ] **Step 2: SELECT pages.** In `knob()` (ui.c), the SELECT encoder: `page_turn(d)` — within
`PAGE_BTN[page_group(ui.page)]`, `ui.page = first + (ui.page - first + d + n) % n`, then `page_leave`. Remove
`set_tempo` from SELECT. HOME held + SELECT: `set_tempo(song tempo + d)` (and store it in the song,
`app.songs_dirty = 1`).

- [ ] **Step 3: Keys by page** (`key_down` / `key_up`): a helper

```c
enum { KR_NONE, KR_CHORDS, KR_TRACK, KR_PICK };
static int key_role(void)
{
    if (ui.page == P_HOME || ui.page == P_LOOP || ui.page == P_LOOP2)
        return KR_CHORDS;
    if (ui.page == P_PATTERN || ui.page == P_PATSET || ui.page == P_TRACK || ui.page == P_TRACK2)
        return KR_TRACK;
    if (ui.page == P_MIXER || ui.page == P_TONE)
        return KR_PICK;
    return ui.page == P_SONG ? KR_PICK : KR_NONE;
}
```

(REC on HOME writes chords: Task 8). `KR_CHORDS` feeds `chord_keys()` (the held
set → `arr_keys_held`); `KR_TRACK` plays `arr_key`; `KR_PICK` on the mixer: black key i < 8 selects track i;
on SONG: Task 10. Delete `ui.chord`, `set_chord_mode`, the CHORD pill in the header, the SEL tap toggle.

- [ ] **Step 4: HOME held.** In `button_down` / `holds()`: HOME pressed starts a hold; a key pressed while
HOME is down is a shortcut (mark `ui.btn_used |= 1u << B_HOME` so its release is not a page press):
white key i → `loop_pick(i)`; black key 1..5 → `arr_section(SEC_INTRO + k)` (k 0..4, 3 = `SEC_FILL`); PRESETS
turned → `song_pick(d)`. `loop_pick(i)`: if `arr_mode == ARR_LOOP && arr_loop_now == i` → `mode_set(ARR_FREE)`;
else `app.loop = i; mode_set(ARR_LOOP)`. The overlay (`draw_home_held`, replacing `draw_sel`): after 400 ms,
"HOME + a key" in the header; the main band lists L1–L16 in two columns (name, "—" for `bars == 0`; the
playing one outlined teal, the queued one coral) and the five sections on a line (the playing one outlined).

- [ ] **Step 5: MODE and the song's settings.** App state: `app.mode` (ARR_*), `app.loop` (the loop picked,
0..15), `app.row` (the SONG cursor). `mode_set(m)`: FREE → `arr_mode_free()`; LOOP → `arr_mode_loop(app.loop)`;
SONG → `arr_mode_song(csong(), arr_playing ? 0 : app.row)`; then if `m != ARR_SONG`, `app.mute_sent = ~m`
(forces `mutes_apply()` to re-send the song's FREE/LOOP mutes). `song_load()` (song change, boot):
`arr_set_tempo(g->tempo); arr_set_transpose(g->transpose);` and `mode_set(app.mode)`. The tempo knob and
HOME + SELECT write `g->tempo`; TRANSPOSE writes `g->transpose`; both `app.songs_dirty = 1; app.chg++`.
Mutes in SONG: SEQ's MUTE knob calls `arr_track_mute` only (the hand mute, until the next row; Task 10 writes
it into the row when REC is on); in FREE / LOOP it changes `g->mute` as today.

- [ ] **Step 6: `host/polly_host.c` expects.** Remove `template`, `chord_mode`, `song_chord<k>`, `song_bars`,
`song_sect<b>`, `cbeat`, `rec_chords`, `key`, `cur_chord`, `song_on`. Add:

```c
    else if (!strcmp(what, "mode")) got = arr_mode;                         /* 0 FREE, 1 LOOP, 2 SONG */
    else if (!strcmp(what, "loop_now")) got = arr_loop_now + 1;             /* 1..16, 0 none */
    else if (!strcmp(what, "loop_beat")) got = arr_loop_beat;
    else if (!strcmp(what, "override")) got = arr_override;
    else if (!strcmp(what, "row")) got = arr_row + 1;
    else if (!strcmp(what, "mutes")) got = arr_mutes;
    else if (!strncmp(what, "loop_bars", 9)) got = songs.loop[atoi(what + 9) - 1].bars;     /* loop_bars9 */
    else if (!strncmp(what, "loop_ch", 7)) {                                 /* loop_ch9_4: L9 beat 4, root*100+type, -1 "·" */
        int l = atoi(what + 7) - 1, b = atoi(strchr(what, '_') + 1);
        uint16_t v = songs.loop[l].ch[b];
        got = v == LC_SAME ? -1 : lc_unpack(v).root * 100 + lc_unpack(v).type;
    } else if (!strcmp(what, "rows")) got = csong()->nrows;
    else if (!strncmp(what, "row_loop", 8)) got = csong()->row[atoi(what + 8) - 1].loop + 1;  /* row_loop2 */
    else if (!strncmp(what, "row_rep", 7)) got = csong()->row[atoi(what + 7) - 1].rep;
    else if (!strncmp(what, "row_sect", 8)) got = csong()->row[atoi(what + 8) - 1].sect;
    else if (!strncmp(what, "row_mute", 8)) got = csong()->row[atoi(what + 8) - 1].mute;
    else if (!strncmp(what, "row_key", 7)) got = csong()->row[atoi(what + 7) - 1].key;
    else if (!strcmp(what, "transpose")) got = csong()->transpose;
    else if (!strcmp(what, "home_held")) got = home_shown();
```

(match the file's existing `got = …` / `else if` style). Rename `sel_shown` → `home_shown`.

- [ ] **Step 7: Scenarios that break.** Run `sh tests/run_tests.sh`; every scenario using removed buttons
(`chords.polly`, `compose.polly`, `tour.polly`, `manual.polly`, `first.polly`, …) fails. In this task rewrite
only `first.polly` to: boot, `expect page 0`, `expect mode 0`, play a chord on HOME (`press` three keys:
use the file's existing key names), `tap PLAY`, `expect chord_root …`, `press HOME` + `tap` white key 1 →
`expect mode 1`, `expect loop_now 1`; `tap SELECT`-turn → `expect page 1` (SONG). Mark the others for Task 11
by moving them to `tests/scenarios/todo/` (not run) — Task 11 brings each back.

- [ ] **Step 8: Build and run.** `./build.sh` (expect its RAM / pool notes, no errors), `sh host/build_host.sh`,
`build/host/polly_host tests/scenarios/first.polly build/scenarios/first`.

- [ ] **Step 9: Commit** `git add -A firmware host tests/scenarios && git commit -m "App: the X0X page map, keys by screen, MODE, HOME + key, per-song tempo and transpose"`.

---

### Task 7: X0X's screen furniture: tag, footer, help cards, SEL lists

**Goal:** Every page shows a tag over the knobs and a footer saying what the keys do; holding a button alone
shows a help card; SEL opens the page as a list (values, MORE, actions ↵, questions); the SAVE-held jobs move
into the lists; SAVE + white key copies (loop on HOME / LOOP, pattern on SEQ).

**Files:**
- Modify: `firmware/src/app/pages.c`, `firmware/src/app/ui.c`, `host/polly_host.c`

**Acceptance Criteria:**
- [ ] Layout: header 24 px, main band to y = 156, tag line 12 px (y 156–168, right-aligned tag + page dots), knobs 56 px (168–224), footer 16 px (224–240, `FONT_XS`, `K_DIM`). The knob strip keeps its four columns
- [ ] Tags per page: HOME `L3 VERSE` (LOOP) / `ROW 3` (SONG) / `FREE`; LOOP `L3 · BAR 2.3`; SONG `ROW 3`; SEQ / EDIT the track `C1 PIANO`; LFO `MIXER: BA`; FX `MASTER`; ENV `STYLE: POP`; GLO `SETUP`
- [ ] Footers exactly as the spec's table; `HOLD ANY BUTTON: WHAT IT DOES` until the first button press
- [ ] Holding any button alone 1 s shows its card (title + 1–2 lines, for this page) until release; release does nothing; a key or knob during the hold cancels the card (the combination works)
- [ ] SEL: the page's list (each knob's value as a row, then MORE rows, then actions ↵); SELECT moves, ALGORITHM changes, SEL runs an action; HOME backs out; an action that loses notes / chords asks first (SEL yes, HOME no)
- [ ] Actions: HOME/LOOP lists: LOAD PROGRESSION ▸ (ALGORITHM picks one of 16), CLEAR LOOP; SONG: INSERT ROW, DELETE ROW, CLEAR SONG, COPY SONG TO ▸; SEQ / EDIT / LFO / FX: ERASE TRACK, CLEAR PATTERN, BACK TO STYLE; MORE: NAME (letters) on LOOP and Pattern setup
- [ ] SAVE + white key n: on HOME / LOOP copy the playing loop to Ln ("L3 COPIED TO L7"), asking first when Ln has chords; on SEQ copy the pattern to P(n) ("P03 COPIED TO P07"), asking first when it has notes. SAVE tapped alone: save
- [ ] `ui.job`, `draw_jobs`, `job_knobs`, `jobs_open`, the SAVE-held jobs are gone

**Verify:** `sh host/build_host.sh && build/host/polly_host tests/scenarios/lists.polly build/scenarios/lists` → exit 0, screenshots `list_home.png`, `card_arp.png`, `ask_clear.png` written

**Steps:**

- [ ] **Step 1: Layout constants** in `pages.c`: `MAIN_H 132`, `TAG_Y 156`, `TAG_H 12`, `KNB_Y 168`,
`KNB_H 56`, `FOOT_Y 224`, `FOOT_H 16`. Move the page dots from the header to the tag line (right of the tag).
Re-check every `draw_*` that assumed `MAIN_H 144` (grep `MAIN_H`, `KNB_Y`, `144`, `172`).

- [ ] **Step 2: `draw_tag()` and `draw_footer()`**, each hashed like `draw_header` (`ui.sig[3]`, `ui.sig[4]`;
grow `sig` to 6). Text per page from two functions `page_tag(char *t)` and `page_footer(char *t)` with the
spec's strings (the footer's REC part follows `app.mode`: HOME in FREE `REC: NEW LOOP`, else `REC: WRITE`).

- [ ] **Step 3: Help cards.** `static const struct { uint8_t btn; uint8_t page; const char *title, *l1, *l2; } CARD[]`
(page 0xFF: any page). At least: HOME ("Home and song", "Press: Home, again Song.", "Hold + a key: a loop (white), a section (black)."),
ARP ("Loops", "Your chord loops. Play chords.", "REC writes them: at the cursor, or in time."),
SEQ, EDIT, LFO, FX, ENV, GLO (their pages, one line each), SEL ("Lists", "This page as a list, MORE, actions."),
SAVE ("Save", "Tap: save. Hold + a white key: copy here."), REC (per page: HOME/LOOP "Write chords",
SEQ/EDIT "Record the track", SONG "Record the song's rows"), PLAY, OCT− / OCT+ (LOOP: "Cursor: + skips a step").
`holds()`: a button down ≥ 1000 ms with nothing else pressed and not used → `ui.card = btn + 1`; drawn over
the main band (`cv_round` panel, title `FONT_B`, lines `FONT_S`); any key / knob / other button → `ui.card = 0`
and the button counts as used.

- [ ] **Step 4: Lists.** A list is built per page into `static struct { uint8_t n, sel, top; struct { const char *name; char val[12]; uint8_t kind, id; } row[24]; } lst;`
(kind: KNOB (id = page, knob), MORE header, ACTION ↵, PICK ▸). Rows: the page's four knobs' values (from the
existing `*_knobs()` builders: call them for each of the page button's pages), then `MORE`, then the actions
above. SELECT moves `lst.sel`; ALGORITHM on a KNOB row calls `knob(k, e)` with that page set temporarily; on a
PICK row cycles its choice; SEL on ACTION runs it (through `ask()` when it loses data); HOME closes.
`ask(const char *q, void (*yes)(void))`: the header says the question, the footer `SEL: YES · HOME: NO`.
Move `job_run` / `song_job_run`'s bodies into the action functions (`act_erase_track`, `act_clear_pattern`,
`act_back_to_style`, `act_clear_song`, `act_copy_song`); add `act_insert_row`, `act_delete_row` (Task 10's
row functions from `loops.c`), `act_load_progression(t)` (`arr_edit(1); loop_from_template(&songs.loop[app.loop], t);
arr_edit(0);` keeping the loop's name only if the user renamed it — simpler: the template's name), `act_clear_loop`
(`loop_clear`, asks).

- [ ] **Step 5: SAVE + white key.** In `key_down`: SAVE held and a white key → `copy_to(n)`; mark SAVE used (no
save on its release). `copy_to`: HOME / LOOP: `ask` if `songs.loop[n].bars` and n != source; `arr_edit(1)`,
`songs.loop[n] = songs.loop[src]`, `arr_edit(0)`; say "L3 COPIED TO L7". SEQ: the existing Copy pattern job's
code with target P(n).

- [ ] **Step 6: A scenario** `tests/scenarios/lists.polly`: tap SEL on HOME, `shot list_home.png`, move with
SELECT, back with HOME; hold ARP 1200 ms → `shot card_arp.png`, release → `expect page 0` (no page change);
on LOOP, SEL, select CLEAR LOOP, SEL → `shot ask_clear.png`, HOME (no) → `expect loop_bars1 4`; SEL again,
SEL (yes) → `expect loop_bars1 0`; then on HOME: hold HOME + `tapkey w1` (L2), release HOME, hold SAVE +
`tapkey w8` → `expect loop_bars9 4` (L2 copied to L9).
Add `expect` names `list_open`, `card`, `asking` to the host.

- [ ] **Step 7: Run** the Verify command; look at the three screenshots (Read them) for overlap and cut text.

- [ ] **Step 8: Commit** `git commit -am "UI: tag and footer, help cards, SEL lists with actions, SAVE + white key copies"`.

---

### Task 8: The HOME screen and REC on it (write into the loop; a new loop from FREE)

**Goal:** HOME shows the chord big (coral when yours, teal from the loop), the section, the loop strip with
the playhead and the next chord, or FREE / the song's row; REC on HOME writes chords into the loop playing, or
in FREE records a new loop into the first empty slot.

**Files:**
- Modify: `firmware/src/app/pages.c` (`draw_home`, `home_knobs`), `firmware/src/app/ui.c` (`rec_chords`, `chord_played`, `free_take_*`)

**Acceptance Criteria:**
- [ ] Knobs STYLE · TEMPO · TRANSPOSE · MODE; TEMPO and TRANSPOSE write the song's
- [ ] FREE before any chord: the box says "Play a chord"; with `arr_override` the chord is coral, else teal
- [ ] LOOP: the strip shows up to 8 beats-groups of the loop around the playhead (bars separated by │, "·" for no change), "next: <chord>"; SONG: "row 2 of 4 · ×1 of 2"
- [ ] REC on HOME in LOOP or SONG: as REC on LOOP (Task 9: step while stopped, live while playing); in SONG it first switches to LOOP on the row's loop ("SONG PAUSED: EDITING L9")
- [ ] REC on HOME in FREE: no empty loop → "NO EMPTY LOOP" "CLEAR ONE (SEL)"; else the take starts at the next downbeat (playing) or at PLAY; chords played land on the nearest beat from the take's start; REC again ends it: bars = `free_rec_bars(beats so far, beats a bar)`; the loop plays (MODE LOOP), "NEW LOOP L9"
- [ ] Scenario `loops.polly` part 1 passes (below)

**Verify:** `build/host/polly_host tests/scenarios/loops.polly build/scenarios/loops` → exit 0

**Steps:**

- [ ] **Step 1: `draw_home()`** (replaces `draw_song`): line 1 `Pop · Main A` (style, section, NEXT when
queued) and `bar 2.3 / 4` right; the chord box (`FONT_L`, `chord_name`), coral `K_ACCENT` if `arr_override`
else `K_TEAL`, "Play a chord" (`FONT_B`, `K_DIM`) when FREE and no chord was ever played (`ui.played == 0`);
the strip: `L3` then the loop's bars from `arr_loop_beat`'s bar, at most 2 bars of 4 beats, each beat a chord
name or "·" (`FONT_XS`), the playing beat underlined; "next: Am" = the loop's next entry after
`arr_loop_beat`. SONG adds the row line. Hash everything drawn.
- [ ] **Step 2: Knobs** `home_knobs(k)`: STYLE (existing `style_knob`), TEMPO (`kn(k, "Tempo", g->tempo, 240)`),
TRANSPOSE (`kv(k, "Transpose", KEY_NAME[(g->transpose + 12) % 12], g->transpose + 6, 11)`), MODE (`kv(k, "Mode",
MODE_NAME[app.mode], app.mode, 2)` with `MODE_NAME = {"free", "loop", "song"}`). `knob()` on HOME: K1
`style_turn`, K2 tempo, K3 transpose (`arr_set_transpose`), K4 `mode_set`.
- [ ] **Step 3: REC on HOME** (`button_down(B_REC)` on P_HOME): FREE → `free_take_toggle()`; SONG →
`app.loop = arr_loop_now; mode_set(ARR_LOOP); say("SONG PAUSED", "EDITING L9")` then as LOOP; LOOP →
`loop_rec_toggle()` (Task 9's).
- [ ] **Step 4: The free take.** State `ui.take` (0 off, 1 waiting for the downbeat, 2 on), `ui.take_loop`,
`ui.take_b0` (`arr_beats` at its start). `free_take_toggle()`: off → find the first `songs.loop[i].bars == 0`
(none: the message, return); `ui.take_loop = i`; `ui.take = arr_playing ? 1 : 2; ui.take_b0 = arr_playing ? 0 : 0`.
In `ui_frame`: `ui.take == 1` and a new bar started (watch `arr_bar` / `arr_beat_tick` wrap) → `ui.take = 2;
ui.take_b0 = arr_beats`. A chord recognised (`chord_played`) while `ui.take == 2`: beat = `arr_beats - ui.take_b0`
(+1 when `arr_beat_tick > ARR_PPQ / 2`: nearest beat); `arr_edit(1); loop_write(&songs.loop[ui.take_loop],
beat, c minus the transpose); arr_edit(0)`. On: second REC → `bars = free_rec_bars(arr_beats - ui.take_b0 + 1,
4)`; `loop_set_bars`; `app.loop = take_loop; mode_set(ARR_LOOP)`; say "NEW LOOP" "L9"; `ui.take = 0`. The header's
red dot shows while `ui.take`. "minus the transpose": `c = shifted(c, -g->transpose)` (add a `chord_shift(c, by)`
helper to `loops.c` with a test: `chord_shift({2,MAJ,NOBASS}, -2)` is C).
- [ ] **Step 5: Scenario `tests/scenarios/loops.polly`, part 1:**

```
# POLLY, loops: a new loop from FREE, then step writing on LOOP, the cursor, skip, name, copy.
wait 300
expect mode 0
tap PLAY
wait 200
tap REC
# (the take starts at the next downbeat)
wait 2000
key 4 down
key 7 down
key 11 down
wait 100
key 4 up
key 7 up
key 11 up
wait 1900
key 0 down
key 4 down
key 7 down
wait 100
key 0 up
key 4 up
key 7 up
wait 1000
tap REC
wait 100
expect mode 1
expect loop_now 9
expect loop_bars9 2
expect loop_ch9_0 916
expect loop_ch9_4 500
shot home_loop.png
```

(Keys: `key N down|up`, N 0..26 from F3: F3 0, G3 2, A3 4, B3 6, C4 7, D4 9, E4 11, G4 14. At 120 BPM a bar
is 2000 ms. `916` = A (9) × 100 + `CH_MIN` (16); `500` = F major.)

- [ ] **Step 6: Run** the Verify command; Read `home_loop.png`.
- [ ] **Step 7: Commit** `git commit -am "HOME: the chord, the loop strip, MODE; REC writes chords, or a new loop from FREE"`.

---

### Task 9: The LOOP screen (ARP): grid, cursor, step and live writing, skip, name, "in N rows"

**Goal:** ARP shows the loop playing (or the last picked) as a grid; the keys write chords at the cursor with
REC (step while stopped, nearest beat while playing), OCT+ skips, OCT− steps back; knobs edit the chord at
the cursor; page 2 LENGTH / BEATS / NAME; PRESETS queues the next loop.

**Files:**
- Modify: `firmware/src/app/pages.c` (`draw_loop`, `loop_knobs`, `draw_loop2`), `firmware/src/app/ui.c` (`loop_rec_toggle`, `chord_played`, `cursor_move`, `loop_knob`)

**Acceptance Criteria:**
- [ ] Grid: one row a bar (bars of 5–7 beats wrap to two lines), the cursor boxed, the playing beat tinted; header `Loop  L3 Verse   in 3 rows` (rows of the current song using it; nothing when 0)
- [ ] KNOB 1 ROOT, 2 TYPE (below maj: "·" clears the beat), 3 BASS, 4 STEP (beat / half / bar) edit the cursor's beat (writing an empty beat writes C maj first)
- [ ] REC stopped: a chord played is written at the cursor; when all keys are up the cursor moves on STEP; past the end the loop grows (bar by bar); at 64 beats "LOOP FULL"
- [ ] REC playing: a chord lands on the nearest beat of the loop (`arr_loop_beat`, +1 when past half a beat, wrapping), overdubbing
- [ ] OCT+ / OCT− move the cursor a STEP forward / back (OCT+ at the end grows the loop by a bar of "·" when REC is on, else wraps)
- [ ] Page 2: LENGTH (`loop_set_bars`), BEATS (`loop_set_beats`, "LENGTH 9: 64 BEATS MAX" when it shrank), NAME (Verse, Chorus, Bridge, Pre, Intro, Outro, Solo, Break, then "Loop N")
- [ ] PRESETS: the next / previous loop: `app.loop`, then `mode_set(ARR_LOOP)` (queued to the bar line while playing), in any mode — you edit what you hear (principle 1)
- [ ] Picking another loop (HOME + white, PRESETS) puts the cursor at beat 0
- [ ] Every write is bracketed with `arr_edit`; `app.songs_dirty`, `app.chg++`
- [ ] `loops.polly` part 2 passes

**Verify:** `build/host/polly_host tests/scenarios/loops.polly build/scenarios/loops` → exit 0

**Steps:**

- [ ] **Step 1: State.** `app.cbeat` (the cursor, a beat of the loop 0..63; replaces the song cursor),
`app.cstep` (0 beat, 1 half, 2 bar). `step_beats()` = 1, `beats/2` (at least 1), `beats`.
- [ ] **Step 2: `chord_played(notes, n)`** on LOOP / HOME with `ui.rec_chords` (REC on): recognise
(`chord_recognize`); stopped → `loop_write(l, app.cbeat, chord_shift(c, -transpose))`, remember
`ui.centry = app.cbeat`; adding a note while still holding rewrites the same beat; all keys up → `cursor_move(+1)`.
Playing → `b = arr_loop_beat + (arr_beat_tick > ARR_PPQ / 2)`, modulo `loop_len`, write there. Keep the existing
`ui.centry` / `ui.clifted` logic from today's Chords page (it already does "write, move on release").
- [ ] **Step 3: `cursor_move(d)`**: `app.cbeat += d * step_beats()`; past `loop_len`: REC on → grow
(`loop_set_bars(l, bars + 1)` up to the max, else "LOOP FULL"), REC off → wrap; below 0 → wrap to the last step.
OCT− / OCT+ on P_LOOP call it instead of changing the octave.
- [ ] **Step 4: Knobs** (P_LOOP): ROOT / TYPE / BASS on `loop_chord_at`'s value at the cursor if the beat has
its own chord, else start from the chord sounding there; TYPE one below `CH_MAJ` (index -1) → `loop_unwrite`.
STEP cycles `STEP_NAME`. Page 2 knobs: LENGTH `kn(k, "Length", bars, loop_max_bars(beats))`, BEATS, NAME
(`NAME_LIST[] = {"Verse","Chorus","Bridge","Pre","Intro","Outro","Solo","Break"}`, index 8 = "Loop N").
- [ ] **Step 5: `draw_loop()`**: rows of the grid from the cursor's bar (scrolled so the cursor's bar shows),
`FONT_S` chord names, "·" `K_DIM`; cursor `cv_round` outline `K_ACCENT`; the playing beat `K_TEAL_T` fill when
`arr_loop_now == app.loop`.
- [ ] **Step 6: `loops.polly` part 2** (append):

```
# step writing on L10 (empty), stopped
tap PLAY
wait 100
press HOME
tapkey w9
release HOME
tap ARP
expect page 2
tap REC
key 7 down
key 11 down
key 14 down
wait 50
key 7 up
key 11 up
key 14 up
wait 50
tap OCT+
key 2 down
key 6 down
key 9 down
wait 50
key 2 up
key 6 up
key 9 up
wait 50
tap REC
expect loop_bars10 3
expect loop_ch10_0 0
expect loop_ch10_4 -1
expect loop_ch10_8 700
shot loop.png
# name it
turn SELECT 1
spin K3 -8
expect loop_name10 Verse
```

(STEP default bar; OCT+ skipped bar 2. Add `loop_name<n>` as a string expect if the host supports strings;
else expect the NAME index `loop_name_i10 0`.)

- [ ] **Step 7: Run**, Read `loop.png`.
- [ ] **Step 8: Commit** `git commit -am "LOOP: the grid, step and live writing, skip, length, beats, name"`.

---

### Task 10: The SONG screen: rows, taps, mutes, sections, key, intro / ending, PLAY from a row, REC

**Goal:** HOME twice shows the song's rows; white keys write rows (same key again: REPEAT + 1), black keys
1–8 toggle the row's mutes; knobs edit ROW / LOOP / SECTION / REPEAT, page 2 the row's KEY, page 3 INTRO /
ENDING / AT END / SONG; PLAY starts at the selected row; REC + PLAY writes rows from HOME + keys and mutes.

**Files:**
- Modify: `firmware/src/app/pages.c` (`draw_song`, `song_knobs`), `firmware/src/app/ui.c` (`song_key`, `song_knob`, `song_rec`)
- Modify: `firmware/src/app/pages.c` page enum: add `P_SONG2`, `P_SONG3` after `P_SONG`. HOME still cycles
  HOME → SONG → HOME (`{B_HOME, P_HOME, 2}` counts SONG's three pages as one stop: pressing HOME on any
  of P_SONG..P_SONG3 goes to P_HOME); SELECT on SONG turns P_SONG → P_SONG2 → P_SONG3 (a special case in
  `page_turn`), and the tag's dots show three

**Acceptance Criteria:**
- [ ] Table: `#  LOOP  SECTION  REPEAT  MUTES  KEY`, 6 rows visible, the cursor row boxed, the playing row marked ▶, "END" after the last; footer per spec
- [ ] White key n: `row_tap(g, app.row, n)`, cursor to the row after (or stays on a REPEAT bump); black keys 1..8 toggle `row[app.row].mute` bit; muted keys glow dim (LEDs)
- [ ] Page 1 knobs ROW · LOOP · SECTION (Main A, Main A + fill, Main B, Main B + fill) · REPEAT; page 2 ROW · KEY (−6..+5, shown "+2", "0"); page 3 INTRO · ENDING · AT END (stop / again) · SONG
- [ ] PLAY on SONG: `app.mode = ARR_SONG; arr_mode_song(g, app.row); arr_play(1)`; the cursor follows `arr_row` while playing until ROW is turned (then stays until the next PLAY)
- [ ] REC on SONG while playing: from the selected row, each row change the player makes (HOME + white → that loop, HOME + black 2/3 → section, mute changes) is written into the row at the current position (`arr_row`): a HOME + white key during the row's last bar sets the *next* row's loop; rows are added at the end
- [ ] Every write to the rows is bracketed with `arr_edit`; edits mark `songs_dirty`
- [ ] `song.polly` (the spec's walk) passes

**Verify:** `build/host/polly_host tests/scenarios/song.polly build/scenarios/song` → exit 0

**Steps:**

- [ ] **Step 1: `draw_song()`** — columns at x 4 (#), 22 (loop "L9 Verse", `text_fit` 70 px), 96 (section
"Main A" / "Main B" + " +fill" in `K_ACCENT`), 160 (×n), 182 (mutes: 8 squares 5 × 7 px, filled = plays), 226
(key, if not 0). Rows 16 px. Below the table on page 3 only: "Intro on · Ending on · at end: stop".
- [ ] **Step 2: Keys on SONG** (`key_role == KR_PICK` and P_SONG..P_SONG3): white i →
`arr_edit(1); r = row_tap(g, app.row, i); arr_edit(0);` then `app.row = r + 1 if a new row was written else r`
(clamped to `nrows`), say nothing (the table shows it); full → "SONG FULL" "32 ROWS". Black i < 8 → toggle
`g->row[app.row].mute ^= 1 << i` (if `app.row < nrows`).
- [ ] **Step 3: Knobs.** ROW `kn(k, "Row", app.row + 1, nrows + 1)` (the row after the last is the "END" row
where taps append); LOOP `kv(k, "Loop", "L9", …)`, SECTION `kv(k, "Section", SECTION4[r.sect], …)`, REPEAT
`kv(k, "Repeat", "x2", …)`; KEY; INTRO / ENDING `kv(.., g->intro ? "on" : "off")`; AT END `"stop" / "again"`;
SONG `kn(k, "Song", proj.song + 1, 4)` → `song_pick`.
- [ ] **Step 4: PLAY and the follow.** `ui.row_follow = 1` at PLAY; while playing and `ui.row_follow`,
`app.row = arr_row`; turning ROW sets `ui.row_follow = 0`.
- [ ] **Step 5: REC on SONG.** `ui.song_rec`: on with REC on P_SONG (and PLAY to run). While on:
HOME + white n → if `arr_row`'s row ends within the bar playing, write row `arr_row + 1`'s loop (append if at
the end), else row `arr_row`'s; also `arr_mode_loop` is *not* called (the song keeps playing: the engine reads
the edited rows at the next row change). HOME + black 2/3 → `row.sect` of the current row (A / B), plus
`arr_section`; mute changes → `row.mute`. Note in the commit message: the engine snapshots rows per block, so
the change is heard from the next row.
- [ ] **Step 6: `tests/scenarios/song.polly`** — the spec's walk:

```
# POLLY, song: verse and chorus loops into a song of 5 rows, chorus on Main B, last chorus +2, ending on.
wait 300
# L9: Am F C G, L10: C G Am F (written on LOOP, step, a chord a bar)
press HOME
tapkey w8
release HOME
tap ARP
tap REC
...four chords (as loops.polly), tap REC
press HOME
tapkey w9
release HOME
tap REC
...four chords, tap REC
tap HOME
tap HOME
expect page 1
# (the taps, as described below)
expect rows 4
expect row_rep1 2
expect row_loop2 10
expect row_sect4 2
spin K1 2
turn SELECT 1
spin K2 2
expect row_key4 2
turn SELECT 1
expect page 3
shot song.png
tap PLAY
wait 300
expect mode 2
expect row 1
wait 16000
expect row 2
expect sect 3
```

Write the taps in two goes so the section default is tested: `tapkey w8`, `tapkey w8`, `tapkey w9`; turn ROW
to row 2 and SECTION to Main B (`expect row_sect2 2`); turn ROW to the END row; then `tapkey w8`, `tapkey w8`,
`tapkey w9`, `tapkey w9` → `expect rows 4`, `expect row_sect4 2` (row 4 takes L10's Main B), `expect
row_rep4 2`. The chord presses are written as in `loops.polly`. At 120 BPM a 4-bar loop is 8000 ms: row 1
×2 = 16 s.
- [ ] **Step 7: Run**, Read `song.png`.
- [ ] **Step 8: Commit** `git commit -am "SONG: rows by white keys, mutes on black keys, sections, key, intro / ending, PLAY from a row, REC"`.

---

### Task 11: Scenarios back, the emulator, the full suite green

**Goal:** Every scenario moved to `tests/scenarios/todo/` is rewritten for the new UI and back in
`tests/scenarios/`; the web emulator builds and its test passes; `sh tests/run_tests.sh` is green; the
branch merges into `polly`.

**Files:**
- Modify / move: `tests/scenarios/*.polly` (chords, compose, tour, manual, factory, factory_funk, cow, grid, mutes, pattern, … — every file in `todo/`)
- Modify: `tests/host/emu_test.mjs`, `web/emu/polly_web.c` (only what references removed names), `docs/ux-pass.md` (the flows table: a new row set for loops / songs)

**Acceptance Criteria:**
- [ ] `tests/scenarios/todo/` is empty and removed
- [ ] `tour.polly` walks: boot, a chord on HOME, PLAY, HOME + white 1 (Pop loop), HOME + black 3 (Main B), ARP (LOOP), SEL list, a help card, HOME HOME (SONG), GLO; screenshots for the manual: `home.png`, `home_held.png`, `loop.png`, `loop2.png`, `song.png`, `song2.png`, `song3.png`, `style.png`, `list.png`, `card.png`, `pattern.png`, `track.png`, `mixer.png`, `tone.png`, `master.png`, `setup.png`, `panel.png`, `recording.png`
- [ ] `sh tests/run_tests.sh` ends with `all tests passed`
- [ ] `./build.sh` passes its checks; image size noted in the commit message

**Verify:** `sh tests/run_tests.sh 2>&1 | tail -3` → `all tests passed`

**Steps:**

- [ ] **Step 1:** For each file in `todo/`: read it, keep what it tests that still exists (patterns, recording
into tracks, copy-on-write, mutes in FREE, the factory styles, the grid), replace removed controls (LFO →
ARP for chords, ENV → HOME HOME for the song, SEL-held → HOME-held, SAVE-held jobs → SEL list actions, CHORD
tap → nothing, SELECT tempo → K2 on HOME), and drop checks on removed features (templates, the 64-bar chord
track). Move it back. Run it alone (`build/host/polly_host tests/scenarios/<n>.polly build/scenarios/<n>`).
- [ ] **Step 2:** `tour.polly` with the screenshots above (named as listed: the manual uses them).
- [ ] **Step 3:** `sh web/emu/build.sh` (needs `emcc`; skip if absent, say so in the commit) and
`node tests/host/emu_test.mjs build/emu/polly.wasm`; fix names it references.
- [ ] **Step 4:** Full suite; fix failures (use superpowers-extended-cc:systematic-debugging for any that
aren't obvious).
- [ ] **Step 5:** `./build.sh`; note `image … B; RAM .data+.bss …; pool …` from its output.
- [ ] **Step 6: Commit** and merge:

```bash
git add -A tests docs/ux-pass.md web
git commit -m "Scenarios on the new UI; the suite green (image N B, RAM M B)"
git switch polly && git merge --no-ff loops -m "Loops, songs of rows, X0X's UI"
```

---

### Task 12: The manual, its figures and a labelled panel drawing

**Goal:** `MANUAL.md` describes the new model and pages (quick start; your own loop; a song — the spec's
walk), with the figures from `tour.polly` and a labelled panel drawing as X0X's.

**Files:**
- Modify: `MANUAL.md`, `docs/img/manual/*.png`
- Create: `docs/img/manual/fm1-panel.svg` (from `../fm1-x0x/docs/img/fm1-panel.svg`, Polly's labels)

**Acceptance Criteria:**
- [ ] Sections: Quick start · Getting around (the panel drawing, buttons table, keys by screen, the footer, help cards, lists) · Loops (L1–L8 factory, L9–L16 yours, writing step / live, skip, length, beats, name, LOAD PROGRESSION, copy) · Playing (FREE / LOOP / SONG, chords until the next change, sections, HOME + keys) · Songs (rows, taps, REPEAT, section default, fills, mutes, key, intro / ending / at end, PLAY from a row, REC) · Recording a part · Sound · Songs and saving · Setup and MIDI · The QY70 bank · Updating and safety · Credits · Page by page
- [ ] No mention left of CHORD mode, templates, the Chords / Arrange / Templates pages, SEL held, SAVE-held jobs
- [ ] Every image referenced exists; the panel drawing labels every control with its Polly meaning
- [ ] Credits unchanged (Felucca, Sonivox, Groove MIDI Dataset, Groove OS, qy100-toolkit, Pyodide, Barlow)

**Verify:** `grep -n -i -E "CHORD mode|template|Arrange page|SEL held|hold SAVE" MANUAL.md` → only "LOAD PROGRESSION" wording, no hits on removed features; `grep -o 'docs/img/manual/[a-z_0-9]*\.\(png\|svg\)' MANUAL.md | sort -u | while read f; do test -f "$f" || echo MISSING $f; done` → nothing

**Steps:**

- [ ] **Step 1:** Copy the screenshots from `build/scenarios/tour/` into `docs/img/manual/` (the names Task 11 set).
- [ ] **Step 2:** The panel drawing: copy X0X's SVG, change each label to Polly's (HOME: Home / Song; ARP: Loop;
SEQ: Pattern; EDIT: Track; LFO: Mixer; FX: Master FX; ENV: Style; GLO: Setup; SEL: List; SAVE: Save ·
hold + key: copy; SELECT: Page; ALGORITHM: Track; PRESETS: Next; OCT: Octave (Loop: cursor); keys: Chords / the
track; REC; PLAY).
- [ ] **Step 3:** Rewrite `MANUAL.md` in the existing voice (short sentences, tables, "Good to know" lists). The
Songs section opens with the walk (verse, chorus, the song, ~25 presses).
- [ ] **Step 4:** Run the Verify commands.
- [ ] **Step 5: Commit** `git commit -am "Manual: loops, songs of rows, X0X's UI; labelled panel drawing"`.

---

### Task 13: On the device (only when the user says to flash)

**Goal:** The new firmware runs on the FM-1: CPU with a song playing, a save while stopped, the loops kept
through a restart.

> **USER-ORDERED GATE — NON-SKIPPABLE.** This task was requested by the user in the current conversation. It MUST NOT be closed by walking around it, by declaring it "verified inline", or by substituting a cheaper check. Close only after every item in `acceptanceCriteria` has been re-validated independently, with output captured.

**Files:** none (build output only)

**Acceptance Criteria:**
- [ ] The user has said to flash (ask: "Polly with loops is built; flash it to the FM-1?")
- [ ] `python3 tools/fm1_install.py build/polly.fwsc --yes` ends with `done: the FM-1 runs FM-1_3…`
- [ ] The Panel (GLO ×2) shows the CPU with a 4-row song playing on a factory style; the user reads it out (or a screenshot)
- [ ] A loop written on the device survives a power cycle (the user confirms)

**Verify:** the installer's last line, and the user's report of the Panel and the loop after a restart

**Steps:**

- [ ] **Step 1:** Ask the user before flashing (memory: flash only on request).
- [ ] **Step 2:** Flash; report the installer's last line.
- [ ] **Step 3:** Ask the user to play the song and read the Panel; record the CPU figure in `docs/ux-pass.md`.
