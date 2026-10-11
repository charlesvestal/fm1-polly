# Polly: shared loops made safe, undo, a click, slots, backup, lead editing (2026-10-11)

Usability holes the user picked, in this order: 1 2 3 5 6 4. Polly is prerelease: no migrations (an older
saved format loads as defaults).

## 1. Each song owns its loops

The user: "Sharing loops makes no sense to me." Loops belong to a song: S1's L9 and S2's L9 are different
loops. Nothing is shared, so there is nothing to warn about.
- RAM holds the current song's 16 loops (as today's table; the engine's `arr_loops` pointer unchanged). The
  songs object (OBJ_SONGS) keeps the four songs' headers and rows only.
- A song's loops are stored in a **slot** (the shared 16, kind "loops", header: magic, kind, song), taken the
  first time that song's loops differ from the factory defaults (L1–L8 factory, L9–L16 empty) and saved when
  they change (autosave at rest). A song with default loops takes no slot. "NO FREE SLOT" when a song's
  loops need one and none is free (with the SLOTS hint, part 5).
- Switching songs (only at a stop, as today): the current song's loops are saved if changed, the new song's
  loaded (or the defaults). COPY SONG copies the loops too (a slot for the target if they're not the
  defaults). CLEAR SONG keeps the loops (as today: rows only) — add CLEAR SONG'S LOOPS? No: CLEAR LOOP per
  loop is enough.
- The "in N rows" header counts the current song only (as today, now exact).
- Format: songs object without loops (format 8); older formats load as defaults (prerelease, no migration).

## 2. Undo / redo

- **HOME + REC: undo; HOME + PLAY: redo** (X0X's). The header says what: "UNDO L9 CHORD", "UNDO ROW 3",
  "UNDO TAKE LD1", "UNDO CLEAR LOOP".
- What is covered: loops (chords, length, beats, name, load / clear / copy), the song's rows and settings,
  lead notes (a take is one step; erase), pattern edits stored by the app (sounds, mixer, types, erase /
  clear / back to style), and a recording pass into a pattern track (one step). Not the tempo while playing,
  not the Setup page.
- A step is what changed before a pause (~1 s without an edit) or one REC take; up to 32 steps, fewer after
  big ones. Kept in RAM (a journal of before-images of the bytes that changed, in the pool, ~16 KB), cleared
  at power-off; a song change clears it (the songs object stays undoable within the song).
- Undo while playing: allowed; loops and rows go through arr_edit as any edit.

## 3. A click

- Setup: **CLICK** off / rec / on (default rec: while REC is on or counting in). A short GM woodblock
  (high on beat 1, low on the others) on MIDI channel 16 (not muted by track mutes), level a Setup knob or
  fixed −6 dB under the band.
- REC's count-in (the bar before a take, a pattern pass or a song from a stop with REC armed) plays the click
  even when CLICK is off.

## 5. Slots: see what fills the 16

- SEL list action **SLOTS ▸** (on SEQ and EDIT): a list of the 16 slots — "P03 · Funk A · S1", "S2 leads 1/2",
  "free" — and on a selected slot **FREE ↵** (asks; a pattern a song plays goes back to its style; a lead
  part's notes are erased).
- "NO FREE SLOT" messages add the hint "SEL: SLOTS".

## 6. Backup and restore over USB

- A SysEx dump / load of the songs object and the 16 slots (and the project), using the existing SysEx
  receive path (bank_rx / M-UPGRADE style framing) and the TX ring; the device refuses a load while playing.
- `tools/polly_backup.py backup FILE` / `restore FILE` (mido, as qy70_install.py), and a "Backup / Restore"
  box on the web installer page later (not now).
- Restoring replaces everything (asks on the device: "RESTORE FROM USB?").
- Later, not now: export a song as a MIDI file.

## 4. Lead editing, small

- SEL on EDIT with a lead selected: **QUANTISE ROW n ↵** (the GRID), **UNDO TAKE** is undo (2).
- The lead's velocity: a VELOCITY knob on the Track page for a lead (all its notes recorded from now on;
  the keys have no velocity).

## Testing

Each part with host tests (engine where touched), scenarios with screenshots, the suite green; the device
check by the user.
