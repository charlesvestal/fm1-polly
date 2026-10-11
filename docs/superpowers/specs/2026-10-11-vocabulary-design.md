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
- **(yours)**: a groove you changed plays your copy: "Pop · Groove A (yours)"; BACK TO STYLE returns it.
  (Was: user pattern P01–P16; the P numbers are gone from the screen.)

**Settings**
- **Tempo**; **Transpose** (the song's, and a section's own on top; was KEY on a row); **Repeat**; **Mute**
  (instrument or melody; a section's mutes last for that section); **Free** (no progression: the band follows
  the chords you play; HOME's KNOB 4 "Prog: –").

**Tools**
- **Memory** (was: slots; "MEMORY FULL", SEL → MEMORY), **Undo / Redo**, **Click**, **Backup**, **Mirror**.

**Pages** (button: name): HOME: Home, then Song · ARP: Progression · SEQ: Band (a groove's notes, instrument
by instrument; was Pattern) · EDIT: Sound (an instrument's or melody's sound; was Track) · LFO: Mixer ·
FX: Effects (was Master FX) · ENV: Style · GLO: Setup.

**Retired from the screen**: track, pattern, user pattern, slot, loop, row, part, lead, Main A/B, key (for
transpose), mode.

## The hierarchy

```
Song
├─ Sections: progression + groove + ×repeat (+ mutes, transpose)
├─ Progressions P1–P16
├─ Melodies M1, M2
└─ Style: Groove A, Groove B, Intro, Fills, Ending → Instruments
```

- **On screen**: the tag line over the knobs is a breadcrumb of where you are: "S1 › Pop › Groove A ›
  C1 Piano" (Sound, Band), "S1 › P9 Verse" (Progression), "S1 › section 3" (Song), "S1 › M1" (a melody's
  Sound). Shortened from the left when long.
- **In the manual**: a cheat sheet near the top: the tree as a figure, one plain sentence per word, a "How do
  I…" table with the exact presses (change an instrument, mute, fix a chord, lengthen a progression, play from
  section 3, record a melody, undo, back up), the buttons one line each.

## Checks

A consistency review of every on-screen string (grep the firmware's strings and the scenarios' screenshots)
and the manual against this list; the suite green; screenshots read.
