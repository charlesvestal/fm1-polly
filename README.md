# Polly for the M-VAVE FM-1

Polly is a pocket arranger inspired by the Yamaha QY70: a style plays, a chord progression steers it.
Its sounds are [Sonivox EAS](https://github.com/EmbeddedSynth/sonivox)'s 200 KB General MIDI set; MIDI in
plays it as a GM module too. The platform is [Felucca](https://github.com/hugelton/Felucca)'s. Private for now.
Try it in the browser at <https://charles.pizza/qy/> (an internal test build, unlisted). The full manual:
[MANUAL.md](MANUAL.md).

## Quick start

1. Switch on: the HOME page. Turn **KNOB 1** to pick a style (the header shows it).
2. Press **PLAY**. Main A plays, over C until you give it a chord.
3. Play a chord: three keys or more. The band follows, and keeps the chord after you let go. Inversions
   work (E G C is C); the lowest note is the bass, so a note under the chord that isn't its root, 3rd or
   5th makes a slash chord (D under C E G: C/D; B♭ under C E G: C/B♭).
4. Hold **HOME** and press a black key: 1 Intro, 2 Main A, 3 Main B, 4 Fill, 5 Ending, from the next bar.
   The Ending plays out and stops.
5. Hold **HOME** and press a white key: one of 16 loops plays, from the next bar (L1–L8 factory
   progressions, L9–L16 your own). With no loop playing (KNOB 4 "–"), **REC**, chords a bar each,
   **REC** again: a new loop of yours. Holding HOME shows the loops and sections on the screen.
6. **HOME**, **SELECT**: the SONG page. Each white key adds a row of that loop; **PLAY** there plays the
   song (Intro, rows with fills, Ending). MANUAL.md's "Writing a song" walks it through.
7. Press **SAVE**. It's lit while something isn't saved; Polly also saves by itself after a few seconds
   at rest, stopped.

**Your parts and the style.** Record into a section (SEQ, REC) and it plays your own user pattern from
then on. The Style knob changes only the sections still playing the style, so your part stays; the
header says which are yours ("Pop, A user"). To put a section back to the style: **SEL** on the Pattern
page, **BACK TO STYLE**. Mutes (SEQ, KNOB 4) are the song's: a muted track is muted in every section,
and muting never makes a user pattern.

**Your QY70 styles.** Polly can play the QY70's 128 preset styles, built from your own QY70 main ROM
(MAME's `qy70main.bin`); nothing of Yamaha's is in Polly. In the browser: press **Load QY70 ROM** on the
emulator's page (the ROM stays in your browser). On the FM-1, over USB:
`python3 tools/qy70_install.py qy70main.bin` (needs `pip3 install mido python-rtmidi`; about 30 s). They
then follow Polly's 16 styles on the Style knob, marked "QY".

## The controls

| Control | |
|---|---|
| PLAY / REC | start / stop (on SONG: the song from row 1); REC writes what the keys play here: chords into the loop, a new loop (HOME, no loop playing), the selected track (SEQ, EDIT), rows (SONG) |
| SEL | the page as a list: its values, MORE, and actions (LOAD PROGRESSION, CLEAR LOOP, PLAY FROM ROW, INSERT ROW, BACK TO STYLE…) |
| HOME held | the shortcuts on screen: + white key 1-16 a loop; + black key 1-5 Intro, Main A, Main B, Fill, Ending (from the next bar); + SELECT the tempo; + PRESETS the song |
| HOME | HOME: KNOB 1 style, 2 tempo, 3 transpose, 4 loop ("–": free, the band follows your chords); SELECT: SONG (rows: loop, section, repeat, mutes, key; intro, ending, at end, song) |
| ARP | LOOP: the loop's chords, a chord a beat (root, type, bass, step); page 2 length, beats, name |
| SEQ | Pattern (the 8 tracks; KNOB 1 the grid, KNOB 4 mute: the song's, every section); page 2 its length, beats, name |
| EDIT | Track: program, type, range; page 2 high key, channel |
| LFO | Mixer (volume, pan, sends); page 2 tone (bright, reso) |
| FX | Master FX |
| ENV | Style: the song's six patterns, KNOB 4 the style (two pages) |
| GLO | Setup: clock, MIDI out, lights (and the QY70 bank's state); page 2 Panel (CPU, voices, version) |
| SAVE | saves (lit: something to save; it also saves by itself at rest); held + a white key: copy the loop (HOME, LOOP) or the pattern (SEQ) there |
| SELECT / ALGORITHM | the page (a list: up / down) / the track |
| OCT- / OCT+ | the keys' octave (LOOP: the cursor) |

A button with more pages shows which one by the dots over the knobs; holding any button a second shows what it does there. MIDI in (USB, TRS) plays all 16 channels;
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
