# Polly's release announcement (tools/polly_video.py tools/video/release.story OUT.mp4 --fps 20): what Polly is and
# does, from Polly itself, and where to get it. Hidden first: song.story, the demo song (two progressions, four
# sections, a melody), so the second half can play it. Keys (octave 0): Am 4 7 11, F 0 4 7, C 7 11 14, G 2 6 9.
look announce
head 0.3 · a pocket arranger for the M-VAVE FM-1
hide
include song.story
# Home, the tempo the song's own (picking a style then keeps 105, so the bars stay put)
tap HOME
wait 600
turn K2 1
wait 300
turn K2 -1
wait 300
turn K4 -20 30
wait 400
expect mode 0
expect tempo 105
show
barms 2285.714
card Version 0.3 | A pocket arranger for the M-VAVE FM-1, inspired by the Yamaha QY70 | Free firmware, GPL-3.0
# ---- the band starts under the title, following the chords played (each a moment before its bar line)
wait 1200
key 4 down
key 7 down
key 11 down
wait 500
anchor A
tap PLAY
wait 300
expect playing 1
wait 900
key 4 up
key 7 up
key 11 up
at A 1 -60
chord 1900 0 4 7
card -
cap Play a chord, and the band follows it | Hold three keys or more. Eight instruments play along: drums, percussion, bass, guitars, keys and pads.
at A 2 -60
chord 1900 7 11 14
at A 3 -60
chord 1900 2 6 9
at A 4 -60
chord 1900 4 7 11
at A 5 -60
chord 1900 0 4 7
# ---- the styles: Funk, Bossa, House, two bars each, then Pop again
cap 16 styles of its own | Each has an Intro, two grooves, fills and an Ending. The drums are real drummers' performances.
at A 5 1950
turn K1 1
at A 6 -60
chord 1900 7 11 14
at A 7 -60
chord 1900 2 6 9
at A 7 1950
turn K1 3 60
at A 8 -60
chord 1900 4 7 11
at A 9 -60
chord 1900 0 4 7
at A 9 1950
turn K1 5 40
at A 10 -60
chord 1900 7 11 14
at A 11 -60
chord 800 2 6 9
at A 11 900
turn K1 -9 30
expect tempo 105
# ---- grooves: HOME held long enough to read, black key 4 on beat 2 of bar 12, the fill in bar 13, Groove B on
cap A busier groove, with a fill on the way | Hold HOME and press a black key. The band changes at the next bar.
at A 12 -400
press HOME
wait 1400
tapkey b3
wait 600
release HOME
at A 13 -60
chord 1900 4 7 11
expect sect 4
at A 14 -60
chord 1900 0 4 7
expect sect 3
at A 15 -60
chord 1900 7 11 14
at A 16 -60
chord 1900 2 6 9
at A 17 -60
chord 1900 4 7 11
at A 17 2000
tap PLAY
wait 900
expect playing 0
# ---- the song from the demo
cap Write your own chords and build a song | Record progressions, put them in sections, and give each section its groove, repeats and mutes.
tap HOME
wait 900
expect page 1
anchor S
tap PLAY
wait 300
at S 3 0
cap Record melodies along the whole song | Two melody tracks, recorded section by section, follow the song's chords.
tap EDIT
wait 200
at S 9 0
cap Fix any note in the piano roll | Change, move, delete or add single notes in a melody or any instrument.
tap SEQ
wait 300
tap SEQ
wait 1800
turn SELECT 3 450
wait 900
turn K2 2 350
wait 900
turn K3 2 350
wait 900
turn SELECT 2 450
at S 14 0
cap 128 General MIDI sounds, a mixer and effects | Every instrument and melody has its own sound, volume, pan, reverb and chorus.
tap LFO
wait 900
cap 128 General MIDI sounds, a mixer and effects | On the Mixer, KNOB 3 and KNOB 4 send the melody to the reverb and the chorus.
turn K3 50 30
wait 500
turn K4 60 30
wait 300
at S 17 0
cap The Ending, and Polly stops by itself | Hold HOME and press black key 5 at any time.
tap HOME
wait 900
press HOME
wait 2500
tapkey b4
wait 1000
release HOME
wait 9000
expect playing 0
card charlesvestal.github.io/fm1-polly | Install it from Chrome or Edge, or try it in your browser first. | Ten short lessons with videos show you around.
wait 6000
