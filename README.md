# Polly for the M-VAVE FM-1

Polly is a pocket arranger inspired by the Yamaha QY70: a style plays, a chord progression steers it.
Its sounds are [Sonivox EAS](https://github.com/EmbeddedSynth/sonivox)'s 200 KB General MIDI set; MIDI in
plays it as a GM module too. The platform is [Felucca](https://github.com/hugelton/Felucca)'s. Private for now.
Try it in the browser at <https://charles.pizza/qy/> (an internal test build, unlisted). The full manual:
[MANUAL.md](MANUAL.md).

## Quick start

The words, in one line each: a **song** is a list of **sections**; a section plays a **progression** (your chords,
P1–P16) with a **groove** (Groove A or B of the song's **style**), so many times; the style's **instruments** (D1 D2
PC BA C1–C4) play it; **melodies** M1, M2 run along the sections. MANUAL.md's cheat sheet has the picture.

1. Switch on: the Home page. Turn **KNOB 1** to pick a style (the header shows it).
2. Press **PLAY**. Groove A plays, over C until you give it a chord.
3. Play a chord: three keys or more. The band follows, and keeps the chord after you let go. Inversions
   work (E G C is C); the lowest note is the bass, so a note under the chord that isn't its root, 3rd or
   5th makes a slash chord (D under C E G: C/D; B♭ under C E G: C/B♭).
4. Hold **HOME** and press a black key: 1 Intro, 2 Groove A, 3 Groove B, 4 Fill, 5 Ending, from the next bar.
   The Ending plays out and stops.
5. Hold **HOME** and press a white key: one of 16 progressions plays, from the next bar (P1–P8 ready-made,
   P9–P16 your own). With none playing (KNOB 4 "–": free), **REC**, chords a bar each, **REC** again: a new
   progression of yours. Holding HOME shows the progressions and the grooves on the screen.
6. **HOME**, **SELECT**: the Song page. Each white key adds a section playing that progression; **PLAY** there plays
   the song (Intro, sections with fills, Ending). MANUAL.md's "Writing a song" walks it through.
7. Press **SAVE**. It's lit while something isn't saved; Polly also saves by itself after a few seconds
   at rest, stopped.

**Your grooves and the style.** Record into a groove (SEQ, REC) and it plays your own copy from then on, "(yours)"
on the screen. The Style knob changes only what still plays the style, so your copy stays; the header says which
are yours ("Pop · Groove A (yours)"). To give a groove back to the style: **SEL** on the Band page, **BACK TO
STYLE**. Mutes (SEQ, KNOB 4) are the song's: a muted instrument is muted in every groove, and muting never makes a
copy.

**Your QY70 styles.** Polly can play the QY70's 128 preset styles, built from your own QY70 main ROM
(MAME's `qy70main.bin`); nothing of Yamaha's is in Polly. In the browser: press **Load QY70 ROM** on the
emulator's page (the ROM stays in your browser). On the FM-1, over USB:
`python3 tools/qy70_install.py qy70main.bin` (needs `pip3 install mido python-rtmidi`; about 30 s). They
then follow Polly's 16 styles on the Style knob, marked "QY".

## The controls

| Control | |
|---|---|
| PLAY / REC | start / stop (on Song: the song from section 1); REC writes what the keys play here: chords into the progression, a new progression (Home, free), the selected instrument (Band, Sound), a melody (Sound, in a song), sections (Song) |
| SEL | the page as a list: its values, MORE, and actions (LOAD PROGRESSION, CLEAR PROGRESSION, PLAY FROM SECTION, INSERT SECTION, BACK TO STYLE…) |
| HOME held | the shortcuts on screen: + white key 1-16 a progression; + black key 1-5 Intro, Groove A, Groove B, Fill, Ending (from the next bar); + SELECT the tempo; + PRESETS the song; + REC undo, + PLAY redo |
| HOME | Home: KNOB 1 style, 2 tempo, 3 transpose, 4 prog ("–": free, the band follows your chords); SELECT: Song (sections: prog, groove, repeat, mutes, transpose; intro, ending, at end, song) |
| ARP | Progression: its chords, a chord a beat (bar, beat, root, type); page 2 length, beats, bass, name |
| SEQ | Band (the groove's 8 instruments; KNOB 1 the grid, KNOB 2, 3 the cursor's bar and beat, KNOB 4 mute: the song's, every groove); page 2 Groove length (its length, beats, name) |
| EDIT | Sound: program, type, range; page 2 high key, channel |
| LFO | Mixer (volume, pan, sends); page 2 tone (bright, reso) |
| FX | Effects (reverb, chorus) |
| ENV | Style: which style each groove and extra plays, KNOB 4 the song's style (two pages) |
| GLO | Setup: clock, MIDI out, lights, click (and the QY70 bank's state); page 2 Panel (CPU, voices, version) |
| SAVE | saves (lit: something to save; it also saves by itself at rest); held + a white key: copy the progression (Home, Progression) or the groove (Band) there |
| SELECT / ALGORITHM | the page (a list: up / down) / the instrument (D1…C4, then the melodies M1, M2) |
| OCT- / OCT+ | the keys' octave (Progression: the cursor) |

A button with more pages shows which one by the dots over the knobs, and the line over the knobs says where you are
("S1 › Pop › Groove A › C1 Piano"); holding any button a second shows what it does there. MIDI in (USB, TRS) plays
all 16 channels; its clock can drive the arranger (Setup).

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
