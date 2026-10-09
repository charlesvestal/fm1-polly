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

## A compact phrase bank (prototype)

`tools/qy70_bank.py` stores the imported styles the way the QY70 does: a style is its tracks' sounds and mixer
plus 6 x 8 references to phrases, and each phrase is stored once, over C M7, at its own length (the player
repeats it to fill a section). Nothing is expanded, so there is no per-section note limit.

```sh
python3 tools/qy70_bank.py qy70main.bin --sizes    # bytes per style, formats 1 and 2, with and without controllers
python3 tools/qy70_bank.py qy70main.bin --test     # decode(encode(phrase)) against the importer's events
python3 tools/qy70_bank.py qy70main.bin --out FILE # the bank, for your own use
```

**A style** (about 175 bytes): name, tempo x 10, beats a bar, bars per section (a nibble each), a mask of the
tracks that play, 7 bytes a track (channel and drum flag, program, volume, pan, reverb, chorus, the velocity
scale of a drum track sharing channel 10), then per section a mask and a 3-byte bank offset a phrase (a
program byte when that use's sound differs from its track's).

**A phrase, format 2** (byte-aligned, decoded front to back; the player needs only the previous four bars of
the phrase in RAM):
- header: type / drum / flags, bars, beats a bar, the time unit (an index), the velocity range, the
  limits and high key (Chord tracks with limits), the phrase's keys (up to 15: notes then name a key by a
  nibble; more: a byte).
- each bar: a byte naming a bar 1..4 back that it repeats (0: none), with a flag when the repeat is exact;
  then a bit mask of the earlier bar's notes it keeps and, unless exact, a byte each (timing -7..+7 ticks,
  velocity -8..+7 steps); then its other notes: a count, and per note a time byte (0..14 steps of the unit
  and -8..+7 ticks: the humanised timing is kept exactly; or an escape and the ticks), a key / velocity byte,
  and for melodic notes a gate byte (10-tick units).
- the controllers: runs of one kind (bend, modulation, expression, pan, pressure), each event the ticks
  since the last and a 7-bit value. Bends keep their top 7 bits (3 cents at +-2 semitones); a step smaller
  than 2 (1 for bends and pan) within a beat of the last is dropped. CC 74 and 71 (XG filter) are dropped:
  Polly's synth ignores them.

Velocity is 16 steps across each phrase's own range (at most 4 off; under half a dB). Drums keep no gate.
Format 1 is the same without bar repeats (every event a time byte and a note).

**Sizes for the 128 styles** (ROM v1 as dumped by MAME; the 230 KB budget is the free flash estimate):

| Encoding | Notes only | With bends and controllers | Styles in 230 KB (smallest first) |
|---|---|---|---|
| .sty through make_styles.py (expanded sections, 480 PPQ) | ~726 KB | - | ~40 |
| phrase bank, format 1 | 394 KB | 488 KB | 91 / 83 |
| phrase bank, format 2 (bar repeats) | 364 KB | 440 KB | 96 / 88 |
| format 2 phrases + zlib in 16 KB blocks (needs a RAM decode) | ~268 KB | - | - |

All 128 in 230 KB needs more than a byte-aligned stream: the fields' order-0 entropy alone is about 244 KB for
the notes, and the humanised timing (3 bits a note) and velocities (4 bits) don't repeat exactly. The next
steps, measured but not built: bars repeating bars of the style's other phrases (Main A / Main B / fills share
most bars: 41,000 of 68,000 repeated notes become 68,000), a static Huffman code per field (about 15%), or
decoding each phrase into RAM at a section change (then LZ / zlib apply).

## Where a bank could live: the free flash

The FM-1's flash is 1 MB (JEDEC 0x856014). Polly's map (`firmware/src/storage.c`, `firmware/hal/fm1_flash.h`,
`firmware/loader/ldr_core.c`, `firmware/src/ota.c`):

| Range | Size | Holds | A bank there? |
|---|---|---|---|
| 0x00000-0x03FFF | 16 KB | SPL, isd_config (flash head) | no |
| 0x04000-0x92FFF | 572 KB | the app (encrypted XIP). The update loader rewrites every sector of it to the package image (padded), so the unused tail (about 140 KB past a 430 KB app) is erased by each update | no |
| 0x93000-0x96FFF | 16 KB | nothing | yes, with a firmware change: the app's flash driver writes only Felucca's store and the staging area (`FL_RANGE_OK`) |
| 0x97000-0x98FFF | 8 KB | songs | no |
| 0x99000-0xC8FFF | 192 KB | 24 user patterns, 2 sectors each | no (the largest lever: fewer or smaller user pattern slots) |
| 0xC9000-0xDBFFF | 76 KB | nothing | yes, writable by the app today |
| 0xDC000-0xDDFFF | 8 KB | panel calibration (2 copies) | no |
| 0xDE000-0xDFFFF | 8 KB | nothing | yes, writable today |
| 0xE0000-0xE4FFF | 20 KB | update loader staging, record at 0xE4F00; erased at boot when used | no |
| 0xE5000-0xFBFFF | 92 KB | nothing | yes, with the `FL_RANGE_OK` change |
| 0xFC000-0xFDFFF | 8 KB | the project (Felucca's globals area) | no |
| 0xFE000-0xFFFFF | 8 KB | rest of Felucca's globals area; the last sector is past the image's declared flash size | no |

**Safe for a bank: 192 KB** (84 KB writable by the app as it is, 108 KB more once its flash driver may write
0x93000-0x96FFF and 0xE5000-0xFBFFF). Everything from 0x93000 up is one plain (unencrypted) XIP window
(`fl_plain_window_init`), so the player can read a bank in place, no `fl_read`. A firmware update writes only
the app area; but at the end of an update the loader erases every sector in 0x93000-0xFBFFF whose last 256 bytes
hold a valid update record (bytes 6-7 "AT", CRC-16 at 0), so the bank writer keeps bytes 0xF00-0xF07 of each 4 KB
sector at 0xFF (no phrase straddles them; about 1% padding), and no sector can look like a record to the loader
or the SPL. Restoring M-VAVE's firmware wipes the bank (the user imports again).

**What fits in 192 KB** (`qy70_bank.py --sizes --budget 192`, smallest styles first): 83 of the 128 styles
(77 with bends and controllers); 42 in the 84 KB writable today. All 128 need about 364 KB (440 KB with
controllers) in format 2. Measured routes past that: bars repeating bars of the style's other phrases (about
-40 KB) and a static Huffman code per field (about -15%) would bring the notes to about 275 KB, still a stream
the player reads in place; decoding each phrase into RAM at a section change with zlib-class compression about
268 KB; freeing 128 KB of user-pattern slots: 120 styles (109 with controllers).

**The player's cost** (format 2, read in place): a note is 2-3 byte loads, a palette lookup and a multiply for
the velocity, a bar repeat a mask test and a copy from the bars before: some 30-60 cycles an event. It keeps the
last four bars of each playing phrase decoded, 5 bytes a note: 8 KB for 8 tracks at 50 notes a bar (99% of
phrases), 19 KB at the ROM's busiest bar (121 notes); repeats only one or two bars back (95% of them) halve that.

## The runtime-decoded bank (`tools/qy70_codec.py`, `firmware/src/dsp/qydec.c`)

The bank the device would use: each style is one range-coded stream, decoded into RAM in one go when the style
is picked (`qy_decode_style`, `firmware/src/dsp/qydec.c`, pure C99). The same Python function codes and decodes
(`code_phrase`), and the C decoder follows it line for line; `--ctest` checks the two agree on all 4,166 phrases,
`--test` that the decode gives the importer's events back (times, keys and gates exact; velocity within 4).

```sh
python3 tools/qy70_codec.py qy70main.bin --out bank.qyb   # the bank
python3 tools/qy70_codec.py qy70main.bin --sizes          # bytes per style
python3 tools/qy70_codec.py qy70main.bin --test           # Python decode == the importer's events
python3 tools/qy70_codec.py qy70main.bin --ctest          # C decoder == Python decoder (and its host speed)
```

**The coder**: LZMA's binary range coder (12-bit probabilities, adapting by 1/16; a multiply, a compare and
shifts a bit; no division). Values are binary trees (small fields) or an exp-Golomb code whose length and first
two mantissa bits have probabilities of their own. The probabilities start from priors trained on the whole bank
and stored once in it (4,535 of them at 4 bits: 2.3 KB; trained priors save 12 KB of streams).

**The model**, per phrase: the header (type, drum, limits, bars, beats a bar, the time grid, the velocity range,
the keys as a first key and gaps); then bar by bar: whether it repeats an earlier bar of the style (any phrase of
the style, same kind and bar length; the encoder picks the cheapest by an estimate) and which, a kept bit per
note of that bar (in the context of the previous bit), exact or with a timing (-7..+7 ticks) and velocity (steps
of 2) change each; then the bar's other notes: same time as the last note (a bit), else grid steps (in the
context of the last step) and the humanised offset in ticks; the key's index (in the context of the last
key's); velocity in 16 steps of the phrase's range (in the context of the key and of the note's place: on the
beat, on the half beat, elsewhere); gate (same as the last, or in 10-tick units). Controllers: runs of a kind, each
event its time step (the same as the last: a bit) and its value change (in the context of the last change's
sign).

| All 128 styles | Bytes |
|---|---|
| phrase bank, format 2, notes only / with controllers | 363,895 / 439,840 |
| range-coded, notes only | ~197,000 |
| **range-coded, with bends and controllers** | **235,247** (priors 2,268; styles 611..5,201 bytes, median 1,615) |

**What fits** (192 KB of free flash plus 8 KB for each user pattern slot given up, less 0.2% for the sectors'
update-record guard):

| User patterns kept | Space | Styles that fit (with controllers) |
|---|---|---|
| 24 | 192 KB | 117 |
| 23 | 200 KB | 120 |
| 22 | 208 KB | 122 |
| 21 | 216 KB | 124 |
| 20 | 224 KB | 126 |
| 19 | 232 KB | 128 (1.6 KB to spare) |
| 18 | 240 KB | 128 (9.8 KB to spare) |

Giving up the top slots joins their space to the free 0xC9000-0xDBFFF region. A style's stream must not straddle
two regions, nor bytes 0xF00-0xF07 of a sector (the writer pads; or the decoder's byte fetch steps over them).

**On the device**: the decoder is 2.4 KB of code (pi32v2 -Os: three hardware divides, no library calls but
memcpy). Working memory 13.5 KB while decoding (the probabilities, 11.1 KB, and a table of the style's bars,
2.4 KB): it can share RAM with anything not needed during a style change. The decoded style stays in RAM: notes 6
bytes, controller events 4, phrases 16; the largest style is 20.6 KB (Hi-Life), the median about 6 KB. Speed: 17.4
binary decisions an event on average, 61,500 for the largest style; at some 25 cycles a decision plus the stores
and the per-bar sort, about 1.7 M cycles: 11-17 ms at 100-160 MHz for the largest style, once, when it is picked
(section changes then cost nothing). On this Mac the 128 styles decode in 12 ms (82 ns an event).
