# Polly 105, recording a melody (docs/course/105-recording-a-melody.md), where 104 ended (hidden: 101 to 104
# replayed), with a pause on each step's caption before its first move and after its last
# (tools/polly_video.py tools/video/105.story OUT.mp4).
# Pop (104's), 105 BPM: a bar 2285.714 ms. 103's song: section 1 P1 Pop (C G Am F, the drums muted), section 2 P2
# Ballad (Am F C G) on Groove B x2, section 3 P1; the style's Intro (2 bars); At end "again" (104's).
# Keys (octave 0): key K is MIDI 53 + K: C4 7, D4 9, E4 11, F4 12, G4 14, A4 16, B4 18, C5 19, D5 21, E5 23, F5 24,
# G5 26.
# Anchor A: PLAY (the Intro: A 0-1, section 1 A 2-5, section 2 A 6-13: its first pass A 6-9).
head 105 Recording a melody
# ---- where 104 ended: Pop, stopped on Home, in the song (three sections, At end again)
hide
include 104.story
expect playing 0
expect page 0
expect tempo 105
expect knob1_is_Pop 1
expect knob4_is_Song 1
expect rows 3
expect song_at_end 1
show
barms 2285.714
cap Recording a melody | Record a melody along your song, add notes to one section, and record a second melody.
wait 3500
expect tempo 105
# ---- 1. M1, armed
cap 1. Start recording M1 | Press PLAY. Press EDIT, turn ALGORITHM past C4 to M1, and press REC. Recording starts after the Intro.
wait 2500
anchor A
tap PLAY
wait 400
expect playing 1
expect sect 1
tap EDIT
wait 300
expect page 8
turn ALGO 4 150
expect lead_sel 1
wait 300
tap REC
expect recording 1
expect msgtext_is_RECORD_M1_INTO_THE_SONG 1
still 2-arm
# ---- 2. the line, across sections 1 and 2 (M1: section 1 A 2-5, section 2's first pass A 6-9)
at A 2 -150
cap 2. Play a melody | Play across sections 1 and 2. The top line shows the whole song, with four bars below it.
mel A 2 0 1.5 23
expect sect 2
expect row 1
mel A 2 1.5 0.5 21
mel A 2 2 1 19
mel A 2 3 1 14
mel A 3 0 1.5 21
mel A 3 1.5 0.5 18
mel A 3 2 2 14
mel A 4 0 1 16
mel A 4 1 1 19
mel A 4 2 1.5 23
mel A 4 3.5 0.5 21
mel A 5 0 2 19
mel A 5 2 1 16
mel A 5 3 0.8 12
at A 6 -150
cap 2. Play a melody | In section 2, the notes land on the line and in the four bars. Press REC, then PLAY, to stop.
mel A 6 0 1 23
expect row 2
mel A 6 1 0.5 24
mel A 6 1.5 0.5 23
mel A 6 2 2 19
mel A 7 0 1 21
mel A 7 1 1 19
poster
mel A 7 2 2 16
mel A 8 0 1.5 14
mel A 8 1.5 0.5 16
still 3-line
mel A 8 2 1 19
mel A 8 3 1 23
mel A 9 0 1 23
mel A 9 1 1 21
mel A 9 2 1.8 18
at A 10 -180
tap REC
expect recording 0
at A 10 600
tap PLAY
wait 300
expect playing 0
# M1: 14 notes in each section, as played (E5 first, F4 the last of section 1; F5 in section 2)
expect lead_count1_1 14
expect lead_count1_2 14
expect lead1_row 1
expect lead1_note 76
expect lead14_note 65
expect lead15_row 2
expect lead16_note 77
# ---- 3. punch in on section 2
wait 1200
cap 3. Add to section 2 | Press HOME for the Home page, and HOME again for the Song page. The cursor is on section 2, where the song stopped.
wait 2500
tap HOME
wait 400
tap HOME
wait 700
expect page 1
expect cursor 2
wait 2000
cap 3. Add to section 2 | Press EDIT, then REC, then PLAY. Polly clicks for one bar, then section 2 plays. Play a few notes.
wait 2500
tap EDIT
wait 500
expect page 8
tap REC
expect recording 1
wait 600
anchor P
tap PLAY
wait 10
expect row 2
expect count_in 4
# a bar of count-in (P 0-1: the click only), then section 2 (Groove B) from P 1; over F (section 2's bar 2): C5 F5 E5, each a
# little late
at P 1 300
expect count_in 0
expect sect 3
mel P 2 2.1 0.45 19
still 4-punch
mel P 2 2.6 0.45 24
mel P 2 3.1 0.5 23
at P 3 -180
tap REC
wait 30
tap PLAY
wait 100
expect playing 0
expect recording 0
expect cursor 2
expect lead_count1_1 14
expect lead_count1_2 17
# ---- 4. M2: strings, from section 2 (the cursor), a held line under M1
cap 4. Record M2 | Turn ALGORITHM to M2. Press REC, then PLAY, and play after the bar of clicks. M1's notes show pale underneath.
wait 2500
turn ALGO 1
expect lead_sel 2
wait 900
tap REC
expect recording 1
wait 600
anchor M
tap PLAY
wait 10
expect row 2
expect count_in 4
# the count-in M 0-1, section 2 from M 1
mel M 1 0.25 3.75 11
mel M 2 0 4 12
mel M 3 0 2 11
still 5-m2
mel M 3 2 2 14
mel M 4 0 3.8 9
at M 5 -180
tap REC
wait 30
tap PLAY
wait 100
expect playing 0
expect lead_count2_2 5
expect lead_count1_2 17
wait 2000
cap Next: 106 | Your own grooves.
wait 4000
