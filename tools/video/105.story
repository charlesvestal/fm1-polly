# Polly 105, recording a melody (docs/course/105-recording-a-melody.md), from a fresh boot, with a pause on each
# step's caption before its first move and after its last (tools/polly_video.py tools/video/105.story OUT.mp4).
# Pop, 105 BPM: a bar 2285.714 ms. The song: section 1 P1 Pop (C G Am F), section 2 P2 Ballad (Am F C G), the
# style's Intro (2 bars) and Ending (2 bars). Keys (octave 0): key K is MIDI 53 + K: C4 7, D4 9, E4 11, F4 12,
# G4 14, A4 16, B4 18, C5 19, D5 21, E5 23, F5 24, G5 26.
# Anchor A: PLAY (the Intro: A 0-1, section 1 A 2-5, section 2 A 6-9, the Ending A 10-11).
head 105 Recording a melody
barms 2285.714
cap Recording a melody | Record a melody along a short song, punch in on a section, and add a second melody.
wait 3500
expect tempo 105
# ---- 1. a short song
cap 1. Make a short song | On Home, press HOME for the Song page. Press white keys 1 and 2: P1 Pop, then P2 Ballad.
wait 2500
tap HOME
expect page 1
wait 700
tapkey w0
wait 600
tapkey w1
still 1-song
wait 2000
expect rows 2
expect row_loop1 1
expect row_loop2 2
expect row_sect1 0
expect row_sect2 0
# ---- 2. M1, armed
cap 2. Arm M1 | Press PLAY. Press EDIT, turn ALGORITHM past C4 to M1, and press REC.
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
# ---- 3. the line, across both sections (M1: section 1 A 2-5, section 2 A 6-9)
at A 2 -150
cap 3. Play a line | Play across both sections. The whole song runs along the top, four bars below.
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
cap 3. Play a line | Section 2: the notes land on the line and in the four bars. REC again to stop.
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
at A 12 300
expect playing 0
# M1: 14 notes in each section, as played (E5 first, F4 the last of section 1; F5 in section 2)
expect lead_count1_1 14
expect lead_count1_2 14
expect lead1_row 1
expect lead1_note 76
expect lead14_note 65
expect lead15_row 2
expect lead16_note 77
# ---- 4. punch in on section 2 (stopped at the song's end: the cursor back on section 1)
wait 1200
cap 4. Punch in | HOME twice for the Song page. KNOB 1 (Section) to section 2. EDIT, REC, PLAY, and play.
wait 2500
tap HOME
wait 400
tap HOME
wait 700
expect page 1
expect cursor 1
turn K1 1
expect cursor 2
wait 900
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
expect sect 2
# over F (section 2's bar 2): C5 F5 E5, each a little late
mel P 1 2.1 0.45 19
still 4-punch
mel P 1 2.6 0.45 24
mel P 1 3.1 0.5 23
at P 2 -180
tap REC
wait 30
tap PLAY
wait 100
expect playing 0
expect recording 0
expect cursor 2
expect lead_count1_1 14
expect lead_count1_2 17
# ---- 5. M2: strings, from section 2 (the cursor), a held line under M1
cap 5. A second melody | Turn ALGORITHM to M2. Press REC and PLAY, and play. M1's notes show pale under it.
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
mel M 0 0.25 3.75 11
mel M 1 0 4 12
mel M 2 0 2 11
still 5-m2
mel M 2 2 2 14
mel M 3 0 3.8 9
at M 4 -180
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
