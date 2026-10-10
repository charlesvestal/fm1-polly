# Polly: chord loops, songs of loops, and X0X's UI (2026-10-10)

The user's questions: how would one write a song, and could one write one's own chord progression? Today the
16 progressions are fixed in the firmware, and a song is a 64-bar chord track edited over three unrelated
buttons (LFO Chords, ENV Arrange, ARP ARP Copy template). This design makes progressions the user's own
(**loops**), makes a song a chain of loops (as an X0X song chains patterns), and gives Polly X0X's UI
conventions.

Decided with the user: loops first, then chain them into a song; chords go in both step by step (stopped)
and live (playing); each song row picks its section, its mutes and its key; a chord played over a loop holds
until the loop's next change; SELECT turns pages; REC writes what the keys play, and REC on HOME in FREE
starts a new loop. Then a songwriting and UX pass (below, folded in), and a simplification: page buttons always
open their first page (SELECT alone turns pages; SONG is HOME's next pages), HOME's KNOB 4 is LOOP (no MODE
knob), and on SONG a white key always writes a row (REPEAT only by its knob; no SAVE + white key there). Then
three more: ARP opens the first empty loop until one is picked (never a factory progression by accident), a loop
is named right after it is written (ARP, SELECT, NAME), and PLAY on SONG always starts the song from row 1 (from a
loop playing: at the next bar line; another row: SEL's PLAY FROM ROW).

## Principles

1. **The screen shows what you hear.** Chords show transposed (TRANSPOSE and the row's KEY); the big chord
   is the one sounding; the loop being edited is the loop playing.
2. **The screen decides what the keys do**, the footer says it, and **REC records what the keys play there.**
3. **HOME + a key jumps** from any screen: a white key to a loop, a black key to a section.
4. **By hand wins until the next change.** In LOOP and SONG, a chord, a mute or a section you set by hand
   lasts until the loop or song next changes it (the loop's next chord; the song's next row).
5. **SAVE + a white key copies what the screen shows** into that slot (X0X's copy and paste): a loop on HOME and
   LOOP, a pattern on SEQ. On SONG it does nothing (a white key adds a row by itself).
6. **Words by place:** knobs are nouns (ROOT, REPEAT); list rows are verbs with ↵ (CLEAR LOOP); messages say
   what happened (L3 COPIED TO L7), all in upper case (LOOP L1 POP), as the tag and the footer; titles (the
   header, a loop's name on its screen) mixed case. A loop with its default name is "L9" alone, not "L9 Loop 9".
7. **One name per thing:** loops L1–L16, user patterns P01–P16, songs S1–S4; sections Intro, Main A,
   Main B, Fill, Ending (Fill AB / Fill BA only where the difference matters).

## Model

### Loops

- 16 loops, L1–L16. A loop: 1–16 bars of 2–7 beats, a chord per beat (or "·", no change), a name of 8
  characters.
- **L1–L8 start as eight factory progressions** (Pop, Ballad, Fifties, ii–V–I, Blues, Minor, Flamenco,
  Canon; a progression's name is a loop's, 8 characters at most); **L9–L16 start empty**, for your own. All 16
  factory progressions (`dsp/templates.c`) stay in flash: the list action **LOAD PROGRESSION ↵** puts any of
  them into the current loop as a start. FACTORY RESET puts L1–L8 back and empties L9–L16.
- **Writing past a loop's end makes it longer**, a bar at a time (an empty loop has length 0 and grows as you
  write). LENGTH trims.
- A loop holds at most 64 beats: 16 bars of 2–4 beats, 12 of 5, 10 of 6, 9 of 7. LENGTH stops at the limit
  for the loop's BEATS, and raising BEATS shortens LENGTH to fit ("LENGTH 9: 64 BEATS MAX").
- Chords are stored at real pitch. What plays is the chord + the song's TRANSPOSE (+ the row's KEY in
  SONG). Writing stores the chord minus those, so playback matches what was played; the screens show what
  sounds (principle 1).
- Storage: 16 loops × 64 beats, a chord packed into 2 bytes (root 4 bits, type 5, bass 4 + "none"): 2 KB,
  plus names and lengths. The songs' per-beat chord tracks (4 × 64 × 4 × 3 bytes + section marks, ~3.4 KB)
  go, so RAM goes down.

### Songs

- 4 songs, S1–S4. Each keeps its own **tempo, TRANSPOSE, style** (and its six section patterns), its
  **mutes** for FREE and LOOP, and its rows; picking a song brings them all back.
- Up to 32 **rows**. A row: a loop, a SECTION (Main A, Main A + fill, Main B, Main B + fill), REPEAT ×1–×8,
  its **mutes** (8 bits, D1 … C4) and its **KEY** (−6..+5 over the song's TRANSPOSE: a last chorus a tone up).
  5 bytes a row, 640 bytes for all.
- Row mutes: the same loop and pattern with tracks silent (a verse with drums and bass only, then the full
  band). In SONG they replace the song's mutes; in FREE and LOOP, SEQ's MUTE sets the song's mutes, across
  every section, as today.
- Fills: when the section changes between rows, the matching fill (AB or BA) plays in the row's last bar by
  itself; a "+ fill" section forces one without a change.
- Per song: **INTRO** on/off, **ENDING** on/off, **AT END** stop / again.
- A song's tempo follows the style picked until it is set by hand (KNOB 2, HOME + SELECT; a flag saved with the
  song); from then on a style picked keeps it.
- Songs format 5. Polly is unreleased: no migration from format 4 (the old songs are dropped and the
  factory songs written).

### What plays: FREE, a loop, the song (HOME's KNOB 4, LOOP)

The engine keeps three modes; the UI has no MODE knob. HOME's KNOB 4 is **LOOP**: "–" or L1–L16.

- **FREE** ("–", at start-up): no loop; the band follows your chords, holding the last one you played (before
  any: C, the patterns as written). HOME + black key picks the section.
- **A loop** (L1–L16): that loop goes round; turned while playing, from the next bar line (as HOME + a white key).
- **The song**: PLAY on a SONG page plays it; LOOP then shows "Song", and turning it leaves the song for the loop
  or FREE turned to (from the loop the song plays: stopped, the cursor's row's).
- By hand wins until the next change (principle 4):
  - a chord played on the keys sounds, in the band's rhythm, until the loop's next chord change; the screen
    shows it in coral, then the loop's again in teal;
  - in SONG, a mute (SEQ's MUTE) or a section (HOME + black key) lasts until the next row.
- HOME + a white key picks that loop (queued to the next bar while playing); in SONG it leaves the song (with REC
  on, it writes the row instead). HOME + the lit white key stops the loop: FREE.
- **PLAY** on a SONG page: the song not playing, it starts from row 1, whatever the cursor: stopped, at once
  (with its Intro when on); while a loop or FREE plays, from row 1 at the next bar line (no stop needed, no
  Intro; "NEXT ROW 1"). The song playing, PLAY stops it. Another row: SEL's **PLAY FROM ROW n ↵** (the cursor's
  row; playing, from the next bar line). Anywhere else, PLAY starts and stops (in the song: from row 1). While the
  song plays, SONG's cursor follows it until you turn ROW; when the song stops by itself (its Ending, its end)
  the cursor goes back to row 1.
- **PLAY in FREE and in a loop starts on Main A**, never the Intro by itself: the Intro plays in SONG with INTRO on,
  or when picked first (HOME + black 1).
- Stopped, HOME's big chord is the one PLAY starts on (the loop's first; in SONG row 1's), and a chord held on
  the keys sounds while held (C1's sound): step writing is not blind.

## Buttons

A page button always opens its screen's first page; **SELECT**, and only SELECT, turns the pages (no "pressed
again: the next page", no memory of the last page). HOME pressed on HOME stays there; from SONG's pages it goes
back to HOME. The tag's dots show the button's pages (HOME: four, HOME and SONG's three).

| Button | Screen (pages) | KNOB 1–4 | PRESETS (the next one of what the screen shows) |
|---|---|---|---|
| HOME | **HOME**; SELECT: **SONG** (3 pages) | STYLE · TEMPO · TRANSPOSE (the song's) · LOOP | loop |
| ARP | **LOOP** (the loop playing, or the last picked; none picked yet since start-up: the first empty loop, so the grid never overwrites a factory progression by accident); page 2 | ROOT · TYPE · BASS · STEP; LENGTH · BEATS · NAME · – | loop (queued: you edit what you hear) |
| SEQ | Pattern (8 tracks of the section playing); page 2 Pattern setup | GRID · – · – · MUTE; BARS · BEATS · LETTER · CHARACTER | the section's pattern |
| EDIT | Track; page 2 Track keys | PROGRAM · TYPE · LOW · HIGH; HIGH KEY · CHANNEL | instrument family |
| LFO | Mixer; page 2 Tone | VOLUME · PAN · REVERB · CHORUS; BRIGHT · RESO | – |
| FX | Master FX | REVERB · LEVEL · CHORUS · LEVEL | – |
| ENV | **STYLE**: the six sections' patterns (two pages) | INTRO · MAIN A · MAIN B · STYLE; FILL AB · FILL BA · ENDING · STYLE | style |
| GLO | Setup; page 2 Panel | CLOCK · MIDI OUT · LIGHTS · –  | – |
| SONG (HOME, SELECT) | the rows; page 2 the row; page 3 the song | ROW · LOOP · SECTION · REPEAT; ROW · KEY · – · –; INTRO · ENDING · AT END · SONG | row |

- **NAME** (LOOP page 2) turns through Verse, Chorus, Bridge, Pre, Intro, Outro, Solo, Break, then "Loop 9"
  (its number). Letter by letter: the SEL list's MORE.
- **TYPE** below maj is "·": the chord is cleared (no change).
- **SELECT**: the previous / next page; in a list, up and down.
- **ALGORITHM**: the track (D1 … C4) on any screen; in a list, changes the value.
- **SEL**: the screen as a list of everything on it, then **MORE** (settings on no page), and actions (↵):
  LOAD PROGRESSION, CLEAR LOOP, PLAY FROM ROW n, INSERT ROW, DELETE ROW, CLEAR SONG, COPY SONG TO, ERASE TRACK, CLEAR PATTERN,
  BACK TO STYLE. Today's SAVE-held jobs move here. In a question SEL is yes, HOME is no (HOME's press, held or
  not; held, its key shortcuts then work); the footer says SEL: YES · HOME: NO as the question appears.
  INSERT ROW copies the cursor's row.
- **SAVE**: save. **Hold SAVE + a white key** (principle 5): on HOME and LOOP, copy the loop playing to that
  loop (L3 COPIED TO L7); on SEQ, copy the pattern to P01–P16 (P03 COPIED TO P07). It asks first if the target
  isn't empty. On SONG: nothing.
- **OCT− / OCT+**: the keys' octave; on LOOP, the cursor: OCT+ **skips** a step (leaves "·", the chord before
  goes on), OCT− steps back. Both on STEP's grid of each bar (4.4 with STEP bar: OCT− 4.1; 3.2 with half: OCT+
  3.3). STEP is a bar again when another loop is picked; when it isn't a bar the tag says so (L10 · BAR 1.1 ·
  HALF).
- **Hold HOME** + white key: a loop (above); + black key 1–5: Intro, Main A, Main B, Fill, Ending; + SELECT:
  tempo; + PRESETS: the song. While HOME is held, the screen shows the 16 loops by name ("—" for empty) and
  the five sections, the playing / next ones outlined (today's SEL-held overlay, moved).
- **Hold any button alone for a second**: a card says what it does on this screen. Letting go does nothing.
- **REC** (principle 2), acting on its release (held, its card; letting go then starts nothing):
  - HOME, LOOP: writes chords into the loop playing. Stopped: at the cursor, which moves on a STEP (bar, half,
    beat) when all keys are up; playing: on the nearest beat, overdubbed. Past the end, the loop grows. REC
    off at the end: the cursor on the start of the last bar written (a knob there changes that bar's chord).
  - HOME in FREE: starts a new loop in the first empty slot. Play chords with the band; REC again ends it,
    rounded up to whole bars (max 16). The new loop plays, LOOP shows it ("NEW LOOP L9"). None empty:
    "NO EMPTY LOOP: CLEAR ONE (SEL)".
  - HOME, LOOP in the song: the row's loop plays as a loop first ("SONG PAUSED: EDITING L9"); PLAY on SONG
    goes back to the song.
  - SEQ, EDIT: records the selected track (as today).
  - SONG: with PLAY, writes rows (from row 1; PLAY FROM ROW: the cursor's; REC pressed while a loop plays: the
    cursor's, from the next bar line): the loops and sections you pick with HOME + keys, and
    the mutes you change.
  - LFO, FX, ENV, GLO: "REC: not here".

## Keys and footer

| Screen | Keys | Footer |
|---|---|---|
| HOME | chords | `KEYS: CHORDS · REC: WRITE` (FREE: `REC: NEW LOOP`) |
| LOOP | chords | `KEYS: CHORDS · REC: WRITE · OCT+: SKIP` |
| SONG | white: on the END row a new row of that loop; on a row, that row's loop (the row set anew); the cursor to the row after. **Never a REPEAT**: the same key again is a row of its own (REPEAT is the knob's). Black 1–8: the row's mutes, D1 D2 PC BA C1 C2 C3 C4 (a muted track's key glows dim; on the END row: the last row's, the one just added) | `WHITE: ROW'S LOOP · BLACK: MUTES` |
| SEQ, EDIT | the selected track's sound | `KEYS: C1 PIANO · REC: RECORD` |
| LFO | black keys pick the track | `BLACK: TRACK` |
| FX, ENV, GLO | nothing | (the screen's hint) |

- A new row's SECTION is the one the same loop had in an earlier row, else Main A: set the chorus to Main B
  once and later chorus rows follow. Changing a row's Main (A / B) also changes the loop's other rows on the old
  one ("L10 ROWS: MAIN B"; a "+ fill" stays the row's own). Two rows of the same loop with different mutes:
  tap its white key again (a row of its own), INSERT ROW, or the LOOP knob.
- Until the first button press the footer says `HOLD ANY BUTTON: WHAT IT DOES`.

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
 STYLE    TEMPO   TRANSPOSE   LOOP
 Pop      120     C           L3
KEYS: CHORDS · REC: WRITE
```

At start-up (FREE, nothing played) the box says **Play a chord: 3 keys**, the strip "FREE" and LOOP "–". In SONG the strip
shows the row, its loop and REPEAT count (row 2 of 4, ×1 of 2).

```
LOOP  L3 Verse   in 3 rows   ● 120 ▶
 1 │ C     ·     ·     ·
 2 │ G     ·    [F]    ·          the cursor
 3 │ Am    ·     ·     ·
 4 │ F     ·     ·     ·
────────────────────── L3 · BAR 2.3  ● ○
 ROOT    TYPE    BASS    STEP
 F       maj     –       beat
KEYS: CHORDS · REC: WRITE · OCT+: SKIP
```

"in 3 rows": the rows of the current song that play this loop (editing changes them all; SAVE + a white key
makes a variant loop). Bars of 5–7 beats wrap to two lines.

```
SONG  S1                ● 120
 #  LOOP        SECTION       REPEAT  MUTES      KEY
 1  L9 Verse    Main A        ×2      ■■□■□□□□
 2  L10 Chorus  Main B +fill  ×1      ■■■■■■■■
▶3  L9 Verse    Main A        ×2      ■■■■■■■□
 4  L10 Chorus  Main B        ×2      ■■■■■■■■   +2
 5  END
 Intro on · Ending on · at end: stop
────────────────────── ROW 3  ● ○ ○
 ROW     LOOP    SECTION  REPEAT
WHITE: ROW'S LOOP · BLACK: MUTES
```

(■ plays, □ muted; KEY shown when not 0.)

## A song, start to finish (the walk this design is checked against)

MANUAL.md's "Writing a song", run as `tests/scenarios/walkthrough.polly`:

1. Groove: KNOB 1 Funk, PLAY, a chord.
2. Verse: REC, Am F C G a bar each, REC: L9 plays; named: ARP, SELECT, NAME Verse.
3. Chorus: HOME, LOOP to "–", REC, C G Am F, REC: L10; named: ARP, SELECT, NAME Chorus.
4. Song: HOME, SELECT; white 9, 10, 9, 10: four rows.
5. Row 2 SECTION Main B (row 4 follows), REPEAT ×2; row 4 page 2 KEY +2; row 3 black 1, 2 (no drums).
6. PLAY on SONG: the song takes over from the loop at the next bar line, row 1: rows with fills, Ending.
7. Stopped: saved by itself (or SAVE).

40 actions (a knob turn, a key or a chord counted once; SAVE included), against ~64 bars of cursor and knob work
before loops.

## Engine

- `arr` plays a chord source: none (FREE), a loop (index, transpose), or a song (rows → loops, sections,
  fills, mutes, keys, intro, ending), replacing the template and song chord-track paths (`arr_template`, the
  song's `chord[]` / `sect[]`).
- A hand override (chord; in SONG also mutes and section) is cleared at the source's next change (chord
  change; row change).
- Intros and endings still hold a chord: the first row's first chord (in its key), and the last chord sounding.
- Loops are saved with the songs (one object, format 5); copy-on-write user patterns are unchanged.

## Testing

- Host tests (`tests/host/arr_test.c`): loop playback and wrap, 3/4 and 7-beat loops, growth past the end,
  the 64-beat limit, transpose and row KEY (and writing through them), the chord override cleared at the next
  change, song rows with REPEAT, the row's section default, a white key a row (never a REPEAT), row mutes and hand mutes /
  sections lasting to the next row, automatic and forced fills, intro / ending, AT END, step writing with
  skip, live writing, FREE → new loop (length rounding, no empty slot), REC in SONG pausing to LOOP.
- Scenarios (`tests/scenarios/`): rewrite `chords.polly`, `compose.polly`, `tour.polly`, `manual.polly` for
  the new buttons; new `loops.polly` and `song.polly` (the walk above); screenshots of HOME (FREE, LOOP,
  SONG), LOOP, SONG's three pages, the HOME-held overlay, a help card, a list.
- Storage test: format 5 round trip (loops, rows, per-song tempo / transpose); a format 4 file gives the
  factory songs and loops.
- Device: CPU on the Panel with a song playing; save while stopped.

## Docs

MANUAL.md rewritten for the new model (quick start: style, PLAY, chords; then your own loop; then a song, the
walk above), a labelled panel drawing as X0X's (`docs/img/manual/`), the page-by-page table, and the figures
regenerated.

## Out of scope

Undo (X0X's HOME + REC); knob motion; more than 4 songs or 16 loops; a third main section (variety comes from
row mutes and KEY).
