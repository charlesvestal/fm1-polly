# Polly for the M-VAVE FM-1

Polly is a pocket arranger inspired by the Yamaha QY70: a style plays, a chord progression steers it.
Its sounds are [Sonivox EAS](https://github.com/EmbeddedSynth/sonivox)'s 200 KB General MIDI set; MIDI in
plays it as a GM module too. The platform is [Felucca](https://github.com/hugelton/Felucca)'s. Private for now.
Try it in the browser at <https://charles.pizza/qy/> (an internal test build, unlisted). The full manual:
[MANUAL.md](MANUAL.md).

## Quick start

1. Switch on: the Song page. Turn **KNOB 1** to pick a style (the header shows it).
2. Press **PLAY**. The Intro plays, then Main A, over C major 7 until you give it a chord.
3. Tap **SEL**: CHORD lights, and the keys enter chords. Hold three or more keys: the band follows, and
   keeps the chord after you let go. Inversions work (E G C is C); the lowest note is the bass, so a
   note under the chord that isn't its root, 3rd or 5th makes a slash chord (D under C E G: C/D; B♭
   under C E G: C/B♭). Tap SEL again and the keys play the selected track (ALGORITHM).
4. Hold **SEL** and press a black key: 1 Intro, 2 Main A, 3 Main B, 4 Fill, 5 Ending, from the next bar.
   The Ending plays out and stops.
5. Hold **SEL** and press a white key: one of 16 chord progressions plays at once, looping, until you
   play a chord. Holding SEL shows both lists on the screen.
6. Turn **SELECT** for the tempo (40 to 240).
7. Press **SAVE**. It's lit while something isn't saved; Polly also saves by itself after a few seconds
   at rest, stopped.

**Your parts and the style.** Record into a section (SEQ, REC) and it plays your own user pattern from
then on. The Style knob changes only the sections still playing the style, so your part stays; the
header says which are yours ("Pop, A user"). To put a section back to the style: hold SAVE on the Pattern
page, job **Back to style**. Mutes (SEQ, KNOB 4) are the song's: a muted track is muted in every section,
and muting never makes a user pattern.

**Your QY70 styles.** Polly can play the QY70's 128 preset styles, built from your own QY70 main ROM
(MAME's `qy70main.bin`); nothing of Yamaha's is in Polly. In the browser: press **Load QY70 ROM** on the
emulator's page (the ROM stays in your browser). On the FM-1, over USB:
`python3 tools/qy70_install.py qy70main.bin` (needs `pip3 install mido python-rtmidi`; about 30 s). They
then follow Polly's 16 styles on the Style knob, marked "QY".

## The controls

| Control | |
|---|---|
| PLAY / REC | start / stop; record into the selected track (on Chords: the chords played) |
| SEL tap | CHORD on / off: the keys enter chords, or play the selected track |
| SEL held | the shortcuts on screen: + black key 1-5 Intro, Main A, Main B, Fill, Ending (from the next bar); + white key 1-16 a progression (at once); + PRESETS the song |
| HOME | Song: KNOB 1 style (PRESETS too), 2 template, 3 key, 4 record grid (clockwise finer: 8, 8T, 16, 16T, 32, off) |
| SEQ | Pattern (the 8 tracks; KNOB 1 the grid, KNOB 4 mute: the song's, every section); again: its length, beats, name |
| EDIT | Track: program, type, range; again: high key, channel |
| FX | Mixer (volume, pan, sends); again: tone (bright, reso); again: Master FX |
| LFO | Chords: the song's chords, beat by beat |
| ENV | Arrange: sections, length, loop, KNOB 4 the song; again (twice): the song's six patterns, KNOB 4 the style |
| ARP | Templates: browse, audition (PLAY); again: copy one into the song |
| GLO | Setup: clock, MIDI out, key, lights, the QY70 bank; again: Panel (CPU, version) |
| SAVE | saves (lit: something to save; it also saves by itself at rest); held: the jobs (pattern: erase, copy, clear, back to style; song: clear, copy) |
| SELECT / ALGORITHM | tempo / the track |
| OCT- / OCT+ | the keys' octave |

A button with more pages shows which one in the header (dots). MIDI in (USB, TRS) plays all 16 channels;
its clock can drive the arranger (Setup).

## Building

```sh
./build.sh                                  # build/polly.fwsc (identity FM-1_300)
sh tests/run_tests.sh                       # host tests, the app in the simulator, the web build
python3 tools/fm1_install.py build/polly.fwsc --yes
web/emu/publish.sh                          # the emulator at charles.pizza/qy/ ([QY70_ROM=...] only for its checks)
```

Factory styles are text files in `styles/` (the format: `styles/README.md`), compiled by
`tools/make_styles.py`. Their drums are performances from Google Magenta's
[Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) (Gillick, Roberts, Engel, Eck,
Bamman; © Google LLC, CC BY 4.0), excerpted and quantised.

See LICENSING.md.
