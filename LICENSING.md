# Polly licensing

Polly is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). If you distribute Polly, or firmware derived from it, you must give your recipients its complete
corresponding source under the same licence.

Polly is built on [fm1-base](https://github.com/charlesvestal/fm1-base) and
[felucca-bsp](https://github.com/hugelton/felucca-bsp), in `base/` (a git submodule, with felucca-bsp inside it as
`base/bsp/`). Their files are under the Mozilla Public License 2.0 (`MPL-2.0`), file by file, and stay so inside
Polly: MPL-2.0 files may be part of a GPL work. Both come from [Felucca](https://github.com/hugelton/Felucca) by Leo
Kuroshita (@kurogedelic), Hügelton Instruments, who approved his code there under MPL-2.0 (felucca-bsp#4). Their own
`LICENSING.md` files say where each of their parts comes from.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| The platform: the chip and board layer, the update loader, boot, audio, USB, the screen and drawing, panel and keys, storage, the screen reader, screen mirroring, backup and restore, the host simulator and browser emulator, the build, package, install and site tools, the installer page, the fonts (`base/`) | fm1-base and felucca-bsp, from Felucca, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, and Charles Vestal (screen mirroring and backup and restore came from Polly) | MPL-2.0 (their files) |
| The GM synth: `app/dsp/eas/` (Sonivox EAS, its 200 KB GM sound set, `eas_host_fm1.c` our host wrapper). Modified for Polly: a per-voice start offset, so a note-on starts on its sample within a block (`eas_wtsynth.c`, `eas_voicemgt.c`, `eas_wtengine.h`, `eas_synth.h`), the reverb and chorus sends summed in 32 bits and clamped to 16, and the effects ringing out after the last send and starting clean (`eas_voicemgt.c`, `eas_synth.h`, `eas_reverb.c`, `eas_chorus.c`), and CC 74 / CC 71 moving the voices' low-pass filter (cutoff and resonance, gliding a block at a time, saturating when moved; `eas_wtsynth.c`, `eas_filter.c`, `eas_filter.h`, `eas_wtengine.h`, `eas_synth.h`, `eas_voicemgt.c`, `eas_midictrl.h`); each change marked "Polly:" | [Sonivox EAS](https://github.com/EmbeddedSynth/sonivox), Copyright (c) 2004-2006 Sonic Network Inc., 2008 The Android Open Source Project; Apache-2.0 (`app/dsp/eas/LICENSE-2.0.txt`, `NOTICE`) | Apache-2.0 |
| The note editor's design (notes selected, not steps, in time order, a chord's notes one by one; `app/editor.c`, `app/dsp/notes.c`, the Notes page in `app/pages.c`) | after [Melodee](https://github.com/keremimo/melodee)'s SEQ > NOTES by Kerem Kilic (keremimo); Polly's own code | credit only |
| The idea of a browser emulator for FM-1 firmware (`app/web/emu/`) | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |
| Everything else: the app (`app/`), its tools (`tools/`), tests, docs, the site, the factory styles' parts other than the drums (`styles/*.sty`) | Polly, Charles Vestal | GPL-3.0-only |

Polly's own code was GPL-3.0-only before it moved onto fm1-base (on its own copy of Felucca's platform, by way of
[X0X](https://github.com/charlesvestal/fm1-x0x) and FoMni-1) and stays so.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors), the UI face | SIL OFL 1.1 | `base/assets/fonts/BarlowSemiCondensed-*.ttf`, `base/assets/fonts/Barlow-OFL.txt` |
| Drum performances in the factory styles (`styles/drums/*.mid`, compiled into `app/dsp/styles.h`): from the [Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) by Jon Gillick, Adam Roberts, Jesse Engel, Douglas Eck and David Bamman (Google Magenta), © Google LLC. Changed: excerpts of a few bars, the kit's notes mapped to General MIDI, quantised to 24 ticks a beat and moved onto the click, velocities scaled (`styles/README.md`) | CC BY 4.0 | `styles/drums/LICENSE`, <https://creativecommons.org/licenses/by/4.0/> |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.
"M-VAVE" and "FM-1" are trademarks of their respective owners. Polly is independent firmware; it is
not affiliated with, endorsed by or supported by any of them.

## Radio

Polly never enables the Bluetooth / Wi-Fi radio of the hardware: none of JieLi's radio libraries is linked.
