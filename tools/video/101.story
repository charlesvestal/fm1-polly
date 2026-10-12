# Polly 101, playing live (docs/course/101-playing-live.md), from a fresh boot, with a pause on each step's caption
# before its first move and after its last (tools/polly_video.py tools/video/101.story OUT.mp4).
# Funk, 100 BPM: a bar 2400 ms. Keys (octave 0): A3 4, C#4 8, E4 11, G4 14, B4 18, D5 21.
# Em7 11 14 18 21, A7 4 8 11 14. Anchor A: PLAY (Groove A's first bar).
head 101 Playing live
barms 2400
cap Playing live | A style and your chords. Nothing here is recorded.
wait 3500
# ---- 1. a style
cap 1. Pick a style | On Home, turn KNOB 1 (Style). Here: Funk.
wait 2500
turn K1 1
wait 2500
expect tempo 100
# ---- 2. play
cap 2. Play | Press PLAY, then hold three keys or more. The band follows your chord.
wait 2500
anchor A
tap PLAY
expect sect 2
at A 0 400
chord 1500 11 14 18 21
at A 1 1200
cap 2. Play | The chord stays when you let go. Play the next one when you like.
at A 2 30
chord 1500 4 8 11 14
at A 4 30
chord 1500 11 14 18 21
at A 6 30
chord 1500 4 8 11 14
# ---- 3. a fill into Groove B
at A 7
cap 3. Go busier | Hold HOME and press black key 4: a fill, then the busier groove.
at A 7 1500
press HOME
tapkey b3
release HOME
at A 8 30
expect sect 4
chord 1500 11 14 18 21
at A 9 30
expect sect 3
at A 10 30
chord 1500 4 8 11 14
# ---- 4. back to Groove A
at A 11
cap 4. Come back down | HOME and black key 4 again: a fill, then the first groove.
at A 11 1500
press HOME
tapkey b3
release HOME
at A 12 30
expect sect 5
chord 1500 11 14 18 21
at A 13 30
expect sect 2
at A 14 30
chord 1500 4 8 11 14
# ---- 5. the Ending
at A 15
cap 5. End it | Hold HOME and press black key 5. The Ending plays, then Polly stops.
at A 15 1500
press HOME
tapkey b4
release HOME
at A 16 30
expect sect 6
chord 1500 11 14 18 21
wait 7000
expect playing 0
cap Next: 102 | Write your own progression, so the band plays your chords by itself.
wait 4000
