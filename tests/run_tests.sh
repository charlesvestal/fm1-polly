#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Polly's tests: the arranger and its parts, the GM synth, the factory styles, the QY70 bank, the store map, the whole
# app in fm1-base's simulator (tests/scenarios/*.polly, screenshots and audio in app/build/scenarios/), the remote, the
# browser build, and the update path against Polly's package (./build.sh first). fm1-base's own tests (the platform,
# backup and restore, the mirror, the installer): base/tests/run_tests.sh.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # the package's SDK files, as ./build.sh
B=app/build
OUT=$B/tests
D=app/dsp
FAIL=0
mkdir -p "$OUT" $B/scenarios
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
ARR="$D/arr.c $D/loops.c $D/leads.c $D/templates.c $D/xpose.c $D/chords.c"
EAS="$(ls $D/eas/*.c | grep -v "wt_22khz\|wt_200k_samples" | tr "\n" " ")"
run chords sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -I$D -o $OUT/chords_test tests/host/chords_test.c $D/chords.c && $OUT/chords_test"
run loops sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -I$D -o $OUT/loops_test tests/host/loops_test.c $D/loops.c $D/templates.c $D/chords.c && $OUT/loops_test"
run leads sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -I$D -o $OUT/leads_test tests/host/leads_test.c $D/leads.c && $OUT/leads_test"
# the note editor's operations (Notes): select order, move across a section, the root at a place, a groove's notes
run notes sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -I$D -o $OUT/notes_test tests/host/notes_test.c $D/notes.c $D/leads.c $D/loops.c $D/templates.c $D/chords.c && $OUT/notes_test"
# the undo journal: steps over regions, undo / redo by swapping, the ring (count and size), redo gone after an edit
run journal sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -o $OUT/journal_test tests/host/journal_test.c && $OUT/journal_test"
run xpose sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -I$D -o $OUT/xpose_test tests/host/xpose_test.c $D/xpose.c $D/chords.c && $OUT/xpose_test"
run arr sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -DARR_TEST -I$D -Itests/host -o $OUT/arr_test tests/host/arr_test.c tests/host/gm_stub.c $ARR && $OUT/arr_test"
run project sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -Iapp -I$D -Ibase/include -Itests/host -o $OUT/project_test tests/host/project_test.c tests/host/gm_stub.c $ARR && $OUT/project_test"
# a test session's objects in RAM (teststore.c): saved and loaded back whole, replaced, cut at max, a full pool
# refusing, the calibration and the session off on the flash
run teststore sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -Iapp -Ibase/include -o $OUT/teststore_test tests/host/teststore_test.c && $OUT/teststore_test"
# the store map: every object and the QY70 bank's sectors inside fm1-base's windows, apart, where Polly 0.4 kept them
sed -n '/#define APP_STORE_MAP/,/}/p' app/app.h | sed 's/#define APP_STORE_MAP//; s/\\$//' > $OUT/map_line.h
run map sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -I$OUT -I$D -Ibase/include -o $OUT/map_test tests/host/map_test.c $D/qybank.c $D/qydec.c && $OUT/map_test"
# the real synth (gm.c + Sonivox) under the arranger: note-ons on their exact sample, plain MIDI bit-identical
run timing sh -c "$CC -std=c99 -O2 -ffp-contract=off -w -DPLAT_HOST -I$D -I$D/eas -o $OUT/timing_test tests/host/timing_test.c $D/gm.c $ARR $EAS -lm && $OUT/timing_test"
# the GM synth's gain staging and releases: no chord reaches EAS's rail, no released note is cut off
run gm sh -c "$CC -std=gnu99 -O2 -ffp-contract=off -w -DPLAT_HOST -Iapp -I$D -I$D/eas -o $OUT/gm_test tests/host/gm_test.c $ARR $EAS -lm && $OUT/gm_test"
# CC74 / CC71 move Sonivox's filter: darker, resonant, bit-identical at 64, reset by CC121, no zipper, no blow-ups
run filter sh -c "$CC -std=c99 -D_DEFAULT_SOURCE -O2 -ffp-contract=off -w -DPLAT_HOST -I$D -I$D/eas -o $OUT/filter_test tests/host/filter_test.c $D/gm.c $ARR $EAS -lm && $OUT/filter_test"
# the factory styles: the .sty files check, dump -> parse gives the same tables, and styles.h is up to date
run styles sh -c "$PY tools/make_styles.py --check styles/*.sty && $PY tools/make_styles.py --roundtrip styles/*.sty && \
    $PY tools/make_styles.py styles/*.sty > $OUT/styles.h && cmp $OUT/styles.h $D/styles.h && \
    sh tests/host/styles_test.sh $OUT"
# the styles' chord parts: every Chord 1 / 2 note held an eighth or more is a chord tone or an allowed tension,
# over the Pop template, the blues, the Andalusian progression and ten chord types (the real engine)
run style-chords sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -I$D -Itests/host -o $OUT/style_chords_test tests/host/style_chords_test.c tests/host/gm_stub.c $ARR && $OUT/style_chords_test"
# the QY70 style bank, without Yamaha's data: Polly's own styles coded as a bank (tools/qybank_synth.py); the C
# decoder against the Python one; the bank written to a simulated flash, mounted, decoded, damaged
run bank-codec $PY tools/qy70_codec.py --synthetic --ctest --image $OUT/synth.qyi
run bank sh -c "$CC -std=c99 -Wall -Wextra -Werror -O2 -I$D -o $OUT/bank_test tests/host/bank_test.c $D/qybank.c $D/qydec.c && $OUT/bank_test $OUT/synth.qyi 17"
# the arranger playing bank styles: hand-made ones (phrases repeated, controllers sent and reset, the Intro's held
# chord, a switch while playing, a section of 800 notes), then every style of the synthetic bank
run bank-arr sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -DARR_TEST -I$D -Itests/host -o $OUT/bank_arr_test tests/host/bank_arr_test.c tests/host/gm_stub.c $ARR $D/qybank.c $D/qydec.c && $OUT/bank_arr_test $OUT/synth.qyi"
# the QY70 importer's chord mapping against xpose.c, the codec, the bank: need a QY70 main ROM (yours; never in the
# repo). What is made from it goes to a temporary directory, removed after (QY70_OUT: keep it there instead)
if [ -n "${QY70_ROM:-}" ] && [ -f "$QY70_ROM" ]; then
    QDIR="${QY70_OUT:-$(mktemp -d)}"
    mkdir -p "$QDIR"
    run qy70-import $PY tools/qy70_import.py "$QY70_ROM" --selftest
    run qy70-codec sh -c "$PY tools/qy70_codec.py '$QY70_ROM' --test && $PY tools/qy70_codec.py '$QY70_ROM' --ctest"
    run qy70-bank sh -c "$PY tools/qy70_codec.py '$QY70_ROM' --image '$QDIR/qy70.qyi' && $OUT/bank_test '$QDIR/qy70.qyi' 128"
    run qy70-bank-arr $OUT/bank_arr_test "$QDIR/qy70.qyi"
    QY_HOST=1                                      # (after the simulator is built: below)
else
    echo "  skip qy70-import, qy70-codec, qy70-bank (set QY70_ROM=path/to/qy70main.bin)"
fi
# the bank written from the host (app/bank_rx.c) on a simulated flash: only its sectors, the guard bytes, the directory
# last (a cut cable, a changed byte, a failed write: no half bank); tools/qy70_install.py against a simulated Polly
run bank-rx sh -c "$CC -std=c99 -Wall -Wextra -Werror -O2 -I$D -o $OUT/bank_rx_test tests/bank_rx_test.c $D/qybank.c $D/qydec.c && $OUT/bank_rx_test $OUT/synth.qyi"
run qy70-install $PY tests/qy70_install_test.py $OUT/synth.qyi
# the update path, against Polly's package (fm1-base's tests; ./build.sh first)
if [ -f $B/polly.fwsc ]; then
    run ota-entry sh -c "$CC -w -o $OUT/ota_test base/tests/ota_test.c && $OUT/ota_test $B/polly.fwsc"
    run update-loader sh -c "head -c 100000 $B/polly.bin > $OUT/old_app.bin && \
        $PY base/tools/fm1pkg_make.py $OUT/old_app.bin $B/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -w -o $OUT/ldr_test base/tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc $B/polly.fwsc"
else
    echo "  skip update path (no $B/polly.fwsc: ./build.sh)"
fi
run host-build sh build.sh --host
# the remote (fm1-base's mirror, cmd 0x54) and tools/polly_remote.py against the simulator on a simulated cable
# (polly_host --serve): the screen as mirrored, taps, keys, turns, MASTER into the UI, let go on stop and on silence,
# refused while off, an older Polly, a whole scenario (style_pick.polly) run through the remote; a test session
# (--fresh): a used simulator blank, walkthrough.polly's expects in it, nothing written, the user's own back after
run polly-remote $PY tests/polly_remote_test.py
if command -v emcc >/dev/null 2>&1; then
    run emu sh -c "PYTHON=$PY sh web/emu/build.sh >/dev/null && node tests/host/emu_test.mjs $B/emu/app.wasm"
    if [ -n "${QY70_ROM:-}" ] && [ -f "$QY70_ROM" ]; then
        run emu-bank node tests/host/emu_bank_test.mjs $B/emu/app.wasm
    fi
else
    echo "  skip emu (no emcc)"
fi
for s in tests/scenarios/*.polly; do
    n=$(basename "$s" .polly)
    mkdir -p "$B/scenarios/$n"
    run "scenario-$n" $B/host/polly_host "$s" "$B/scenarios/$n"
done
# tools/polly_backup.py (fm1-base's fm1_backup.py with Polly's kind) against fm1-base's simulated FM-1
run polly-backup $PY tests/polly_backup_test.py
# the mirror on Polly's screens (tests/scenarios/mirror.polly): fm1-base's mirror_test.c on the dumps, the viewer's
# decoder (base/web/mirror) on the stream, tools/polly_mirror.py making a PNG
M=$B/scenarios/mirror
run mirror sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -I$B/gen -o $OUT/mirror_test base/tests/mirror_test.c && \
    $OUT/mirror_test $M/home.bin $M/seq.bin $M/fx.bin $M/glo.bin $M/arp.bin $M/final.bin"
if command -v node >/dev/null 2>&1; then
    run mirror-viewer node tests/host/mirror_test.mjs $M
fi
run mirror-png $PY tools/polly_mirror.py $OUT/mirror.png --from $M/stream.syx
# three QY70 styles played whole from the ROM's bank, to WAVs in $QDIR (QY70_OUT keeps them)
if [ -n "${QY_HOST:-}" ]; then
    run qy70-styles $B/host/polly_host tests/rom/qy70_styles.polly "$QDIR"
    [ -n "${QY70_OUT:-}" ] && echo "       WAVs: $QDIR/bossa.wav bigband.wav 8btrock1.wav"
fi
if [ -n "${QDIR:-}" ] && [ -z "${QY70_OUT:-}" ]; then
    rm -rf "$QDIR"                                 # what was made from the ROM: gone
fi
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
