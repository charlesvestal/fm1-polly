# Polly 101, playing live (docs/course/101-playing-live.md), from a fresh boot, with a pause on each step's caption
# before its first move and after its last (tools/polly_video.py tools/video/101.story OUT.mp4).
# Funk, 100 BPM: a bar 2400 ms. Keys (octave 0): A3 4, C#4 8, E4 11, G4 14, B4 18, D5 21.
# Em7 11 14 18 21, A7 4 8 11 14. Anchor A: PLAY (Groove A's first bar).
head 101 Playing live
barms 2400
cap Playing live | Pick a style, play chords, and the band follows.
wait 3500
# ---- 1. a style
cap 1. Pick a style | On the Home page, turn KNOB 1 (Style) to Funk.
wait 2500
turn K1 1
still 1-style
wait 2500
expect tempo 100
# ---- 2. play
cap 2. Play chords | Press PLAY, then hold three keys or more. Polly names the chord, and the band plays it.
wait 2500
anchor A
tap PLAY
expect sect 2
at A 0 400
still 2-play
chord 1500 11 14 18 21
at A 1 1200
cap 2. Play chords | The chord keeps playing when you let go. Play Em7 and A7, two bars each.
at A 2 30
poster
chord 1500 4 8 11 14
at A 4 30
chord 1500 11 14 18 21
at A 6 30
chord 900 4 8 11 14
# ---- 3. a fill into Groove B
at A 6 1200
cap 3. Go busier | Hold HOME and press black key 4. A fill plays, then the busier Groove B.
at A 7 700
press HOME
wait 300
still 3-busier
note 900 8
release HOME
at A 8 30
expect sect 4
chord 1500 11 14 18 21
at A 9 30
expect sect 3
at A 10 30
chord 900 4 8 11 14
# ---- 4. back to Groove A
at A 10 1200
cap 4. Come back down | Hold HOME and press black key 4 again. A fill plays, then Groove A.
at A 11 700
press HOME
wait 300
still 4-down
note 900 8
release HOME
at A 12 30
expect sect 5
chord 1500 11 14 18 21
at A 13 30
expect sect 2
at A 14 30
chord 900 4 8 11 14
# ---- 5. the Ending
at A 14 1200
cap 5. End it | Hold HOME and press black key 5. The Ending plays, then Polly stops.
at A 15 700
press HOME
wait 300
still 5-end
note 900 10
release HOME
at A 16 30
expect sect 6
chord 1500 11 14 18 21
wait 7000
expect playing 0
cap Next: 102 | Your own progressions.
wait 4000
