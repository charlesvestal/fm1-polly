#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Polly host tests: the maths, the platform pieces kept from Felucca / X0X, the GM synth, and the
# whole firmware app in the simulator (tests/scenarios/*.polly), with
# screenshots and audio in build/scenarios/.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
FAIL=0
OUT=build/host
mkdir -p "$OUT" build/scenarios
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run fastmath sh -c "$CC -O2 -ffp-contract=off -Wall -Wextra -Werror -o $OUT/fastmath_test tests/host/fastmath_test.c -lm && $OUT/fastmath_test"
# the input layer (hal/fm1_input.h, Felucca 1.5.1's): encoder turns up to 300 detents/s, key taps and bounce
run encoder sh -c "$CC -O2 -w -Ifirmware/hal -o $OUT/encoder_test tests/host/encoder_test.c && $OUT/encoder_test"
run uac sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/uac_test tests/host/uac_test.c -lm && $OUT/uac_test"
run trs sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/trs_test tests/host/trs_test.c && $OUT/trs_test"
run chords sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/src/dsp -o $OUT/chords_test tests/host/chords_test.c firmware/src/dsp/chords.c && $OUT/chords_test"
run loops sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/src/dsp -o $OUT/loops_test tests/host/loops_test.c firmware/src/dsp/loops.c firmware/src/dsp/templates.c firmware/src/dsp/chords.c && $OUT/loops_test"
run leads sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/src/dsp -o $OUT/leads_test tests/host/leads_test.c firmware/src/dsp/leads.c && $OUT/leads_test"
# the note editor's operations (Notes): select order, move across a section, the root at a place, a groove's notes
run notes sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/src/dsp -o $OUT/notes_test tests/host/notes_test.c firmware/src/dsp/notes.c firmware/src/dsp/leads.c firmware/src/dsp/loops.c firmware/src/dsp/templates.c firmware/src/dsp/chords.c && $OUT/notes_test"
# the undo journal: steps over regions, undo / redo by swapping, the ring (count and size), redo gone after an edit
run journal sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -o $OUT/journal_test tests/host/journal_test.c && $OUT/journal_test"
run xpose sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/src/dsp -o $OUT/xpose_test tests/host/xpose_test.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c && $OUT/xpose_test"
run arr sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -DARR_TEST -Ifirmware/src/dsp -Itests/host -o $OUT/arr_test tests/host/arr_test.c tests/host/gm_stub.c firmware/src/dsp/arr.c firmware/src/dsp/loops.c firmware/src/dsp/leads.c firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c && $OUT/arr_test"
run project sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/src -Ifirmware/src/app -Ifirmware/src/dsp -Ifirmware/hal -Itests/host -o $OUT/project_test tests/host/project_test.c tests/host/gm_stub.c firmware/src/dsp/arr.c firmware/src/dsp/loops.c firmware/src/dsp/leads.c firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c && $OUT/project_test"
# a test session's objects in RAM (teststore.c): saved and loaded back whole, replaced, cut at max, a full pool
# refusing, the calibration and the session off on the flash
run teststore sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -Ifirmware/src -o $OUT/teststore_test tests/host/teststore_test.c && $OUT/teststore_test"
# the real synth (gm.c + Sonivox) under the arranger: note-ons on their exact sample, plain MIDI bit-identical
run timing sh -c "$CC -std=c99 -O2 -ffp-contract=off -w -DGM_HOST -Ifirmware/src/dsp -Ifirmware/src/dsp/eas -o $OUT/timing_test tests/host/timing_test.c firmware/src/dsp/gm.c firmware/src/dsp/arr.c firmware/src/dsp/loops.c firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c \$(ls firmware/src/dsp/eas/*.c | grep -v 'wt_22khz\|wt_200k_samples') && $OUT/timing_test"
# CC74 / CC71 move Sonivox's filter: darker, resonant, bit-identical at 64, reset by CC121, no zipper, no blow-ups
# the GM synth's gain staging and releases: no chord reaches EAS's rail, no released note is cut off
run gm sh -c "$CC -std=gnu99 -O2 -ffp-contract=off -w -DGM_HOST -Ifirmware/src -Ifirmware/src/dsp -Ifirmware/src/dsp/eas -o $OUT/gm_test tests/host/gm_test.c firmware/src/dsp/arr.c firmware/src/dsp/loops.c firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c \$(ls firmware/src/dsp/eas/*.c | grep -v 'wt_22khz\\|wt_200k_samples') -lm && $OUT/gm_test"
run filter sh -c "$CC -std=c99 -O2 -ffp-contract=off -w -DGM_HOST -Ifirmware/src/dsp -Ifirmware/src/dsp/eas -o $OUT/filter_test tests/host/filter_test.c firmware/src/dsp/gm.c firmware/src/dsp/arr.c firmware/src/dsp/loops.c firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c \$(ls firmware/src/dsp/eas/*.c | grep -v 'wt_22khz\|wt_200k_samples') -lm && $OUT/filter_test"
# the factory styles: the .sty files check, dump -> parse gives the same tables, and styles.h is up to date
run styles sh -c "python3 tools/make_styles.py --check styles/*.sty && python3 tools/make_styles.py --roundtrip styles/*.sty && \
    python3 tools/make_styles.py styles/*.sty > $OUT/styles.h && cmp $OUT/styles.h firmware/src/dsp/styles.h && \
    sh tests/host/styles_test.sh $OUT"
# the styles' chord parts: every Chord 1 / 2 note held an eighth or more is a chord tone or an allowed tension,
# over the Pop template, the blues, the Andalusian progression and ten chord types (the real engine)
run style-chords sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/src/dsp -Itests/host -o $OUT/style_chords_test tests/host/style_chords_test.c tests/host/gm_stub.c firmware/src/dsp/arr.c firmware/src/dsp/loops.c firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c && $OUT/style_chords_test"
# the QY70 style bank, without Yamaha's data: Polly's own styles coded as a bank (tools/qybank_synth.py); the C
# decoder against the Python one; the bank written to a simulated flash, mounted, decoded, damaged
run bank-codec python3 tools/qy70_codec.py --synthetic --ctest --image $OUT/synth.qyi
run bank sh -c "$CC -std=c99 -Wall -Wextra -Werror -O2 -Ifirmware/src/dsp -o $OUT/bank_test tests/host/bank_test.c firmware/src/dsp/qybank.c firmware/src/dsp/qydec.c && $OUT/bank_test $OUT/synth.qyi 17"
# the arranger playing bank styles: hand-made ones (phrases repeated, controllers sent and reset, the Intro's held
# chord, a switch while playing, a section of 800 notes), then every style of the synthetic bank
run bank-arr sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -DARR_TEST -Ifirmware/src/dsp -Itests/host -o $OUT/bank_arr_test tests/host/bank_arr_test.c tests/host/gm_stub.c firmware/src/dsp/arr.c firmware/src/dsp/loops.c firmware/src/dsp/qybank.c firmware/src/dsp/qydec.c firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c && $OUT/bank_arr_test $OUT/synth.qyi"
# the QY70 importer's chord mapping against xpose.c, the codec, the bank: need a QY70 main ROM (yours; never in the
# repo). What is made from it goes to a temporary directory, removed after (QY70_OUT: keep it there instead)
if [ -n "${QY70_ROM:-}" ] && [ -f "$QY70_ROM" ]; then
    QDIR="${QY70_OUT:-$(mktemp -d)}"
    mkdir -p "$QDIR"
    run qy70-import python3 tools/qy70_import.py "$QY70_ROM" --selftest
    run qy70-codec sh -c "python3 tools/qy70_codec.py '$QY70_ROM' --test && python3 tools/qy70_codec.py '$QY70_ROM' --ctest"
    run qy70-bank sh -c "python3 tools/qy70_codec.py '$QY70_ROM' --image '$QDIR/qy70.qyi' && $OUT/bank_test '$QDIR/qy70.qyi' 128"
    run qy70-bank-arr $OUT/bank_arr_test "$QDIR/qy70.qyi"
    QY_HOST=1                                      # (after the simulator is built: below)
else
    echo "  skip qy70-import, qy70-codec, qy70-bank (set QY70_ROM=path/to/qy70main.bin)"
fi
run storage sh -c "$CC -O2 -Ifirmware/src/dsp -o $OUT/storage_test tests/storage_test.c firmware/src/dsp/qybank.c firmware/src/dsp/qydec.c && $OUT/storage_test"
# the bank written from the host (firmware/src/bank_rx.c) on a simulated flash: only its sectors, the guard bytes,
# the directory last (a cut cable, a changed byte, a failed write: no half bank); tools/qy70_install.py against a
# simulated Polly
run bank-rx sh -c "$CC -std=c99 -Wall -Wextra -Werror -O2 -Ifirmware/src/dsp -o $OUT/bank_rx_test tests/bank_rx_test.c firmware/src/dsp/qybank.c firmware/src/dsp/qydec.c && $OUT/bank_rx_test $OUT/synth.qyi"
run qy70-install python3 tests/qy70_install_test.py $OUT/synth.qyi
# backup and restore over USB (firmware/src/backup.c) on storage.c over a simulated flash: the stream's frames, a
# restore refused while playing, the cable cut at each read, a changed byte, HOME: nothing changed; a power cut at
# each write: the old set or the new, whole. Then tools/polly_backup.py against a simulated Polly, on that image
run backup sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -o $OUT/backup_test tests/backup_test.c && $OUT/backup_test $OUT/backup.plb"
run polly-backup python3 tests/polly_backup_test.py $OUT/backup.plb
# the web backup page (web/backup/index.html): the same bytes as polly_backup.py's on that conversation, replayed;
# then a simulated Polly: a backup and a restore byte for byte, STOP FIRST, HOME, a damaged or older file not sent
if command -v node >/dev/null 2>&1; then
    run backup-page sh -c "python3 tests/polly_backup_record.py $OUT/backup.plb $OUT/backup_transcript.json && \
        node tests/host/backup_page_test.mjs $OUT/backup_transcript.json"
fi
# the update path, against the firmware package (Felucca's tests; needs ./build.sh)
if [ -f build/polly.fwsc ]; then
    run ota-entry sh -c "$CC -o $OUT/ota_test tests/ota_test.c && $OUT/ota_test build/polly.fwsc"
    if [ -z "${AC79_SDK:-}" ]; then
        echo "  skip update-loader (needs AC79_SDK, as the build)"
    else
    run update-loader sh -c "head -c 100000 build/polly.bin > $OUT/old_app.bin && \
        python3 tools/fm1pkg_make.py $OUT/old_app.bin build/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -o $OUT/ldr_test tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc build/polly.fwsc"
    fi
    run installer python3 tests/install_test.py
    # The SMK-37 build (BOARD_SMK37): the flash map and the store's owner, the package layout, the update's staging and
    # the loader's windows, on SMK-layout packages made here from the FM-1 binaries (layout and protocol under test)
    run smk37-flash sh -c "$CC -O2 -Wall -DBOARD_SMK37=1 -Ifirmware/hal -Ifirmware/src -o $OUT/smk_flash_test tests/smk_flash_test.c &&         $OUT/smk_flash_test"
    if [ -z "${AC79_SDK:-}" ]; then
        echo "  skip smk37 package / update path (needs AC79_SDK, as the build)"
    else
    run smk37-package sh -c "python3 tools/fm1pkg_make.py --board smk37 build/polly.bin build/loader/ota.bin         $OUT/smk_new.fwsc >/dev/null && python3 tests/smk_pkg_test.py $OUT/smk_new.fwsc"
    run smk37-ota-entry sh -c "$CC -DBOARD_SMK37=1 -o $OUT/ota_test_smk tests/ota_test.c && $OUT/ota_test_smk $OUT/smk_new.fwsc"
    for m in 1 2; do                     # the MODEL=elite / mke builds of ota.c
        run smk37-ota-entry-model$m sh -c "$CC -DBOARD_SMK37=1 -DT_MODEL=$m -o $OUT/ota_test_smk_m$m tests/ota_test.c &&             $OUT/ota_test_smk_m$m $OUT/smk_new.fwsc"
    done
    [ -f build/polly-smk37.fwsc ] && run smk37-build-package python3 tests/smk_pkg_test.py build/polly-smk37.fwsc "SMK-37 Pro_300"
    [ -f build/polly-smk37-elite.fwsc ] && run smk37-build-package-elite python3 tests/smk_pkg_test.py build/polly-smk37-elite.fwsc "SMK-37 Elite_300"
    [ -f build/polly-smk37-mke.fwsc ] && run smk37-build-package-mke python3 tests/smk_pkg_test.py build/polly-smk37-mke.fwsc "MKE-P37_300"
    run smk37-update-loader sh -c "head -c 100000 build/polly.bin > $OUT/old_app.bin &&         python3 tools/fm1pkg_make.py --board smk37 $OUT/old_app.bin build/loader/ota.bin $OUT/smk_old.fwsc >/dev/null &&         $CC -DBOARD_SMK37=1 -o $OUT/ldr_test_smk tests/ldr_test.c && $OUT/ldr_test_smk $OUT/smk_old.fwsc $OUT/smk_new.fwsc"
    fi
else
    echo "  skip update-path tests (no build/polly.fwsc: run ./build.sh)"
fi
run host-build sh host/build_host.sh
command -v node >/dev/null 2>&1 && run web-installer node web/test_web.mjs   # fm1ota.js / fm1pkg.js against a simulated device
# the remote (firmware/src/mirror.c cmd 0x54) and tools/polly_remote.py against the simulator on a simulated cable
# (polly_host --serve): the screen as mirrored, taps, keys, turns, MASTER into the UI, let go on stop and on silence,
# refused while off, an older Polly, a whole scenario (style_pick.polly) run through the remote; a test session
# (--fresh): a used simulator blank, walkthrough.polly's expects in it, nothing written, the user's own back after
run polly-remote python3 tests/polly_remote_test.py
if command -v emcc >/dev/null 2>&1; then
    run emu sh -c "sh web/emu/build.sh >/dev/null && node tests/host/emu_test.mjs build/emu/polly.wasm"
    if [ -n "${QY70_ROM:-}" ] && [ -f "$QY70_ROM" ]; then
        run emu-bank node tests/host/emu_bank_test.mjs build/emu/polly.wasm
    fi
fi
for s in tests/scenarios/*.polly; do
    n=$(basename "$s" .polly)
    mkdir -p "build/scenarios/$n"
    run "scenario-$n" build/host/polly_host "$s" "build/scenarios/$n"
done
# the SMK-37 build's UI in the simulator: its pads, outer keys, faders, wheels and extra buttons
run host-build-smk37 sh host/build_host.sh smk37
for s in tests/scenarios/smk37/*.polly; do
    [ -f "$s" ] || continue
    n=$(basename "$s" .polly)
    mkdir -p "build/scenarios/smk37-$n"
    run "scenario-smk37-$n" build/host/polly_host_smk37 "$s" "build/scenarios/smk37-$n"
done
# the screen mirrored over USB (firmware/src/mirror.c): its pixel code on the simulator's screens (the mirror
# scenario's dumps: the bytes each takes), rows not sent twice, a full ring (drops, redraws until whole), a refresh,
# the input reports, off; then the viewer's own decoder (web/mirror/index.html) on the simulator's stream, against its
# screen, and tools/polly_mirror.py making a PNG of it
M=build/scenarios/mirror
run mirror sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -Ibuild/gen -o $OUT/mirror_test tests/host/mirror_test.c && \
    $OUT/mirror_test $M/home.bin $M/seq.bin $M/fx.bin $M/glo.bin $M/arp.bin $M/final.bin"
if command -v node >/dev/null 2>&1; then
    run mirror-viewer node tests/host/mirror_test.mjs $M
fi
run mirror-png python3 tools/polly_mirror.py $OUT/mirror.png --from $M/stream.syx
# three QY70 styles played whole from the ROM's bank, to WAVs in $QDIR (QY70_OUT keeps them)
if [ -n "${QY_HOST:-}" ]; then
    run qy70-styles build/host/polly_host tests/rom/qy70_styles.polly "$QDIR"
    [ -n "${QY70_OUT:-}" ] && echo "       WAVs: $QDIR/bossa.wav bigband.wav 8btrock1.wav"
fi
if [ -n "${QDIR:-}" ] && [ -z "${QY70_OUT:-}" ]; then
    rm -rf "$QDIR"                                 # what was made from the ROM: gone
fi
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
