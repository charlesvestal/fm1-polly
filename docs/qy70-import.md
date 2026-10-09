# Importing the QY70's preset styles (bring your own ROM)

`tools/qy70_import.py` reads the 128 preset styles out of a Yamaha QY70 main ROM that you supply (the MAME set's
`qy70main.bin`, 2 MB) and writes them as Polly `.sty` files. The repository holds only the tool: no ROM bytes,
no Yamaha patterns. What it writes is Yamaha's music, for your own use; don't commit or share it.

```sh
python3 tools/qy70_import.py qy70main.bin --list                      # number, name, tempo, meter, bars a section
python3 tools/qy70_import.py qy70main.bin --style Bossa --out DIR     # DIR/17-bossa.sty (--number N for another)
python3 tools/qy70_import.py qy70main.bin --all --out DIR --report    # all 128, numbered from --first (17)
python3 tools/qy70_import.py qy70main.bin --phrase Bossa-1a           # one phrase, decoded, to look at
python3 tools/qy70_import.py qy70main.bin --selftest                   # the chord mapping against xpose.c
python3 tools/qy70_import.py qy70main.bin --style Bossa --out DIR --legacy   # 24 ticks a beat, 4 sections
```

By default a style keeps the QY70's own timing (480 ticks a beat) and all six sections (`[intro] [main a]
[main b] [fill ab] [fill ba] [ending]`). `--legacy` writes the older Polly format: 24 ticks a beat (each note
to the nearest tick) and four sections (Main A, Main B, Fill = Fill AB, Ending; the Intro and Fill BA are
left out). A pattern holds at most `--max-notes` notes (600, `ARR_NEV`); past that, the quietest short notes
are dropped and the report says how many. Then: `python3 tools/make_styles.py --check DIR/*.sty` (with the
numbers continuing Polly's, no gaps), and `--sizes` for the flash each takes.

The tool finds its tables by their shape (a phrase pointer table, then 128 style records), not by fixed
addresses, so another QY70 ROM version should work if its layout is the same; it stops with a message if
it finds none.

## The format, as we found it

All numbers big-endian. Ticks are 480 a quarter note.

**Styles.** 128 records of 546 bytes, one after another. Each has the same layout as the QY70's user pattern
header in a bulk dump (as decoded by [qy100-toolkit](https://github.com/doffu0000/qy100-toolkit)):

| Offset | What |
|---|---|
| 0 | tempo x 10 (2 bytes) |
| 6 | name, 8 characters |
| 14 | time signature: `(numerator - 1) << 3 \| 16 / denominator` |
| 15 | 6 bytes: bars - 1 for Intro, Main A, Main B, Fill AB, Fill BA, Ending |
| 21 | 48 bytes (section x 8 + track): the phrase's category and beat, `category << 3 \| beat` (beat 1 = 16 beat, 2 = 8 beat, 3 = 3/4), or `FE` for none |
| 69 | 48 bytes: the phrase's number within its category and beat |
| 138, 146, 154 | 8 bytes each: the track voice, bank MSB, bank LSB, program; all zero: the phrase's own voice |
| 170, 178, 202, 210 | 8 bytes each: volume, pan, reverb, chorus |

Tracks are D1 D2 PC BA C1 C2 C3 C4. The rest of the record (per-section chords, effect settings) isn't used.

**Phrases.** A table of 4-byte addresses, one per phrase (4167 in the ROM we have), the first entry pointing
just past the table. In front of it, one 16-byte row per category code: the first phrase number of each beat
(16 beat, 8 beat, 3/4). A style's reference (category, beat, n) is phrase `start[category][beat] + n`. Category
codes: 1 Drum a, 2 Drum b, 5 Fill a, 6 Fill b, 9 Percussion, 13 Bass a, 14 Bass b, 18 Guitar chord a, 19 Guitar
chord b, 20 Guitar riff, 23 Keyboard chord, 24 Keyboard riff, 28 Pad, 29 Brass, 30 Sound effects.

A phrase: 12 characters of name, 16 bytes of header, events, `F2`.

| Header byte | What |
|---|---|
| 0 | bars - 1 |
| 1 | time signature (as the style's) |
| 2, 3, 4 | voice: bank MSB, LSB, program (MSB 127: drum kit, 126: SFX kit, 64: SFX voice) |
| 5, 6 | high limit, low limit (notes outside fold in by octaves) |
| 7 | high key, 0..11 (C..B): the highest root before the line drops an octave (Chord 1, Bass) |
| 8 | type: `03` Bypass, `90` Chord 1, `92` Bass, `94` Parallel, `A0` Chord 2 |
| 9, 10 | the source chord: root 0..11, type (0 M7, 1 M, 2 6, 3 m7(11), 4 M7(9), 5 add9, 6 m, 7 m6, 8 m7, 9 m7(b5), 10 mM7, 11 m7(9), 12 madd9, 13 7, 14 7(#5), 15 7(b9), 16 7(9), 17 7(#9), 18 7(#11), 19 7(b13), 20 7(13), 21 7sus4, 22 sus4, 23 dim, 24 aug, 25 6(9), 26 7(b5)) |
| 12 | tempo x 10 (2 bytes) |
| 14 | `F0 00`: the events start |

Events (the same grammar as the QY70's bulk dump tracks):

| Bytes | Event |
|---|---|
| `80`..`9F` | wait `b & 1F` ticks |
| `A0`..`BF` x | wait `(b & 1F) << 7 \| x` ticks |
| `C0`..`CF` key vel | a note, gate `b & 0F` ticks (drums) |
| `D0`..`DF` g key vel | a note, gate `(b & 0F) << 7 \| g` |
| `E0`..`EF` g1 g2 key vel | a note, gate `(b & 0F) << 14 \| g1 << 7 \| g2` |
| `FB` cc v | a control change |
| `FE` lsb msb | pitch bend |
| `FD` x | (2 bytes; slow sweeps: a coarse bend or pressure) |
| `F3` + 5 bytes, `F8` | unknown, skipped |
| `F2` | end |

Phrases are played as recorded: their timing is humanised (a hit a few ticks either side of the beat).

## How a style becomes a .sty

| QY70 | Polly |
|---|---|
| Intro, Main A, Main B, Fill AB, Fill BA, Ending | the same six sections (`--legacy`: Main A, Main B, Fill, Ending) |
| section bars, time signature | `bars` (1..8), `beats` (3/4, 4/4, 6/4) |
| phrase shorter than its section | repeated to fill it; longer: cut at the section's end |
| type Bypass / Chord 1 / Chord 2 / Bass / Parallel | `bypass` / `chord1` / `chord2` / `bass` / `parallel`, per section |
| high limit, low limit, high key | `hi`, `lo`, `hikey` |
| source chord (often Am7, sometimes C6, C7, Gm7(11), ...) | each note moved to the same degree of C M7 (Polly's source chord): over Am7, A becomes C, C becomes E, E G, G B; a 6 chord's 6th is its 7th degree (Polly's own reading); a quartal m7(11)'s 11th becomes the 5th (see below); Parallel phrases move by the root only. The octave is the one that plays back as written over the source chord (the high key included) |
| Chord 2 with no limits (0..127) | a window around the phrase's notes (Polly's Chord 2 follows the note before, and with no range a voicing walks off the keyboard) |
| track voice, else the phrase's | GM program: bank MSB 0 with any LSB (XG variations) -> the same program; MSB 64 (SFX voices) -> GM 120..127 by range; drum and SFX kits -> channel 10 |
| volume, pan, reverb, chorus | the same; the tracks on channel 10 share the first drum track's mixer, the others' volume scaled into their velocities |
| XG drum keys outside GM (13..34, 82..87) | the nearest GM sound (brushes -> snare, softer; soft kick -> kick 2; sticks, open rim -> side stick; shaker -> maracas; ...) |
| tempo | `tempo`, rounded (40..240) |

Channels: BA 2, C1..C4 3..6 (Polly's defaults), drums 10; a drum track playing a melodic voice gets 7, 8 or 9.

## What doesn't carry over

- **Controllers and bends.** Expression swells, pitch bends and the other non-note events are skipped (Polly's
  patterns hold notes only): in all 128 styles about 44,000 control changes and 17,700 bends. The brass and
  guitar styles lose their swells and slides.
- **Drum kits.** Polly has one GM kit: the QY70's Brush, Jazz, Room, Analog, ... kits all play it.
- **XG voices.** Variations (bank LSB) play their GM program; SFX voices (MSB 64) a GM effect.
- **Per-track pan on channel 10.** D1, D2 and PC (and drums on other tracks) share one pan and effects.
- **Quartal m7(11) voicings** (about 400 phrases): their 11th is stored as the 5th, so a plain m7 voicing
  plays: kept as the 11th, Polly's Chord 1 plays it as a 4th over every major and dominant chord.
- **Intros and endings** are mostly Parallel phrases carrying their own chord progression (a ii-V-i into the
  last chord), written to sound over one held chord, as the QY70 plays them. Over a template that keeps
  changing chords (Polly's templates go on during the Intro and the Ending) they are moved to each new root
  and clash. A minor-key ending (Bossa's is in A minor, over Am7) stays minor over a major chord, as on the QY70.
- **Chord 2 voicings**: Polly moves each note near the one before, the QY70 the whole voicing; a wide voicing
  closes up, and two notes can land on one pitch.

`--selftest` plays every phrase, as stored, through Polly's transposer (`firmware/src/dsp/xpose.c`, compiled
with `cc`) over its own source chord: Chord 1 and Bass must give the ROM's notes back exactly, Chord 2 and
Parallel their pitch classes (all but 65 of 64,000 notes: leading tones over triads, which Polly's tables play
as the root). `tests/run_tests.sh` runs it when `QY70_ROM` names a ROM.
- **Note limit.** 600 notes a pattern: 31 of the 128 styles have a busy section over it at 480 ticks
  (1,284 notes dropped in all, the quietest short ones).
- **Timing, `--legacy` only.** At 24 ticks a beat about half the notes move (by 2.8 of the QY70's ticks on
  average, 3 ms at 120 bpm): the humanised feel mostly survives, the finest flams don't.

## Sizes

About 5.7 KB of flash a style at 480 ticks a beat with six sections (2.5 to 12 KB; all 128: about 730 KB), 4.3
KB with `--legacy` (all 128: about 550 KB). Pick a few: they don't all fit.
