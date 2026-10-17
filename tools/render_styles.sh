#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Render factory styles to WAVs in the host simulator, for listening: build/styles/NN.wav plays the style
# (picked for song 1's six sections with the Style knob, at its own tempo) over the Pop template, the 12-bar
# blues and the Minor template; Bossa and Swing also over ii-V-I and Jazz; Reggae, House, Disco and Country over
# Pop and Minor (and Latin); Ambient over Pop and Canon; Hip hop over Minor and Vamp; Gospel over Pop and Gospel;
# Waltz over the Waltz template. Each pass: the Intro, Main A (2 bars), Main B (2), the fill from B (Fill BA),
# Main A, the fill from A (Fill AB), Main B, the Ending. Prints each WAV's level.
#   sh tools/render_styles.sh [NN ...]        (default: every style)
set -e
cd "$(dirname "$0")/.."
[ -x app/build/host/polly_host ] || sh host/build_host.sh >/dev/null 2>&1
mkdir -p build/styles
if [ $# -eq 0 ]; then set -- $(ls styles/[0-9][0-9]-*.sty | sed 's|styles/\([0-9]*\)-.*|\1|'); fi
for nn in "$@"; do
    f=$(ls styles/$nn-*.sty)
    n=$(echo "$nn" | sed 's/^0*//')
    tempo=$(sed -n 's/^tempo  *\([0-9]*\).*/\1/p' "$f")
    beats=$(sed -n '/^\[main a\]/,/^\[/s/^beats  *\([0-9]*\).*/\1/p' "$f" | head -1)
    intro=$(sed -n '/^\[intro\]/,/^\[/s/^bars  *\([0-9]*\).*/\1/p' "$f" | head -1)
    ending=$(sed -n '/^\[ending\]/,/^\[/s/^bars  *\([0-9]*\).*/\1/p' "$f" | head -1)
    bar=$((60000 * ${beats:-4} / tempo))
    intro=${intro:-0}
    ending=${ending:-0}
    case "$f" in                                  # template numbers (templates.c): the progressions it suits
        *bossa*|*swing*) tmpls="0 4 7 3 12" ;;    # + ii-V-I, Jazz
        *latin*) tmpls="0 7 0" ;;                 # Pop, Minor, Pop (its bars are short)
        *reggae*|*house*|*disco*|*country*) tmpls="0 7" ;;   # Pop, Minor
        *ambient*) tmpls="0 8" ;;                 # Pop, Canon
        *gospel*) tmpls="0 14" ;;                 # Pop, Gospel
        *waltz*) tmpls="15 15 15" ;;              # Waltz, three times (its bars are short)
        *hiphop*) tmpls="7 13" ;;                 # Minor, Vamp
        *) tmpls="0 4 7" ;;                       # Pop, Blues, Minor
    esac
    s=build/styles/$nn.polly
    {
        echo "# render $f at $tempo: intro $intro bars, ending $ending"
        echo "wait 300"
        echo "tap ENV"
        echo "tap ENV"
        k=0
        while [ $k -lt "$n" ]; do echo "spin K4 1"; k=$((k + 1)); done
        echo "wait 50"
        echo "expect song1_style1 $((24 + 6 * (n - 1) + 1))"
        echo "expect tempo $tempo"
        echo "wav $nn.wav"
        for t in $tmpls; do
            echo "press SEL"
            echo "tapkey w$t"
            echo "release SEL"
            echo "wait 10"
            echo "tap PLAY"
            # bar lines from PLAY: the Intro's, then Main A 2 bars, Main B 2, Fill BA, Main A, Fill AB, Main B, the
            # Ending; each section asked for half a bar before its bar line (SEL + tapkey + SEL: 94 ms)
            a=$intro
            echo "wait $((bar * a + bar * 3 / 2 - 94))"
            echo "expect sect 2"                                        # Main A after the Intro
            echo "press SEL"; echo "tapkey b2"; echo "release SEL"      # Main B at bar a + 2
            echo "wait $((bar * 2 - 94))"
            echo "expect sect 3"
            echo "press SEL"; echo "tapkey b3"; echo "release SEL"      # the fill from B: Fill BA at a + 4
            echo "wait $((bar * 2 - 94))"
            echo "expect sect 2"                                        # back to Main A
            echo "press SEL"; echo "tapkey b3"; echo "release SEL"      # from Main A (a + 5): Fill AB at a + 6
            echo "wait $((bar * 2 - 94))"
            echo "expect sect 3"                                        # on to Main B
            echo "press SEL"; echo "tapkey b4"; echo "release SEL"      # the Ending at a + 8
            echo "wait $((bar * ending + bar - 94))"
            echo "expect playing 0"
        done
        echo "wavstop"
    } > "$s"
    app/build/host/polly_host "$s" build/styles > build/styles/$nn.log || { cat build/styles/$nn.log; exit 1; }
    python3 - "build/styles/$nn.wav" <<'PY'
import math, struct, sys, wave
w = wave.open(sys.argv[1]); n = w.getnframes(); d = struct.unpack("<%dh" % (2 * n), w.readframes(n))
rms = math.sqrt(sum(x * x for x in d) / len(d)) / 32768; pk = max(abs(x) for x in d) / 32768
print("%s  %.0f s  rms %.1f dBFS  peak %.1f dBFS" % (sys.argv[1], n / 44100, 20 * math.log10(rms), 20 * math.log10(pk)))
PY
done
