# Polly on the M-Vave SMK-37

Polly also builds for the M-Vave SMK-37, a 37-key controller with the same chip as the FM-1. The FM-1 build is
unchanged; everything for the SMK-37 is behind `BOARD_SMK37`. On the SMK-37 it is the same Polly: the same
styles, songs, pages, screen and manual.

Until this project's releases include SMK-37 packages, unofficial builds can be installed with the web installer:
https://genkigenki.github.io/smk37/

## Supported models

- **SMK-37 Pro**: the default build (package identity `SMK-37 Pro_300`).
- **SMK-37 Elite**: `MODEL=elite` (identity `SMK-37 Elite_300`).
- **MKE-P37**: `MODEL=mke` (identity `MKE-P37_300`).
- **Starrykey 37 Play**: uses the default build.

All of them are the same hardware. Each build carries its model's name, so M-Vave's updater still takes that
unit's stock package later: install the package for your model. The installers take a package for any of these
models, and refuse an FM-1 package (and an SMK package on an FM-1).

## Controls

The FM-1's controls keep their Polly meaning, so the manual applies; the screen keeps the FM-1's names (HOME,
SEL, ENV, ...). Holding HOME and pressing a key works as on the FM-1. The SMK-37 adds the rest.

| SMK-37 | Polly |
| --- | --- |
| The middle 27 keys (F3..G5) | the FM-1's keys: chords on Home and the Progression page, the selected instrument on Band, Sound and Notes, sections and mutes on the Song page, a strip on the Mixer |
| The outer 10 keys (C3..E3, G#5..C6) | play as the other keys do: in chords, and as notes of the selected instrument |
| Key velocity | the instruments' notes play and record with the keys' velocity; a melody keeps its own VELOCITY setting |
| Pads, top row | pads 1-6: Intro, Groove A, Groove B, Fill, Ending (as HOME + black keys 1-5), FOLLOW (no progression: the band follows your chords, as KNOB 4 at "follow"); pad 7: free (dark); pad 8: switches the bottom row between P1-P8 (blue) and P9-P16 (yellow) |
| Pads, bottom row | eight progressions, P1-P8 or P9-P16: a pad plays its progression (as HOME + a white key; the one playing again: follow). On the Song page it adds a section, as a white key there. Hold SAVE (BT) and press one to copy there, as SAVE + a white key |
| Pad lights | the sections and FOLLOW light green, the progressions in their half's colour (P1-P8 blue, P9-P16 yellow): dim, bright for what plays (stopped: where PLAY starts). A change waiting for the bar line: the pad playing blinks, the one coming is light orange. Empty progressions are dim too, so the active half always shows |
| K1 | MASTER (volume) |
| K2 / K3 / K4 | SELECT / ALGORITHM / PRESETS |
| K5..K8 | KNOB 1..4 |
| PATCH | HOME: Home, the Song page; held, the shortcuts |
| ARP / SEQ | ARP / SEQ: the Progression page, the Band page |
| PARA | EDIT: the Sound page |
| NOTE REPEAT / FX | LFO / FX: the Mixer, Effects |
| CHORD / GLOBE | ENV / GLO: the Style page, Setup |
| SCALE | SEL: the page as a list |
| BT | SAVE |
| SEQ PLAY / SEQ REC | PLAY / REC |
| OCTAVE- / OCTAVE+ | OCT- / OCT+ |
| < / > | SELECT one step back / on: the pages, a list's rows, Notes' notes (never the tempo) |
| KNOB BANK | the pads' map on the screen (the screen HOME held shows): the sections and the progressions. Lit while it is up; press again to put it away |
| FADER BANK | what the faders set: A the volumes of D1, D2, PC and BA; B of C1, C2, C3 and C4; C of the melodies M1 and M2, and the reverb and chorus levels. Lit off bank A; the header says the bank. Puts the map away |
| Faders 1..4 | the bank's four values, as the Mixer's VOLUME (the groove playing's, or the song's melodies') and the Effects page's levels; a fader takes over only once it moves (also after a bank change). The header says what it set |
| Pitch wheel | pitch bend, +-2 semitones, on every channel but the drums', at once |
| Mod wheel | vibrato (General MIDI modulation, up to +-1/2 semitone), on every channel but the drums' |
| OCTAVE- + OCTAVE+ held 5 s | UPDATE MODE |
| OCTAVE- + OCTAVE+ held at power-on | HARDWARE CALIBRATION: teach the buttons and knobs again (it asks by the FM-1's names) |

Only one bank mode is up at a time: FADER BANK puts the map away. The wheels never change what is saved: the
bend is gone when the pitch wheel is back at rest, the vibrato with the mod wheel at zero. The SMK-37 has no key
lights and no dim button glow, so Setup's Lights makes no difference there; the pads are always lit.

The SMK-37's TRS jack is a MIDI output and this build does not use it; there is no TRS MIDI IN, so MIDI and its
clock come in over USB only.

**The QY70 styles are not available on the SMK-37.** Their bank takes 64 flash sectors; on the SMK-37 the
flash outside the keyboard's protected areas holds Polly's songs and memory, and the bank does not fit beside
them. `tools/qy70_install.py` is refused there (nothing is written); Polly's 16 styles play as ever.

## Build

See [BUILDING.md](BUILDING.md) for the toolchain.

```sh
BOARD=smk37 ./build.sh                 # build/polly-smk37.fwsc (SMK-37 Pro, Starrykey 37 Play)
BOARD=smk37 MODEL=elite ./build.sh     # build/polly-smk37-elite.fwsc
BOARD=smk37 MODEL=mke ./build.sh       # build/polly-smk37-mke.fwsc
./build.sh                             # the FM-1 build again before tests/run_tests.sh (the tests use its files)
```

## Install

Connect the keyboard by USB and run, with the package for your model:

```sh
python3 tools/fm1_install.py build/polly-smk37.fwsc
```

`fm1_install.py` needs `mido` and `python-rtmidi` (`pip3 install mido python-rtmidi`). It checks that the package
is for the connected keyboard. The keyboard shows up as "Polly SMK-37".

## Going back to the stock firmware

Install M-Vave's firmware package for your model with M-Vave's updater, M-UPGRADE. It works directly, because
the installed package keeps the keyboard's model name.

## Safety

- Back up first: have M-Vave's stock package for your model and M-UPGRADE ready before you install.
- Keep the USB cable connected until the install has finished and the keyboard has restarted.
- The first start clears the stock firmware's settings, and any other firmware's data, in the areas Polly
  stores in.
- The firmware never writes the keyboard's calibration, Bluetooth pairing or key areas; the flash map refuses
  them at compile time and again in the flash driver.
- Installing other firmware is at your own risk.
