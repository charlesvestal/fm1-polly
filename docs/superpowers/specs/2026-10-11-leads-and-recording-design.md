# Polly: two lead tracks, recording you can follow, 16-bar patterns (2026-10-11)

From the user's first session on the device with loops: recording into a track (EDIT, REC) came out
transposed (C1's type, Chord 1, moved the notes onto chord tones), and playing two loops' worth overdubbed
(the section's pattern, 1–2 bars, went round under the 4-bar loop, and nothing showed where it was). The user
asked for melody tracks that run along the song, not tied to loops, recorded "as played but noting the key it
was in, so if that chord changes, the melody changes with it"; two of them; and patterns as long as loops.

## Decisions (with the user)

- **As played, following the chord's root.** A note recorded keeps its pitch and remembers the root of the
  chord sounding when it was played; on playback it moves by how far the root has moved since (folded to
  −6..+5 semitones), so it keeps its shape. Same chord: exactly as played. TRANSPOSE and a row's KEY move it
  too. The chord's quality is not followed (Am → A keeps a C a C).
- **Two lead tracks per song, LD1 and LD2**, linear along the song, playing only in SONG.
- **Patterns up to 16 bars**, like loops.
- **Storage: the 16 user-pattern slots are shared** by user patterns and lead parts (the flash is full: the
  QY70 bank fills its regions). A song's leads take a slot when first recorded, a second when they outgrow it.

## 1. Recording into a pattern's track (SEQ / EDIT)

- **Record as played:** a track recorded into stores its notes by the **Parallel** rule (the transposer's
  existing type: moves with the root, keeps the intervals). The first REC into a track whose type is Chord 1,
  Chord 2 or Bass switches that track to Parallel and says so ("C1: AS PLAYED"); the Track page shows the type,
  and the user can set it back. Drum tracks (Bypass) stay as they are.
- **Where you are:** while REC is on (and waiting), SEQ and EDIT show the pattern being recorded as bars —
  the same bar view as HOME's chord recording (cells, the bar playing teal with its beats, notes as small
  ticks per bar rather than chord names) — with the line "REC C1 · bar 3/4 · pass 2", and from the second
  pass "pass 2: adding". A count-in to the next bar line before the first pass ("starts at the next bar", the
  beats left).
- **Patterns up to 16 bars** (`ARR_MAXBARS` 16; ticks are 16-bit: 16 bars of 7 beats = 53,760 < 65,536).
  BARS on Pattern setup goes to 16; the Pattern page draws up to 16 bars (two rows of 8, or scrolls). Notes
  per pattern stay 600. The factory and QY70 styles are unchanged (≤ 8 bars).

## 2. Lead tracks LD1, LD2

### Model

- Each song has two lead tracks. A lead note: its **row** (0..31), its position in the row (**beat × 480 +
  tick**, a row being its loop's length × REPEAT, up to 8 × 64 beats), its **length** in ticks, **pitch**,
  **velocity**, **track** (LD1 / LD2) and the **root** of the chord sounding when it was recorded. 9 bytes,
  ~420 a slot.
- **A note belongs to its row:** inserting or deleting a row moves the notes with their rows (the app
  renumbers them; a deleted row's notes go, after a question: "ROW 3 HAS LEAD NOTES: DELETE?"); a row made
  shorter keeps its notes past the end (not played). Rows copied (INSERT ROW copies the cursor's) do not copy
  lead notes.
- Each lead track has its sound and mixer like a pattern's track (program, channel, volume, pan, reverb,
  chorus, bright, reso), stored with the song's leads; defaults LD1 a lead synth (GM 81, channel 7), LD2 a
  string pad (GM 49, channel 8). Mutes: the song's (FREE / LOOP mutes don't apply: leads play only in SONG)
  and the row's mutes get two more bits (LD1, LD2: `row_t.mute` widens via its `rsv` byte).

### Storage: shared slots

- The 16 flash objects OBJ_PAT0..+15 become **slots**; each holds a user pattern or a song's lead part
  (a header says which: magic + kind + song + part number). `pattern_free()` and the copy-on-write rule count
  only slots holding no pattern and no lead part.
- A song's leads: part 1 in one slot (~420 notes), part 2 in another when it outgrows it (~840 notes in all at
  most). "NO FREE SLOT" when none is left (SEL's actions free them: CLEAR PATTERN / BACK TO STYLE on patterns,
  ERASE LEAD on leads).
- RAM: the playing song's leads in RAM (2 × 3.8 KB pool, `EN_POOL`), loaded with the song, saved at rest like
  the rest (autosave), only the parts that changed.
- Songs format 7: songs gain the leads' sounds and the slot numbers of their parts. A format 6 object is
  migrated (rows and loops kept; the new fields at their defaults, no lead parts): the user has songs on the
  device now.

### Engine

- A lead player beside the patterns: in SONG, for the row playing, the lead notes of that row whose position
  is due (beat since the row's start × 480 + tick, linear: a row of ×2 has two passes' worth of positions),
  each shifted by (root sounding now − its recorded root) folded to −6..+5,
  sent on the lead track's channel. Note-offs owed and MIDI out as for pattern notes. Notes sounding at a
  row change play to their end.
- Lead notes are read from the app's RAM copy with the same busy rule as loops (`arr_edit`), row by row
  (a pointer + count for the row playing, set at row start from a per-row index the app keeps).
- MIDI clock catch-up: positions jump with the place, no restrike (as patterns).

### Recording

- On **EDIT** (and SEQ), **ALGORITHM** goes on past C4 to **LD1, LD2**. With a lead selected the keys play it,
  the Track page shows its sound, the Mixer its strip.
- **REC with a lead selected:** armed; it records while a song plays (PLAY on a SONG page, or PLAY FROM ROW
  to punch in later). Not in FREE / LOOP: "LEADS RECORD IN A SONG". Notes are written to the row playing at
  their position, quantised by the Pattern page's GRID, with the root of the chord sounding. Overdub: a
  second pass adds (the same note at the same place replaces).
- **Where you are:** EDIT shows "REC LD1 · row 3 · bar 2.3" and the row as bars (the bar view) with the lead
  notes as ticks; the song's rows in a strip below (the one playing teal).
- **Erase:** SEL's list on EDIT with a lead selected: ERASE LD1 IN ROW n ↵ (the cursor row / the row
  playing), ERASE LD1 ↵ (the whole song's), each asking first.
- **Full:** "LEAD FULL" when the song's parts can't grow (no slot, or both parts full).

## Screens

- **EDIT** with a lead selected: the track's sound as today (program, type is "as played" fixed, low/high),
  plus the row bar view while recording. The tag says "LD1 LEAD SYNTH".
- **Mixer**: ten strips (D1…C4, LD1, LD2), or two pages of strips — fit the width.
- **SONG**: the MUTES column gets two more squares (LD1, LD2) after a gap; black keys 9 and 10 mute them.
- **HOME** in SONG: no change (leads play; the track list shows LD1 / LD2 when they have notes).

## Testing

- Engine (`tests/host/arr_test.c`): lead notes play at their row positions, across REPEAT, shifted by the
  root (C → F: up a fourth; same: as played), with TRANSPOSE and KEY; row mutes; a row change mid-note; the
  busy rule; clock catch-up.
- Loops/helpers (`tests/host/loops_test.c` or a new `leads_test.c`): renumbering on row insert/delete,
  the slot allocator (patterns and leads sharing; NO FREE SLOT), format 6 → 7 migration.
- Scenarios: `record.polly` (pattern recording as played, the bar view with passes, 16-bar pattern),
  `leads.polly` (record LD1 over a 4-row song, punch in at row 3, LD2 a second part, erase in a row, play back
  after a key change and a chord edit, save and reboot), screenshots of each new screen state.

## Out of scope

Following the chord's quality (thirds); leads in FREE / LOOP; more than two leads; leads in the QY70 bank.
