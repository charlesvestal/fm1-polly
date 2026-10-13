# Polly for the M-VAVE FM-1

Polly is a pocket arranger for the M-VAVE FM-1. It's inspired by the Yamaha QY70. A style plays a band of eight
instruments, and the chords you play steer it. The sounds come from the General MIDI set of
[Sonivox EAS](https://github.com/EmbeddedSynth/sonivox). MIDI in plays them too, so Polly also works as a
General MIDI sound module. Polly is built on the platform layer of [Felucca](https://github.com/hugelton/Felucca).

Install it, learn it or try it in the browser on [Polly's site](https://charlesvestal.github.io/fm1-polly/).

- Start with [School](docs/course/README.md). It has ten short lessons, and each one comes with a video.
- For all the details, see [MANUAL.md](MANUAL.md). For the words and the common tasks on one page, see its
  [cheat sheet](MANUAL.md#cheat-sheet).

## The words

A **song** is a list of **sections**. Each section plays a chord **progression** a set number of times. There
are 16 progressions, P1 to P16. Each **style** has two **grooves**, Groove A and Groove B, and each section plays
one of them in the song's style. The style's eight **instruments** are D1, D2, PC, BA and C1–C4. Two **melodies**, M1
and M2, run along the sections.

## Good to know

**Your grooves and the style.** When you record into a groove with **SEQ** and **REC**, Polly makes an edited
copy and plays it from then on, and the style itself doesn't change. The groove's pill then shows "(edited)", as in "Groove A (edited)". When you turn the Style knob, only
the grooves that still play the style change, so your edited copy stays. To give a groove back to the style, press
**SEL** on the Band page and choose BACK TO STYLE. You set mutes with **KNOB 4** on the Band page, and they
belong to the song. A muted instrument is muted in every groove, and muting never makes a copy.

**Your QY70 styles.** If you own a QY70, Polly can play its 128 preset styles. Polly builds them from your QY70's
main ROM, which MAME calls `qy70main.bin`. Nothing of Yamaha's comes with Polly. In the browser, press **Load QY70 ROM** on the
emulator's page, and the ROM stays in your browser. To put them on the FM-1, connect it with a USB cable and run
`python3 tools/qy70_install.py qy70main.bin`. The tool needs `pip3 install mido python-rtmidi`,
and the install takes about 30 seconds. The QY70 styles then follow Polly's 16 styles on the Style knob, whose label then reads "QY style".

## The controls

| Control | What it does |
|---|---|
| **PLAY** / **REC** | **PLAY** starts and stops. On the Song page, it plays the song from section 1. **REC** records what the keys play on this page. On Home it records a new progression. On Band and Sound it records the selected instrument or melody, and on Song the sections. |
| **SEL** | Shows the page as a list over the knobs. The list has the page's values and its actions, such as CLEAR PROGRESSION or BACK TO STYLE. |
| **HOME**, held | While you hold it, the screen shows the shortcuts. Press a white key 1–16 for a progression, or black key 1–5 for the Intro, Groove A, Groove B, a Fill or the Ending, from the next bar. Turn **SELECT** for the tempo, or **PRESETS** for the song. Press **REC** to undo and **PLAY** to redo. |
| **HOME** | Opens Home. **KNOB 1** is the style, **KNOB 2** the tempo, **KNOB 3** the transpose and **KNOB 4** the progression. With the progression on "follow", the band follows the chords you play. Press **HOME** again for the Song page, which lists the sections. Turn **SELECT** there for page 2, with each section's transpose and the song's Intro, Ending and At end. |
| **ARP** | Opens the Progression page, where you write chords by bar, beat, root and type. Page 2 has the length, beats, bass and name. |
| **SEQ** | Opens the Band page, which shows the groove's eight instruments. **KNOB 1** is the grid, **KNOB 2** and **KNOB 3** move the cursor by bar and beat, and **KNOB 4** mutes. Page 2 sets the groove's length, beats and name. Press **SEQ** again to edit notes. |
| **EDIT** | Opens the Sound page, with the sound, type and range. Page 2 has the high key and channel. A melody has one page with its sound, channel and velocity. |
| **LFO** | Opens the Mixer, with volume, pan and sends. Page 2 sets the tone with Bright and Reso. |
| **FX** | Opens Effects, with reverb and chorus. The page shows each instrument's sends. |
| **ENV** | Opens the Style page. It sets which style each groove plays, on two pages. **KNOB 4** is the song's style. |
| **GLO** | Opens Setup, with the clock, MIDI out, lights, click and QY70 styles. Page 2 shows the CPU load, voices and version. |
| **SAVE** | Saves. It's lit when something isn't saved, and Polly also saves by itself when it's stopped. On Home or the Progression page, hold it and press a white key to copy the progression there. On the Band page, it copies the groove. |
| **SELECT** / **ALGORITHM** | **SELECT** turns the page, or moves up and down in a list. **ALGORITHM** picks the instrument, from D1 to C4 and then the melodies M1 and M2. |
| **OCT−** / **OCT+** | Move the keys an octave. On Progression, they move the cursor. |

The dots over the knobs show which page of a button you're on. The line over the knobs shows where you are,
such as "S1 › Pop › Groove A › C1 Piano". Hold any button for a second to see what it does there. Each
colour means one thing. Teal is what sounds now, and coral is what you do. Slate and amber are Groove A and B,
plum is the melodies, and ink is everything else. MIDI in, over USB or TRS, plays all 16
channels. On the Setup page, you can let its clock drive the arranger.

## Building

```sh
./build.sh                                  # build/polly.fwsc (identity FM-1_300)
sh tests/run_tests.sh                       # host tests, the app in the simulator, the web build
python3 tools/fm1_install.py build/polly.fwsc --yes
web/emu/publish.sh                          # the emulator at charles.pizza/qy/ ([QY70_ROM=...] only for its checks)
```

The factory styles are text files in `styles/`, and `tools/make_styles.py` compiles them. `styles/README.md`
describes the format. Their drums are performances from Google Magenta's
[Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) (Gillick, Roberts, Engel, Eck,
Bamman; © Google LLC, CC BY 4.0), excerpted and quantised.

See LICENSING.md.
