# Polly licensing

Polly is free software under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). It is built on Felucca's platform layer (through X0X and FoMni)
and keeps Felucca's licence. If you distribute Polly, or firmware derived from it, you must give
your recipients its complete corresponding source under the same licence.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| Platform: `firmware/hal/`, `firmware/loader/`, `firmware/src/{libc,lcd,gfx,usb,storage,ota,midi_uart}.c`, `tools/` (build, package, install, rescue), `web/fm1*.js` | [Felucca](https://github.com/hugelton/Felucca), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, as changed for [X0X](https://github.com/charlesvestal/fm1-x0x) and FoMni, then Polly | GPL-3.0-only |
| `firmware/src/app/{panel,plat_fm1,main_fm1}.c`, `host/polly_host.c`, `web/emu/` | adapted from X0X (itself from Felucca's `panel.c`, `main.c`, `audio.c`) | GPL-3.0-only |
| The GM synth: `firmware/src/dsp/eas/` (Sonivox EAS, its 200 KB GM sound set, `eas_host_fm1.c` our host wrapper). Modified for Polly: a per-voice start offset, so a note-on starts on its sample within a block (`eas_wtsynth.c`, `eas_voicemgt.c`, `eas_wtengine.h`, `eas_synth.h`), the reverb and chorus sends summed in 32 bits and clamped to 16, and the effects ringing out after the last send and starting clean (`eas_voicemgt.c`, `eas_synth.h`, `eas_reverb.c`, `eas_chorus.c`); each change marked "Polly:" | [Sonivox EAS](https://github.com/EmbeddedSynth/sonivox), Copyright (c) 2004-2006 Sonic Network Inc., 2008 The Android Open Source Project; Apache-2.0 (`firmware/src/dsp/eas/LICENSE-2.0.txt`, `NOTICE`) | Apache-2.0 |
| The idea of a browser emulator for FM-1 firmware (`web/emu/`) | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |
| Everything else in `firmware/src/{app,dsp}`, `tests/`, the factory styles' parts other than the drums (`styles/*.sty`) | Polly | GPL-3.0-only |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors), the UI face | SIL OFL 1.1 | `assets/fonts/BarlowSemiCondensed-*.ttf`, `assets/fonts/Barlow-OFL.txt` |
| Terminus (Dimitar Toshkov Zhekov), an alternative font set | SIL OFL 1.1 | `assets/fonts/ter-u*.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| Drum performances in the factory styles (`styles/drums/*.mid`, compiled into `firmware/src/dsp/styles.h`): from the [Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) by Jon Gillick, Adam Roberts, Jesse Engel, Douglas Eck and David Bamman (Google Magenta). Changed: excerpts of a few bars, the kit's notes mapped to General MIDI, quantised to 24 ticks a beat and moved onto the click, velocities scaled (`styles/README.md`) | CC BY 4.0 | `styles/drums/LICENSE`, <https://creativecommons.org/licenses/by/4.0/> |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.
"M-VAVE" and "FM-1" are trademarks of their respective owners. Polly is independent firmware; it is
not affiliated with, endorsed by or supported by any of them.

## Radio

Polly never enables the Bluetooth / Wi-Fi radio of the hardware.
