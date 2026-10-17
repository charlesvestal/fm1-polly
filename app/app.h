/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly: the project (everything that is saved), the app's entry points and its half of the contract with fm1-base
 * (base/device/unity.c, base/host/plat_host.c). Included first by fm1-base's unity build and by its simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "dsp/gm.h"
#include "dsp/arr.h"

#define APP_NAME "Polly"
#define APP_TAGLINE "A pocket arranger"
#ifdef APP_VERSION                       /* a release build passes its own (base/tools/build.py --release) */
#define GM_VERSION APP_VERSION " BETA"
#else
#define GM_VERSION "DEV"
#endif

/* the store, where Polly 0.4 kept it (an update keeps the songs): the project at 0xFC000 (Felucca's settings pair),
 * the panel calibration at 0xDC000, the songs at 0x97000 and the 16 shared slots from 0x99000 to 0xB9000, two sectors
 * each. The QY70 style bank (dsp/qybank.h) has the rest of the data area and both of plat.h's windows: 0x93000..
 * 0x96FFF, 0xB9000..0xDBFFF, 0xDE000..0xDFFFF and 0xE5000..0xFBFFF */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_SONGS, OBJ_PAT0, OBJ_NOBJ = OBJ_PAT0 + 16 };   /* 16: ARR_USER_PATTERNS */
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0x97000u, 0x99000u, 0x9B000u, 0x9D000u, 0x9F000u, 0xA1000u, 0xA3000u, \
                       0xA5000u, 0xA7000u, 0xA9000u, 0xAB000u, 0xAD000u, 0xAF000u, 0xB1000u, 0xB3000u, 0xB5000u, 0xB7000u}

/* backup and restore over USB (fm1-base's backup.c; tools/fm1_backup.py): the project, the songs and the 16 slots, in
 * Polly 0.4's image ("PLYB", version 2: its backups restore) */
#define APP_BACKUP_OBJS {OBJ_PROJ, OBJ_SONGS, OBJ_PAT0, OBJ_PAT0 + 1, OBJ_PAT0 + 2, OBJ_PAT0 + 3, OBJ_PAT0 + 4, \
                         OBJ_PAT0 + 5, OBJ_PAT0 + 6, OBJ_PAT0 + 7, OBJ_PAT0 + 8, OBJ_PAT0 + 9, OBJ_PAT0 + 10, \
                         OBJ_PAT0 + 11, OBJ_PAT0 + 12, OBJ_PAT0 + 13, OBJ_PAT0 + 14, OBJ_PAT0 + 15}
#define APP_BACKUP_MAGIC 0x42594C50u     /* "PLYB" */
#define APP_BACKUP_VERSION 2u            /* 2: the songs hold their progressions (format 10); a version 1 backup (theirs
                                          * in the slots) is refused */
#define APP_BACKUP_WHAT "replaces all songs"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

/* the contract: the platform calls these (app.c) */
void app_init(void);                     /* boot: the synth, the arranger, then the saved project (or the defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
#define APP_USB_CMD                      /* the QY70 bank from the host (bank_rx.c, cmd 0x40) */
void app_usb_cmd(const uint8_t *d, uint32_t n);
#define APP_REDRAW                       /* every band drawn again (the mirror lost a part) */
void app_redraw(void);
#define APP_MIRROR_TEST                  /* a test session from the mirror's remote (tools/polly_remote.py --fresh) */
int app_mirror_test(int on);

/* the host simulator's part (host.c): Polly's checks, its commands, the screen reader's dump */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
#define APP_HOST_REBOOT
void app_host_reboot(void);              /* what RAM forgets at a restart */
#define APP_HOST_FAILS
int app_host_fails(void);                /* the messages too wide for the header (ui.c say) */
#define APP_HOST_MS
void app_host_ms(void);                  /* each simulated millisecond: the tracks' note-on counts */
#define APP_WEB                          /* the browser page's own exports (web.c: the QY70 bank) */

#define PROJ_MAGIC 0x314D4750u           /* "PGM1" */
#define PROJ_FORMAT 5u                   /* 5: 480 ticks a beat (patterns and songs of older formats are reset) */
#define SONGS_MAGIC 0x314E4753u          /* "SGN1" */
#define SONGS_FORMAT 10u                 /* 10: the songs' loops packed in it (arr.h songs_t.prog); 9: a lead's velocity;
                                         * 8: no loops (each song's in a slot); older: reset (Polly is prerelease) */

typedef struct {
    uint32_t magic, format;
    uint8_t ch;                          /* the channel the keys play, 0..15 */
    int8_t oct;                          /* the keys' octave, -3..3 */
    uint8_t leds;                        /* lights: 0 off, 1 notes and the buttons' glow, 2 notes only */
    uint8_t rsv0;
    uint8_t program[GM_NCH], volume[GM_NCH], pan[GM_NCH], reverb[GM_NCH], chorus[GM_NCH];
    uint8_t song, pattern;                /* the selected song (0..ARR_SONGS-1) and user pattern (0..15) */
    uint8_t trk;                          /* the selected track (0..7): the keys play it, REC records into it */
    uint8_t rsv_tempo;                     /* unused: a song's tempo plays now (songs_t); kept for the layout */
    uint8_t rsv_key;                       /* unused: a song's transpose plays now (songs_t); kept for the layout */
    uint8_t quant;                        /* the recording grid, ARR_Q_* */
    uint8_t rev_type, rev_level;          /* the master reverb: 0 off, 1..4 (large hall, hall, chamber, room); 0..127 */
    uint8_t cho_type, cho_level;          /* the master chorus: preset 0..3; 0..127 (0 off) */
    uint8_t clock;                        /* 0 internal (MIDI clock in ignored), 1 MIDI (followed when it comes) */
    uint8_t mout;                         /* MIDI out: the tracks on their channels, the clock */
    uint8_t click;                       /* the click: ARR_CLICK_OFF, REC (while recording or counting in), ON */
    uint8_t usb_full;                     /* USB audio: 0 follows the MASTER knob, 1 full level (the knob for the speaker
                                          * and headphones only); older projects: 0 */
    uint8_t rsv[2];
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every channel's program and controllers to the synth */

int project_pattern_load(int i, pattern_t *p);         /* user pattern i, 0 = loaded, else empty */
int project_pattern_save(int i, const pattern_t *p);
int project_songs_load(songs_t *s);
int project_songs_save(const songs_t *s);
/* the 16 shared slots (OBJ_PAT0 + i): each a user pattern (a bare pattern_t, as stored before), a lead part (arr.h
 * lead_part_t, LEAD_MAGIC first), a song's loops when they do not fit the songs' pool (arr.h loops_part_t, the same
 * header, LOOPS_KIND: rare), or nothing */
enum { SLOT_NONE, SLOT_PATTERN, SLOT_LEAD, SLOT_OTHER, SLOT_LOOPS };   /* OTHER: unreadable as any (an old format): free */
typedef struct {
    uint8_t kind, song, part;            /* a lead part's song and part; a song's loops' song */
    uint16_t n;                          /* a pattern's notes (nev), a lead part's; a song's loops: ARR_LOOPS (0: free) */
} slot_info_t;
int project_slot_info(int i, slot_info_t *si, pattern_t *buf);   /* buf: a pattern looked at (its notes counted) */
int project_slot_blank(int i, pattern_t *buf);    /* nothing, a pattern or lead part with no notes, freed loops, OTHER */
/* the first slot none of the songs plays as a pattern, not in `reserved` (bit i: slot i, the leads' own in RAM), and
 * blank; -1 none. The user patterns' copy-on-write and the lead parts both take slots here */
int project_slot_free(const songs_t *s, uint32_t reserved, pattern_t *buf);
/* song n's lead notes from the slots whose header says so, into l (its parts; l->trk untouched); slot[k]: part k's
 * slot, -1 none. Returns 1 when they were put right (leads.c leads_fix: out of order, bad notes, part 0 not full
 * before part 1): saved again from it, -1 for none at all, 0 as stored. The caller holds the engine off (arr_edit) */
int project_leads_load(int song, leads_t *l, int8_t *slot);
/* parts `dirty` (bit k: part k) of song n saved: part k with notes into slot[k] (the caller took it: project_slot_free,
 * when the part was first needed); a part with none into its slot as an empty part (the slot free again), its slot[k]
 * -1. 0 = saved; -1 a flash error; -2 a part with notes and no slot */
int project_leads_save(int song, leads_t *l, int8_t *slot, unsigned dirty);
int project_leads_free(int song);                 /* every slot holding a part of song n: free (CLEAR / COPY SONG) */
/* a slot a pattern must not be saved into: it holds a lead part with notes, or a song's loops */
int project_slot_held(int i);
/* a song's loops (each song has its own 16): packed in the songs object (arr.h songs_t.prog, the defaults: nothing),
 * or, when they do not fit its pool, whole in a slot (plen ARR_PLEN_SLOT).
 * project_loops_slot: the slot holding song n's, -1 none.
 * load: song n's from s (and its slot) into p (the defaults when none is stored, or damaged; -1 then, 0 loaded, put
 *   in range), *slot their slot (-1: packed, or none).
 * fit: 1 when t packed fits s's pool beside the other songs' (no slot needed).
 * store: p into s: packed (0; the caller saves the songs, then project_loops_tidy lets a slot they had go), else
 *   into *slot (the caller took it: project_slot_free; written now, before the songs that say so: 1), -2 no slot,
 *   -1 a flash error.
 * free: every slot holding song n's let go. tidy: every loops' slot its song no longer says it is in (s: as saved)
 *   let go (after a save that packed them; at start, after a power cut between the two writes).
 * The caller holds the engine off (arr_edit) while p is the engine's table */
int project_loops_slot(int song);
int project_loops_load(const songs_t *s, int song, loops_part_t *p, int8_t *slot);
int project_loops_fit(const songs_t *s, int song, const loop_t *t);
int project_loops_store(songs_t *s, int song, loops_part_t *p, int8_t *slot);
int project_loops_free(int song);
int project_loops_tidy(const songs_t *s);
int project_factory_reset(songs_t *s, pattern_t *buf);   /* Setup's FACTORY RESET: project, songs (and their
                                                          * progressions), the 16 places */
int project_slot_clear(int i);                   /* a lead part's or a song's loops' slot let go (its header, n 0) */
void pattern_empty(pattern_t *p);
void song_default(song_t *g, int n);                   /* song n, empty: no bars, the first factory style */
int song_style_valid(int st);                          /* a pattern number a song may hold */
int song_style_base(int st);                           /* the style pattern st is a section of: its Intro's number; -1 */
void song_style_derive(song_t *g);                     /* g->sty from its sections (Main A's first), else kept */

void app_start(void);                  /* at boot, after project_load: the songs and patterns into the arranger */
void app_bank_changed(void);           /* the QY70 style bank was written: mounted again, the song reloaded */
void app_restored(void);               /* a restore over USB was written: everything from the flash again */
void app_flush(void);                  /* stopped: what is not saved, saved (a backup over USB starts) */
void app_hold_saves(int on);           /* 1: no autosave (a backup over USB streams), 0: as ever */
int app_test(int on);                  /* a test session (blank, RAM only) on / off: 1 done, 0 refused */
int app_testing(void);                 /* 1: in a test session */
void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);
