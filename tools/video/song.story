# Polly: a song from scratch (tools/polly_video.py tools/video/song.story OUT.mp4), MANUAL.md's "Writing a song"
# from a fresh boot. Pop, 105 BPM: a bar 2285.714 ms. Keys (octave 0): F3 0, G3 2, A3 4, B3 6, C4 7, D4 9, E4 11,
# F4 12, G4 14, A4 16, B4 18, C5 19, D5 21, E5 23. Am 4 7 11, F 0 4 7, C 7 11 14, G 2 6 9.
barms 2285.714
cap Polly | a pocket arranger after the QY70, on the M-VAVE FM-1. Fresh from boot: Home, nothing playing.
wait 2500
# ---- 1. a style
cap 1. Pick a style | Home's KNOB 1 turns through the styles (each brings its tempo); back to Pop at 105 BPM
wait 600
turn K1 1 500
turn K1 1 500
turn K1 1 600
turn K1 -3 250
wait 900
expect tempo 105
# ---- 2. chords in free
cap 2. Play some chords | Three keys make a chord; PLAY, and the band follows whatever you play: Am, F, C, G
chord 900 4 7 11
wait 300
anchor A
tap PLAY
wait 1200
expect playing 1
at A 1 30
chord 1900 0 4 7
at A 2 30
chord 1900 7 11 14
at A 3 30
chord 1900 2 6 9
at A 4 30
chord 900 4 7 11
# ---- 3. the verse take
cap 3. Record the verse | REC: the click counts in to the next bar; then Am, F, C, G, a bar each; REC again
at A 4 1050
tap REC
expect take 2
at A 5 30
chord 1900 4 7 11
at A 6 30
chord 1900 0 4 7
at A 7 30
chord 1900 7 11 14
at A 8 30
chord 1900 2 6 9
at A 9 -250
tap REC
expect take 0
wait 300
expect mode 1
expect loop_bars9 4
expect loop_ch9_0 916
expect loop_ch9_12 700
# ---- 4. name it
cap 4. Name it | ARP shows the progression, P9; SELECT to page 2, KNOB 4 (Name) to "Verse". It plays round meanwhile.
wait 500
tap ARP
wait 1300
turn SELECT 1
wait 700
turn K4 1
wait 1800
expect loop_name9 Verse
# ---- 5. the chorus
cap 5. Record the chorus | HOME, KNOB 4 back past P1 to "-": free again. REC, then C, G, Am, F; REC
tap HOME
wait 700
turn K4 -9 90
wait 400
expect mode 0
at A 12 30
chord 900 7 11 14
at A 12 1050
tap REC
expect take 2
at A 13 30
chord 1900 7 11 14
at A 14 30
chord 1900 2 6 9
at A 15 30
chord 1900 4 7 11
at A 16 30
chord 1900 0 4 7
at A 17 -250
tap REC
expect take 0
wait 300
expect mode 1
expect loop_bars10 4
expect loop_ch10_0 0
expect loop_ch10_4 700
expect loop_ch10_8 916
expect loop_ch10_12 500
# ---- 6. name it
cap 6. Name it | ARP, SELECT to page 2, KNOB 4 (Name) to "Chorus": P10. The band plays it round.
wait 400
tap ARP
wait 1100
turn SELECT 1
wait 600
turn K4 2 300
wait 1500
expect loop_name10 Chorus
# ---- 7. the song
cap 7. Build the song | HOME twice: Home, then the Song page; white keys 9, 10, 9, 10: verse, chorus, verse, chorus
tap HOME
wait 600
tap HOME
wait 900
expect page 1
tapkey w8
wait 500
tapkey w9
wait 500
tapkey w8
wait 500
tapkey w9
wait 1200
expect rows 4
expect row_loop1 9
expect row_loop2 10
expect row_loop3 9
expect row_loop4 10
# ---- 8. shape it
cap 8. Shape it | KNOB 1 to the chorus (section 2), KNOB 3 (Groove) to B: busier
turn K1 -3 250
wait 600
expect cursor 2
turn K3 2 350
wait 1500
expect row_sect2 2
expect row_sect4 0
cap 8. Shape it | The last chorus: KNOB 3 to B too, KNOB 4 (Repeat) to x2, then SELECT to page 2, KNOB 1 (Transpose) to +2
turn K1 2 300
wait 500
expect cursor 4
turn K3 2 350
wait 500
expect row_sect4 2
turn K4 1
wait 1000
expect row_rep4 2
turn SELECT 1
wait 700
expect page 2
turn K1 2 350
wait 1500
expect row_key4 2
cap 8. Shape it | A breakdown: back on page 1, KNOB 1 to verse 2 (section 3); black keys 1, 2, 3 mute D1, D2, PC there
turn SELECT -1
wait 500
turn K1 -1
wait 600
expect cursor 3
tapkey b0
wait 350
tapkey b1
wait 350
tapkey b2
wait 1500
expect row_mute3 7
# ---- 9. a melody: one pass along the whole song, M1, a different line in each section (the song from bar A 28:
# section 1 the verse A 28-31 (Am F C G), 2 the chorus A 32-35 (C G Am F, Groove B), 3 verse 2 A 36-39 (the
# breakdown), 4 the last chorus A 40-47 (x2, +2: the keys sound a tone up, stored as played). mel A BAR BEAT DUR KEY
cap 9. Add a melody | On the Song page PLAY: the song takes over at the next bar. EDIT, ALGORITHM past C4 to M1, REC
at A 27 100
tap PLAY
wait 250
tap EDIT
wait 200
turn ALGO 4 110
expect lead_sel 1
wait 100
tap REC
expect recording 1
at A 28 -150
cap 9. Add a melody | The whole song along the top, four bars below; the notes land on both. Section 1: the verse line
mel A 28 0 1.5 23
mel A 28 1.5 0.5 21
mel A 28 2 1 19
mel A 28 3 1 16
mel A 29 0 1.5 19
mel A 29 1.5 0.5 16
mel A 29 2 0.5 19
mel A 29 2.5 0.5 21
mel A 29 3 1 19
mel A 30 0 1 14
mel A 30 1 1 19
mel A 30 2 1.5 23
mel A 30 3.5 0.5 21
mel A 31 0 1.5 21
mel A 31 2 1 18
mel A 31 3 0.8 14
at A 32 -150
cap 9. Add a melody | Section 2, the chorus: the four bars run on across the boundary; a higher line, new notes
mel A 32 0 0.5 23
mel A 32 0.5 1.5 26
mel A 32 2 1 23
mel A 32 3 1 19
mel A 33 0 1 21
mel A 33 1 1.5 26
mel A 33 2.5 0.5 24
mel A 33 3 1 21
mel A 34 0 1.5 23
mel A 34 1.5 0.5 19
mel A 34 2 0.5 23
mel A 34 2.5 1.5 26
mel A 35 0 1 24
mel A 35 1 1 23
mel A 35 2 1.8 19
at A 36 -150
cap 9. Add a melody | Section 3, verse 2 (the breakdown): the verse line again, varied
mel A 36 0 1 16
mel A 36 1 1 19
mel A 36 2 2 23
mel A 37 0 1.5 24
mel A 37 1.5 0.5 23
mel A 37 2 2 19
mel A 38 0 1 14
mel A 38 1 0.5 19
mel A 38 1.5 0.5 21
mel A 38 2 1 23
mel A 38 3 1 26
mel A 39 0 1 21
mel A 39 1 1 18
mel A 39 2 1.8 14
at A 40 -150
cap 9. Add a melody | Section 4, the last chorus (x2): the chorus line, then a new ending; it sounds a tone up with the key change
mel A 40 0 0.5 23
mel A 40 0.5 1.5 26
mel A 40 2 1 23
mel A 40 3 1 19
mel A 41 0 1 21
mel A 41 1 1.5 26
mel A 41 2.5 0.5 24
mel A 41 3 1 21
mel A 42 0 1.5 23
mel A 42 1.5 0.5 19
mel A 42 2 0.5 23
mel A 42 2.5 1.5 26
mel A 43 0 1 24
mel A 43 1 1 23
mel A 43 2 1.8 19
mel A 44 0 1 26
mel A 44 1 1 23
mel A 44 2 1 19
mel A 44 3 1 23
mel A 45 0 1.5 21
mel A 45 1.5 0.5 19
mel A 45 2 1 21
mel A 45 3 1 26
mel A 46 0 1 23
mel A 46 1 1 21
mel A 46 2 1 19
mel A 46 3 1 16
mel A 47 0 1 19
mel A 47 1 2.8 19
at A 48 -180
tap REC
wait 30
tap PLAY
wait 100
expect playing 0
expect recording 0
# the melody, a section at a time: so many notes in each, and each section's own pitches
expect lead_count1_1 16
expect lead_count1_2 15
expect lead_count1_3 14
expect lead_count1_4 29
# (MIDI notes, as heard; the list goes section by section) verse: from E5 down to A4, never past E5
expect lead1_row 1
expect lead1_note 76
expect lead4_note 69
expect lead12_note 76
# the chorus: higher, up to G5 (79) and F5 (77)
expect lead17_row 2
expect lead18_note 79
expect lead23_note 77
expect lead28_note 79
# verse 2: starts low (A4), climbs to F5 and G5, unlike verse 1
expect lead32_row 3
expect lead32_note 69
expect lead35_note 77
expect lead42_note 79
# the last chorus: the chorus line a tone up (G5 -> A5, E5 -> F#5), then its new ending from bar 5 (A5 on its downbeat)
expect lead46_row 4
expect lead46_note 78
expect lead47_note 81
expect lead61_row 4
expect lead61_pos 7680
expect lead61_note 81
expect lead74_note 74
# ---- 10. play it
cap 10. Play it | From the top, on the Song page: the Intro, the sections with fills between them, the Ending
wait 900
tap HOME
wait 500
tap HOME
wait 900
expect page 1
anchor S
hitreset
tap PLAY
wait 300
expect sect 1
cap 10. Play it | The Intro: PLAY from a stop on the Song page opens with the style's Intro
at S 2 400
expect row 1
cap 10. Play it | Section 1, the verse. M1: the verse line
at S 6 400
expect row 2
expect sect 3
cap 10. Play it | Section 2, the chorus on Groove B (a fill leads in). EDIT: M1's page, the song as one line
wait 700
tap EDIT
expect page 8
expect lead_sel 1
cap 10. Play it | M1 along the whole song: the teal line runs through the sections, the four bars below follow it
at S 10 400
expect row 3
expect mutes 7
cap 10. Play it | Section 3, the breakdown (D1, D2, PC muted): on into verse 2, M1's varied verse line
at S 14 400
expect row 4
cap 10. Play it | Section 4, the last chorus, twice: the widest box on the line. M1 a tone up with the key change (+2)
at S 18 400
cap 10. Play it | The last chorus again: M1 takes its new ending. HOME twice: back to the Song page
tap HOME
wait 400
tap HOME
expect page 1
at S 22 400
expect sect 6
cap 10. Play it | The Ending, and the song stops by itself
at S 24 300
expect playing 0
# M1 played along: its 74 notes (track 9, LD1)
expect track_hits9 74
cap Done | Two progressions, four sections, one melody along the song, from a fresh boot. Stopped, Polly saves the song by itself.
wait 3200
