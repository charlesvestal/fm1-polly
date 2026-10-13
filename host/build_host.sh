#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the Polly host simulator (the whole app on this machine) into build/host/polly_host.
# Same sources as the firmware, -ffp-contract=off like the device (no fused multiply-add).
# host/build_host.sh smk37: the SMK-37 build's UI (-DBOARD_SMK37=1) into build/host/polly_host_smk37.
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
OUTB=build/host/polly_host
DEFS=
if [ "${1:-}" = smk37 ]; then
    OUTB=build/host/polly_host_smk37
    DEFS=-DBOARD_SMK37=1
fi
mkdir -p build/gen build/host
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter $DEFS \
    -DGM_HOST -Ifirmware/src -Ifirmware/src/dsp -Ifirmware/src/dsp/eas -Ibuild/gen -o $OUTB host/polly_host.c \
    firmware/src/dsp/gm.c firmware/src/dsp/chords.c firmware/src/dsp/xpose.c firmware/src/dsp/arr.c firmware/src/dsp/loops.c firmware/src/dsp/leads.c firmware/src/dsp/notes.c firmware/src/dsp/qybank.c firmware/src/dsp/qydec.c firmware/src/dsp/templates.c $(ls firmware/src/dsp/eas/*.c | grep -v "wt_22khz\|wt_200k_samples") -lm
echo "host: $OUTB"
