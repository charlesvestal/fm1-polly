#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# tools/make_styles.py on bad .sty files: each fails (exit 1) with the exact file:line message.
#   sh tests/host/styles_test.sh OUTDIR
set -u
D="$1/styles_test"
rm -rf "$D"
mkdir -p "$D"
FAIL=0
HEAD='style T
[main b]
bars 1
[fill]
bars 1
[ending]
bars 1
[main a]
bars 1
track ba'
# bad NAME 'the .sty body after HEAD' 'the expected message'
bad() {
    f="$D/01-$1.sty"
    printf '%s\n%s\n' "$HEAD" "$2" > "$f"
    out=$(python3 tools/make_styles.py --check "$f" 2>&1)
    rc=$?
    if [ $rc -ne 1 ] || [ "$out" != "make_styles: $f:$3" ]; then
        echo "FAIL $1: exit $rc"
        echo "  got:      $out"
        echo "  expected: make_styles: $f:$3"
        FAIL=1
    else
        echo "  ok $1"
    fi
}
bad velocity '1.1.0 C2 6 200' '11: velocity 200 outside 1..127'
bad length '1.1.0 C2 2000 90' '11: ba: length 2000 outside 1..1920'
bad note '1.1.0 130 6 90' '11: ba: note 130 outside 0..127'
bad grid 'grid 1.1 s C2+ x.x.' "11: not a note: "
bad gridshort 'grid 1.1 s' "11: grid POS STEP NOTES STEPS [len=L]"
bad gridnolen 'grid 1.1 s C2 x.x.x.x.x.x.x.x.x' "11: ba: tick 1920 past the pattern's end"
bad position '1.5.0 C2 6 90' '11: position 1.5.0 outside 1 bars of 4 beats'
bad drumsfile 'drums from=nothing.mid' "11: $D/nothing.mid: No such file or directory"
bad field 'track c1 tempo=3' '11: unknown track field tempo'
printf 'bars 2\n' > "$D/01-bars.sty"
out=$(python3 tools/make_styles.py --check "$D/01-bars.sty" 2>&1)
[ "$out" = "make_styles: $D/01-bars.sty:1: bars outside a section" ] && echo "  ok bars" || { echo "FAIL bars: $out"; FAIL=1; }
# too many notes: the section's line
f="$D/01-full.sty"
printf '%s\n%s\n' "$HEAD" '1.1.0 C2 1 90 x96 1' > "$f"
printf 'track c1\n' >> "$f"
for k in 1 2 3 4 5 6; do printf '1.1.0 C%s 1 90 x96 1\n' $k >> "$f"; done
out=$(python3 tools/make_styles.py --check "$f" 2>&1)
case "$out" in "make_styles: $f:8: [main a] has "*" notes, more than 600 (ARR_NEV)") echo "  ok full" ;; *) echo "FAIL full: $out"; FAIL=1 ;; esac
# a sharp is not a comment: the fields after hikey=F# are read
f="$D/01-sharp.sty"
printf '%s\n%s\n' "$HEAD" 'track ba hikey=F# vol=90 # a comment' > "$f"
out=$(python3 tools/make_styles.py --dump "$f" 2>&1 | sed -n '/\[main a\]/,$p' | grep '^track ba')
case "$out" in *"hikey=F# vol=90 "*) echo "  ok sharp" ;; *) echo "FAIL sharp: $out"; FAIL=1 ;; esac
# sections: [main a] alone is a style (the rest empty, 6 patterns); [fill] is [fill ab]; an unknown one; no [main a]
f="$D/01-mainonly.sty"
printf 'style M\n[main a]\nbars 1\ntrack ba\n1.1.0 C2 120 90\n' > "$f"
out=$(python3 tools/make_styles.py --check "$f" 2>&1)
[ "$out" = "1 styles, 6 patterns ok" ] && echo "  ok mainonly" || { echo "FAIL mainonly: $out"; FAIL=1; }
out=$(python3 tools/make_styles.py --roundtrip "$f" 2>&1)
[ "$out" = "1 styles round trip ok" ] && echo "  ok mainonly roundtrip" || { echo "FAIL mainonly roundtrip: $out"; FAIL=1; }
f="$D/01-fill2.sty"
printf 'style M\n[main a]\nbars 1\n[fill]\nbars 1\n[fill ab]\nbars 1\n' > "$f"
out=$(python3 tools/make_styles.py --check "$f" 2>&1)
[ "$out" = "make_styles: $f:6: section twice: [fill ab]" ] && echo "  ok fill alias" || { echo "FAIL fill alias: $out"; FAIL=1; }
f="$D/01-unknown.sty"
printf 'style M\n[main c]\n' > "$f"
out=$(python3 tools/make_styles.py --check "$f" 2>&1)
[ "$out" = "make_styles: $f:2: unknown section [main c] ([intro] [main a] [main b] [fill ab] [fill ba] [ending])" ] && echo "  ok unknown section" || { echo "FAIL unknown section: $out"; FAIL=1; }
f="$D/01-nomain.sty"
printf 'style M\n[intro]\nbars 1\n' > "$f"
out=$(python3 tools/make_styles.py --check "$f" 2>&1)
[ "$out" = "make_styles: $f: no [main a] section" ] && echo "  ok no main a" || { echo "FAIL no main a: $out"; FAIL=1; }
# drums from a MIDI file: on the 24-a-beat grid by default (every tick a multiple of 20); quant=off keeps the
# drummer's own ticks (some not); quant=s the sixteenths (multiples of 120)
for q in "" " quant=off" " quant=s"; do
    printf 'style M\n[main a]\nbars 2\ntrack d1\ndrums from=%s/styles/drums/drummer3-session1-1_rock_105_beat_4-4.mid start=3%s\n' "$(pwd)" "$q" > "$D/01-q.sty"
    python3 tools/make_styles.py --dump "$D/01-q.sty" | awk '/^\[main a\]/{on=1} on && $1 ~ /^[0-9]+\.[0-9]+\.[0-9]+$/ {split($1, p, "."); print p[3]}' > "$D/q$q.txt"
done
off20=$(awk '$1 % 20' "$D/q quant=off.txt" | wc -l)
def20=$(awk '$1 % 20' "$D/q.txt" | wc -l)
s120=$(awk '$1 % 120' "$D/q quant=s.txt" | wc -l)
n=$(wc -l < "$D/q.txt")
[ "$n" -gt 20 ] && [ "$def20" -eq 0 ] && [ "$off20" -gt 0 ] && [ "$s120" -eq 0 ] && echo "  ok drums quant" || \
    { echo "FAIL drums quant: $n hits, $def20 off the 20 grid by default, $off20 with quant=off, $s120 off the 16ths with quant=s"; FAIL=1; }
# lengths in the tables: up to 1020 exact, longer to 20 ticks (said), past 21580 refused; the firmware's decoder
# (sty_len in the generated styles.h) gives back what --dump shows
f="$D/01-len.sty"
printf 'style L\n[main a]\nbars 8\nbeats 7\ntrack c1\n1.1.0 C4 1020 90\n1.2.0 D4 1033 90\n1.3.0 E4 1880 90\n1.4.0 G4 21580 90\n' > "$f"
out=$(python3 tools/make_styles.py --check "$f" 2>&1)
[ "$out" = "make_styles: $f:7: c1: length 1033 kept as 1040 in the tables
1 styles, 6 patterns ok" ] && echo "  ok lengths" || { echo "FAIL lengths: $out"; FAIL=1; }
python3 tools/make_styles.py "$f" > "$D/len_styles.h" 2>/dev/null
cat > "$D/len.c" <<'C'
#include <stdio.h>
#include "len_styles.h"
int main(void)
{
    for (int i = 0; i < STY_NEV; i++)
        printf("%u ", sty_len(STY_EV[i] >> 22 | (uint32_t)(STY_VEL[i] >> 7) << 10));
    printf("\n");
    return 0;
}
C
cc -w -o "$D/len" "$D/len.c" && got=$("$D/len")
[ "$got" = "1020 1040 1880 21580 " ] && echo "  ok length decoder" || { echo "FAIL length decoder: $got"; FAIL=1; }
printf '1.5.0 A4 21581 90\n' >> "$f"
out=$(python3 tools/make_styles.py --check "$f" 2>&1 | grep -v "kept as")
[ "$out" = "make_styles: $f:10: c1: length 21581 longer than the tables hold (21580 ticks)" ] && echo "  ok length too long" || \
    { echo "FAIL length too long: $out"; FAIL=1; }
# style numbers: a gap, a twice, no number
cp "$D/01-velocity.sty" "$D/03-x.sty"
out=$(python3 tools/make_styles.py --check "$D/01-bars.sty" "$D/03-x.sty" 2>&1)
[ "$out" = "make_styles: no style 2: styles are numbered 1..2 with no gaps" ] && echo "  ok gap" || { echo "FAIL gap: $out"; FAIL=1; }
cp "$D/01-velocity.sty" "$D/x.sty"
out=$(python3 tools/make_styles.py --check "$D/x.sty" 2>&1)
[ "$out" = "make_styles: $D/x.sty: the name must start with its style number (NN-name.sty)" ] && echo "  ok unnumbered" || { echo "FAIL unnumbered: $out"; FAIL=1; }
out=$(python3 tools/make_styles.py --check "$D/01-bars.sty" "$D/01-velocity.sty" 2>&1)
[ "$out" = "make_styles: $D/01-velocity.sty: style 1 twice (also $D/01-bars.sty)" ] && echo "  ok twice" || { echo "FAIL twice: $out"; FAIL=1; }
exit $FAIL
