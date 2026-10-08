#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the LIFE host simulator (the whole app on this machine) into build/host/gm_host.
# Same sources as the firmware, -ffp-contract=off like the device (no fused multiply-add).
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build/gen build/host
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
    -DGM_HOST -Ifirmware/src -Ifirmware/src/dsp -Ifirmware/src/dsp/eas -Ibuild/gen -o build/host/gm_host host/gm_host.c \
    firmware/src/dsp/gm.c $(ls firmware/src/dsp/eas/*.c | grep -v "wt_22khz\|wt_200k_samples") -lm
echo "host: build/host/gm_host"
