# Polly: chord loops, songs of loops, and X0X's UI (2026-10-10)

The user's questions: how would one write a song, and could one write one's own chord progression? Today the
16 progressions are fixed in the firmware, and a song is a 64-bar chord track edited over three unrelated
buttons (LFO Chords, ENV Arrange, ARP ARP Copy template). This design makes progressions the user's own
(**loops**), makes a song a chain of loops (as an X0X song chains patterns), and gives Polly X0X's UI
conventions.

Decided with the user: loops first, then chain them into a song; chords go in both step by step (stopped)
and live (playing); each song row picks its section; a chord played over a loop holds until the loop's next
change; SELECT turns pages; REC writes what the keys play, and REC on HOME in FREE starts a new loop.

## Principles

1. **The screen shows what you hear.** Chords show transposed when TRANSPOSE is set; the big chord is the one
   sounding; the loop being edited is the loop playing.
2. **The screen decides what the keys do**, the footer says it, and **REC records what the keys play there.**
3. **HOME + a key jumps** from any screen: a white key to a loop, a black key to a section.
4. **Words by place:** knobs are nouns (ROOT, REPEAT); list rows are verbs with ↵ (CLEAR LOOP); messages say
   what happened (L3 COPIED TO L7).
5. **One name per thing:** loops L1–L16, user patterns P01–P16, songs S1–S4; sections Intro, Main A,
   Main B, Fill, Ending (Fill AB / Fill BA only where the difference matters).

## Model

### Loops

- 16 loops, L1–L16. A loop: 1–16 bars of 2–7 beats, a chord per beat (or "·", no change), a name of 8
  characters.
- The 16 factory progressions (`dsp/templates.c`) become the loops' factory contents (FACTORY RESET, and a
  loop's list action RESET LOOP ↵). The 12-bar blues needs 12 bars, hence the 16-bar limit.
- Chords are stored at real pitch. **TRANSPOSE** (−6..+5, today's Key) shifts everything that plays: loops,
  songs and live chords. Writing stores the chord minus TRANSPOSE, so playback matches what was played, and
  the screens show stored + TRANSPOSE (principle 1).
- A loop holds at most 64 beats: 16 bars of 2–4 beats, 12 of 5, 10 of 6, 9 of 7. LENGTH stops at the limit
  for the loop's BEATS, and raising BEATS shortens LENGTH to fit ("LENGTH 9: 64 BEATS MAX").
- Storage: 16 loops × 64 beats, a chord packed into 2 bytes (root 4 bits, type 5, bass 4 + "none"): 2 KB,
  plus names and lengths. The songs' per-beat chord tracks (4 × 64 × 4 × 3 bytes + section marks, ~3.4 KB)
  go, so RAM goes down.

### Songs

- 4 songs, S1–S4. A song: up to 32 **rows**; a row is a loop, a SECTION (Main A, Main A + fill, Main B,
  Main B + fill), REPEAT ×1–×8, and its **mutes** (8 bits, D1 … C4; 1 byte a row).
- Row mutes: the same loop and pattern with tracks silent (a verse with drums and bass only, then the full
  band). In SONG they replace the song-wide mutes; in FREE and LOOP, SEQ's MUTE works as today, across
  every section. A mute changed by hand while a song plays lasts until the next row; with REC on, it is
  written into the row (as X0X records mutes into song bars).
- Fills: when the section changes between rows, the matching fill (AB or BA) plays in the row's last bar by
  itself; a "+ fill" section forces one even without a change.
- Per song: **INTRO** on/off, **ENDING** on/off, **AT END** stop / again; its style and section patterns
  (the song-wide mutes go: rows carry them).
- Songs format 5. Polly is unreleased: no migration from format 4 (the old songs are dropped and the
  factory songs written).

### MODE (HOME's KNOB 4)

- **FREE** (at start-up): no loop; the band holds the last chord you played. HOME + black key picks the
  section.
- **LOOP**: the selected loop goes round.
- **SONG**: the song plays its rows from row 1 (or the selected row on the SONG screen).
- In LOOP and SONG, a chord played on the keys **sounds until the loop's next chord change**, in the band's
  rhythm, and the screen shows it (coral); then the loop carries on (teal).
- HOME + a white key picks that loop and switches to LOOP (queued to the next bar while playing). HOME + the
  lit white key stops the loop: FREE.

## Buttons

A button always opens the same screen; pressing it again, or turning **SELECT**, gives its next page.

| Button | Screen (pages) | KNOB 1–4 | PRESETS (the next one of what the screen shows) |
|---|---|---|---|
| HOME | **HOME**; again **SONG**, again back | STYLE · TEMPO · TRANSPOSE · MODE | loop |
| ARP | **LOOP** (the loop playing); page 2 | ROOT · TYPE · BASS · STEP; LENGTH · BEATS · LETTER · CHARACTER | loop (queued: you edit what you hear) |
| SEQ | Pattern (8 tracks of the section playing); page 2 Pattern setup | GRID · – · – · MUTE; BARS · BEATS · LETTER · CHARACTER | the section's pattern |
| EDIT | Track; page 2 Track keys | PROGRAM · TYPE · LOW · HIGH; HIGH KEY · CHANNEL | instrument family |
| LFO | Mixer; page 2 Tone | VOLUME · PAN · REVERB · CHORUS; BRIGHT · RESO | – |
| FX | Master FX | REVERB · LEVEL · CHORUS · LEVEL | – |
| ENV | **STYLE**: the six sections' patterns (two pages) | INTRO · MAIN A · MAIN B · STYLE; FILL AB · FILL BA · ENDING · STYLE | style |
| GLO | Setup; page 2 Panel | CLOCK · MIDI OUT · LIGHTS · –  | – |
| SONG (HOME ×2) | the song's rows; page 2 | ROW · LOOP · SECTION · REPEAT; INTRO · ENDING · AT END · SONG | row |

- **SELECT**: the previous / next page; in a list, up and down.
- **ALGORITHM**: the track (D1 … C4) on any screen; in a list, changes the value.
- **SEL**: the screen as a list of everything on it, then **MORE** (settings on no page), and actions (↵):
  CLEAR LOOP, RESET LOOP, INSERT ROW, DELETE ROW, CLEAR SONG, COPY SONG TO, ERASE TRACK, CLEAR PATTERN,
  COPY PATTERN TO, BACK TO STYLE. Today's SAVE-held jobs move here. In a question SEL is yes, HOME is no.
- **SAVE**: save. **Hold SAVE + a white key**: copy the loop playing to that loop (L3 COPIED TO L7). It asks
  first if the target has chords.
- **OCT− / OCT+**: the keys' octave; on LOOP, the cursor back / forward a step.
- **Hold HOME** + white key: a loop (above); + black key 1–5: Intro, Main A, Main B, Fill, Ending; + SELECT:
  tempo; + PRESETS: the song. While HOME is held, the screen shows the 16 loops by name and the five
  sections, the playing / next ones outlined (today's SEL-held overlay, moved).
- **Hold any button alone for a second**: a card says what it does on this screen. Letting go does nothing.
- **REC** (principle 2):
  - HOME, LOOP: writes chords into the loop playing. Stopped: at the cursor, which moves on a STEP (bar, half
    bar, beat) when all keys are up; playing: on the nearest beat, overdubbed.
  - HOME in FREE: starts a new loop in the first empty slot. Play chords with the band; REC again ends it,
    rounded up to whole bars (max 16). The new loop plays, MODE becomes LOOP ("NEW LOOP L9").
  - SEQ, EDIT: records the selected track (as today).
  - SONG: with PLAY, writes rows from what you play with HOME + keys (loop, section) and the mutes you change, from the
    selected row.
  - LFO, FX, ENV, GLO: "REC: not here".

## Keys and footer

| Screen | Keys | Footer |
|---|---|---|
| HOME, LOOP | chords | `KEYS: CHORDS · REC: WRITE` (LOOP adds `OCT: ◀ ▶`) |
| SONG | white: the row's loop, then the next row (adding rows at the end); black 1–8: the row's mutes, D1 D2 PC BA C1 C2 C3 C4 (a muted track's key glows dim) | `WHITE: ROW'S LOOP · BLACK: MUTES` |
| SEQ, EDIT | the selected track's sound | `KEYS: C1 PIANO · REC: RECORD` |
| LFO | black keys pick the track | `BLACK: TRACK` |
| FX, ENV, GLO | nothing | (the screen's hint) |

Until the first button press the footer says `HOLD ANY BUTTON: WHAT IT DOES`.

CHORD mode, SEL's tap toggle and SEL-held shortcuts are gone; the Templates, Copy template, Chords and
Arrange pages are gone (LOOP and SONG replace them).

## Screens

Polly keeps its look (cream, coral, teal, Barlow, mixed case). From X0X: a **tag** over the knob row saying
whose they are (L3 · BAR 2.3, ROW 3, C1 PIANO, MIXER: BA) with the page dots, the **big readout** of a turned
knob for a second, and the **footer**. The header: screen name, S/L, a dot while unsaved, tempo (`ext` on
MIDI clock), ▶ / ● (recording).

```
HOME  S1 · L3 Verse     ● 120 ▶
Pop · Main A           bar 2.3 / 4
        ┌────────┐
        │   F    │   coral: yours, until the loop's next change
        └────────┘
   L3   C │ G · F · │ Am │ F        next: Am
                             next section: Main B
────────────────────── L3 VERSE  ● ○
 STYLE    TEMPO   TRANSPOSE   MODE
 Pop      120     C           LOOP
KEYS: CHORDS · REC: WRITE
```

```
LOOP  L3 Verse          ● 120 ▶
 1 │ C     ·     ·     ·
 2 │ G     ·    [F]    ·          the cursor
 3 │ Am    ·     ·     ·
 4 │ F     ·     ·     ·
────────────────────── L3 · BAR 2.3  ● ○
 ROOT    TYPE    BASS    STEP
 F       maj     –       beat
KEYS: CHORDS · REC: WRITE · OCT: ◀ ▶
```

```
SONG  S1                ● 120
 #  LOOP        SECTION       REPEAT  MUTES
 1  L1 Verse    Main A        ×2      ■■□■□□□□
 2  L2 Chorus   Main B +fill  ×1      ■■■■■■■■
▶3  L1 Verse    Main A        ×2      ■■■■■■■□
 4  END
 Intro on · Ending on · at end: stop
────────────────────── ROW 3  ● ○
 ROW     LOOP    SECTION  REPEAT
WHITE: ROW'S LOOP · BLACK: MUTES
```

(■ plays, □ muted.)

In FREE, HOME's strip shows "FREE: play chords" in place of the loop. In SONG, it shows the row, its loop
and REPEAT count (row 2 of 4, ×1 of 2).

## Engine

- `arr` plays a chord source: none (FREE), a loop (index, transposed), or a song (rows → loops, sections,
  fills, intro, ending), replacing the template and song chord-track paths (`arr_template`, the song's
  `chord[]` / `sect[]`).
- A live chord in LOOP / SONG sets an override that the source's next chord change clears.
- Intros and endings still hold a chord: the first loop's first chord, and the last chord sounding.
- Loops are saved with the songs (one object, format 5); copy-on-write user patterns are unchanged.

## Testing

- Host tests (`tests/host/arr_test.c`): loop playback and wrap, 3/4 and 7-beat loops, transpose, override
  cleared at the next change, song rows with REPEAT, row mutes (and a hand mute
  lasting to the next row), automatic and forced fills, intro / ending, AT END,
  step and live chord writing, FREE → new loop length rounding.
- Scenarios (`tests/scenarios/`): rewrite `chords.polly`, `compose.polly`, `tour.polly`, `manual.polly` for
  the new buttons; new `loops.polly` and `song.polly`; screenshots of HOME, LOOP, SONG, the HOME-held
  overlay, a help card, a list.
- Storage test: format 5 round trip; a format 4 file gives the factory songs.
- Device: CPU on the Panel with a song playing; save while stopped.

## Docs

MANUAL.md rewritten for the new model (quick start: style, PLAY, chords; then loops, then songs), a labelled
panel drawing as X0X's (`docs/img/manual/`), the page-by-page table, and the figures regenerated.

## Out of scope

Undo (X0X's HOME + REC); knob motion; per-row TRANSPOSE; more than 4 songs.
