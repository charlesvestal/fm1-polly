/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Polly's Bluetooth: audio out to headphones and speakers (fm1-base's plat_bta_*), and whether Bluetooth MIDI is on.
 * Adapted from LOOM-8's app/bt.c (fm1-loom, MPL-2.0), without its MIDI devices. Setup's SEL list has BT AUDIO (on or
 * off), AUDIO DEVICE (another list: the devices remembered, then the new ones a look finds; SEL connects one, LOOK
 * AGAIN, FORGET) and BT MIDI (on or off); ui.c's bt_poll looks while that list is up and says what happened. Both
 * settings are the FM-1's, kept in the project's proj.bt (app.h BT_*) whichever project is loaded: at power-on Polly
 * connects the last device again when audio was on, and switches Bluetooth MIDI off again once the radio is up when
 * it was off. Included by pages.c, after its string helpers. */

#define BT_ROWS 8                                    /* the devices the list shows (remembered first) */

static struct {
    uint8_t want;                                    /* Bluetooth audio on, as the player set it */
    uint8_t midi;                                    /* Bluetooth MIDI on, as the player set it */
    uint8_t read;                                    /* the settings read from the project (once, at power-on) */
    uint8_t boot;                                    /* power-on: the last device connected once the radio is up */
    uint8_t cur;                                     /* the remembered device asked last (its row shows the state) */
    uint8_t was;                                     /* the audio's state last frame (what changed is said) */
    uint8_t list;                                    /* the device list up last frame */
} bt;
static char bt_nm[BT_ROWS][17];                      /* the list's devices' names (its rows point here) */
static uint8_t bt_ix[BT_ROWS];                       /* each one's index: found, or 0x80 | remembered */

#if FM1B_BT_AUDIO
#define BTA(f) plat_bta_##f
#else
/* no Bluetooth audio in this build (the simulator, the browser): a stand-in. The simulator finds "Speaker" half a
 * second into a scan (10 s long) and streams to it 0.3 s after a connect; the browser finds nothing */
#define BTA(f) sim_bta_##f
static struct { uint8_t on, st, found, nk; uint32_t t; char k[4][17]; } sb;
static void sim_name(char *d, const char *s)
{
    memset(d, 0, 17);
    cat_s(d, s);
}
static void sim_bta_on(int on)
{
    if (!on == !sb.on)
        return;
    sb.on = (uint8_t)(on != 0);
    sb.st = on ? PLAT_BTA_NONE : PLAT_BTA_OFF;
}
static void sim_bta_scan(int on)
{
    if (!sb.on)
        return;
    if (on) {
        sb.st = PLAT_BTA_SCANNING;
        sb.found = 0;
        sb.t = plat_ms();
    } else if (sb.st == PLAT_BTA_SCANNING)
        sb.st = PLAT_BTA_NONE;
}
static int sim_bta_state(void)
{
    uint32_t dt = plat_ms() - sb.t;
#ifndef PLAT_WEB
    if (sb.st == PLAT_BTA_SCANNING && dt >= 500u)
        sb.found = 1;
#endif
    if (sb.st == PLAT_BTA_SCANNING && dt >= 10000u)
        sb.st = PLAT_BTA_NONE;
    if (sb.st == PLAT_BTA_PAGING && dt >= 300u)
        sb.st = PLAT_BTA_STREAMING;
    return sb.st;
}
static int sim_bta_found(int i, char *name)
{
    (void)sim_bta_state();
    if (i != 0 || !sb.found)
        return 0;
    if (name)
        sim_name(name, "Speaker");
    return 1;
}
static int sim_bta_known(int i, char *name)
{
    if (i < 0 || i >= sb.nk)
        return 0;
    if (name)
        sim_name(name, sb.k[i]);
    return 1;
}
static void sim_bta_pick(int i)
{
    if (!sb.on || !sim_bta_found(i, 0))
        return;
    if (!sb.nk || !same(sb.k[0], "Speaker")) {
        for (int j = sb.nk < 4 ? sb.nk++ : 3; j > 0; j--)
            memcpy(sb.k[j], sb.k[j - 1], sizeof sb.k[0]);
        sim_name(sb.k[0], "Speaker");
    }
    sb.st = PLAT_BTA_PAGING;
    sb.t = plat_ms();
}
static int sim_bta_reconnect(int i)
{
    if (!sb.on || i < 0 || i >= sb.nk)
        return -1;
    sb.st = PLAT_BTA_PAGING;
    sb.t = plat_ms();
    return 0;
}
static void sim_bta_forget(int i)
{
    if (i < 0 || i >= sb.nk)
        return;
    for (sb.nk--; i < sb.nk; i++)
        memcpy(sb.k[i], sb.k[i + 1], sizeof sb.k[0]);
}
static void sim_bta_hangup(void)
{
    if (sb.on)
        sb.st = PLAT_BTA_NONE;
}
#endif

static int bt_linking(int st) { return st >= PLAT_BTA_PAGING && st <= PLAT_BTA_SETUP; }
/* the audio's state in a word: a row's value, a screen reader's too */
static const char *bt_state_word(int st)
{
    return st == PLAT_BTA_STREAMING ? "playing" : bt_linking(st) ? "connecting" : st == PLAT_BTA_REFUSED ? "refused"
         : st == PLAT_BTA_SCANNING ? "looking" : st == PLAT_BTA_OFF ? "off" : "not connected";
}
/* AUDIO DEVICE's value: the device playing, connecting, or "choose" (SEL opens the list) */
static void bt_adev_text(char *t)
{
    int st = BTA(state)();
    *t = 0;
    if (st == PLAT_BTA_STREAMING && BTA(known)(bt.cur, t))
        return;
    cat_s(t, bt_linking(st) || (bt.want && bt.boot) ? "connecting" : "choose");
}
static uint8_t bt_bits(void) { return (uint8_t)((bt.want ? BT_AUDIO_ON : 0u) | (bt.midi ? 0u : BT_MIDI_OFF)); }
