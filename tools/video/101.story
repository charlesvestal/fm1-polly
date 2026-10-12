# Polly 101, playing live (docs/course/101-playing-live.md), from a fresh boot, with a pause on each step's caption
# before its first move and after its last (tools/polly_video.py tools/video/101.story OUT.mp4).
# Funk, 100 BPM: a bar 2400 ms, its Intro 2 bars. Keys (octave 0): A3 4, C#4 8, E4 11, G4 14, B4 18, D5 21.
# Em7 11 14 18 21, A7 4 8 11 14. Anchor A: PLAY (the Intro's first bar), Groove A from A 2.
head 101 Playing live
barms 2400
cap Playing live | A style, your chords, the grooves, a fill, an ending. Nothing here is recorded.
wait 3500
# ---- 1. a style
cap 1. Pick a style | On Home, turn KNOB 1 (Style). Here: Funk. The tempo comes with it.
wait 2500
turn K1 1
wait 2500
expect tempo 100
# ---- 2. the Intro
cap 2. Start with an Intro | Hold HOME and press black key 1. Polly shows what PLAY will start with.
wait 2500
press HOME
wait 600
tapkey b0
wait 600
release HOME
wait 2000
cap 2. Start with an Intro | Play a chord, Em7, then press PLAY. The Intro holds that chord, then Groove A starts.
wait 2500
chord 900 11 14 18 21
wait 600
anchor A
tap PLAY
expect sect 1
# ---- 3. chords
at A 2 30
expect sect 2
cap 3. Play chords | Hold three keys or more. The chord stays when you let go: play the next one when you like.
at A 3 30
chord 1500 4 8 11 14
at A 4 30
chord 1500 11 14 18 21
at A 5 30
chord 1500 4 8 11 14
# ---- 4. a fill into Groove B
at A 6
cap 4. Go busier | Hold HOME and press black key 4: a one-bar fill, then Groove B.
at A 6 1200
press HOME
tapkey b3
release HOME
at A 7 30
expect sect 4
chord 1500 11 14 18 21
at A 8 30
expect sect 3
cap 4. Go busier | Groove B, the busier groove. Black key 3 goes straight to it, without a fill.
at A 9 30
chord 1500 4 8 11 14
# ---- 5. back to Groove A
at A 10
cap 5. Come back down | Black key 4 again: from Groove B the fill leads back to Groove A.
at A 10 1200
press HOME
tapkey b3
release HOME
at A 11 30
expect sect 5
chord 1500 11 14 18 21
at A 12 30
expect sect 2
cap 5. Come back down | Groove A again. What you ask for always starts at the next bar.
at A 13 30
chord 1500 4 8 11 14
# ---- 6. the Ending
at A 14
cap 6. End it | Hold HOME and press black key 5. The Ending plays at the next bar, then Polly stops.
at A 14 1200
press HOME
tapkey b4
release HOME
at A 15 30
expect sect 6
chord 1500 11 14 18 21
wait 7000
expect playing 0
cap Next: 102 | Write your own progression, so the band plays your chords by itself.
wait 4000
