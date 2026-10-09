/* SPDX-License-Identifier: GPL-3.0-only */
/* The GM synth (dsp/gm.c and its EAS): ten-note chords at velocity 127 never reach EAS's 16-bit rail
 * and the output never passes full scale (gain staging); a released note fades out, its reverb and
 * chorus tail too, with no sudden drop (EAS stopped the effects with the last voice: a hard edge). */
#include <stdio.h>
#include <stdlib.h>
#include "dsp/gm.c"
/* the arranger's MIDI out (gm.c links it): nowhere here */
void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
uint32_t arr_midi_out_room(void) { return 1000; }
uint32_t arr_midi_out_link(void) { return 0; }

static int fails;
static void chord(int prog, int notes)
{
    static const int key[] = {48, 52, 55, 60, 64, 67, 72, 76, 79, 84};
    static int32_t out[512];
    int rail = 0, over = 0, i, b;
    gm_midi(0xC0, prog, 0);
    for (i = 0; i < notes; i++)
        gm_midi(0x90, key[i], 127);
    for (b = 0; b < 44100 * 3 / 2 / 256; b++) {
        gm_render(out, 256, 4096);
        for (i = 0; i < EAS_FRAMES * 2; i++)
            rail += pcm[i] >= 32767 || pcm[i] <= -32768;
        for (i = 0; i < 512; i++)
            over += out[i] > FS24 || out[i] < -FS24;
    }
    gm_midi(0xB0, 120, 0);
    for (b = 0; b < 50; b++)
        gm_render(out, 256, 4096);
    if (rail || over) {
        printf("program %d, %d notes: %d samples at EAS's rail, %d past full scale\n", prog, notes, rail, over);
        fails++;
    }
}
/* a note held, released: from then on no 6 ms block may fall more than 8 dB below the one before
 * while still above -70 dBFS (a cut, not a decay; a release falls ~2 dB a block) */
static void release(int prog)
{
    static int32_t out[512];
    float last = 0;
    int b, i;
    gm_midi(0xC0, prog, 0);
    gm_midi(0x90, 60, 100);
    for (b = 0; b < 60; b++)
        gm_render(out, 256, 4096);
    gm_midi(0x80, 60, 0);
    for (b = 0; b < 1000; b++) {
        float m = 0;
        gm_render(out, 256, 4096);
        for (i = 0; i < 512; i++)
            if ((float)abs(out[i]) > m)
                m = (float)abs(out[i]);
        if (last > FS24 * 3.2e-4f && m < last / 2.5f) {
            printf("program %d: cut %.0f -> %.0f, %d ms after the note-off\n", prog, last, m, b * 256 * 1000 / 44100);
            fails++;
            break;
        }
        last = m;
    }
}
int main(void)
{
    static const int progs[] = {0, 4, 16, 19, 30, 48, 61, 81, 88};
    static const int ringing[] = {0, 4, 5, 11, 46, 88};   /* their notes ring on (organs stop at once) */
    if (gm_init()) {
        printf("gm_init failed\n");
        return 1;
    }
    for (unsigned p = 0; p < sizeof progs / sizeof progs[0]; p++)
        chord(progs[p], 10);
    for (unsigned p = 0; p < sizeof ringing / sizeof ringing[0]; p++)
        release(ringing[p]);
    printf("%s\n", fails ? "FAIL" : "ok: no clipping, no cut releases");
    return fails != 0;
}
