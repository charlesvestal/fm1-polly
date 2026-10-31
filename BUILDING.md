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

A release is built on GitHub from its tag, so anyone can check that the package came from this source:
`.github/workflows/release.yml` calls the shared FM-1 builder,
[`zvenson/fm1-build` at `v1`](https://github.com/zvenson/fm1-build/tree/v1) (as LOOM-8, MONK-1, X0X and Melodee do).
The builder checks out the tagged commit, installs JieLi's Linux toolchain and the AC79 SDK at its fixed tag, runs
`./build.sh --release X.Y` (with fm1-base and felucca-bsp at the commits this one links) and `tests/run_tests.sh` (a
failing test stops the release; the QY70 checks need your own ROM and the emulator's need Emscripten, and are
skipped there), then signs build provenance for `polly-X.Y.fwsc` and `polly-X.Y-app.bin`. The Firmware Hub shows
such packages as built from source.

1. Write the notes first, as a draft: `gh release create vX.Y --draft --title "Polly X.Y" --notes-file NOTES.md`
   (the workflow keeps a draft's notes; without one it makes a draft from the commits).
2. Push the tag: `git tag vX.Y && git push origin vX.Y`. The workflow builds, tests, attests, checks the SHA-256 sums
   and the signatures against the builder, the tag and the commit, then publishes the draft. A tag with a suffix
   (`v0.6-beta`) is a prerelease. A published release is never rebuilt: a new build needs a new tag. A manual run
   on a branch (Actions, Run workflow) builds and attests a development package, in the run's `packages` artifact.
3. The site from that package: `gh release download vX.Y --pattern '*.fwsc' --dir app/build --clobber`, then
   `tools/publish_pages.sh X.Y`: it builds the browser emulator and the site (landing page, School, manual, browser
   emulator, web installer, mirror, backup, firmware) with `tools/make_pages.py` from `app/build/polly-X.Y.fwsc`, and
   pushes it to the `gh-pages` branch, which GitHub Pages serves at <https://charlesvestal.github.io/fm1-polly/>.

To check a downloaded package yourself (GitHub CLI, signed in):

```sh
tag=vX.Y repo=charlesvestal/fm1-polly
gh release download "$tag" --repo "$repo" --dir build/verify --pattern '*.fwsc'
git fetch origin "refs/tags/$tag:refs/tags/$tag"
gh attestation verify "build/verify/polly-${tag#v}.fwsc" --repo "$repo" \
  --signer-workflow zvenson/fm1-build/.github/workflows/build.yml \
  --source-ref "refs/tags/$tag" --source-digest "$(git rev-parse "$tag^{commit}")"
```

The same checks the `-app.bin`. `SHA256SUMS` comes with the downloads; a checksum alone proves nothing about where a
package was built.

A local release build (`./build.sh --release X.Y`, package `app/build/polly-X.Y.fwsc`; its identity FM-1_3XXYYZZ
is what the installer checks) is still how to try one on the FM-1 before tagging.
