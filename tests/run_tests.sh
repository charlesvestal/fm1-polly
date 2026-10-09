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
run encoder sh -c "$CC -O2 -w -Ifirmware/hal -o $OUT/encoder_test tests/host/encoder_test.c && $OUT/encoder_test"
run uac sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/uac_test tests/host/uac_test.c -lm && $OUT/uac_test"
run trs sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/trs_test tests/host/trs_test.c && $OUT/trs_test"
run chords sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/src/dsp -o $OUT/chords_test tests/host/chords_test.c firmware/src/dsp/chords.c && $OUT/chords_test"
run xpose sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/src/dsp -o $OUT/xpose_test tests/host/xpose_test.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c && $OUT/xpose_test"
run arr sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -DARR_TEST -Ifirmware/src/dsp -Itests/host -o $OUT/arr_test tests/host/arr_test.c tests/host/gm_stub.c firmware/src/dsp/arr.c firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c && $OUT/arr_test"
# the real synth (gm.c + Sonivox) under the arranger: note-ons on their exact sample, plain MIDI bit-identical
run timing sh -c "$CC -std=c99 -O2 -ffp-contract=off -w -DGM_HOST -Ifirmware/src/dsp -Ifirmware/src/dsp/eas -o $OUT/timing_test tests/host/timing_test.c firmware/src/dsp/gm.c firmware/src/dsp/arr.c firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c \$(ls firmware/src/dsp/eas/*.c | grep -v 'wt_22khz\|wt_200k_samples') && $OUT/timing_test"
# CC74 / CC71 move Sonivox's filter: darker, resonant, bit-identical at 64, reset by CC121, no zipper, no blow-ups
run filter sh -c "$CC -std=c99 -O2 -ffp-contract=off -w -DGM_HOST -Ifirmware/src/dsp -Ifirmware/src/dsp/eas -o $OUT/filter_test tests/host/filter_test.c firmware/src/dsp/gm.c firmware/src/dsp/arr.c firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c \$(ls firmware/src/dsp/eas/*.c | grep -v 'wt_22khz\|wt_200k_samples') -lm && $OUT/filter_test"
# the factory styles: the .sty files check, dump -> parse gives the same tables, and styles.h is up to date
run styles sh -c "python3 tools/make_styles.py --check styles/*.sty && python3 tools/make_styles.py --roundtrip styles/*.sty && \
    python3 tools/make_styles.py styles/*.sty > $OUT/styles.h && cmp $OUT/styles.h firmware/src/dsp/styles.h && \
    sh tests/host/styles_test.sh $OUT"
# the styles' chord parts: every Chord 1 / 2 note held an eighth or more is a chord tone or an allowed tension,
# over the Pop template, the blues, the Andalusian progression and ten chord types (the real engine)
run style-chords sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/src/dsp -Itests/host -o $OUT/style_chords_test tests/host/style_chords_test.c tests/host/gm_stub.c firmware/src/dsp/arr.c firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c && $OUT/style_chords_test"
# the QY70 style bank, without Yamaha's data: Polly's own styles coded as a bank (tools/qybank_synth.py); the C
# decoder against the Python one; the bank written to a simulated flash, mounted, decoded, damaged
run bank-codec python3 tools/qy70_codec.py --synthetic --ctest --image $OUT/synth.qyi
run bank sh -c "$CC -std=c99 -Wall -Wextra -Werror -O2 -Ifirmware/src/dsp -o $OUT/bank_test tests/host/bank_test.c firmware/src/dsp/qybank.c firmware/src/dsp/qydec.c && $OUT/bank_test $OUT/synth.qyi 17"
# the arranger playing bank styles: hand-made ones (phrases repeated, controllers sent and reset, the Intro's held
# chord, a switch while playing, a section of 800 notes), then every style of the synthetic bank
run bank-arr sh -c "$CC -std=c99 -Wall -Wextra -Werror -O1 -DARR_TEST -Ifirmware/src/dsp -Itests/host -o $OUT/bank_arr_test tests/host/bank_arr_test.c tests/host/gm_stub.c firmware/src/dsp/arr.c firmware/src/dsp/qybank.c firmware/src/dsp/qydec.c firmware/src/dsp/templates.c firmware/src/dsp/xpose.c firmware/src/dsp/chords.c && $OUT/bank_arr_test $OUT/synth.qyi"
# the QY70 importer's chord mapping against xpose.c, the codec, the bank: need a QY70 main ROM (yours; never in the
# repo). What is made from it goes to a temporary directory, removed after (QY70_OUT: keep it there instead)
if [ -n "${QY70_ROM:-}" ] && [ -f "$QY70_ROM" ]; then
    QDIR="${QY70_OUT:-$(mktemp -d)}"
    mkdir -p "$QDIR"
    run qy70-import python3 tools/qy70_import.py "$QY70_ROM" --selftest
    run qy70-codec sh -c "python3 tools/qy70_codec.py '$QY70_ROM' --test && python3 tools/qy70_codec.py '$QY70_ROM' --ctest"
    run qy70-bank sh -c "python3 tools/qy70_codec.py '$QY70_ROM' --image '$QDIR/qy70.qyi' && $OUT/bank_test '$QDIR/qy70.qyi' 128"
    run qy70-bank-arr $OUT/bank_arr_test "$QDIR/qy70.qyi"
else
    echo "  skip qy70-import, qy70-codec, qy70-bank (set QY70_ROM=path/to/qy70main.bin)"
fi
run storage sh -c "$CC -O2 -Ifirmware/src/dsp -o $OUT/storage_test tests/storage_test.c firmware/src/dsp/qybank.c firmware/src/dsp/qydec.c && $OUT/storage_test"
# the bank written from the host (firmware/src/bank_rx.c) on a simulated flash: only its sectors, the guard bytes,
# the directory last (a cut cable, a changed byte, a failed write: no half bank); tools/qy70_install.py against a
# simulated Polly
run bank-rx sh -c "$CC -std=c99 -Wall -Wextra -Werror -O2 -Ifirmware/src/dsp -o $OUT/bank_rx_test tests/bank_rx_test.c firmware/src/dsp/qybank.c firmware/src/dsp/qydec.c && $OUT/bank_rx_test $OUT/synth.qyi"
run qy70-install python3 tests/qy70_install_test.py $OUT/synth.qyi
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
else
    echo "  skip update-path tests (no build/polly.fwsc: run ./build.sh)"
fi
run host-build sh host/build_host.sh
if command -v emcc >/dev/null 2>&1; then
    run emu sh -c "sh web/emu/build.sh >/dev/null && node tests/host/emu_test.mjs build/emu/polly.wasm"
fi
for s in tests/scenarios/*.polly; do
    n=$(basename "$s" .polly)
    mkdir -p "build/scenarios/$n"
    run "scenario-$n" build/host/polly_host "$s" "build/scenarios/$n"
done
if [ -n "${QDIR:-}" ] && [ -z "${QY70_OUT:-}" ]; then
    rm -rf "$QDIR"                                 # what was made from the ROM: gone
fi
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
