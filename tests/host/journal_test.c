/* SPDX-License-Identifier: GPL-3.0-only */
/* The undo journal (firmware/src/app/journal.c): steps over several regions, undo and redo by swapping, a step's
 * edits grouped (one commit), the ring dropping the oldest (by count and by size), redo gone after a new edit, an
 * entry too big for the pool, a region swapped through a copy (as ui.c does a pattern's). */
#include <stdio.h>
#include <string.h>
#define UJ_POOL 4096u
#include "../../firmware/src/app/journal.c"

static int fails;
#define CHECK(c)                                                                                                      \
    do {                                                                                                              \
        if (!(c)) {                                                                                                   \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                        \
            fails++;                                                                                                  \
        }                                                                                                             \
    } while (0)

static uj_t uj;
static uint8_t a[600], a_sh[600], b[5000], b_sh[5000];

static void start(void)
{
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    uj_region(&uj, 0, a, a_sh, sizeof a);
    uj_region(&uj, 1, b, b_sh, sizeof b);
    uj_region(&uj, 2, 0, 0, 0);
    uj_region(&uj, 3, 0, 0, 0);
    for (int r = 0; r < UJ_REGIONS; r++)
        uj_resync(&uj, r);
    uj_clear(&uj);
}
/* undo / redo the way ui.c does: every region of the entry, then the place moves */
static int step(int redo)
{
    int k = redo ? uj_redo_at(&uj) : uj_undo_at(&uj);
    if (k < 0)
        return 0;
    uj_apply(&uj, k, 0, a, a_sh);
    uj_apply(&uj, k, 1, b, b_sh);
    uj_moved(&uj, redo);
    return 1;
}

int main(void)
{
    char t[UJ_LABEL + 1];
    int rg, ax;
    uint32_t off, len;

    /* ---- nothing changed: no step; one edit, one step; undo, redo */
    start();
    CHECK(uj_commit(&uj, "X") == 0);
    CHECK(uj.n == 0 && uj_undo_at(&uj) == -1 && uj_redo_at(&uj) == -1);
    a[10] = 7;
    CHECK(uj_commit(&uj, "L9 CHORD") == 1);
    CHECK(uj.n == 1 && uj.cur == 1);
    uj_label_of(&uj, 0, t);
    CHECK(!strcmp(t, "L9 CHORD"));
    CHECK(uj_mask(&uj, 0) == 1);
    CHECK(uj_run(&uj, 0, 0, &rg, &ax, &off, &len) && rg == 0 && off == 10 && len == 1);
    CHECK(!uj_run(&uj, 0, 1, &rg, &ax, &off, &len));
    CHECK(step(0) && a[10] == 0 && a_sh[10] == 0);
    CHECK(!step(0));                                   /* nothing more to undo */
    CHECK(step(1) && a[10] == 7 && a_sh[10] == 7);
    CHECK(!step(1));                                   /* nothing more to redo */
    CHECK(uj_commit(&uj, 0) == 0);                     /* undo / redo leave no change behind */

    /* ---- a step: everything changed between two commits, in both regions, is one; undone at once */
    start();
    a[1] = 1;
    a[2] = 2;                                          /* (the gap: one run) */
    a[100] = 3;                                        /* (far: another) */
    b[2000] = 4;
    CHECK(uj_commit(&uj, "") == 1);
    CHECK(uj.n == 1 && uj_mask(&uj, 0) == 3);
    CHECK(uj_run(&uj, 0, 0, &rg, &ax, &off, &len) && rg == 0 && off == 1 && len == 2);
    CHECK(uj_run(&uj, 0, 1, &rg, &ax, &off, &len) && rg == 0 && off == 100 && len == 1);
    CHECK(uj_run(&uj, 0, 2, &rg, &ax, &off, &len) && rg == 1 && off == 2000 && len == 1);
    uj_label_of(&uj, 0, t);
    CHECK(t[0] == 0);
    a[50] = 9;                                         /* the next step */
    CHECK(uj_commit(&uj, "TWO") == 1);
    CHECK(step(0) && a[50] == 0 && a[1] == 1);
    CHECK(step(0) && a[1] == 0 && a[2] == 0 && a[100] == 0 && b[2000] == 0);
    CHECK(step(1) && a[1] == 1 && a[2] == 2 && a[100] == 3 && b[2000] == 4 && a[50] == 0);
    CHECK(step(1) && a[50] == 9);

    /* ---- a new edit after an undo: the redo is gone */
    start();
    a[0] = 1;
    uj_commit(&uj, "A");
    a[0] = 2;
    uj_commit(&uj, "B");
    CHECK(step(0) && a[0] == 1);
    a[5] = 5;
    CHECK(uj_commit(&uj, "C") == 1);
    CHECK(uj.n == 2 && uj.cur == 2 && uj_redo_at(&uj) == -1);
    CHECK(step(0) && a[5] == 0 && a[0] == 1);
    CHECK(step(0) && a[0] == 0);
    CHECK(!step(0));

    /* ---- the ring: 40 small steps, the newest UJ_MAX kept */
    start();
    for (int i = 0; i < 40; i++) {
        a[i] = (uint8_t)(i + 1);
        uj_commit(&uj, 0);
    }
    CHECK(uj.n == UJ_MAX && uj.cur == UJ_MAX);
    {
        int undone = 0;
        while (step(0))
            undone++;
        CHECK(undone == UJ_MAX);
        for (int i = 0; i < 40; i++)                   /* the 8 oldest stay done */
            CHECK(a[i] == (i < 8 ? i + 1 : 0));
        while (step(1))
            ;
        for (int i = 0; i < 40; i++)
            CHECK(a[i] == i + 1);
    }

    /* ---- big steps: fewer kept (the pool) */
    start();
    for (int i = 0; i < 5; i++) {
        memset(b, i + 1, 1000);                        /* ~1 KB a step: 4 fit in 4 KB with the small ones */
        uj_commit(&uj, 0);
    }
    CHECK(uj.n < 5 && uj.n >= 3 && uj.used <= UJ_POOL);
    {
        int kept = uj.n;
        for (int i = 0; i < kept; i++)
            CHECK(step(0));
        CHECK(b[0] == 5 - kept);                       /* back to before the oldest kept */
    }
    /* a small step after big ones: they make room */
    start();
    memset(b, 1, 2100);
    uj_commit(&uj, 0);
    memset(b, 2, 2100);
    CHECK(uj_commit(&uj, 0) == 1 && uj.n == 1);        /* 2.1 KB + 2.1 KB do not fit 4 KB: the first went */
    CHECK(step(0) && b[0] == 1 && !step(0));
    CHECK(step(1) && b[0] == 2);

    /* ---- one bigger than the pool: the history emptied, that state the start */
    start();
    a[0] = 1;
    uj_commit(&uj, 0);
    memset(b, 7, sizeof b);
    memset(a, 7, sizeof a);
    CHECK(uj_commit(&uj, 0) == -1);
    CHECK(uj.n == 0 && !step(0));
    CHECK(uj_commit(&uj, 0) == 0);                     /* (the shadows are where it is) */

    /* ---- a region through a copy (a pattern: copied out, swapped, stored back), aux kept with its runs; swapping
     * twice is no change (a store refused) */
    {
        static uint8_t eng[200], cp[200], sh[200];
        start();
        uj_region(&uj, 3, cp, sh, sizeof cp);
        uj.r[3].aux = 4;
        memcpy(cp, eng, sizeof eng);
        uj_resync(&uj, 3);
        eng[30] = 5;                                    /* the engine's changed */
        memcpy(cp, eng, sizeof eng);                    /* the commit's copy */
        a[3] = 1;
        CHECK(uj_commit(&uj, "TAKE C1") == 1);
        CHECK(uj_mask(&uj, 0) == 9 && uj_aux(&uj, 0, 3) == 4 && uj_aux(&uj, 0, 2) == -1);
        uj.r[3].live = 0;                               /* (live only at a commit) */
        memcpy(cp, eng, sizeof eng);
        CHECK(uj_apply(&uj, 0, 3, cp, sh) == 1 && cp[30] == 0);
        CHECK(uj_apply(&uj, 0, 3, cp, sh) == 1 && cp[30] == 5);   /* refused: back */
        CHECK(uj_apply(&uj, 0, 3, cp, sh) == 1 && cp[30] == 0);
        memcpy(eng, cp, sizeof eng);                    /* stored */
        uj_apply(&uj, 0, 0, a, a_sh);
        uj_moved(&uj, 0);
        CHECK(eng[30] == 0 && a[3] == 0);
        memcpy(cp, eng, sizeof eng);
        uj_apply(&uj, 0, 3, cp, 0);                     /* (no shadow: another pattern is the one followed) */
        memcpy(eng, cp, sizeof eng);
        uj_apply(&uj, 0, 0, a, a_sh);
        uj_moved(&uj, 1);
        CHECK(eng[30] == 5 && a[3] == 1);
    }

    if (fails)
        printf("journal: %d FAILED\n", fails);
    else
        printf("journal: ok\n");
    return fails != 0;
}
