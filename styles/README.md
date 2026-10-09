# Polly's factory styles

Each `.sty` file here is one factory style: six patterns (Intro, Main A, Main B, Fill AB, Fill BA, Ending, the
QY70's sections) of up to 8 tracks,
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

The file name's number is the style's place: `01-pop.sty` is style 1. Songs store these numbers, so they
never change: a new style goes at the end (the next number), and the tool fails on a gap, a number used
twice, or a file without one. On the device, the Arrange page's patterns (ENV, ENV again) picks each
section's pattern with its knob: P01..P16, then the styles by name. Picking a style for Main A of a song
with no chords written yet also sets the tempo to the style's (`tempo`); otherwise the tempo stays.

## The format

`#` at the start of a line or after a space starts a comment (`F#4`, `hikey=F#` are not comments). Blank
lines are ignored.

```
style Pop                  # the name, up to 8 characters (shown on the screen)
tempo 105                  # the tempo it was written at (kept in the tables; the song's tempo is yours)

track ba type=bass program=33 chan=2 lo=E1 hi=G3 hikey=G vol=104 rev=12   # before any section: every section's

[main a]                   # the sections: [intro] [main a] [main b] [fill ab] [fill ba] [ending]
bars 2                     # 1..8
beats 4                    # 2..7 (default 4)
track ba vol=110           # in a section: this section's changes; and the track the notes below go to
1.1.0  C2 320 106          # bar.beat.tick note length velocity
```

**Sections:** `[main a]` is required; any other may be left out: the style then has no notes there, and the
engine falls back (no Intro: PLAY starts at Main A; no Fill BA: Fill AB plays instead, and the other way
round; neither: no fill; no Ending: the stop at the bar line). `[fill]` is another name for `[fill ab]` (the
fill from Main A into Main B; Fill BA leads from Main B back to Main A). The Intro plays once at PLAY, then
Main A; a fill plays one bar. A section left out takes the style's tracks (the lines before any section), one
bar of Main A's beats.

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

**Positions:** `bar.beat.tick`, bar and beat from 1, tick 0..479 (480 a beat, the QY70's resolution); `2.3`
is `2.3.0`.

**Notes:** `C4` is 60 (`C#4`, `Eb3`), or a number; several at once with `+`: `E4+G4+C5`. Drums by GM
name: `kick kick2 rim snare clap esnare chh phh ohh crash crash2 ride ride2 bell splash china tamb cowbell
ftom ftom2 ltom lmtom hmtom htom shaker cabasa maracas claves hbongo lbongo hconga lconga ...` (the full list
is `DRUMS` in `tools/make_styles.py`).

**Lengths:** ticks (`120`), or `w h q e s t` (whole, half, quarter, eighth, sixteenth, 32nd: 1920 960 480 240
120 60), a count before (`3e` = 720), a dot after (`q.` = 720). In the firmware's tables a length up to 1020
ticks is kept exactly, a longer one to the nearest 20 ticks (`--check` says when one is rounded).

**Repeats:** `POS NOTES LEN VEL xN STEP` plays the line N times, STEP apart; velocities `a/b/...` cycle:
`1.1.0 E4+G4+C5 200 90/62 x8 240` is eight eighths, accented on the beat.

**Grids:** `grid POS STEP NOTES STEPS [len=L]`, one character a step from POS: `.` or `-` rest, `g` ghost
(40), `x` (90), `X` accent (118), `1`..`9` (14 each); `|` is ignored (bar lines). Each note lasts STEP, or L:

```
grid 1.1 s E4+B4 X.gx..x.g.xg..xg|x.gx..x.g.Xg.gx. len=60    # a clavinet in sixteenths
grid 1.1 e tamb  ggXgggXg|ggXgggXX
```

**Drums from a MIDI file:** `drums from=FILE.mid [bars=N] [start=B] [at=A] [quant=Q] [vel=P] [scale=..]
[only=..] [skip=..] [shift=..] [map=..]` takes N bars (default: the section's) starting at the file's bar B (from 0)
into the track from the section's bar A (default 1). Every note-on of the file counts (the Groove MIDI
Dataset's Roland TD-11 notes are mapped to GM: hi-hat edges, rims, the second ride, ...); times go to the
nearest multiple of `quant` ticks: by default 20 (24 a beat: straight and triplet sixteenths, the swing kept,
as every factory style's drums), `quant=s` (or `120`) the nearest sixteenth, `quant=off` the drummer's own
timing to the tick (480 a beat). `shift` (default `auto`): the median
distance from the beat of the hits near a beat (within a 32nd) is removed, so a drummer playing just behind
the click sits on the band's grid while keeping the feel (straight or swung); or a number of the file's own
ticks. `vel=P`
scales every velocity by P percent, `scale=kick:140,chh:80` by instrument. `only=` / `skip=` take drum names (the file's, after the
GM mapping); `map=snare:rim` plays one instrument as another (after `only` / `skip`; `scale` names the new one).
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

| # | Style | Tempo | Drums | Main A | Main B | Fill AB | Ending |
|---|---|---|---|---|---|---|---|
| 1 | Pop | 105 | GMD drummer 3, rock 105: A bars 4-5, B 22-23 (on the ride), fill 17, ending 13 + a crash | kick-locked bass, 3-3-2 piano, strings | eighth bass with a walk, eighth piano, clean guitar arpeggio, tambourine | bass climbs into the toms | a tom fill, everything on the one |
| 2 | Funk | 100 | GMD drummer 7, funk 100: A bars 37-38, B 13-14 (open hi-hat), fill 23, ending 45 + a crash | bass with octave pops, clavinet 3rd+7th in sixteenths, guitar chank on 2 and 4 | busier bass, guitar scratch, brass stabs, tambourine | a bass run, brass hits | a break on the and of 4, the root on the one |
| 3 | Rock | 120 | GMD drummer 7, rock 120: A bars 27-28, B 10-11 + a crash, fill 16 (snare build), ending 20 + hits | palm-muted power chords in eighths (distortion guitar), root-eighths bass | open power chords, overdriven guitar arpeggio, rock organ pad, bass to the octave | eighths into sixteenths on bass and guitar | a tom fill, everything on the one |
| 4 | Ballad | 72 | GMD drummer 7, rock 75: A bars 2-3 on the cross-stick (`map=snare:rim`), B 10-11 on the snare + a soft crash, fill 16, ending 8 + hits | piano arpeggios in eighths, fretless bass, soft strings | piano quarter-note chords, a high string line, strings up, the bass moving | the piano climbs the chord | a broken chord, then the whole chord held |
| 5 | Bossa | 128 | by hand: the bossa clave on the cross-stick, kick 1 / 2& / 3 / 4&, hi-hat eighths; B on the ride + shaker | nylon guitar comping in 3rd-7th-9th voicings, bass root-fifth with pickups, soft strings | a flute line, the ride and shaker, the bass walking to the 3rd | toms down the kit | the chord on the one with a soft crash |
| 6 | Swing | 124 | GMD drummer 10, jazz swing 124: A bars 10-11 on the closed hi-hat (`map=ohh:chh`), B 14-15 on the ride, fill 12, ending 10 + hits | walking bass, piano Charleston in 3-5-7 voicings, soft drawbar organ | ride, walk with triplet pickups, busier piano, a vibraphone line, organ up | a walk up and two kicks | the 3-5-7 chord held over the root |
| 7 | Shuffle | 112 | GMD drummer 8, rock shuffle 112: A bars 5-6 (hi-hat), B 9-10 (ride) + a crash, fill 16, ending 20 + hits | boogie bass (1-3-5-6-b7-6-5-3, each on the beat and the swung and), the guitar's 5-6 shuffle, soft organ | the ride, piano triplets on top, organ up | the bass up in triplets | up the boogie, the stop, the root |
| 8 | Reggae | 78 | GMD drummer 1, reggae 78 (kick and cross-stick on 2 and 4: a one drop felt in double time): A bars 6-7, B 9-10, fill 13, ending 17 + a hit | the skank on every offbeat (muted guitar), an organ doubling it, the bass leaving the one open | the organ bubble in sixteenths, a melodica line, a busier bass, strings up | the bass climbs | the chord on the one |
| 9 | Hip hop | 90 | GMD drummer 8, hip hop 90: A bars 2-3 without the open hats (`skip=ohh`), B 10-11 with them, fill 15 (the drummer stops after beat 2: a break), ending 14 + a hit | synth bass on the kick, Rhodes 3-5-7 chords laid back a 48th, a soft pad | the open hats, a vibraphone loop, pad up | the break | one hit |
| 10 | House | 122 | by hand: four on the floor, clap on 2 and 4, open hat on the offbeats; B adds sixteenth hats, a shaker and a crash | the bass on the offbeats, organ stabs, a soft pad | piano stabs 3-3-2 (3-5-7-9, short), the bass jumping octaves, pad up | a snare build into 32nds | the chord on the one |
| 11 | Disco | 120 | GMD drummer 1, disco 120: A bars 2-3, B 14-15 (open hats on the offbeats) + a crash, fill 9 (toms), ending 4 + hits; a snare and clap on 2 and 4 added by hand (the drummer plays none) | octave bass in eighths, muted guitar sixteenths, Rhodes offbeat stabs, soft strings | string stabs and a held string line, the bass with sixteenth pickups | the bass runs the chord in sixteenths, string hits | the chord on the one |
| 12 | Country | 114 | GMD drummer 1, country 114 (a train beat: sixteenths on the snare, accents on the ands): A bars 2-3, B 6-7 + tambourine and a crash, fill 12, ending 4 + hits | boom-chick: bass root on 1, fifth on 3, steel-string strums rolled on 2 and 4, a walk up 5-6-7 | a fiddle line (the GM set has no pedal steel), honky-tonk piano eighths | the bass walks up the chord | the 5-6-7 tag into the root, a rolled chord held |
| 13 | Gospel | 66 (dotted quarters) | by hand (the dataset's 6/8 takes are rock): kick 1 and 3 with a triplet pickup, cross-stick (A) or snare and tambourine (B) on 2 and 4, hi-hat (A) or ride (B) triplets | bass root-fifth with triplet pickups, piano triplet chords, drawbar organ held | a choir on the chord, the bass walking in triplets, organ up, a crash | a triplet snare and tom run | the chord held |
| 14 | Waltz | 150 (3/4) | by hand: kick on 1, snare and hi-hat on 2 and 3; B on the ride | oom-pah-pah: the bass on 1 (root, then the fifth), accordion on 2 and 3, soft strings | a violin melody, the bass walking on 3, strings up | the bass climbs, a violin pickup | the chord on the one |
| 15 | Latin | 190 | by hand: the 2-3 son clave on claves, the conga tumbao (slap on 2, open tones on 4 and its and), maracas; B adds the cowbell on the beat, timbale accents and a crash | the bass tumbao (the and of 2, beat 4 held over the bar line), piano montuno in two alternating voicings, soft strings | a trombone mambo riff, strings up | a timbale roll | the clave's 3 side, the chord on the one |
| 16 | Ambient | 70 | none in A; B a soft shaker and triangles | two pads on Chord 2 breathing in turn (a warm pad on the bar, a halo pad from beat 3 over the bar line), a celesta arpeggio, a sub bass holding the root | the shaker and triangles, a vibraphone second voice, pads up | the arpeggio climbs | the root chord held, the arpeggio thinning and fading |

(The bar numbers above count from 1; `start=` counts from 0.)

The intros and the second fills (Fill BA, back home to Main A):

| # | Style | Intro | Fill BA |
|---|---|---|---|
| 1 | Pop | 2 bars: the piano's 3-3-2 alone over a light hi-hat, then the band, the bass climbing up the chord and the drummer's snare roll (bar 25) | bar 29 (a plain hi-hat bar, snare doubled on 4); the bass back down the chord, the guitar arpeggio falling, piano hits with the snare |
| 2 | Funk | 2 bars: drums bars 30-31 (groove, then a fill); the clavinet and guitar first, then the bass with a run down into Main A and brass hits with the fill | bar 35, a break: everyone stops after beat 2 (a bass, guitar and brass hit on 2), then a bass pickup |
| 3 | Rock | 2 bars: palm-muted guitar counted in on hi-hat quarters, then the band in open chords and hits with the drummer's snare fill (bar 23) | bar 12 (the hi-hat drops to the floor tom on 3 and 4); the open chord left ringing, the bass walking down 5-3-2 to the fifth below |
| 4 | Ballad | 2 bars: the piano alone, the broken chord from the top down; then the bass, a soft kick and hi-hat, a cross-stick pickup and a rolled chord | bar 24 (down the toms on beat 4); the piano chord, then a broken chord falling from the top, a string line, the bass 1-5-3-5 |
| 5 | Bossa | 2 bars (drums by hand): the nylon guitar alone over the hi-hat foot; the clave, kick and hi-hat join in bar 2, the bass on the half bar | the cross-stick fills the bar in sixteenths (by hand); the flute falls to the root, the comping settles into a held chord, the bass walks down |
| 6 | Swing | 2 bars: the bass in two under the drummer's hi-hat (bar 9) and the Charleston, then a walk up, a vibraphone pickup and a snare-kick setup on beat 4 | bar 20, still on the ride (snare and tom accents on the swung ands); the bass walks down from the octave, the vibes run down, piano hits pushed |
| 7 | Shuffle | 2 bars (drums by hand): the piano's triplets alone over the hi-hat, then stop-time hits on 1 and 3 with a crash, a snare triplet and the bass up in triplets into the one | bar 8 (back on the hi-hat, open hats); the bass down the boogie from the octave, the piano running down in triplets to the root |
| 8 | Reggae | 2 bars: the skank and organ over the drummer's hi-hat alone (bar 2, `only=chh`), then the bass and the drummer rolling in on the toms (bar 21) | bar 25 (a snare roll from the and of 2); a dub drop: the bass plays half the bar, the melodica's last phrase falls to the root |
| 9 | Hip hop | 2 bars: the Rhodes and pad alone, then the drummer's hats (bar 3) come in, with a snare on 4 and a double-kick pickup | the kick drops out under the drummer's snare and hats (bar 12); the bass and Rhodes held while the vibes fall G-E-C; a kick pickup back in |
| 10 | House | 4 bars, drums first: the kick alone, then open hats, then the clap and the bass; a rising sixteenth snare roll in bar 4, the pad swelling and organ stabs | a small breakdown: the kick only on the one, with a soft crash; claps on 2, 4 and its and; the bass held; organ stabs on every offbeat |
| 11 | Disco | 2 bars: the drummer's hi-hats alone under a string sweep up the chord, then the band with octave bass and the drummer's floor-tom pickup (bar 15) | the drummer's tom bar (bar 21); the strings fall down the chord; the bass octaves run down C-B-G-E home |
| 12 | Country | 2 bars: the train beat on the snare alone under a ringing rolled chord and a fiddle kick-off, then boom-chick with the 5-6-7 walk up (drums bars 2 and 11) | the drummer's tom fill (bar 20); the bass walks down from the octave to the fifth; a big strum held on 3; a piano lick down |
| 13 | Gospel | 2 bars: the piano triplets alone over the organ, the hi-hat creeping in; then the kick, cross-stick and bass, with a triplet snare pickup | a stop: the band hits beat 3 with a crash and holds; the hi-hat pedals beat 4's triplets; the bass picks back up |
| 14 | Waltz | 4 bars: the strings and a violin phrase alone, then oom-pah-pah and the drums for two bars, the violin rising into Main A | a hemiola: the bass, accordion and snare in two dotted quarters across the bar; the violin falls from the octave |
| 15 | Latin | 4 bars (two claves): the piano montuno over the clave and maracas alone, then the congas, bass tumbao and strings join; a timbale pickup | a band break on the clave's 2 side: everyone hits the one, the and of 2 and beat 4 (held), with timbales and a crash; the congas and cowbell out |
| 16 | Ambient | 4 bars: the warm pad fades in alone (a soft triangle), then the halo pad, then the sub bass, the celesta's first notes climbing into Main A | the arpeggio falls from G6 down the chord; the vibes answer; a soft triangle |

**12/8 and 3/4.** Patterns can be 2 to 7 beats a bar, so the Waltz is plain 3/4 (`beats 3`; a song's bar
and the Waltz template's bar are three beats while it plays). There is no compound meter: a 12/8 (or 6/8)
style is written as 4/4 with each beat split in three triplet eighths, at ticks 0, 160 and 320 (`grid 1.1 160
...` steps a triplet eighth), and its tempo counts the dotted quarters. The Gospel style does this; the chord
track and sections then stay on the dotted-quarter beat, as they would in 12/8.

Latin's percussion is written by hand: the dataset's latin takes are kit grooves without the clave, congas
and cowbell salsa needs. House's drums are written by hand: the dataset is played on a kit, and house is programmed. Gospel's and the
Waltz's too: the dataset's 6/8 and 3/4 takes are rock, jazz and Latin. Bossa's drums are
written by hand: the dataset's bossa takes (drummer 5, 127) are improvised around the
groove, the cross-stick sparse and the kick at ghost level, and a style needs the steady bossa clave.

## Checking and listening

- `tests/host/style_chords_test.c` (in `run_tests.sh`) plays every pattern (all six sections) of every style through the real
  engine as a song's Main A over the Pop template, the 12-bar blues, the Andalusian progression and a bar
  each of ten chord types (maj, m, 7, M7, m7, 6, sus4, m7(b5), dim, aug): every note of a Chord 1 / Chord 2
  track sounding an eighth or longer must be a tone of its chord or an allowed tension (9 and 13 over major
  and dominant, 9 and 11 over minor, 11 and b13 over m7(b5), ...: `allowed()`). A deliberate tension goes in
  its `ALLOW` list (style, track, interval, chord type).
- `sh tools/render_styles.sh [NN ...]` renders `build/styles/NN.wav`: the style at its tempo over the Pop
  template, the blues and the Minor template (Bossa and Swing also over ii-V-I and Jazz; Reggae and House
over Pop and Minor; Disco, Country and Latin over Pop and Minor; Ambient over Pop and Canon; Hip hop over Minor and Vamp; Gospel over Pop and
the Gospel template; the Waltz three times over the Waltz template), each pass the Intro, Main A, Main B,
  Fill BA, Main A, Fill AB, Main B, the ending (checking each section comes when it should); it prints each WAV's
  level (the styles sit between -25 and -30 dBFS RMS at the default master, with no sample at the synth's limit).

## The drums: the Groove MIDI Dataset

**Decision:** the Main and Fill drums come from Google Magenta's Groove MIDI Dataset (real drummers, played
to a click on an electronic kit), quantised to 24 a beat (20 of Polly's 480 ticks) with the drummer's timing and dynamics kept;
the endings' last hits and the extra percussion are written by hand. The licence was checked at its source
on 2026-10-09: the dataset page (<https://magenta.tensorflow.org/datasets/groove>) says *"The dataset is
made available by Google LLC under a Creative Commons Attribution 4.0 International (CC BY 4.0) License."*,
and the download's `LICENSE` (groove-v1.0.0-midionly.zip) says *"This work is licensed under the Creative
Commons Attribution 4.0 International License."* (kept here as `drums/LICENSE`). Only the MIDI files the
styles use are kept, under `drums/` (renamed: `drummerN-sessionM-` + the dataset's file name).

**Credit:** drum performances from the Groove MIDI Dataset by Jon Gillick, Adam Roberts, Jesse Engel, Douglas
Eck and David Bamman (Google Magenta), © Google LLC, CC BY 4.0, <https://magenta.tensorflow.org/datasets/groove>.
Changes: excerpts of a few bars, the kit's notes mapped to General MIDI, timing moved to 24 steps a beat
and onto the click by the median offset, velocities scaled per instrument. Paper: Gillick, Roberts, Engel,
Eck, Bamman, "Learning to Groove with Inverse Sequence Transformations", ICML 2019.
