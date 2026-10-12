# Polly 106, your own grooves (docs/course/106-your-own-grooves.md), where 105 ended (hidden: 101 to 105 replayed),
# with a pause on each step's caption before its first move and after its last
# (tools/polly_video.py tools/video/106.story OUT.mp4).
# Pop (104's), 105 BPM: a bar 2285.714 ms. Out of the song (Home's Prog on "-"), on Groove A (stopped in section 2,
# Polly would start on its Groove B). Pop's Groove A is 2 bars; its C2 (Clean Guitar) has no notes, so the first REC
# makes it "as played". The band plays free, on the song's last chord. Keys (octave 0): key K is MIDI 53 + K: C4 7,
# E4 11, G4 14, A4 16, C5 19, D5 21, E5 23, G5 26. Anchor A: PLAY (Groove A from A 0).
head 106 Your own grooves
# ---- where 105 ended: Pop, stopped on the Sound page on M2, in the song (stopped in section 2)
hide
include 105.story
expect playing 0
expect page 8
expect tempo 105
expect lead_sel 2
expect rows 3
expect lead_count1_2 17
expect song1_style1 33
show
barms 2285.714
cap Your own grooves | Record an instrument into the style's groove, and the groove becomes yours.
wait 3500
expect tempo 105
expect song1_style1 33
# ---- 1. free, Groove A, an instrument
cap 1. Pick an instrument | Press HOME for the Home page. Turn KNOB 4 (Prog) down, past P1, to "-". The band plays free, outside the song.
wait 2500
tap HOME
wait 900
expect page 0
expect knob4_is_Song 1
turn K4 -2 400
expect knob4_is_- 1
expect mode 0
wait 1500
cap 1. Pick an instrument | Hold HOME and press black key 2 for Groove A.
wait 2500
press HOME
wait 300
note 900 b1
wait 300
expect next_sect 2
release HOME
wait 1500
cap 1. Pick an instrument | Press PLAY. Press SEQ for the Band page, and turn ALGORITHM to C2. Its row is empty.
wait 2500
anchor A
tap PLAY
wait 600
expect playing 1
expect sect 2
tap SEQ
wait 700
expect page 6
expect track 8
expect lead_sel 2
turn ALGO -4 300
expect track 6
expect slot1_trk6_count 0
still 1-instrument
wait 2000
# ---- 2. record: REC at A 3, it starts at A 4 (the groove's bar 1); pass 1 A 4-5, pass 2 A 6-7
at A 2 1300
cap 2. Record | Press REC. Groove A becomes your own copy, Groove A (yours). Play from the next bar.
at A 3 900
tap REC
expect recording 1
expect msgtext_is_C2:_AS_PLAYED_GROOVE_A_(YOURS) 1
# (Memory's place 1 holds 105's melodies: the copy goes into place 2)
expect slot_kind1 2
expect song1_style1 1
mel A 4 0 1 19
expect rec_wait 0
expect rec_pass 1
mel A 4 1.5 0.5 23
still 2-record
mel A 4 2 1 26
mel A 4 3 1 23
mel A 5 0 1.5 21
mel A 5 1.5 0.5 19
mel A 5 2 2 14
at A 6 -150
cap 2. Record | The groove loops, and each pass adds to it. Press REC again to stop.
mel A 6 0.5 0.5 14
expect rec_pass 2
mel A 6 2.5 0.5 19
poster
mel A 7 0.5 0.5 16
mel A 7 2.5 0.5 11
at A 8 -180
tap REC
expect recording 0
expect slot1_trk6_count 11
wait 2000
# ---- 3. the groove's length
cap 3. Make it longer | Turn SELECT to the Groove length page. Turn KNOB 1 (Bars) to 4.
wait 2500
turn SELECT 1
wait 600
expect page 7
expect slot1_bars 2
turn K1 2 400
expect slot1_bars 4
still 3-length
wait 2500
# ---- 4. another style: the question, kept
cap 4. Change the style | Press HOME and turn KNOB 1 (Style) to Funk. Polly asks: keep Groove A?
wait 2500
tap HOME
wait 600
expect page 0
turn K1 1
expect knob1_is_Funk_(yours) 1
wait 1200
expect asking 1
expect footer_is_SEL_keep|HOME_use_Funk's 1
still 4-style
wait 2000
cap 4. Change the style | Press SEL to keep it. Your Groove A plays with Funk's band.
wait 2500
tap SEL
wait 50
expect asking 0
expect msgtext_is_GROOVES_KEPT_(YOURS) 1
expect song1_style1 1
expect song1_style2 40
expect tempo 100
wait 3000
# ---- 5. back to the style (stopped: while playing with changes unsaved it says UNSAVED SAVE FIRST)
cap 5. Give the groove back | Press PLAY to stop. Press SEQ, then SEL for the list.
wait 2500
tap PLAY
wait 600
expect playing 0
tap SEQ
wait 700
expect page 6
tap SEL
wait 500
expect list_open 1
expect list_sel 8
cap 5. Give the groove back | Turn SELECT to BACK TO STYLE and press SEL. Your copy stays in Memory.
wait 2500
turn SELECT 2 300
expect list_sel 10
expect list_has_BACK_TO_STYLE 1
still 5-back
wait 1500
tap SEL
wait 50
expect list_open 0
expect msgtext_is_BACK_TO_FUNK 1
expect song1_style1 39
expect slot_kind2 1
wait 2500
cap Next: 107 | Fixing notes.
wait 4000
