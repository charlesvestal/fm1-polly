# Building Polly

Polly is an app on [fm1-base](https://github.com/charlesvestal/fm1-base) (in `base/`, a git submodule, with
[felucca-bsp](https://github.com/hugelton/felucca-bsp) inside it as `base/bsp/`). Get both with the source:

```
git clone --recursive https://github.com/charlesvestal/fm1-polly
git submodule update --init --recursive      # (a checkout made without --recursive)
```

The build makes three files in `app/build/`:

| File | What |
| --- | --- |
| `polly.bin` | the firmware app |
| `loader/ota.bin` | the update loader (felucca-bsp's) |
| `polly.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow, with libraqm for the fonts (Homebrew's Python: `pip3 install Pillow`); `PYTHON` picks
  another Python.
- Docker Desktop, or podman (a `docker` script that runs `exec podman "$@"` first on your PATH). The JieLi
  toolchain is Linux x86-64 only; the build runs it in a `linux/amd64` container (Rosetta on Apple silicon). Keep
  the source tree in a folder the container can share, e.g. under `/Users`.
- The JieLi Linux toolchain: `base/tools/get_toolchain.sh` (installs to `~/.jieli/toolchain`).
- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files (`cpu/wl82/tools/uboot.boot`,
  `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree. fm1-base's README has the clone command.
- Emscripten and Node.js (optional: the browser emulator and the web tests).

On Linux x86-64 the toolchain runs natively and no container is needed.

## Build

```
./build.sh                    # app/build/polly.fwsc (identity FM-1_300)
./build.sh --release 0.5      # app/build/polly-0.5.fwsc (identity FM-1_3000500)
./build.sh --host             # the simulator: app/build/host/polly_host
web/emu/build.sh              # the browser emulator: app/build/emu/ (fm1-base's --web, and Polly's page and bank builder)
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations (`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).
`./build.sh` runs fm1-base's `tools/build.py` on `app/`; `app/fm1.toml` says what goes in (Polly's sources, the
parts of fm1-base it uses: USB audio in, screen mirroring, backup and restore). The build fails if a soft-double
routine is linked (a `double` crept in), checks that nothing outside felucca-bsp touches a register, and checks the
image, RAM and pool sizes.

## Tests

```
tests/run_tests.sh            # Polly's
base/tests/run_tests.sh       # fm1-base's (the platform, the installer, backup and restore, the mirror)
```

Polly's: the arranger and its parts, the GM synth, the factory styles, the QY70 bank (with `QY70_ROM` set, against
your own ROM), the store map, the update path against `app/build/polly.fwsc`, the remote, the browser build, and the
whole app in the simulator (`tests/scenarios/*.polly`), with its screenshots and audio in `app/build/scenarios/`.

`app/build/host/polly_host SCRIPT OUTDIR` runs one script: fm1-base's commands are at the top of
`base/host/plat_host.c`, Polly's own at the top of `app/host.c`.

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 base/tools/fm1_install.py app/build/polly.fwsc
python3 base/tools/fm1_install.py --info     # identity of the connected FM-1
```

Installing firmware is at your own risk. Charge the FM-1 first, and keep the computer awake and the cable in until
the install is done: an update cut off by a flat battery is not picked up again. Hold OCT− and OCT+ for 5 seconds for
the update mode. If the FM-1 no longer starts but reaches the chip's update mode (4C4A:8057 on USB), felucca-bsp's
`base/bsp/tools/fm1_rescue.py` puts stock firmware back; otherwise recovery needs
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## Publishing a release

1. `./build.sh --release X.Y` (the identity, FM-1_3XXYYZZ, is what the installer checks).
2. `tools/publish_pages.sh X.Y`: builds the site (landing page, School, manual, browser emulator, web installer,
   mirror, backup, firmware) with `tools/make_pages.py` and pushes it to the `gh-pages` branch, which GitHub Pages
   serves at <https://charlesvestal.github.io/fm1-polly/>.
3. `gh release create vX.Y app/build/polly-X.Y.fwsc` for the command-line download.
