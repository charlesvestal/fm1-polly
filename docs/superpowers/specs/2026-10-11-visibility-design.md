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
- **Progression**: the cursor as a thick coral ring on its beat cell + the bar.beat readout by the grid; the
  playing beat teal; a written chord flashes.
- **Band** (a groove's notes): add an edit position: a beat cursor (BAR / BEAT as on Progression, or OCT to
  step) shown as a vertical coral line across the instruments, the selected instrument's row highlighted,
  the playhead teal; notes recorded flash. (Note editing itself is a later step; this gives the place.)
- **Melodies**: on Sound with a melody selected, the section's bars always shown (not only while recording),
  the cursor/punch point visible, the melody's notes as ticks, the other melody dimmed.
- **Song**: the cursor section boxed in coral (thick), the playing section filled teal, distinct even when
  the same; a changed row flashes; "section 3 of 6" by the cursor.
- **Home**: the recording bar view already shows position; check the playing bar and the next chord read
  at a glance.
- **Messages**: undo / redo flash the place they changed and jump the screen there if it's on another bar.

Checks: screenshots of each screen stopped, playing and right after an edit, read at 240×240; host expects
for the cursor positions; the suite green.
