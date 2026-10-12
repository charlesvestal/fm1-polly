# Polly for the M-VAVE FM-1

Polly is a pocket arranger for the M-VAVE FM-1, inspired by the Yamaha QY70. A style plays a band of eight
instruments, and the chords you play steer it. Its sounds are the 200 KB General MIDI set from
[Sonivox EAS](https://github.com/EmbeddedSynth/sonivox). MIDI in plays them too, so Polly also works as a
General MIDI sound module. It's built on the platform layer of [Felucca](https://github.com/hugelton/Felucca).
Polly is private for now.

You can try it in the browser at <https://charles.pizza/qy/>. That's an internal test build, and it's
unlisted.

- [School](docs/course/README.md): ten short lessons, each with a video. Start here.
- [MANUAL.md](MANUAL.md): the full manual. Its [cheat sheet](MANUAL.md#cheat-sheet) has the words and the
  common tasks on one page.

## The words

A **song** is a list of **sections**. Each section plays a **progression** (your chords, P1–P16) with a
**groove** (Groove A or Groove B of the song's **style**), a set number of times. The style's eight
**instruments** play it: D1, D2, PC, BA and C1–C4. Two **melodies**, M1 and M2, run along the sections.

## Good to know

**Your grooves and the style.** When you record into a groove (**SEQ**, **REC**), it plays your own copy
from then on, and the screen shows "(yours)". Turning the Style knob changes only what still plays the
style, so your copy stays. The groove's pill shows it's yours: "Groove A (yours)". To give a groove back to
the style, press **SEL** on the Band page and choose BACK TO STYLE. Mutes (**SEQ**, **KNOB 4**) belong to the
song. A muted instrument is muted in every groove, and muting never makes a copy.

**Your QY70 styles.** Polly can play the QY70's 128 preset styles, built from your own QY70 main ROM
(MAME's `qy70main.bin`). Nothing of Yamaha's is in Polly. In the browser, press **Load QY70 ROM** on the
emulator's page, and the ROM stays in your browser. On the FM-1, connect it over USB and run
`python3 tools/qy70_install.py qy70main.bin`. This needs `pip3 install mido python-rtmidi` and takes about
30 seconds. The QY70 styles then follow Polly's 16 styles on the Style knob, marked "QY".

## The controls

| Control | What it does |
|---|---|
| **PLAY** / **REC** | **PLAY** starts and stops. On the Song page, it plays the song from section 1. **REC** records what the keys play on this page: chords, a new progression (on Home, when free), the selected instrument (Band, Sound), a melody (Sound, in a song) or sections (Song). |
| **SEL** | Shows the page as a list, over the knobs: its values, MORE, and actions such as LOAD PROGRESSION, CLEAR PROGRESSION, PLAY FROM SECTION, INSERT SECTION, CLEAR C1 and BACK TO STYLE. |
| **HOME**, held | Shows the shortcuts on screen. Add a white key 1–16 for a progression. Add black key 1–5 for the Intro, Groove A, Groove B, a Fill or the Ending, from the next bar. Turn **SELECT** for the tempo, or **PRESETS** for the song. Press **REC** to undo, **PLAY** to redo. |
| **HOME** | Home. **KNOB 1** is the style, **KNOB 2** the tempo, **KNOB 3** the transpose, and **KNOB 4** the progression ("–" is free: the band follows your chords). Press **HOME** again for the Song page: each section's progression, groove, repeat and mutes. Turn **SELECT** there for Song page 2: a section's transpose, and the song's Intro, Ending and At end. |
| **ARP** | Progression: its chords, one per beat, by bar, beat, root and type. Page 2 has its length, beats, bass and name. |
| **SEQ** | Band: the groove's eight instruments. **KNOB 1** is the grid, **KNOB 2** and **KNOB 3** the cursor's bar and beat, **KNOB 4** the mute. Page 2 is Groove length, with the length, beats and name. Press **SEQ** again for Notes, the note editor. |
| **EDIT** | Sound: the sound (by name), type and range. Page 2 has the high key and channel. A melody has one page, with its sound, channel and velocity. |
| **LFO** | Mixer: volume, pan and sends. Page 2 is tone: bright and reso. |
| **FX** | Effects: reverb and chorus. The page shows each instrument's sends. |
| **ENV** | Style: which style each groove and extra plays, on two pages. **KNOB 4** is the song's style. |
| **GLO** | Setup: clock, MIDI out, lights, click, and the QY70 bank's state. Page 2 is Status: CPU, voices and version. |
| **SAVE** | Saves. It's lit while something isn't saved, and Polly also saves by itself at rest. Hold it and press a white key to copy the progression (Home, Progression) or the groove (Band) there. |
| **SELECT** / **ALGORITHM** | **SELECT** turns the page, or moves up and down in a list. **ALGORITHM** picks the instrument: D1 to C4, then the melodies M1 and M2. |
| **OCT−** / **OCT+** | Move the keys an octave. On Progression, they move the cursor. |

The dots over the knobs show which page of a button you're on. The line over the knobs shows where you are,
such as "S1 › Pop › Groove A › C1 Piano". Hold any button for a second to see what it does there. Each
colour means one thing: teal is what sounds now, coral is you (the cursor, an edit, REC), slate and amber
are Groove A and B, plum is the melodies, and ink is everything else. MIDI in (USB and TRS) plays all 16
channels, and its clock can drive the arranger (Setup).

## Building

```sh
./build.sh                                  # build/polly.fwsc (identity FM-1_300)
sh tests/run_tests.sh                       # host tests, the app in the simulator, the web build
python3 tools/fm1_install.py build/polly.fwsc --yes
web/emu/publish.sh                          # the emulator at charles.pizza/qy/ ([QY70_ROM=...] only for its checks)
```

Factory styles are text files in `styles/`, compiled by `tools/make_styles.py`. The format is described in
`styles/README.md`. Their drums are performances from Google Magenta's
[Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) (Gillick, Roberts, Engel, Eck,
Bamman; © Google LLC, CC BY 4.0), excerpted and quantised.

See LICENSING.md.
