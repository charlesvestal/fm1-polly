# Polly: a pocket arranger for the FM-1 (after the Yamaha QY70) — design

Status: draft for review, 2026-10-08. Repo: `fm1-gm` (the GM sound module grows into this; renamed
`fm1-polly` when the arranger lands). Name: **Polly** (no Yamaha or QY in it).

## What it is

A pocket songwriting machine in the spirit of the QY70: you write short multi-track **patterns** in C, and a
**chord track** re-voices them as it plays, so one loop becomes a whole song. Played live, you hold chords
on the keys and the band follows. Sound: the General MIDI set already running on the FM-1 (Sonivox EAS,
`dsp/gm.c`), so patterns and songs also drive any GM gear over MIDI.

What the research says matters most (see Research below): the independence of style and chords, instant
gratification (pick a pattern, pick a progression, PLAY), and finishing a song in your hands. What it says
to avoid: menu diving, slow browsing, fiddly editing, and an unlit tiny screen. The FM-1 brings a colour
screen and seven encoders, so every common value gets a knob.

## Scope

**v1:**
- 16 user patterns (24 for a day, 2026-10-09; the rest of their flash holds the QY70 style bank), each 1-8 bars,
  8 tracks. A new song plays a style; a section gets a user pattern of its own when it is first recorded into
  or edited (copy-on-write, from the section as it plays)
- 16 factory styles, well made and our own (written for Polly, not taken from Yamaha's ROM: its styles are
  Yamaha's copyright), each with an Intro, Main A, Main B, Fill AB, Fill BA and an ending
- the transposition engine: five track types (Bypass, Chord 1, Chord 2, Bass, Parallel) with range limits
- chords: 26 types plus slash bass; entered live (held chords recognised) or step by step; one per beat
- 6 sections per song (the QY70's, added 2026-10-09): Intro, Main A, Main B, Fill AB, Fill BA, Ending
- 4 songs: a section track and a chord track per song, 64 bars
- 16 chord templates (progressions in C) for instant play
- realtime loop recording into a pattern track (overdub, quantised), track erase
- mixer per track: GM program, volume, pan, reverb, chorus, mute
- MIDI: clock in / out, all tracks out on their channels, MIDI in plays the synth (as the GM module)

**Later (not v1):** step entry of notes, the 16 free song tracks, groove templates,
event editing, SMF export, a MIDI-file importer (a multitrack loop in, a pattern out: your own material),
a QY700 style-file importer (the format undocumented: check what the community has mapped first; for
styles you made yourself: Yamaha's downloadable converted QY70 styles forbid modification and derivative
works, so Polly never ships or converts them), phrase library.

## Architecture

```
dsp/gm.c            the GM synth (exists): Sonivox EAS, a MIDI queue, rendered in the audio ISR
dsp/arr.c, arr.h    the arranger engine (new), ticked from the audio render like Life's sequencer:
                      clock (internal, or MIDI clock in), pattern playback, the chord track, sections,
                      the transposer, note lifetimes, MIDI out; posts notes straight to gm.c
dsp/xpose.c, .h     the transposer (new, pure C, host-tested): (note, track type, source chord,
                      target chord, previous output, limits) -> note or none
dsp/chords.c, .h    chord types, names, recognition from held notes (pure C, host-tested)
app/ui.c            pages, input, drawing
app/project.c       storage: settings, patterns, songs, across several flash objects
```

Same threading model as Life and Eno: the engine ticks inside the audio render (every 128 synth frames,
5.8 ms; or finer — see Timing); the UI posts commands into a queue; the engine publishes state for the
screen (positions, current chord, what each track is playing).

### Timing

480 PPQ internally (the QY70's resolution, so its styles and a drummer's feel keep their timing; was 24, the MIDI
clock's, until 2026-10-09): recorded notes are quantised to the 1/16 grid by default (1/8T, 1/16T, 1/32 or off
as a setting: off keeps the tick). MIDI clock in and out: 20 ticks a clock, the ticks between two clocks in
spread at the clock's measured rate. The engine ticks at the clock's
rate inside the render: the render is split at tick boundaries, as Eno's is (`en_render`), so timing is
sample-accurate rather than block-quantised.

### Data

- **Pattern**: up to 8 bars x 8 tracks. Per track: GM program, type, source chord (fixed: CM7 in v1, as
  Yamaha's), low / high limit, high key, channel, and its notes as events (start tick, length, note,
  velocity; 6 bytes). Budget 512 notes per pattern (~3.2 KB): one flash object per pattern.
- **Song**: 64 bars; per bar a section (Intro, A, B, Fill AB, Fill BA, End, or same); per beat a chord (root, type, slash bass,
  or "same"): ~0.6 KB.
- **Settings**: the mixer is per pattern; tempo, key transpose, MIDI and lights are global.
- **Flash**: Felucca's data area (0x97000..0xDFFFF) holds ~30 objects of 3.75 KB (two sectors each, torn-
  write safe): 16 patterns (0x99000..0xB9000) + 1 for the songs + 1 for settings fits, with room. The 16
  factory styles (96 patterns: the six sections each) live in the firmware image, ~43 KB (a user pattern
  starts as a copy of one, or empty).
- **Sections**: a pattern *is* a section's music; each song holds six pattern numbers (its "style"): Intro,
  Main A, Main B, Fill AB, Fill BA, Ending; its section track says which section plays. The Intro plays once
  at PLAY, then Main A; Fill AB plays a bar from A then B, Fill BA from B then A (the Fill command picks by the
  Main playing); Ending plays once and stops. A section without notes falls back (no Fill BA: Fill AB's, and
  the other way round; no Intro: Main A; no Ending: stop at the bar line).

## The transposer (the heart)

Patterns are written over the **source chord C M7** (C E G B). For each note of a non-Bypass track, at
each chord change and at each note start, the transposer maps it to the current chord. Types (QY70 manual
p.147, our own implementation):

| Type | For | Rule |
|---|---|---|
| Bypass | drums | never transposed |
| Parallel | riffs that must keep their shape | shifted by (target root - C), nothing else |
| Chord 1 | chord parts, arpeggios | each note is classified against the source chord as a chord tone (root, 3rd, 5th, 7th) or a scale tone in between, and moved to the same degree of the target chord, keeping its octave relative to the root |
| Chord 2 | pads, held voicings | as Chord 1, then each note moved by octaves to be nearest the note this track played last (smooth voice leading) |
| Bass | bass lines | as Chord 1, except the first note after a chord change is the target root (or its slash bass) |

Then **limits**: a note outside the track's low / high range moves by octaves until it fits; **high key**
(Chord 1, Bass): the highest root before the line folds down an octave, so a bass line does not climb as
the chords rise.

Chord tables: for each of the 26 types, its tones by degree (root, 3rd, 5th, 7th, and tensions 9, 11, 13)
and, for the scale tones between them, the scale they imply (e.g. m7 -> dorian, 7 -> mixolydian, m7(b5)
-> locrian, dim -> diminished). Missing degrees fold to the nearest present one (a triad has no 7th: the
source's 7th maps to the octave root for Chord 1, to the 6th for a 6 chord). The "---" type (no chord) is
Parallel to its root. These tables and rules are the main thing the host tests pin down: every type, every
root, every source degree, against hand-written expected voicings.

**At a chord change**, notes still sounding on Chord 1 / Chord 2 / Bass tracks are cut and restruck on their
new pitch if they have more than an eighth note left; shorter tails just end (the QY leaves this
unspecified; this keeps pads following the changes).

## Chords in

- **Live (CHORD on):** the keys are a chord zone. Held notes are recognised (FoMni's `kb_chord` best fit,
  extended to the 26 types; the lowest note held below the chord is a slash bass). The chord changes when a
  new shape is recognised, held after release (as the QY's fingered mode). While recording, chord changes
  land on the nearest beat (v1: rounding only; anticipation, a chord sounding an eighth early when played
  within a 16th before its beat, is later: the song stores one chord a beat).
- **Step (the Chords page, LFO):** the cursor on a beat (PRESETS moves it), KNOB 1 root, KNOB 2 type, KNOB 3
  slash bass, KNOB 4 the cursor's step (beat, half bar, bar); or with CHORD on, play a chord: it is written
  at the cursor, which moves on a step.
- **Templates:** 16 progressions in C (I-V-vi-IV, vi-IV-I-V, I-vi-ii-V, ii-V-I, 12-bar blues, minor blues,
  I-IV, i-VI-III-VII, Canon, Andalusian, I-bVII-IV, doo-wop, jazz I-VI-ii-V, modal vamp, gospel IV-I, and a
  waltz), transposed by Key; picking one fills the song's chord track (or plays it live, looping, before
  any song is written: instant gratification).

## Controls

Rule: a page button holds at most three pages (pressed again: the next; the header's dots show which);
more than that gets its own button. Sections are played with **SEL held + a black key**, which leaves ENV, LFO, ARP and SEQ for pages.

| Control | Does |
|---|---|
| PLAY | start / stop (stops at the bar end when an Ending is queued) |
| REC | record: into the selected track (Pattern pages: the pattern loops, overdubs); on the Song and Chords pages: the chords and sections played |
| SEL tap | CHORD on / off: the keys enter chords (lit) or play the selected track |
| SEL held | the shortcuts drawn on screen (after 0.4 s; the release is then no tap) |
| SEL held + black key | the section, from the next bar ("NEXT" on screen): 1 Intro, 2 Main A, 3 Main B, 4 Fill (Fill AB from Main A, Fill BA from Main B), 5 Ending; 6-11 jump to song bar markers (later) |
| SEL held + white key | the chord template 1-16 (a white key each), playing at once |
| SEL held + PRESETS | the song (while playing: loaded after the stop) |
| HOME | Song: the play screen; the song and its style in the header; KNOB 1 style, 2 template, 3 key, 4 record grid |
| SEQ | Pattern: the 8 tracks; again: the pattern's length, time, name |
| EDIT | Track: the selected track's sound (GM program by family), its type and limits; again: high key, channel |
| FX | Mixer: volume, pan, reverb, chorus per track; again: tone (brightness, resonance); again: the master effects |
| LFO | Chords: the song's chord track, step by step (a beat at a time) |
| ENV | Arrange: the song's section track, length, loop, KNOB 4 the song; again: its six patterns, three a page (Intro, Main A, Main B; Fill AB, Fill BA, Ending), KNOB 4 the whole style |
| ARP | Templates: the 16 progressions, browse and audition; again: copy one into the song |
| GLO | Setup: tempo source, MIDI, key, lights; again: Panel (CPU, version) |
| SAVE | saves (lit while something is unsaved); held: jobs (copy / clear a pattern, a track, a song) |
| OCT- / OCT+ | the keys' octave; held + a key in step chord entry: slash bass |
| SELECT | tempo |
| ALGORITHM | the track (1..8) |
| PRESETS | the style (Song, Song patterns), the pattern (Pattern pages), the cursor (Chords, Arrange), the template (Templates), the program family (Track); with SEL held: the song |
| KNOB 1-4 | the page's four values |
| keys | play the selected track as played (you hear what you play; recording stores it over C M7 through the inverse transposer, as the QY does, so it follows later chords), or chords (CHORD on) |

## Screens (240 x 240, the cream / coral / teal family)

- **Song (HOME):** big current chord name and the next one; section and bar.beat; the song's bars as a
  strip (sections coloured, the chord changes marked); the 8 tracks as activity bars with their GM names.
- **Pattern (EDIT):** 8 track rows, each a mini piano-roll of its notes over the pattern's length, the
  playhead, mute state; the selected track highlighted; header: pattern name, length, source chord.
- **Track settings (EDIT again):** program (by GM family), type, low / high limit, high key, channel.
- **Mixer (FX):** 8 strips: volume, pan, reverb, chorus; KNOB 1-4 for the selected track.
- **Setup / Templates (GLO).**

## Testing

- `xpose` and `chords`: host tests of every type and root against expected voicings; chord recognition
  from held-note sets (including inversions, slash bass, ambiguous sets).
- The engine in the host simulator: patterns play at the right ticks; sections switch on the bar; fills
  return; ending stops; MIDI clock in drives it; MIDI out matches what sounds.
- Scenarios through the UI (the simulator's scripts): record a track, write chords, play a song; screenshots.
- On the device: CPU with all 8 tracks plus live chords (budget: the GM module measured ~30% at 12 voices).

## Research (summary; notes kept outside the repo)

- QY70 / QY100 owner's manuals (archive.org): 8 pattern tracks (D1, D2, PC, BA, C1-C4); 5 phrase types
  (Bypass, Chord 1, Chord 2, Bass, Parallel; manual p.147); source chord (CM7 for presets); LO / HI limit
  and HI KEY (p.210); 26 chord types plus on-bass; chord per beat with 8th / 16th anticipation; fingered
  chord recognition; 99 templates; sections INTRO, MAIN A / B, FILL AB / BA, ENDING with automatic
  follow-on; sections change at the next bar.
- Reviews (Sound On Sound 1997 / 2001, Audiofanzine users, the QY70 Shrine, forums): the magic is "style
  and chords are independent"; loved for ergonomics and finishing songs on the go; frustrations: unlit small
  screen, menu diving, slow browsing, fiddly editing, weak acoustic sounds, battery life; most use it as a
  sketchpad, for backing tracks, and as a sound module.

## Decided

- Drum grooves: Google Magenta's Groove MIDI Dataset (real drummers, MIDI; CC BY 4.0 to be confirmed)
  as the source for the factory styles' drums, credited; bass, chord and phrase parts written for Polly.

- The name: Polly.
- 16 factory styles, well made, our own.
- Sections and templates on SEL held + keys; ENV, LFO, ARP, SEQ are pages; three pages at most per button.
