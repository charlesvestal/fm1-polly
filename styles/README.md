# Polly's factory styles

Each `.sty` file here is one factory style: four patterns (Main A, Main B, Fill, Ending) of up to 8 tracks,
written over the source chord **C M7** and re-voiced live by the chord track. They are compiled into
`firmware/src/dsp/styles.h` (const tables in flash, copied into the arranger when a song picks them):

```sh
python3 tools/make_styles.py styles/*.sty > firmware/src/dsp/styles.h   # after any change (the tests check it)
python3 tools/make_styles.py --check styles/*.sty      # "2 styles, 8 patterns ok", or file:line errors
python3 tools/make_styles.py --dump styles/01-pop.sty  # the style back as plain text, drums expanded
python3 tools/make_styles.py --roundtrip styles/*.sty  # dump -> parse -> the same tables
python3 tools/make_styles.py --sizes styles/*.sty      # flash bytes per style
python3 tools/make_styles.py --grid FILE.mid [BARS]    # a MIDI file's drums as a 16th grid, to choose loops
```

The file name's number is the style's place: `01-pop.sty` is style 1. On the device, the Arrange page's
patterns (ENV, ENV again) picks each section's pattern with its knob: P01..P16, then the styles by name.

## The format

`#` starts a comment. Blank lines are ignored.

```
style Pop                  # the name, up to 8 characters (shown on the screen)
tempo 105                  # the tempo it was written at (kept in the tables; the song's tempo is yours)

track ba type=bass program=33 chan=2 lo=E1 hi=G3 hikey=G vol=104 rev=12   # before any section: every section's

[main a]                   # the sections: [main a] [main b] [fill] [ending], all four
bars 2                     # 1..8
beats 4                    # 2..7 (default 4)
track ba vol=110           # in a section: this section's changes; and the track the notes below go to
1.1.0  C2 16 106           # bar.beat.tick note length velocity
```

**Tracks:** `d1 d2 pc ba c1 c2 c3 c4` (the QY70's names; or `1`..`8`). A track not mentioned keeps the
default pattern's settings (`pattern_empty`): d1, d2, pc drums on channel 10; ba a bass; c1, c2, c4 Chord 1;
c3 Chord 2 (strings). Tracks sharing a MIDI channel (d1, d2, pc on 10) share its program and mixer: give
them the same `vol pan rev cho`, and balance them with velocities. Fields (all optional):

| Field | Values |
|---|---|
| `type` | `bypass` (drums), `parallel`, `chord1`, `chord2`, `bass` (`firmware/src/dsp/xpose.c`) |
| `program` | GM program 0..127 (0 = Acoustic Grand; drums: ignored) |
| `chan` | MIDI channel 1..16 (drums 10) |
| `lo`, `hi` | the range notes fold into by octaves: a note name or number |
| `hikey` | Chord 1 / Bass: the highest root before the line folds down an octave (`C`..`B`), or `off` |
| `vol`, `pan`, `rev`, `cho` | the mixer, 0..127 (pan 64 centre) |
| `mute` | 0 or 1 |

**Positions:** `bar.beat.tick`, bar and beat from 1, tick 0..23 (24 a beat); `2.3` is `2.3.0`.

**Notes:** `C4` is 60 (`C#4`, `Eb3`), or a number; several at once with `+`: `E4+G4+C5`. Drums by GM
name: `kick kick2 rim snare clap esnare chh phh ohh crash crash2 ride ride2 bell splash china tamb cowbell
ftom ftom2 ltom lmtom hmtom htom shaker cabasa maracas claves hbongo lbongo hconga lconga ...` (the full list
is `DRUMS` in `tools/make_styles.py`).

**Lengths:** ticks (`6`), or `w h q e s t` (whole, half, quarter, eighth, sixteenth, 32nd), a count before
(`3e` = 36), a dot after (`q.` = 36).

**Repeats:** `POS NOTES LEN VEL xN STEP` plays the line N times, STEP apart; velocities `a/b/...` cycle:
`1.1.0 E4+G4+C5 10 90/62 x8 12` is eight eighths, accented on the beat.

**Grids:** `grid POS STEP NOTES STEPS [len=L]`, one character a step from POS: `.` or `-` rest, `g` ghost
(40), `x` (90), `X` accent (118), `1`..`9` (14 each); `|` is ignored (bar lines). Each note lasts STEP, or L:

```
grid 1.1 s E4+B4 X.gx..x.g.xg..xg|x.gx..x.g.Xg.gx. len=3     # a clavinet in sixteenths
grid 1.1 e tamb  ggXgggXg|ggXgggXX
```

**Drums from a MIDI file:** `drums from=FILE.mid [bars=N] [start=B] [at=A] [quant=Q] [vel=P] [scale=..]
[only=..] [skip=..] [shift=..]` takes N bars (default: the section's) starting at the file's bar B (from 0)
into the track from the section's bar A (default 1). Every note-on of the file counts (the Groove MIDI
Dataset's Roland TD-11 notes are mapped to GM: hi-hat edges, rims, the second ride, ...); times go to the
nearest of 24 ticks a beat (`quant=6`: the nearest sixteenth). `shift` (default `auto`): the median
distance of the window's hits from their nearest sixteenth is removed, so a drummer playing just behind
the click sits on the band's grid while keeping the feel; or a number of the file's own ticks. `vel=P`
scales every velocity by P percent, `scale=kick:140,chh:80` by instrument. `only=` / `skip=` take drum names.
A hit just before the window (a downbeat played early) counts as its first tick; one just before its end
belongs to the next window. Use `--grid` to find good bars.

**Rules** (`--check`): at most 600 notes a pattern (`ARR_NEV`), notes and velocities in range, every note
inside its pattern. The same note on the same tick of a track is kept once (the last).

## Writing over C M7

Read `firmware/src/dsp/xpose.c`. Over C M7 each pitch class is a degree: C root, E 3rd, G 5th, B 7th, and the
tensions D 9th, F 11th, A 13th. Chord 1 moves each to the same degree of the chord playing; the chord tables
(`chords.c`) say what each degree is for each of the 26 types. So:

- Write chord parts on C E G, with D and A as colour. **B is "the 7th"**: B over C7 or Cm7 is Bb (good for
  funk and blues), but over a plain major or minor triad the 7th folds to the octave root. A B leading into C
  in a bass line becomes C, C over a major chord. **F** is the 4th: a passing note, never a held one.
- Bass: the first note after a chord change is the root (or the slash bass) whatever was written; then
  degrees as Chord 1. `hikey` keeps the line from climbing with the roots; `lo`/`hi` fold the rest.
- Chord 2 moves each note to the octave nearest the note the track played before, so a pad's voicing walks
  smoothly; `lo`/`hi` keep it in its register.
- An ending resolves on C (C, E, G): the transposer moves it to the song's last chord.

## The styles

| # | Style | Tempo | Drums | Main A | Main B | Fill | Ending |
|---|---|---|---|---|---|---|---|
| 1 | Pop | 105 | GMD drummer 3, rock 105: A bars 4-5, B 22-23 (on the ride), fill 17, ending 13 + a crash | kick-locked bass, 3-3-2 piano, strings | eighth bass with a walk, eighth piano, clean guitar arpeggio, tambourine | bass climbs into the toms | a tom fill, everything on the one |
| 2 | Funk | 100 | GMD drummer 7, funk 100: A bars 37-38, B 13-14 (open hi-hat), fill 23, ending 45 + a crash | bass with octave pops, clavinet 3rd+7th in sixteenths, guitar chank on 2 and 4 | busier bass, guitar scratch, brass stabs, tambourine | a bass run, brass hits | a break on the and of 4, the root on the one |

(The bar numbers above count from 1; `start=` counts from 0.)

## The drums: the Groove MIDI Dataset

**Decision:** the Main and Fill drums come from Google Magenta's Groove MIDI Dataset (real drummers, played
to a click on an electronic kit), quantised to Polly's 24 PPQ with the drummer's timing and dynamics kept;
the endings' last hits and the extra percussion are written by hand. The licence was checked at its source
on 2026-10-09: the dataset page (<https://magenta.tensorflow.org/datasets/groove>) says *"The dataset is
made available by Google LLC under a Creative Commons Attribution 4.0 International (CC BY 4.0) License."*,
and the download's `LICENSE` (groove-v1.0.0-midionly.zip) says *"This work is licensed under the Creative
Commons Attribution 4.0 International License."* (kept here as `drums/LICENSE`). Only the MIDI files the
styles use are kept, under `drums/` (renamed: `drummerN-sessionM-` + the dataset's file name).

**Credit:** drum performances from the Groove MIDI Dataset by Jon Gillick, Adam Roberts, Jesse Engel, Douglas
Eck and David Bamman (Google Magenta), CC BY 4.0, <https://magenta.tensorflow.org/datasets/groove>.
Changes: excerpts of a few bars, the kit's notes mapped to General MIDI, timing moved to 24 ticks a beat
and onto the click by the median offset, velocities scaled per instrument. Paper: Gillick, Roberts, Engel,
Eck, Bamman, "Learning to Groove with Inverse Sequence Transformations", ICML 2019.
