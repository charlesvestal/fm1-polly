# Polly: one word per idea, the hierarchy shown (2026-10-11)

The user: the terms overlap ("track, which can also mean song; pattern, which can also mean loop; style
which is really a … set of patterns?"); "we just need to be clear and consistent and show the hierarchy /
relations". Agreed vocabulary below. On screen (pages, tags, footers, knob labels, list rows, messages, help
cards), in the manual and the README. Code identifiers stay as they are.

## The words

**The song**
- **Song** (S1–S4): one piece of music; its own style, progressions, sections, melodies, tempo, transpose.
- **Section**: a stretch of the song (verse, chorus, bridge): which progression, played with which groove,
  ×repeat (+ mutes, transpose). A song is a list of sections, top to bottom. (Was: row.)
- **Progression** (P1–P16, "Prog" where short): your chords, 1–16 bars, going round. P1–P8 ready-made, P9–P16
  yours. (Was: loop, L1–L16.)
- **Chord**: one chord in a progression.
- **Melody** (M1, M2): your lines along the song's sections. (Was: lead, LD1, LD2.)

**The band**
- **Style**: the band's way of playing (Pop, Funk…), one per song.
- **Groove**: how the band plays a section — **Groove A** (calmer), **Groove B** (busier). (Was: Main A/B.)
- **Intro, Fill, Ending**: the style's extras, played by itself (start, groove changes or asked, end).
- **Instrument**: one player: D1 D2 drums, PC percussion, BA bass, C1–C4 chords / riffs / pads. Each a sound
  and its notes. (Was: track.)
- **Sound**: what an instrument or melody plays with, one of the 128 General MIDI sounds or the drum kit, named
  ("Celesta", "Drum kit"; the GM number small, "GM 9"). Sound page KNOB 1. (Was: Program.)
- **(yours)**: a groove you changed plays your copy; "(yours)" follows the groove's name on its pill ("Groove A
  (yours)") or the style's on Style ("Pop (yours)"); BACK TO STYLE returns it.
  (Was: user pattern P01–P16; the P numbers are gone from the screen.)

**Settings**
- **Tempo**; **Transpose** (the song's, and a section's own on top; was KEY on a row); **Repeat**; **Mute**
  (instrument or melody; a section's mutes last for that section); **Free** (no progression: the band follows
  the chords you play; HOME's KNOB 4 "Prog: –").

**Tools**
- **Memory** (was: slots; "MEMORY FULL", SEL → MEMORY), **Undo / Redo**, **Click**, **Backup**, **Mirror**.
- **Clear** is the one verb for taking notes or chords away (CLEAR CHORD, CLEAR PROGRESSION, CLEAR C1, CLEAR GROOVE A,
  CLEAR M1 IN SECTION 3, CLEAR SONG; "CLEARED"). (Was also: erase.)

**Short forms, one each**
- Songs **S1–S4** everywhere (lists, messages: "COPY SONG TO S2", "CLEAR S1?", "SONG S2"); progressions **P1–P16**
  (a progression's own default name shows as its number, "P9", not "Prog 9"); "Prog" where room is short.
- A place in bars: **bar 2.3** (bar 2, beat 3) by a cursor; **groove bar 1 / 2**, **prog bar 3 / 4** while
  recording; a song's place: **section 2 of 4 · repeat 1 of 2**.
- The encoders: **SELECT**, **ALGORITHM** (ALG on the encoders' line and in footers: "ALG: CHANGE"), **PRESETS**
  (PRE). Help cards spell them out.
- Values: Mute **muted** / **plays**; Type **Bypass, Parallel, Chord 1, Chord 2, Bass**, "as played"; Clock
  **internal** / **MIDI**; the song's end **next: stop**, "then stop". Name letters: **Place** (which), **Letter**
  (what). Effects' levels: **Level** (each right of its effect's knob).

**Pages** (button: name): HOME: Home, then Song (two pages) · ARP: Progression · SEQ: Band (a groove's notes,
instrument by instrument; was Pattern) · EDIT: Sound (an instrument's or melody's sound; was Track; a melody's is
one page) · LFO: Mixer · FX: Effects (was Master FX) · ENV: Style · GLO: Setup, then Status (was Panel: the
processor's load, the voices, the version).

**Retired from the screen**: track, pattern, user pattern, slot, loop, row, part, lead, Main A/B, key (for
transpose), mode, program, erase, panel (for the status page), "Song 1" (S1), char, "Rev lvl".

## The hierarchy

```
Song
├─ Sections: progression + groove + ×repeat (+ mutes, transpose)
├─ Progressions P1–P16
├─ Melodies M1, M2
└─ Style: Groove A, Groove B, Intro, Fills, Ending → Instruments
```

- **On screen**: the tag line over the knobs is a breadcrumb of where you are, and only that: "S1 › Pop › Groove
  A › C1 Piano" (Sound, Band), "S1 › P9 Verse" (Progression), "S1" (Song; Home in the song), "S1 › free", "S1 ›
  M1 Square Lead" (a melody's Sound). Shortened from the left when long. The place inside the page (a bar, a
  section) is said once, by the cursor (docs/superpowers/specs/2026-10-11-visibility-design.md), not in the tag.
- **In the manual**: a cheat sheet near the top: the tree as a figure, one plain sentence per word, a "How do
  I…" table with the exact presses (change an instrument, mute, fix a chord, lengthen a progression, play from
  section 3, record a melody, undo, back up), the buttons one line each.

## Checks

A consistency review of every on-screen string (grep the firmware's strings and the scenarios' screenshots)
and the manual against this list; the suite green; screenshots read.

## The screen audit (2026-10-11)

An audit of every screen asked of each element "what is it for, is it needed here, is it said twice?" and found the
words above used several ways; this list is the result (Program → Sound, Song 1 → S1, Prog 9 → P9, erase → clear,
Panel → Status, Char → Letter, the place formats). The header carries no knob's value (no style on Home, no
progression's name on Progression, no section count on Song); footers say what the keys and REC do, or nothing.
