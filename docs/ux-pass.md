# Polly: a UI / UX flow pass (2026-10-09)

The main flows walked as a new user would, on the simulator's screenshots: `tests/scenarios/tour.polly`
(`sh tests/run_tests.sh`, or `build/host/polly_host tests/scenarios/tour.polly build/scenarios/tour`). The
screenshots named below are in `build/scenarios/tour/` (after this pass); the firmware before it, run
through the same flow (without the new checks), gave `build/scenarios/tour_before/` (kept locally, not in the
repo).

Priorities: **must** (blocks or misleads a new player), **should** (friction, cheap to fix), **could**
(polish, or not cheap). Done: marked so.

## The flows, presses before and after

| Flow | Before | After |
|---|---|---|
| Pick a style (from boot) | ENV, ENV, KNOB 4 on a page called "Song patterns": 3 presses, 2 page changes, nothing on the Song page says a style exists | KNOB 1 (or PRESETS) on the Song page, the boot page: 1 turn; the style in the header |
| Hear something | SEL + white key, PLAY (the hint said so) | same; holding SEL shows the 16 progressions by name |
| Next section | SEL + black key 1..5, from memory (the spec, the lights) | same; holding SEL shows the five, the one playing / next outlined |
| Another song | HOME, PRESETS: 2 | SEL + PRESETS on any page: 1 (or ENV, KNOB 4) |
| Tempo | SELECT, or the Song page's KNOB 2 (two controls, one job) | SELECT (KNOB 2 is the template now) |
| Which page of a button am I on | the title only; ENV's two pattern pages had the same title | the dots after the title: page 2 of 3 |
| Is something unsaved | a 5 px grey dot after the title, unexplained | SAVE lit, and "unsaved" in the header where it fits |
| Record a part from a style | REC: "RECORD C1" covered "NEW PATTERN P01"; the take ended silently at the section end, and the next REC tap armed it again | "RECORD C1 into new P01"; "RECORD OFF section ended" |

## Findings

### Must

1. **Style choice buried.** (done) `12_song_patterns.png`, `01_boot.png`. The style, Polly's main choice,
   lived on ENV's 2nd / 3rd page. Now KNOB 1 on the Song page ("Style", or "QY style" for a bank style),
   PRESETS on the Song page too, and the header reads "Song 1 Pop" / "Song 1 QY Pop" (`01_boot.png`,
   `01b_style_knob.png`, `63_song_qy.png`). The Song page's Tempo knob went (SELECT does it). The song moved
   from the Song page's PRESETS to SEL + PRESETS (anywhere) and the Arrange page's KNOB 4 (`71_song_at_stop.png`,
   `73_arrange_song.png`): a player browses styles far more than songs, and a song change is a save.
2. **SEL's key shortcuts invisible.** (done) `02_sel_held.png`, `07a_sel_next_b.png`. SEL held 0.4 s draws
   the five sections (black keys, in their colours, the playing or queued one outlined) and the 16 templates
   (white keys, the playing one lit) over the main band; the header says "SEL + a key" and "PRESETS: song".
   SEL's release after that is no CHORD tap (it was 0.6 s). The Song page's hint now says "Hold SEL:
   progressions, sections".
3. **Pages of one button looked alike.** (done) `12_song_patterns.png` / `14_song_patterns2.png` had the
   same title and nothing to tell them apart. Every button with more pages shows dots after the title.
4. **A message cut off.** (done) `42_jobs_on_song.png` before: "JOBS on the Pattern or Chord" (the rest off
   screen). Now "JOBS on the Pattern, Chords pages". The simulator now fails any scenario whose message is
   wider than the header (`say()` under `GM_HOST`); every fixed message was measured too (the widest: 221 of
   232 px).
5. **The tracks stayed lit after STOP.** (done, a bug) `40_song_unsaved.png`, `63_song_qy.png` before: the
   tracks that played last kept their highlight forever (the engine's tick stops with the transport). Lit only
   while playing (`08_stopped.png`).
6. **A take ended silently.** (done) `22_recording.png` before: the engine ends recording at a section change
   (the Intro into Main A); REC's light went out, nothing said why, and the next REC tap (meant as "stop")
   armed it again (`23_pattern_setup.png` before: the red dot on while stopped). Now "RECORD OFF section
   ended" (`22_record_ended.png`).
7. **The copy-on-write notice was lost.** (done) `21_recording_msg.png` before: "NEW PATTERN P01" was
   replaced by "RECORD C1" in the same frame. Now "RECORD C1 into new P01"; into a user pattern: "RECORD C1
   Main A".

### Should

8. **Key shown three ways.** (done) Song page "C", Templates "0", Setup "0 (C)". Now the note everywhere on
   the knobs ("C", "D", "Bb") and "D (+2)" on Setup's row; the Song page's key knob stops at -6 / +5 like the
   others (it wrapped).
9. **Long knob values overflowed.** (done) A template name ("Andalusian") or a QY70 style name on a 60 px
   knob: now drawn smaller, then cut, to fit. The Song page's Template knob shows the name, not a number.
10. **Chord recording had no REC dot.** (done) The header's dot showed only track recording; REC on the
    Chords page now shows it too.
11. **Low contrast.** (done) "next -" (stopped, no next chord) and the Track page's "range" were drawn in the
    line colour on cream; now in the dim text colour.
12. **"Track: plays / muted"** on the Pattern page's KNOB 4 read as a track selector. (done) Now "Mute: off /
    on".
13. **Recording from STOP on a Pattern page goes into the Intro.** `20_pattern.png` shows Main B (the last
    section played), PLAY starts the Intro (a new song's first section), REC records into the Intro, and the
    take ends when it does. The engine already skips the Intro when REC is armed before PLAY, but nothing
    says so. Fix: stopped, the Pattern pages' REC + PLAY (or SEL + black key) should pick the section shown
    and loop it; or say "REC first: records Main A". Not cheap (the engine's start rules).
14. **Which section the Pattern pages edit, stopped.** They edit the section playing; stopped, the last one.
    SEL + black key while stopped queues the first section but the pages keep showing the old one. Fix:
    stopped, show (and edit) the queued section.
15. **"unsaved" stays up while playing.** Autosave runs only at rest (a flash write pauses the audio), so
    after any change "unsaved" sits in the header until the stop (`05_playing_main_a.png`). Dim and small;
    SAVE's light carries it too. Could drop the word while playing.
16. **A style on a song with chords keeps the tempo silently.** The style's tempo comes only with a blank
    song. Fix: say "STYLE Funk  tempo kept" (fits), or a tempo hint on the knob.

### Could

17. Empty knob slots: Pattern KNOB 2 / 3, Track keys 3 / 4, Mixer tone 3 / 4, Templates 3 / 4, Panel (all).
    Candidates: the Pattern page's track (as ALGORITHM), Templates' "Copy" times.
18. Master FX's chorus types are "Chorus 1..4" (`34_master_fx.png`): name them as the reverbs are.
19. The jobs' KNOB 1 shows "Job 1 / 2 / 3"; the list shows the name, the knob could too (`43_pattern_jobs.png`).
20. Setup's "Clock: MIDI in" (`60_setup_nobank.png`) means "follows MIDI clock when it comes"; "auto" or
    "MIDI in / internal" reads better. "Panel" (CPU, version) could be "About".
21. The Chords page's "." for "no change" is pale on its panel (`10_chords.png`).
22. After a song change the Song page shows the old bar.beat dimmed ("2.1", `72_song2.png`) until PLAY.
23. A template and a style can share a name (the Pop template, the Pop style). The knob labels and the
    header now tell them apart; renaming the templates' "Pop" to "I-V-vi-IV" would end it.
24. The key is on three pages (Song, Templates, Setup); Setup's could go.

## States checked

Stopped / playing (`04`, `05`, `08`), recording (`21`, `22`), CHORD mode (`06`: the CHORD pill, the chord in
teal, "next: the keys"), MIDI clock in (`70_ext_clock.png`: "ext" for the tempo), unsaved (`40`, SAVE lit) and
saved (`41`), a queued song (`71`), no bank (`60_setup_nobank.png`: "QY70 styles none"), a bank
(`62_setup_bank.png`: "17 styles"), a damaged style (`64_damaged_msg.png`: "QY70 STYLE DAMAGED", nothing
changes), the jobs (`42`-`44`).

## Tests added

`tour.polly` checks what changed: the Song page's KNOB 1 and PRESETS set the style, SEL held shows the
shortcuts (not before 0.4 s) and its release is no CHORD tap, SEL + PRESETS picks the song (after the stop
while playing), the Arrange page's KNOB 4 too, SAVE's light, the REC messages. New `expect`s in the simulator:
`sel_shown`, `lit_BTN`, `msg2_is_WORDS`; and every scenario fails on a message wider than the header.
