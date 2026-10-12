# Polly: see where you are and where an edit lands (2026-10-11)

The user, on the device: editing is "invisible and needs to be direct" — not just the edits but where they
land: cursors and positions. A rule for every editing screen:

1. **Always a visible place.** A bold cursor where the knobs and keys write (coral, thick enough to see at
   arm's length on 240×240), and a playhead while playing (teal), both at once; never confused (cursor =
   outline, playhead = fill).
2. **The edit lights up as it happens.** The chord, note, section or setting just changed flashes (coral,
   ~0.5 s), so you see it land — also for knob edits, CLEAR / COPY / ERASE actions and undo / redo (undo
   flashes what came back).
3. **Where, in numbers, next to the cursor.** "bar 1 · beat 3" (or "section 3", "M1 · section 2 · bar 3")
   near the cursor in the main area, not only in the tag line.

Screens:
- **Progression**: the cursor as a thick coral ring on its beat cell, its bar's and beat's numbers on coral (the
  "where" readout by the grid was dropped in the screen audit: with the badges and the ring it said the place
  four ways; the badges sit at the cursor, so they stayed); the playing beat teal; a written chord flashes.
- **Band** (a groove's notes): add an edit position: a beat cursor (BAR / BEAT as on Progression, or OCT to
  step) shown as a vertical coral line across the instruments, the selected instrument's row highlighted,
  the playhead teal; notes recorded flash. (Note editing itself is a later step; this gives the place.)
- **Melodies**: on Sound with a melody selected, always shown (not only while recording): the whole song as one
  line (a box a section, as wide as its bars, its progression's name, its groove's tint, the notes as plum ticks,
  the playhead teal, the section shown outlined), under it four bars running on across the sections (the cursor's
  bar ringed coral, the bar playing teal), and one readout "song bar 9 · Chorus bar 1" (2026-10-12: one linear
  timeline, the notes still kept by section); the other melody dimmed.
- **Song**: the cursor section boxed in coral (thick), the playing section filled teal, distinct even when
  the same; a changed row flashes; "section 3 of 6" by the cursor.
- **Home**: the recording bar view already shows position; check the playing bar and the next chord read
  at a glance.
- **Messages**: undo / redo flash the place they changed and jump the screen there if it's on another bar.

Checks: screenshots of each screen stopped, playing and right after an edit, read at 240×240; host expects
for the cursor positions; the suite green.

## The colours (the screen audit, 2026-10-11)

Each colour means one thing, on every page:

- **Teal: sounding now**, and nothing else: the bar, beat, section, progression playing; the playhead; a melody's
  chip while it sounds; HOME held's progression playing (filled) and groove playing (ringed), only while playing;
  Free's chord while the band plays it.
  - **A bar playing** (HOME's bars, recording a groove, recording a melody; `bar_lit` / `bar_beats` in pages.c): a
    light teal tint (K_TEAL_T) inside a 2 px teal outline, its number dim and its chord in ink (readable at arm's
    length; of several chords the one sounding ink, the others dim). Its beats as segments along its foot, centred:
    the beat now a bold solid teal block (6 px tall), those gone by solid pale teal (K_TEAL_M, 4 px), those to come a
    pale teal outline (4 px); 6 and 7 beats closer together. A cursor's coral ring on the bar playing keeps the tint
    inside it (the ring stays distinct); with the cursor stepping (no playhead) the beat now is coral on neutral.
    A cell too short for the big chord over the beats (under 52 px: HOME's two rows) shows it in the middle font
    (capitals only, "F7") or the bold one, every cell alike.
- **Coral: you**: the cursor, an edit's flash, the knob turned last (value and bar), REC (its dot, the row being
  recorded), the chord you play over a progression (Home, top right, until the progression's next change), the
  progression or groove you asked for (HOME held), questions and messages.
- **Ink and its neutrals: everything static**: names, values, chords, notes on Band, mute squares; a list's
  selection (ink fill, light text); a selected row or strip (Band, Mixer: a darker panel, an ink outline); the knobs'
  bars (dim; the hot one coral); the page dots; the help card's and an instrument's pill (an ink outline); Mixer
  strips (muted: pale); where PLAY starts, stopped (an ink ring: Home's bar 1, HOME held's progression and groove).
  Hints are dim ink, not coral.
- **Section colours only on the groove pills** (Home, HOME held, Band, Style, Song's GROOVE mark; light, a melody's
  song line): **Groove A slate,
  Groove B amber**; Intro, Fill AB / BA, Ending are neutral outlined pills (one look for both fills).
- **Plum: the melodies** (M1, M2 pills, strips, mute squares). Drums lose their coral (notes and strips are ink).

The simulator's expects check the rule where it was broken: HOME held after a stop rings nothing teal
(tests/scenarios/screens.polly, held_ringN: 1 teal only while playing, 2 coral asked for, 3 ink where PLAY starts).
