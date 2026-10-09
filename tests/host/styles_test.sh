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
