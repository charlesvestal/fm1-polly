# Life for the M-VAVE FM-1

A Game of Life sequencer playing Plinky's synth. The 16 × 16 world is a Game of Life: columns are steps,
rows are notes of the scale. Four voices walk across it at their own speeds and play the live cells
they find, each with its own sound; an empty column is a rest, and the world keeps changing under them.
The keys plant seeds: the key's note is a row, the seed lands just ahead of the selected voice.

The sequencer is [plinky-life](https://github.com/charlesvestal/plinky-life) (the Plinky 12 panel),
brought over with its logic unchanged; the synth is the original
[Plinky](https://github.com/plinkysynth/plinky_public)'s engine (MIT), ported; the platform is
[Felucca](https://github.com/hugelton/Felucca)'s. Private for now. Try it in the browser at
<https://charles.pizza/fm1-gm/>.

## Playing it

| Control | |
|---|---|
| Keys | plant a seed (the shape is on the World page's second page); the note sounds through the selected voice |
| PLAY | start / stop |
| REC | freeze the world |
| OCT− | sprinkle cells in; held 1 s: clear the world |
| OCT+ | one generation (also while stopped) |
| ENV LFO ARP SEQ | voices 1–4: select; held: mute / unmute |
| HOME / EDIT / FX / GLO / SAVE | the World, Voice, Sound, Setup and Scenes pages (press again for the next page) |
| SELECT | tempo |
| ALGORITHM / PRESETS | the cursor; SEL flips the cell under it |
| KNOB 1–4 | the page's four values |

On the Scenes page KNOB 1 picks one of eight slots, SAVE saves into it and SEL loads it. A scene holds
the world and every voice and sound; Setup (key, scale, MIDI) is global. Life also saves by itself a
few seconds after a change, while stopped.

MIDI: notes out per voice (channels 1–4 by default), clock out, clock in (it follows), plinky-life's
block of 35 controllers in and out (from CC 30, channel 1), the world as CCs 20–27, notes in planting
seeds.

## Building

```sh
./build.sh                                  # build/gm.fwsc (identity FM-1_300)
sh tests/run_tests.sh                       # host tests, the app in the simulator, the web build
python3 tools/fm1_install.py build/gm.fwsc --yes
web/emu/publish.sh                          # the emulator at charles.pizza/fm1-gm
```

`tools/import_plinky.py PLINKY_PUBLIC_DIR` regenerates the synth's tables from Plinky's sources.
See LICENSING.md.
