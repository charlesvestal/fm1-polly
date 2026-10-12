# Polly: a note editor, after Melodee's NOTES (2026-10-12)

The biggest gap left: no way to fix one wrong note — melodies and groove instruments can only be cleared and
recorded again. Melodee (keremimo, GPL-3.0, on Felucca like Polly; `../melodee`, SEQ > NOTES in its README) has a
piano-roll note editor whose interaction fits Polly's conventions. We adopt its design (credited in the manual's
credits and in the code where drawing or selection logic is adapted); the data side is Polly's own (free-timed
notes at 480 ticks a beat: melodies per section, groove instruments per pattern).

## Where

1. **Melodies first**: in the melody timeline view (whole-song strip + the 4-bar lane): the lane becomes editable.
2. **Band next**: ALGO picks the instrument; its notes in the groove are editable on Band.

## Interaction (as Melodee's, mapped to Polly)

- **Notes are selected, not steps.** SELECT walks through the notes in time order (chord notes separately);
  in the editor SELECT stays on notes (pages are reached by leaving the editor — e.g. the button again, or HOME).
  The selected note: coral outline (the cursor); the playhead: teal line; colours per the visibility spec.
- **KNOB 1 PLACE** moves the cursor through time (by the grid: Band's GRID / the melody's quantise grid), including
  empty places, so a note can be entered there: playing a key at an empty place enters a note (melodies: with the
  chord root sounding there); on a note, a key auditions only.
- **KNOB 2 PITCH**, **KNOB 3 LENGTH** (by the grid; a modifier for fine steps), **KNOB 4 VEL** edit the selected note.
- **PRESET zooms** the lane (melody: 1, 2, 4, 8 bars; Band: the groove's bars down to a beat).
- **Move**: hold SEL and turn SELECT or KNOB 1 to move the selected note in time (keeps its length and velocity).
- **Delete**: EDIT deletes the selected note (EDIT is free inside the editor; its page is one press away outside).
- **Erase while recording**: hold EDIT while recording: notes the playhead crosses are removed (Melodee's).
- **Undo**: every edit is one undo step (HOME + REC / HOME + PLAY), through the existing journal.
- Readout by the cursor: "M1 · song bar 9 · Chorus bar 1.3 · E5" / "BA · bar 2.3 · A2".
- Footer / encoder line per the conventions: "[SELECT] note · [KNOB1] place …", "[EDIT] delete · [SEL]+[SELECT] move".

## Melody specifics

- A melody note is stored per section (row + position); moving a note across a section boundary moves it into
  the next section (re-anchored, root recorded = the chord sounding at its new place).
- Entering a note records the chord root sounding at that place (so it follows chord edits like recorded notes).

## Band specifics

- Editing a style's groove makes it "(yours)" first (copy-on-write, as recording does).
- Drums: PITCH changes the drum sound (the GM kit note), named ("Snare", "Closed hat").
- Notes past ARR_NEV refused ("GROOVE FULL").

## Testing

Host tests for the note operations (select order, enter, move across a section boundary, delete, length, velocity,
undo), scenarios with screenshots (melody: select / edit / move / delete / erase-while-recording; Band: a bass note,
a drum hit), the manual (a "Fixing notes" section + credits), the cheat sheet's "How do I fix one wrong note".
