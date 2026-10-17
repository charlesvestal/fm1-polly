/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly in fm1-base's host simulator (base/host/plat_host.c, which includes app.c and so this): Polly's checks for
 * "expect", its commands, a reboot, its counters and the screen reader's dump. fm1-base's own commands and checks are
 * at the top of plat_host.c (wait, press, key, turn, spin, master, midi, clock, wav, shot, fbdump, leds, reboot,
 * listen, said, said_none, mirror, backup, restore, flash_fail; store_writes, store_tries, restore_rc, peak_db_max ...).
 *
 * Polly's commands:
 *   bank FILE                    a QY70 style bank image (tools/qy70_codec.py --image) written into the flash, as
 *                                tools/qy70_install.py writes it on the device; mounted, the song reloaded.
 *                                FILE "out:NAME": NAME in OUTDIR
 *   bank_damage N                a byte of the bank's style N changed in the flash
 *   style NAME                   the song's six sections from the style named NAME ("_" for a space; "qy:NAME": the
 *                                QY70 bank's only), as the Style knob sets them
 *   hitreset                     restart the tracks' note-on counts (expect track_hitsN), notes_out and heard_chN
 *   expect loop_nameN TEXT       progression N's name, exactly
 *   poke_*                       host-only debug: objects written straight into storage (see each) */
#include <strings.h>                                /* strncasecmp */
static void run_ms(uint32_t ms);                    /* (plat_host.c, below the app) */
static int find_name(const char *const *t, int n, const char *s);
static const char *const BTN_N[NB];
static uint32_t trk_hits[10], trk_last[10];         /* note-ons seen per track since hitreset (arr_track_hit moving;
                                                     * 10: ARR_ALLTRK) */
static int host_fails;                              /* a command's failures (a missing file, an unknown style) */

/* host-only debug buffers for poke_pattern and the pat0_ / song0_ expects (Step 4): static, as
 * pattern_t and songs_t are too big for a stack frame. */
static pattern_t dbg_pat;
static songs_t dbg_songs;
static loops_part_t dbg_loops;

int app_expect(const char *what, int *got_out)
{
    int got;
    if (!strcmp(what, "voices"))                  /* the synth's voices sounding */
        got = gm_voices;
    else if (!strcmp(what, "clicks_out"))         /* the click's note-ons out (channel 16) since hitreset */
        got = (int)click_count;
    else if (!strcmp(what, "click"))              /* Setup's CLICK: 0 off, 1 rec, 2 on */
        got = proj.click;
    else if (!strcmp(what, "notes_out"))          /* note-ons out (MIDI out on: every note played) since hitreset */
        got = (int)note_on_count;
    else if (!strcmp(what, "level"))              /* the output's peak, percent */
        got = (int)(gm_level * 100.0f);
    else if (!strcmp(what, "ch"))
        got = proj.ch + 1;
    else if (!strncmp(what, "program", 7))        /* programN: channel N's program (1..128) */
        got = proj.program[(atoi(what + 7) - 1) & 15] + 1;
    else if (!strcmp(what, "dirty"))              /* anything not saved */
        got = unsaved();
    else if (!strcmp(what, "usb_full"))           /* Setup's USB AUDIO: 1 full */
        got = proj.usb_full;
    else if (!strncmp(what, "heard_ch", 8)) {     /* heard_chN: the last note-on into the synth on channel N (1..16), -1 none */
        extern int16_t gm_host_heard[16];
        got = gm_host_heard[(atoi(what + 8) - 1) & 15];
    } else if (!strncmp(what, "ons_ch", 6)) {     /* ons_chN: note-ons into the synth on channel N (1..16) since hitreset */
        extern uint32_t gm_host_ons[16];
        got = (int)gm_host_ons[(atoi(what + 6) - 1) & 15];
    } else if (!strcmp(what, "chord_root"))       /* the chord now: root 0..11 (C = 0), type CH_* */
        got = (int)(arr_chord_now & 0xFFu);
    else if (!strcmp(what, "chord_type"))
        got = (int)(arr_chord_now >> 8 & 0xFFu);
    else if (!strcmp(what, "sect"))               /* SEC_INTRO 1, SEC_A 2, SEC_B 3, Fill AB 4, Fill BA 5, SEC_END 6 */
        got = arr_sect;
    else if (!strcmp(what, "next_sect"))          /* the section waiting for the bar, 0 none (7: Fill, AB or BA) */
        got = arr_next_sect;
    else if (!strncmp(what, "msg2_is_", 8)) {     /* msg2_is_WORDS: its second part starts with WORDS ("_": a space) */
        char w[24];
        int i;
        for (i = 0; i < 23 && what[8 + i]; i++)
            w[i] = what[8 + i] == '_' ? ' ' : what[8 + i];
        w[i] = 0;
        got = plat_ms() < ui.msg_until && !strncasecmp(ui.msg[1], w, strlen(w));   /* (messages are upper case) */
    } else if (!strncmp(what, "songtext_is_", 12)) {   /* songtext_is_WORDS: HOME's header style text ("_": a space) */
        char w[32], t[32];
        int i;
        for (i = 0; i < 31 && what[12 + i]; i++)
            w[i] = what[12 + i] == '_' ? ' ' : what[12 + i];
        w[i] = 0;
        song_style_text(t);
        got = !strcmp(t, w);
        if (!got)
            printf("  (song text: \"%s\")\n", t);
    } else if (!strncmp(what, "next_is_", 8)) {   /* next_is_WORDS: HOME's "next: ..." in a song, briefest ("_": a space,
                                                  * "|": its parts' divider) */
        char w[64], t[96];
        int i;
        for (i = 0; i < 63 && what[8 + i]; i++)
            w[i] = what[8 + i] == '_' ? ' ' : what[8 + i];
        w[i] = 0;
        song_next(t, arr_playing, 2);
        got = !strcmp(t, w);
        if (!got)
            printf("  (next: \"%s\")\n", t);
    } else if (!strncmp(what, "tag_is_", 7) || !strncmp(what, "footer_is_", 10)) {
        /* tag_is_WORDS / footer_is_WORDS: the tag over the knobs, the footer, exactly ("_": a space, "|": a dot, ">": the
         * breadcrumb's chevron) */
        char w[96], t[96];
        int i, f = what[0] == 'f', o = f ? 10 : 7;
        for (i = 0; i < 95 && what[o + i]; i++)
            w[i] = what[o + i] == '_' ? ' ' : what[o + i];
        w[i] = 0;
        if (f)
            page_footer(t);
        else
            page_tag(t);
        got = !strcmp(t, w);
        if (!got)
            printf("  (%s: \"%s\")\n", f ? "footer" : "tag", t);
    } else if (!strncmp(what, "where_is_", 9)) {  /* where_is_WORDS: a melody's readout, exactly ("_": a space, "|": the
                                                  * dot): "song_bar_9|Chorus_bar_1" */
        char w[64], t[64];
        int i;
        for (i = 0; i < 63 && what[9 + i]; i++)
            w[i] = what[9 + i] == '_' ? ' ' : what[9 + i];
        w[i] = 0;
        lead_where(t);
        got = !strcmp(t, w);
        if (!got)
            printf("  (where: \"%s\")\n", t);
    } else if (!strncmp(what, "notes_where_is_", 15)) {   /* notes_where_is_WORDS: Notes' readout, exactly ("_": a space,
                                                  * "|": the dot): "M1|song_bar_5|Chorus_bar_1.1|E5" */
        char w[96], t[96];
        int i;
        for (i = 0; i < 95 && what[15 + i]; i++)
            w[i] = what[15 + i] == '_' ? ' ' : what[15 + i];
        w[i] = 0;
        ne_ctx();
        ne_where(t, 1);
        got = !strcmp(t, w);
        if (!got)
            printf("  (notes: \"%s\")\n", t);
    } else if (!strcmp(what, "ne_t"))             /* Notes' cursor: its place (song ticks / the groove's) */
        got = (ne_ctx(), (int)ne.t);
    else if (!strcmp(what, "ne_note"))            /* the note selected there: its pitch, -1 none */
        got = (ne_ctx(), ne.has ? ne.k.note : -1);
    else if (!strcmp(what, "ne_at"))              /* ... its place, -1 none */
        got = (ne_ctx(), ne.has ? (int)ne.k.t : -1);
    else if (!strncmp(what, "enc_is_", 7)) {   /* enc_is_WORDS: the encoders' line over the footer, exactly ("_": a
                                                  * space, "|": between the encoders; "enc_is_" alone: no line) */
        char w[96], t[96];
        int i;
        for (i = 0; i < 95 && what[7 + i]; i++)
            w[i] = what[7 + i] == '_' ? ' ' : what[7 + i];
        w[i] = 0;
        enc_line(t);
        got = !strcmp(t, w);
        if (!got)
            printf("  (encoders: \"%s\")\n", t);
    } else if (!strncmp(what, "teal_", 5)) {      /* teal_X_Y: the screen's pixel X, Y is K_TEAL (a mark drawn) */
        int x = atoi(what + 5), y = strchr(what + 5, '_') ? atoi(strchr(what + 5, '_') + 1) : 0;
        uint16_t p = fb[(y & 255) % 240 * 240 + (x & 255) % 240], t = K_TEAL;
        got = p == t || p == (uint16_t)(t >> 8 | t << 8);
    } else if (!strncmp(what, "coral_", 6)) {     /* coral_X_Y: the screen's pixel X, Y is K_ACCENT (a cursor, a flash) */
        int x = atoi(what + 6), y = strchr(what + 6, '_') ? atoi(strchr(what + 6, '_') + 1) : 0;
        uint16_t p = fb[(y & 255) % 240 * 240 + (x & 255) % 240], t = K_ACCENT;
        got = p == t || p == (uint16_t)(t >> 8 | t << 8);
    } else if (!strncmp(what, "knob", 4) && what[4] >= '1' && what[4] <= '4' && !strncmp(what + 5, "name_is_", 8)) {
        /* knobNname_is_WORDS: knob N's name on this page, exactly ("_": a space) */
        char w[32];
        knob_t k[4];
        int i;
        for (i = 0; i < 31 && what[13 + i]; i++)
            w[i] = what[13 + i] == '_' ? ' ' : what[13 + i];
        w[i] = 0;
        memset(k, 0, sizeof k);
        page_knobs(k);
        got = k[what[4] - '1'].name && !strcmp(k[what[4] - '1'].name, w);
        if (!got)
            printf("  (knob %c's name: \"%s\")\n", what[4], k[what[4] - '1'].name ? k[what[4] - '1'].name : "");
    } else if (!strncmp(what, "knob", 4) && what[4] >= '1' && what[4] <= '4' && !strncmp(what + 5, "_is_", 4)) {
        /* knobN_is_WORDS: knob N's value on this page, exactly ("_": a space) */
        char w[32];
        knob_t k[4];
        int i;
        for (i = 0; i < 31 && what[9 + i]; i++)
            w[i] = what[9 + i] == '_' ? ' ' : what[9 + i];
        w[i] = 0;
        memset(k, 0, sizeof k);
        page_knobs(k);
        got = !strcmp(k[what[4] - '1'].val, w);
        if (!got)
            printf("  (knob %c: \"%s\")\n", what[4], k[what[4] - '1'].val);
    } else if (!strncmp(what, "msgtext_is_", 11)) {   /* msgtext_is_WORDS: the message, both parts, exactly ("_": a space) */
        char w[64], t[64];
        int i;
        for (i = 0; i < 63 && what[11 + i]; i++)
            w[i] = what[11 + i] == '_' ? ' ' : what[11 + i];
        w[i] = 0;
        snprintf(t, sizeof t, "%s%s%s", ui.msg[0], ui.msg[1][0] ? " " : "", ui.msg[1]);
        got = plat_ms() < ui.msg_until && !strcmp(t, w);
        if (!got)
            printf("  (message: \"%s\")\n", t);
    } else if (!strcmp(what, "home_chord_root") || !strcmp(what, "home_chord_type")) {   /* the big chord HOME shows */
        chord_t c = arr_chord_unpack(home_chord());
        got = what[11] == 'r' ? c.root : c.type;
    } else if (!strcmp(what, "xpose_now"))        /* the transposition chords show at: the song's + the row playing's
                                                      * (stopped: row 1's, where PLAY starts) */
        got = xpose_now();
    else if (!strcmp(what, "home_shown"))       /* HOME held long enough: the loops and sections on screen */
        got = home_shown();
    else if (!strcmp(what, "setup_box"))          /* Setup's box: 1 the four knobs' jobs, 2 + K knob K's (0..3) words */
        got = setup_shown;
    else if (!strcmp(what, "play_led"))           /* PLAY's light: 0 off, 1 green, 2 red, 3 red blinking (a recording
                                                   * armed, stopped: PLAY starts it) */
        got = play_led;
    else if (!strncmp(what, "lit_", 4)) {         /* lit_BTN: that button's light (1 / 0), as last set */
        int b = find_name(BTN_N, NB, what + 4);
        got = b >= 0 && (lit_btn >> b & 1u);
    } else if (!strncmp(what, "msg_is_", 7))      /* msg_is_WORD: the message showing starts with WORD (1 / 0) */
        got = plat_ms() < ui.msg_until && !strncasecmp(ui.msg[0], what + 7, strlen(what + 7));
    else if (!strcmp(what, "undo_steps"))         /* the undo journal's steps kept (done and undone) */
        got = uj.n;
    else if (!strcmp(what, "undo_at"))            /* how many of them are done (undo takes back the last) */
        got = uj.cur;
    else if (!strcmp(what, "msg"))                /* a message showing */
        got = plat_ms() < ui.msg_until;
    else if (!strcmp(what, "msg_refusal"))        /* the message showing is a refusal (say_no's look) */
        got = plat_ms() < ui.msg_until && ui.msg_no;
    else if (!strcmp(what, "black_lit")) {        /* the black keys lit, bit i = black key i (b0..b10) */
        got = 0;
        for (int i = 0; i < 11; i++)
            got |= (int)(lit_keys >> BLACK_K[i] & 1u) << i;
    } else if (!strcmp(what, "black_dim")) {      /* the black keys lit dim, bit i = black key i (SONG: muted tracks) */
        got = 0;
        for (int i = 0; i < 11; i++)
            got |= (int)(dim_keys >> BLACK_K[i] & 1u) << i;
    } else if (!strcmp(what, "playing"))
        got = arr_playing;
    else if (!strcmp(what, "count_in"))           /* PLAY's count-in: its beats left (4 3 2 1), 0 none */
        got = count_beats();
    else if (!strcmp(what, "rec_wait"))           /* REC waiting for its bar line (or PLAY) */
        got = arr_rec_wait;
    else if (!strcmp(what, "rec_pass"))           /* recording a track: its pass, 1.. (from the bar it started on) */
        got = arr_rec_bars / (app.hdr[sect_slot(arr_sect)].bars ? app.hdr[sect_slot(arr_sect)].bars : 1) + 1;
    else if (!strcmp(what, "rec_shown"))          /* SEQ / EDIT show the track being recorded as bars */
        got = trk_rec_shown();
    else if (!strncmp(what, "xlo", 3) || !strncmp(what, "xhi", 3) || !strncmp(what, "xkey", 4)) {
        /* xloN / xhiN / xkeyN: the selected section's track N's range and high key (12 off) */
        const xtrack_t *x = &app.trk[sect_slot(arr_sect)][(atoi(what + (what[1] == 'k' ? 4 : 3)) - 1) & 7].x;
        got = what[1] == 'k' ? x->hikey : what[1] == 'l' ? x->lo : x->hi;
    } else if (!strncmp(what, "type", 4))           /* typeN: the selected section's track N's type, XT_* (1 Parallel) */
        got = app.trk[sect_slot(arr_sect)][(atoi(what + 4) - 1) & 7].x.type;
    else if (!strcmp(what, "recording"))
        got = arr_recording;
    else if (!strcmp(what, "page"))               /* P_* (pages.c) */
        got = ui.page;
    else if (!strcmp(what, "tempo"))              /* the song's */
        got = csong()->tempo;
    else if (!strcmp(what, "mode"))               /* 0 FREE, 1 LOOP, 2 SONG */
        got = arr_mode;
    else if (!strcmp(what, "loop_now"))           /* 1..16, 0 none */
        got = arr_loop_now + 1;
    else if (!strcmp(what, "loop_beat"))
        got = arr_loop_beat;
    else if (!strcmp(what, "override"))           /* a chord from the keys holds until the source's next change */
        got = arr_override;
    else if (!strcmp(what, "rec_chords"))         /* REC writes chords (HOME, LOOP) */
        got = ui.rec_chords;
    else if (!strcmp(what, "take"))               /* REC on HOME in FREE: a new loop's take, 0 off, 1 waiting, 2 on */
        got = ui.take;
    else if (!strcmp(what, "cbeat"))              /* the chord cursor, a beat of the loop, 0.. */
        got = app.cbeat;
    else if (!strcmp(what, "app_loop"))           /* the loop the LOOP screen shows, 1..16 */
        got = app.loop + 1;
    else if (!strcmp(what, "row"))                /* the song's row playing, 1.. (0: a deleted one playing out) */
        got = arr_row == 0xFF ? 0 : arr_row + 1;
    else if (!strcmp(what, "row_next"))           /* the row the engine plays after it, 1.. (0: none, the song's end) */
        got = arr_row_next == 0xFF ? 0 : arr_row_next + 1;
    else if (!strcmp(what, "marks"))              /* Song's sections marked to go (PROG "-"), bit r row r + 1 */
        got = (int)ui.row_marks;
    else if (!strncmp(what, "ask_is_", 7)) {      /* ask_is_WORDS: the header's question, exactly ("_": a space) */
        char w[64];
        int i;
        for (i = 0; i < 63 && what[7 + i]; i++)
            w[i] = what[7 + i] == '_' ? ' ' : what[7 + i];
        w[i] = 0;
        got = ui.ask && !strcmp(ui.ask_q, w);
        if (!got)
            printf("  (ask: %d \"%s\")\n", ui.ask, ui.ask_q);
    } else if (!strcmp(what, "ask_font"))         /* the question's size in the header: 2 bold, 1 smaller, 0 cut down */
        got = !ui.ask ? -1 : text_w(&FONT_B, ui.ask_q) <= 224 ? 2 : text_w(&FONT_S, ui.ask_q) <= 228 ? 1 : 0;
    else if (!strcmp(what, "mutes"))              /* the engine's muted tracks now, bit t track t + 1 */
        got = arr_mutes;
    else if (!strncmp(what, "loop_bars", 9))      /* loop_barsN: loop N's bars (0: empty) */
        got = sloops.loop[(atoi(what + 9) - 1) & 15].bars;
    else if (!strncmp(what, "loop_beats", 10))    /* loop_beatsN: loop N's beats a bar */
        got = sloops.loop[(atoi(what + 10) - 1) & 15].beats;
    else if (!strncmp(what, "loop_ch", 7)) {      /* loop_chN_B: loop N's beat B, root*100 + type, -1 "." */
        int l = (atoi(what + 7) - 1) & 15, b = strrchr(what, '_') ? atoi(strrchr(what, '_') + 1) : 0;
        uint16_t v = sloops.loop[l].ch[b % ARR_LOOP_BEATS];
        got = v == LC_SAME ? -1 : lc_unpack(v).root * 100 + lc_unpack(v).type;
    } else if (!strncmp(what, "loop_name_i", 11))   /* loop_name_iN: loop N's name, the NAME list index (8: "Prog N" / custom) */
        got = loop_name_index(&sloops.loop[(atoi(what + 11) - 1) & 15]);
    else if (!strcmp(what, "pcur"))               /* Band's cursor: a beat of the groove shown, 0.. */
        got = pat_cur(sect_slot(arr_sect));
    else if (!strcmp(what, "flash_beats"))        /* flashing now (an edit landed): a progression's beats (bit b beat b, */
        got = flash_on() ? (int)(uint32_t)fw.beats : 0;   /* the first 32) */
    else if (!strcmp(what, "flash_rows"))         /* ... the song's sections, a groove's instruments, a melody's sections */
        got = flash_on() ? (int)fw.rows : 0;
    else if (!strcmp(what, "flash_bars"))         /* ... the selected instrument's bars, a melody's section's bars */
        got = flash_on() ? (int)fw.bars : 0;
    else if (!strcmp(what, "cstep"))              /* the cursor's step: 0 beat, 1 half, 2 bar */
        got = app.cstep;
    else if (!strcmp(what, "rows"))             /* the song's rows */
        got = csong()->nrows;
    else if (!strncmp(what, "row_loop", 8))       /* row_loopN: row N's loop, 1..16 */
        got = csong()->row[(atoi(what + 8) - 1) % ARR_ROWS].loop + 1;
    else if (!strncmp(what, "row_rep", 7))
        got = csong()->row[(atoi(what + 7) - 1) % ARR_ROWS].rep;
    else if (!strncmp(what, "row_sect", 8))       /* ROW_A 0, ROW_A_FILL 1, ROW_B 2, ROW_B_FILL 3, ROW_A_NOFILL 4, ROW_B_NOFILL 5 */
        got = csong()->row[(atoi(what + 8) - 1) % ARR_ROWS].sect;
    else if (!strncmp(what, "row_mute", 8))
        got = csong()->row[(atoi(what + 8) - 1) % ARR_ROWS].mute;
    else if (!strncmp(what, "row_key", 7))
        got = csong()->row[(atoi(what + 7) - 1) % ARR_ROWS].key;
    else if (!strcmp(what, "cursor"))             /* SONG's cursor row, 1.. (the END row: rows + 1) */
        got = app.row + 1;
    else if (!strcmp(what, "song_intro"))         /* the song's INTRO, ENDING, AT END (0 stop, 1 again) */
        got = csong()->intro;
    else if (!strcmp(what, "song_ending"))
        got = csong()->ending;
    else if (!strcmp(what, "song_at_end"))
        got = csong()->at_end;
    else if (!strcmp(what, "song_rec"))           /* REC on SONG: rows written from HOME + keys */
        got = ui.song_rec;
    else if (!strcmp(what, "row_follow"))         /* SONG's cursor follows the row playing */
        got = ui.row_follow;
    else if (!strcmp(what, "transpose"))          /* the song's, -6..+5 */
        got = csong()->transpose;
    else if (!strcmp(what, "track"))              /* the selected track, 1..8 */
        got = proj.trk + 1;
    else if (!strcmp(what, "song_style"))         /* the song's style (its Intro's pattern number) */
        got = csong()->sty;
    else if (!strcmp(what, "test_session"))       /* a test session (the remote's op 5): RAM only, blank */
        got = ts.on;
    else if (!strcmp(what, "test_bytes"))         /* its objects' bytes in RAM (coded) */
        got = (int)ts.used;
    else if (!strcmp(what, "mirror_on"))          /* the screen mirrored over USB */
        got = mirror_on;
    else if (!strcmp(what, "mirror_drops"))       /* frames that did not fit the ring (their rows dirty) */
        got = (int)mr.drops;
    else if (!strcmp(what, "mirror_redraws"))     /* the UI asked to draw again (dirty rows, a refresh) */
        got = (int)mr.redraws;
    else if (!strcmp(what, "mirror_dirty")) {     /* rows the viewer has wrong now */
        got = 0;
        for (uint32_t r = 0; r < 240u; r++)
            got += mr_bit(mr.dirty, r);
    } else if (!strcmp(what, "mirror_inputs"))    /* input reports sent */
        got = (int)mr.inputs;
    else if (!strcmp(what, "bar"))                /* the pattern's bar playing, 0.. */
        got = arr_bar;
    else if (!strcmp(what, "ext"))                /* following MIDI clock in */
        got = arr_ext;
    else if (!strcmp(what, "song"))               /* the selected song, 1..4 */
        got = proj.song + 1;
    else if (!strcmp(what, "bank_count"))         /* the QY70 bank's styles */
        got = qb_count();
    else if (!strcmp(what, "bank_status"))        /* qb_status(): styles, 0 none, -1 damaged, -2 old */
        got = qb_status();
    else if (!strncmp(what, "slot", 4) && strstr(what, "_bank"))    /* slotN_bank: 1 plays a QY70 style's section */
        got = app.slot_buf[atoi(what + 4) % ARR_NSECT] >= 0;
    else if (!strncmp(what, "slot", 4) && strstr(what, "_trk") && (strstr(what, "_note") || strstr(what, "_tick") ||
                                                                strstr(what, "_count") || strstr(what, "_xtype"))) {
        /* slotS_trkT_noteI / _tickI: the arranger's slot S's track T (1..8), its event I (0..): pitch, tick (-1 none);
         * slotS_trkT_count: its notes */
        const char *q = strstr(what, "_trk") + 4, *f = strchr(q, '_');
        int t = (atoi(q) - 1) & 7, i = f ? atoi(f + 5) : 0;
        const arr_trk_t *tr;
        arr_pattern_copy(atoi(what + 4) % ARR_NSECT, &dbg_pat);
        tr = &dbg_pat.trk[t];
        if (f && !strncmp(f, "_count", 6))
            got = tr->count;
        else if (strstr(what, "_xtype"))           /* slotS_trkT_xtype: its follow type (XT_*: 2 Chord 1, 4 Bass) */
            got = tr->x.type;
        else if (i < 0 || i >= tr->count)
            got = -1;
        else
            got = f && !strncmp(f, "_note", 5) ? dbg_pat.ev[tr->first + i].note : dbg_pat.ev[tr->first + i].tick;
    } else if (!strncmp(what, "slot", 4) && strstr(what, "_nev")) {   /* slotN_nev: the arranger's slot N's notes (0 Intro, 1 Main A, ..) */
        arr_pattern_copy(atoi(what + 4) % ARR_NSECT, &dbg_pat);
        got = dbg_pat.nev;
    }
    else if (!strncmp(what, "pat", 3) && strstr(what, "_nev")) {   /* patN_nev: user pattern N's notes (0..23) */
        project_pattern_load(atoi(what + 3), &dbg_pat);
        got = dbg_pat.nev;
    } else if (!strncmp(what, "track_hits", 10))  /* track_hitsN (1..10: 9, 10 LD1, LD2): its note-ons since hitreset */
        got = (int)trk_hits[(unsigned)(atoi(what + 10) - 1) % ARR_ALLTRK];
    else if (!strcmp(what, "lead_notes"))         /* the song's lead notes (both tracks) */
        got = leads.n;
    else if (!strcmp(what, "loops_slot"))         /* the song's loops' slot, 1..16, 0 none (the defaults) */
        got = app.lpslot + 1;
    else if (!strncmp(what, "lead_slot", 9))      /* lead_slotK (1, 2): part K's slot, 1..16, 0 none */
        got = app.lslot[(atoi(what + 9) - 1) & 1] + 1;
    else if (!strcmp(what, "lead_dirty"))         /* the lead parts unsaved (bit k part k + 1) */
        got = app.ldirty;
    else if (!strcmp(what, "lead_rowtick"))       /* the row playing's place, ticks (arr_row_tick) */
        got = (int)arr_row_tick;
    else if (!strcmp(what, "lead_sel"))           /* the lead track selected: 1 LD1, 2 LD2, 0 none (a pattern's track) */
        got = app.lead;
    else if (!strncmp(what, "lead_count", 10))    /* lead_countK_R: lead K's (1, 2) notes in row R (1..; 0: all rows) */
        got = leads_count(&leads, atoi(strrchr(what, '_') + 1) - 1, (what[10] - '1') & 1);
    else if (!strncmp(what, "lsnd", 4) && strchr(what, '_')) {
        /* lsndK_prog / _chan / _vol / _pan / _rev / _cho / _bright / _vel: lead K's (1, 2) sound, the song's (prog 1..128, chan 1..16);
         * engsndK_prog: the engine's copy (leads.trk) */
        const lead_trk_t *l = &csong()->lead[(what[4] - '1') & 1];
        const char *f = strchr(what, '_') + 1;
        got = !strcmp(f, "prog") ? l->program + 1 : !strcmp(f, "chan") ? l->chan + 1 : !strcmp(f, "vol") ? l->vol
            : !strcmp(f, "pan") ? l->pan : !strcmp(f, "rev") ? l->rev : !strcmp(f, "cho") ? l->cho
            : !strcmp(f, "bright") ? (l->bright ^ 64) & 127 : !strcmp(f, "vel") ? l->vel : -1;
    } else if (!strncmp(what, "engsnd", 6) && strstr(what, "_prog"))
        got = leads.trk[(what[6] - '1') & 1].program + 1;
    else if (!strncmp(what, "row_lmute", 9))      /* row_lmuteN: row N's lead mutes, bit 0 LD1, bit 1 LD2 */
        got = csong()->row[(atoi(what + 9) - 1) % ARR_ROWS].lmute;
    else if (!strncmp(what, "lead", 4) && strchr(what, '_') && what[4] >= '0' && what[4] <= '9') {
        /* leadI_row / _pos / _note / _root / _trk / _len / _vel: lead note I (1..) of the list; -1 none */
        int i = atoi(what + 4) - 1;
        const char *f = strchr(what, '_') + 1;
        const lead_ev_t *e = i >= 0 && i < leads.n ? lead_at_c(&leads, (uint32_t)i) : 0;
        got = !e ? -1 : !strcmp(f, "row") ? e->row + 1 : !strcmp(f, "pos") ? (int)lead_pos(e) : !strcmp(f, "note") ? e->note
            : !strcmp(f, "root") ? lead_root(e) : !strcmp(f, "trk") ? lead_trk(e) + 1 : !strcmp(f, "len") ? (int)lead_len(e)
            : !strcmp(f, "vel") ? e->vel : -1;
    } else if (!strncmp(what, "slot_kind", 9)) {  /* slot_kindN (1..16): SLOT_NONE 0, PATTERN 1, LEAD 2, OTHER 3, LOOPS 4 */
        slot_info_t si;
        got = project_slot_info((atoi(what + 9) - 1) & 15, &si, &dbg_pat);
    }
    else if (!strncmp(what, "trk", 3) && strstr(what, "_program")) {   /* trkN_program: the playing slot's track N */
        arr_pattern_copy(sect_slot(arr_sect), &dbg_pat);
        got = dbg_pat.trk[(atoi(what + 3) - 1) & 7].program + 1;
    } else if (!strncmp(what, "trk", 3) && strstr(what, "_mute"))   /* trkN_mute: track N muted (the song's) */
        got = trk_muted(atoi(what + 3) - 1);
    else if (!strncmp(what, "ptrk", 4) && strstr(what, "_mute")) {   /* ptrkN_mute: the playing slot's pattern's own */
        arr_pattern_copy(sect_slot(arr_sect), &dbg_pat);
        got = dbg_pat.trk[(atoi(what + 4) - 1) & 7].mute;
    } else if (!strncmp(what, "trk", 3) && strstr(what, "_bright")) {   /* trkN_bright / _reso: CC 74 / 71 */
        arr_pattern_copy(sect_slot(arr_sect), &dbg_pat);
        got = arr_bright(&dbg_pat.trk[(atoi(what + 3) - 1) & 7]);
    } else if (!strncmp(what, "trk", 3) && strstr(what, "_reso")) {
        arr_pattern_copy(sect_slot(arr_sect), &dbg_pat);
        got = arr_reso(&dbg_pat.trk[(atoi(what + 3) - 1) & 7]);
    } else if (!strncmp(what, "trk", 3) && strstr(what, "_vol")) {
        arr_pattern_copy(sect_slot(arr_sect), &dbg_pat);
        got = dbg_pat.trk[(atoi(what + 3) - 1) & 7].vol;
    } else if (!strncmp(what, "slot", 4) && strstr(what, "_bars")) {
        arr_pattern_copy(atoi(what + 4) % ARR_NSECT, &dbg_pat);
        got = dbg_pat.bars;
    } else if (!strncmp(what, "song", 4) && strstr(what, "_mute") && what[4] >= '1' && what[4] <= '4')
        got = songs.song[what[4] - '1'].mute;     /* songN_mute: its muted tracks, bit t track t + 1 */
    else if (!strncmp(what, "song", 4) && strstr(what, "_sty") && !strstr(what, "_style") && what[4] >= '1' &&
             what[4] <= '4')
        got = songs.song[what[4] - '1'].sty;      /* songN_sty: its style, the Intro's pattern number (32: Pop) */
    else if (!strncmp(what, "song", 4) && strstr(what, "_style") && what[4] >= '1' && what[4] <= '4')
        got = songs.song[what[4] - '1'].style[atoi(strstr(what, "_style") + 6) % ARR_NSECT];   /* songN_styleI (I: the slot) */
    else if (!strncmp(what, "song", 4) && strstr(what, "_rows") && what[4] >= '1' && what[4] <= '4')
        got = songs.song[what[4] - '1'].nrows;    /* songN_rows (1..4), as in RAM */
    else if (!strncmp(what, "song", 4) && strstr(what, "_tempo") && what[4] >= '1' && what[4] <= '4')
        got = songs.song[what[4] - '1'].tempo;    /* songN_tempo (1..4), as in RAM */
    else if (!strcmp(what, "clock"))
        got = proj.clock;
    else if (!strcmp(what, "quant"))              /* the record grid: ARR_Q_8 0, 8T 1, 16 2, 16T 3, 32 4, off 5 */
        got = proj.quant;
    else if (!strcmp(what, "grid_bar"))           /* the Grid knob's bar, as drawn: its fill */
        got = grid_fill();
    else if (!strcmp(what, "slots_list"))         /* the SLOTS list on screen */
        got = lst.open && lst.slots;
    else if (!strncmp(what, "slot_shown", 10))    /* slot_shownN (1..16): its kind as SLOTS shows it (0 free, 1 pattern,
                                                   * 2 lead part, 4 loops) */
        got = slot_kind[(atoi(what + 10) - 1) & 15];
    else if (!strcmp(what, "list_open"))          /* SEL's list on screen */
        got = lst.open;
    else if (!strncmp(what, "list_has_", 9)) {    /* list_has_WORDS: the open list has a row named WORDS ("_": a space) */
        char w[48];
        int i;
        for (i = 0; i < 47 && what[9 + i]; i++)
            w[i] = what[9 + i] == '_' ? ' ' : what[9 + i];
        w[i] = 0;
        list_build();
        got = 0;
        for (i = 0; i < lst.n; i++)
            got |= !strcmp(lst.row[i].name, w);
    } else if (!strncmp(what, "slottext", 8) && strstr(what, "_is_")) {
        /* slottextN_is_WORDS: MEMORY's row N (1..16) as it reads ("_": a space, "|": a dot) */
        char w[48];
        const char *s = strstr(what, "_is_") + 4;
        int i, n = (atoi(what + 8) - 1) & 15;
        for (i = 0; i < 47 && s[i]; i++)
            w[i] = s[i] == '_' ? ' ' : s[i];
        w[i] = 0;
        got = !strcmp(slot_txt[n], w);
        if (!got)
            printf("  (memory %d: \"%s\")\n", n + 1, slot_txt[n]);
    } else if (!strncmp(what, "held_ring", 9))    /* held_ringN (1..16): HOME held, progression N's ring: 0 none, 1 teal
                                                   * (playing), 2 coral (asked for), 3 ink (PLAY starts it) */
        got = held_ring((atoi(what + 9) - 1) & 15);
    else if (!strcmp(what, "knobs_shown"))        /* the knob strip has a knob in it */
        got = knobs_shown();
    else if (!strcmp(what, "list_sel"))           /* the list's row picked, 0.. */
        got = lst.sel;
    else if (!strcmp(what, "card"))               /* a help card on screen: its button + 1 (B_*), 0 none */
        got = ui.card;
    else if (!strcmp(what, "asking"))             /* a question on screen (SEL yes, HOME no) */
        got = ui.ask;
    else if (!strcmp(what, "name_cur"))           /* the name's letter edited, 0..7 */
        got = ui.name_cur;
    else if (!strcmp(what, "rev_type"))
        got = proj.rev_type;
    else if (!strcmp(what, "song0_tempo")) {      /* song 0's tempo, as saved */
        project_songs_load(&dbg_songs);
        got = dbg_songs.song[0].tempo;
    } else if (!strcmp(what, "song0_rows")) {      /* song 0's rows, as saved */
        project_songs_load(&dbg_songs);
        got = dbg_songs.song[0].nrows;
    } else if (!strcmp(what, "loop9_ch0")) {       /* song 1's loop 9's beat 0, as saved: root*100 + type, -1 "." */
        int8_t sl;
        project_songs_load(&dbg_songs);
        project_loops_load(&dbg_songs, 0, &dbg_loops, &sl);
        got = dbg_loops.loop[8].ch[0] == LC_SAME ? -1 : lc_unpack(dbg_loops.loop[8].ch[0]).root * 100 +
                                                            lc_unpack(dbg_loops.loop[8].ch[0]).type;
    } else if (!strncmp(what, "saved", 5) && strstr(what, "_loop") && strstr(what, "_ch")) {
        /* savedS_loopL_chB: song S's (1..4) progression L's (1..16) beat B (0..), as saved (the songs object, or its
         * slot): root*100 + type, -1 "."; savedS_loopL_bars: its bars */
        int8_t sl;
        int s = (what[5] - '1') & 3, l = (atoi(strstr(what, "_loop") + 5) - 1) & 15;
        uint16_t v;
        project_songs_load(&dbg_songs);
        project_loops_load(&dbg_songs, s, &dbg_loops, &sl);
        v = dbg_loops.loop[l].ch[atoi(strstr(what, "_ch") + 3) % ARR_LOOP_BEATS];
        got = v == LC_SAME ? -1 : lc_unpack(v).root * 100 + lc_unpack(v).type;
    } else if (!strncmp(what, "saved", 5) && strstr(what, "_loop") && strstr(what, "_bars")) {
        int8_t sl;
        project_songs_load(&dbg_songs);
        project_loops_load(&dbg_songs, (what[5] - '1') & 3, &dbg_loops, &sl);
        got = dbg_loops.loop[(atoi(strstr(what, "_loop") + 5) - 1) & 15].bars;
    } else if (!strncmp(what, "slot_held", 9)) {  /* slot_heldN (1..16): 1 it holds melodies or a song's progressions
                                                   * (project_slot_held: never a groove's), 0 not */
        got = project_slot_held((atoi(what + 9) - 1) & 15);
    } else if (!strncmp(what, "prog_bytes", 10)) {  /* prog_bytesS: song S's (1..4) progressions' bytes in the songs
                                                     * object, as saved (0 the defaults, -1 in a slot) */
        project_songs_load(&dbg_songs);
        got = dbg_songs.plen[(atoi(what + 10) - 1) & 3];
        got = got == ARR_PLEN_SLOT ? -1 : got;
    } else if (!strncmp(what, "memrow", 6) && strstr(what, "_is_")) {
        /* memrowN_is_WORDS: the MEMORY list's row N (1..16, as it is listed: by song, free last) ("_" a space, "|" a
         * dot) */
        int n = (atoi(what + 6) - 1) & 15;
        char w[48];
        const char *t = slot_txt[slot_ord[n]];
        snprintf(w, sizeof w, "%s", strstr(what, "_is_") + 4);
        for (char *c = w; *c; c++)
            if (*c == '_')
                *c = ' ';
        got = !strcmp(t, w);
        if (!got)
            printf("  (memory row %d: \"%s\")\n", n + 1, t);
    } else
        return 0;                                 /* (fm1-base's own: store_writes, peak_db_max ...) */
    *got_out = got;
    return 1;
}

static int host_file(char *out, size_t n, const char *a)   /* "out:NAME" is NAME in the run's directory */
{
    if (!strncmp(a, "out:", 4))
        return snprintf(out, n, "%s/%s", plat_host_dir, a + 4);
    return snprintf(out, n, "%s", a);
}

int app_host_cmd(const char *cmd, const char *a0, const char *b, const char *c, const char *d)
{
    char a[256], out[400];
    (void)c, (void)d;
    snprintf(a, sizeof a, "%s", a0);
    if (!strcmp(cmd, "expect") && !strncmp(a, "loop_name", 9) && a[9] >= '0' && a[9] <= '9') {
        /* loop_nameN VAL: loop N's name, exactly (its own string, not "_is_"'s 0/1: "Prog 10" has a space) */
        const char *what = a, *val = b;
        const loop_t *l = &sloops.loop[(atoi(what + 9) - 1) & 15];
        char nm[9];
        int i, ok;
        for (i = 0; i < 8 && l->name[i]; i++)
            nm[i] = l->name[i];
        nm[i] = 0;
        ok = !strcmp(nm, val);
        printf("%s loop_name%d == \"%s\" (got \"%s\")\n", ok ? "  ok" : "FAIL", atoi(what + 9), val, nm);
        host_fails += !ok;
        return 1;
    }
    if (!strcmp(cmd, "bank")) {                     /* bank FILE: an image into the bank's flash, as the device writes it */
        static uint8_t img[320u * 1024u];               /* (a whole bank image: the directory and 63 sectors) */
        FILE *bf;
        long n;
        host_file(out, sizeof out, a);
        if (!(bf = fopen(out, "rb"))) {
            printf("FAIL no bank image %s\n", a);
            host_fails++;
            return 1;
        }
        n = (long)fread(img, 1, sizeof img, bf);
        fclose(bf);
        plat_data_erase(QB_DIR);                    /* (the flash erased first) */
        for (uint32_t i = 0; i < QB_NSECT; i++)
            plat_data_erase(qb_sector(i));
        for (long i = QB_SECTOR; i < n; i++)
            plat_data_write(qb_phys((uint32_t)(i - QB_SECTOR)), &img[i], 1);
        if (n >= (long)QB_SECTOR)
            plat_data_write(QB_DIR, img, QB_SECTOR);   /* the directory last */
        app_bank_changed();
        run_ms(1);
        return 1;
    }
    if (!strcmp(cmd, "bank_damage")) {             /* a byte of the bank's style N changed in the flash */
        const uint8_t *e = plat_data(QB_DIR + QB_DIR_HEAD + QB_DIR_ENTRY * (uint32_t)atoi(a));
        uint32_t off = e[8] | e[9] << 8 | (uint32_t)e[10] << 16, at = qb_phys(off + 30);
        data_area[at - DATA_BASE] ^= 0x10;          /* (both ways, as a bad byte: not a flash write) */
        return 1;
    }

    if (!strcmp(cmd, "style")) {         /* style NAME: all six sections from it (style_all) */
        int qy = !strncmp(a, "qy:", 3), i, found = -1;
        char *nm = a + (qy ? 3 : 0);
        for (char *c = nm; *c; c++)
            if (*c == '_')
                *c = ' ';
        for (i = qy ? arr_styles() : 0; i < styles_n() && found < 0; i++)
            if (!strcmp(style_label(i), nm))
                found = i;
        if (found < 0) {
            printf("FAIL no style %s\n", nm);
            host_fails++;
            return 1;
        }
        style_all(found);
        run_ms(20);
        return 1;
    }
    if (!strcmp(cmd, "hitreset")) {
        extern int16_t gm_host_heard[16];
        extern uint32_t gm_host_ons[16];
        memset(trk_hits, 0, sizeof trk_hits);
        click_count = 0;
        for (int c = 0; c < 16; c++)
            gm_host_heard[c] = -1;
        memset(gm_host_ons, 0, sizeof gm_host_ons);
        return 1;
    }
    if (!strcmp(cmd, "poke_full_patterns")) {   /* host-only: all 16 user patterns saved with a note, none
                                                         * played by a song */
        pattern_empty(&dbg_pat);
        dbg_pat.nev = 1;
        dbg_pat.trk[4].first = 0;
        dbg_pat.trk[4].count = 1;
        dbg_pat.ev[0].len = ARR_PPQ;
        dbg_pat.ev[0].note = 60;
        dbg_pat.ev[0].vel = 100;
        for (int i = 0; i < ARR_USER_PATTERNS; i++)
            project_pattern_save(i, &dbg_pat);
        return 1;
    }
    if (!strcmp(cmd, "poke_user_songs")) {   /* host-only: songs 1 and 2 on user patterns P01..P06, P07..P12 */
        songs_defaults(&dbg_songs);
        for (int n = 0; n < 2; n++)
            for (int k = 0; k < ARR_NSECT; k++)
                dbg_songs.song[n].style[k] = (uint16_t)(n * ARR_NSECT + k);
        project_songs_save(&dbg_songs);
        return 1;
    }
    if (!strcmp(cmd, "poke_lead_rec")) {   /* host-only: poke_lead_rec K: lead K (1, 2) selected and armed (the keys
                                                   * play it, a song's rows record it; 0: back to the track, off), as
                                                   * ALGORITHM past C4 and REC do */
        int k = atoi(a);
        app.lead = (uint8_t)(k >= 1 && k <= 2 ? k : 0);
        arr_record(k >= 1 && k <= 2 ? ARR_LD1 + k - 1 : proj.trk, k >= 1 && k <= 2);
        run_ms(20);
        return 1;
    }
    if (!strcmp(cmd, "poke_row")) {        /* host-only: poke_row L: a row of loop L (1..16) at the song's end
                                                   * (until the SONG screen writes rows) */
        songs_write(1);
        row_tap(csong(), csong()->nrows, (atoi(a) - 1) & 15);
        songs_write(0);
        return 1;
    }
    if (!strcmp(cmd, "poke_share")) {      /* host-only: song 1 plays one user pattern as Main A and Main B */
        songs_defaults(&dbg_songs);
        for (int k = 0; k < ARR_NSECT; k++)
            dbg_songs.song[0].style[k] = (uint16_t)k;
        dbg_songs.song[0].style[SEC_B - 1] = dbg_songs.song[0].style[SEC_A - 1];
        project_songs_save(&dbg_songs);
        return 1;
    }
    if (!strcmp(cmd, "poke_pattern")) {     /* host-only debug (Step 4): a known pattern and song */
        pattern_empty(&dbg_pat);
        dbg_pat.nev = 3;
        dbg_pat.trk[3].first = 0;
        dbg_pat.trk[3].count = 3;
        dbg_pat.ev[0].tick = 0;
        dbg_pat.ev[0].len = ARR_PPQ;
        dbg_pat.ev[0].note = 36;
        dbg_pat.ev[0].vel = 100;
        dbg_pat.ev[1].tick = ARR_PPQ;
        dbg_pat.ev[1].len = ARR_PPQ;
        dbg_pat.ev[1].note = 40;
        dbg_pat.ev[1].vel = 100;
        dbg_pat.ev[2].tick = 2 * ARR_PPQ;
        dbg_pat.ev[2].len = ARR_PPQ;
        dbg_pat.ev[2].note = 43;
        dbg_pat.ev[2].vel = 100;
        project_pattern_save(SEC_A - 1, &dbg_pat);    /* song 1's Main A (user pattern 2) */
        songs_defaults(&dbg_songs);
        dbg_songs.song[0].tempo = 100;
        dbg_songs.song[0].nrows = 2;
        {
            chord_t am7 = {9, CH_MIN7, CH_NOBASS};          /* song 1's L9's first beat: A m7 (packed in the songs) */
            int8_t sl = -1;
            loops_default(dbg_loops.loop);
            loop_write(&dbg_loops.loop[8], 0, am7);
            project_loops_store(&dbg_songs, 0, &dbg_loops, &sl);
        }
        project_songs_save(&dbg_songs);
        return 1;
    }
    if (!strcmp(cmd, "poke_dense_progs")) {   /* host-only: the song's first N progressions (all 16 when no
                                                      * N) full: 16 bars of 4, a chord on every beat (196 bytes
                                                      * each packed: 16 of them pass the songs' pool), as if written
                                                      * on the Progression page */
        int n = a[0] ? atoi(a) : ARR_LOOPS;
        songs_write(1);
        for (int l = 0; l < n && l < ARR_LOOPS; l++) {
            loop_t *lp = &sloops.loop[l];
            lp->bars = 16;
            lp->beats = 4;
            for (int b = 0; b < ARR_LOOP_BEATS; b++) {
                chord_t c = {(uint8_t)((b * 5 + l) % 12), (uint8_t)(b % 2 ? CH_MIN : CH_MAJ), CH_NOBASS};
                lp->ch[b] = lc_pack(c);
            }
        }
        songs_write(0);
        return 1;
    }
    if (!strcmp(cmd, "poke_songs_default")) {   /* host-only: the songs object as on a new FM-1, straight
                                                         * into storage (their packed progressions gone) */
        songs_defaults(&dbg_songs);
        project_songs_save(&dbg_songs);
        return 1;
    }
    if (!strcmp(cmd, "poke_bad_style")) {   /* host-only: songs with pattern numbers past the styles */
        songs_defaults(&dbg_songs);
        dbg_songs.song[0].style[SEC_A - 1] = (uint16_t)(ARR_FACTORY0 + ARR_NSECT * arr_styles());
        dbg_songs.song[1].style[SEC_FILL_AB - 1] = 250;
        dbg_songs.song[1].style[SEC_B - 1] = (uint16_t)(ARR_FACTORY0 + ARR_NSECT * arr_styles() - 1);   /* the last: kept */
        dbg_songs.song[1].style[SEC_INTRO - 1] = ARR_USER_PATTERNS;                  /* past the user patterns */
        dbg_songs.song[1].style[SEC_END - 1] = 0xFFFF;
        project_songs_save(&dbg_songs);
        return 1;
    }
    if (!strcmp(cmd, "poke_muted_pattern")) {   /* host-only: P01 saved with C1 muted (patterns held their
                                                         * mutes before they were the song's), song 1's Main A */
        pattern_empty(&dbg_pat);
        dbg_pat.nev = 1;
        dbg_pat.trk[4].first = 0;
        dbg_pat.trk[4].count = 1;
        dbg_pat.ev[0].len = ARR_PPQ;
        dbg_pat.ev[0].note = 60;
        dbg_pat.ev[0].vel = 100;
        dbg_pat.trk[4].mute = 1;
        project_pattern_save(0, &dbg_pat);
        songs_defaults(&dbg_songs);
        dbg_songs.song[0].style[SEC_A - 1] = 0;
        project_songs_save(&dbg_songs);
        return 1;
    }
    if (!strcmp(cmd, "poke_bad_pattern")) {   /* host-only debug: an invalid nev, straight into storage */
        pattern_empty(&dbg_pat);
        dbg_pat.nev = ARR_NEV + 1;
        project_pattern_save(SEC_A - 1, &dbg_pat);
        return 1;
    }
    return 0;
}

void app_speak_dump(void) { speak_dump(); }
void app_host_reboot(void)                          /* RAM cleared: a test session gone, the project forgotten */
{
    ts_set(0);
    memset(&proj, 0, sizeof proj);
    started = 0;
}
int app_host_fails(void) { return msg_too_wide + host_fails; }
void app_host_ms(void)                              /* the tracks' note-ons: arr_track_hit moves at each one */
{
    for (uint32_t k = 0; k < ARR_ALLTRK; k++)
        if (arr_track_hit[k] != trk_last[k]) {
            trk_last[k] = arr_track_hit[k];
            trk_hits[k]++;
        }
}
