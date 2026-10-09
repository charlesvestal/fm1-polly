# Polly for the M-VAVE FM-1

Polly is becoming a pocket arranger inspired by the Yamaha QY70: a style plays, a chord progression
steers it. Today it is still the GM sound module it started as, running
[Sonivox EAS](https://github.com/EmbeddedSynth/sonivox)'s 200 KB General MIDI sound set; the arranger
is in progress. The platform is [Felucca](https://github.com/hugelton/Felucca)'s. Private for now.
Try it in the browser at <https://charles.pizza/fm1-polly/>.

## Playing it (the GM module, today)

| Control | |
|---|---|
| Keys | play the selected channel |
| OCT− / OCT+ | the keys' octave |
| PRESETS | the channel's program (SELECT: a family of eight at a time) |
| ALGORITHM | the channel (1..16; 10 is the drums) |
| KNOB 1–4 | the channel's volume, pan, reverb, chorus |
| PLAY | a demo song (to hear it, and to load the synth) |
| HOME | all notes off |
| GLO | lights |
| SAVE | saves (it also saves by itself, a few seconds after a change) |

MIDI in (USB, TRS) plays all 16 channels.

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
