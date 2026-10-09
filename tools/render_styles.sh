#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Render factory styles to WAVs in the host simulator, for listening: build/styles/NN.wav plays the style
# (picked for song 1's four sections, at its own tempo) over the Pop template, the 12-bar blues and the
# Minor template; Bossa and Swing also over ii-V-I and Jazz; Reggae, House, Disco and Country over Pop and
# Minor; Hip hop over Minor and Vamp; Gospel over Pop and Gospel; Waltz over the Waltz template. Each pass: Main A, Main B, the fill, Main A,
# the ending (8 bars). Prints each WAV's level.
#   sh tools/render_styles.sh [NN ...]        (default: every style)
set -e
cd "$(dirname "$0")/.."
[ -x build/host/polly_host ] || sh host/build_host.sh >/dev/null
mkdir -p build/styles
if [ $# -eq 0 ]; then set -- $(ls styles/[0-9][0-9]-*.sty | sed 's|styles/\([0-9]*\)-.*|\1|'); fi
for nn in "$@"; do
    f=$(ls styles/$nn-*.sty)
    n=$(echo "$nn" | sed 's/^0*//')
    tempo=$(sed -n 's/^tempo  *\([0-9]*\).*/\1/p' "$f")
    beats=$(sed -n '/^\[main a\]/,/^\[/s/^beats  *\([0-9]*\).*/\1/p' "$f" | head -1)
    bar=$((60000 * ${beats:-4} / tempo))
    case "$f" in                                  # template numbers (templates.c): the progressions it suits
        *bossa*|*swing*) tmpls="0 4 7 3 12" ;;    # + ii-V-I, Jazz
        *reggae*|*house*|*disco*|*country*) tmpls="0 7" ;;   # Pop, Minor
        *gospel*) tmpls="0 14" ;;                 # Pop, Gospel
        *waltz*) tmpls="15 15 15" ;;              # Waltz, three times (its bars are short)
        *hiphop*) tmpls="7 13" ;;                 # Minor, Vamp
        *) tmpls="0 4 7" ;;                       # Pop, Blues, Minor
    esac
    s=build/styles/$nn.polly
    {
        echo "# render $f at $tempo"
        echo "wait 300"
        echo "tap ENV"
        echo "tap ENV"
        echo "spin K1 $((15 + n))"
        echo "spin K2 $((14 + n))"
        echo "spin K3 $((13 + n))"
        echo "spin K4 $((12 + n))"
        echo "wait 50"
        echo "expect song1_style0 $((16 + 4 * (n - 1)))"
        echo "expect tempo $tempo"
        echo "wav $nn.wav"
        for t in $tmpls; do
            echo "press SEL"
            echo "tapkey w$t"
            echo "release SEL"
            echo "wait 10"
            echo "tap PLAY"
            echo "wait $((bar * 3 / 2 - 100))"
            echo "press SEL"; echo "tapkey b1"; echo "release SEL"
            echo "wait $((bar * 2 - 94))"
            echo "press SEL"; echo "tapkey b2"; echo "release SEL"
            echo "wait $((bar * 2 - 94))"
            echo "press SEL"; echo "tapkey b3"; echo "release SEL"
            echo "wait $((bar * 3 - 94 + bar / 2))"
            echo "expect playing 0"
        done
        echo "wavstop"
    } > "$s"
    build/host/polly_host "$s" build/styles > build/styles/$nn.log || { cat build/styles/$nn.log; exit 1; }
    python3 - "build/styles/$nn.wav" <<'PY'
import math, struct, sys, wave
w = wave.open(sys.argv[1]); n = w.getnframes(); d = struct.unpack("<%dh" % (2 * n), w.readframes(n))
rms = math.sqrt(sum(x * x for x in d) / len(d)) / 32768; pk = max(abs(x) for x in d) / 32768
print("%s  %.0f s  rms %.1f dBFS  peak %.1f dBFS" % (sys.argv[1], n / 44100, 20 * math.log10(rms), 20 * math.log10(pk)))
PY
done
