# Polly for the M-VAVE FM-1

Polly is a pocket arranger inspired by the Yamaha QY70: a style plays, a chord progression steers it.
Its sounds are [Sonivox EAS](https://github.com/EmbeddedSynth/sonivox)'s 200 KB General MIDI set; MIDI in
plays it as a GM module too. The platform is [Felucca](https://github.com/hugelton/Felucca)'s. Private for now.
Try it in the browser at <https://charles.pizza/fm1-polly/>.

## Playing it

Boot, turn KNOB 1 to pick a style, hold SEL and press a white key for a progression, press PLAY. Hold SEL
and press a black key for the next section; tap SEL and the keys play chords.

| Control | |
|---|---|
| PLAY / REC | start / stop; record into the selected track (on Chords: the chords played) |
| SEL tap | CHORD on / off: the keys enter chords, or play the selected track |
| SEL held | the shortcuts on screen: + black key 1-5 Intro, Main A, Main B, Fill, Ending (from the next bar); + white key 1-16 a progression (at once); + PRESETS the song |
| HOME | Song: KNOB 1 style (PRESETS too), 2 template, 3 key, 4 record grid |
| SEQ | Pattern (the 8 tracks, mute); again: its length, beats, name |
| EDIT | Track: program, type, range; again: high key, channel |
| FX | Mixer (volume, pan, sends); again: tone (bright, reso); again: Master FX |
| LFO | Chords: the song's chords, beat by beat |
| ENV | Arrange: sections, length, loop, KNOB 4 the song; again (twice): the song's six patterns, KNOB 4 the style |
| ARP | Templates: browse, audition (PLAY); again: copy one into the song |
| GLO | Setup: clock, MIDI out, key, lights, the QY70 bank; again: Panel (CPU, version) |
| SAVE | saves (lit: something to save; it also saves by itself at rest); held: the jobs |
| SELECT / ALGORITHM | tempo / the track |
| OCT- / OCT+ | the keys' octave |

A button with more pages shows which one in the header (dots). MIDI in (USB, TRS) plays all 16 channels;
its clock can drive the arranger (Setup).

## Building

```sh
./build.sh                                  # build/polly.fwsc (identity FM-1_300)
sh tests/run_tests.sh                       # host tests, the app in the simulator, the web build
python3 tools/fm1_install.py build/polly.fwsc --yes
web/emu/publish.sh                          # the emulator at charles.pizza/fm1-polly
```

Factory styles are text files in `styles/` (the format: `styles/README.md`), compiled by
`tools/make_styles.py`. Their drums are performances from Google Magenta's
[Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) (Gillick, Roberts, Engel, Eck,
Bamman; © Google LLC, CC BY 4.0), excerpted and quantised.

See LICENSING.md.
