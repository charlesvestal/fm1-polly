# Polly 102, your own progressions (docs/course/102-your-own-progressions.md), from a fresh boot, with a pause on each
# step's caption before its first move and after its last (tools/polly_video.py tools/video/102.story OUT.mp4).
# Pop, 105 BPM: a bar 2285.714 ms. Keys (octave 0): F3 0, G3 2, A3 4, B3 6, C4 7, D4 9, E4 11, G4 14.
# Am 4 7 11, F 0 4 7, C 7 11 14, G 2 6 9. White key 1 = key 0, white key 9 = key 14. Anchor A: PLAY.
head 102 Your own progressions
barms 2285.714
cap Your own progressions | Record your chords, fix one, set the length, name it, and play it from anywhere.
wait 3500
# ---- 1. record: REC late in bar 0 counts in to bar 1; Am F C G on bars 1-4; REC just before bar 5
cap 1. Record a progression | On Home, press PLAY, then REC. The click counts in to the next bar.
wait 2500
anchor A
tap PLAY
expect playing 1
expect mode 0
at A 0 1100
tap REC
expect take 2
at A 1 30
cap 1. Record a progression | Play Am, F, C and G, one chord per bar.
chord 1900 4 7 11
at A 2 30
chord 1900 0 4 7
at A 3 30
still 1-record
chord 1900 7 11 14
at A 4 30
chord 1900 2 6 9
at A 5 -250
tap REC
expect take 0
wait 300
expect mode 1
expect loop_now 9
expect loop_bars9 4
expect loop_ch9_0 916
expect loop_ch9_4 500
expect loop_ch9_8 0
expect loop_ch9_12 700
cap 1. Record a progression | REC again. Your chords are P9, the first empty progression, and the band plays them round.
at A 7 1500
# ---- 2. fix the chord on bar 4: G to E7
cap 2. Fix a chord | Press ARP for the Progression page. Turn KNOB 1 (Bar) to bar 4.
wait 2500
tap ARP
wait 1200
expect page 4
expect app_loop 9
turn K1 3 400
expect cbeat 12
wait 1500
cap 2. Fix a chord | Turn KNOB 3 (Root) to E and KNOB 4 (Type) to 7. G becomes E7.
wait 2500
turn K3 -3 400
expect knob3_is_E 1
wait 500
turn K4 6 300
expect knob4_is_7 1
still 2-fix
expect loop_ch9_12 406
wait 1000
poster
wait 3000
# ---- 3. the length: 2 bars, then 4 again
cap 3. Change its length | Turn SELECT to page 2, then KNOB 1 (Length) to 2. The band plays Am and F.
wait 2500
turn SELECT 1
wait 800
expect page 5
turn K1 -2 400
still 3-length
expect loop_bars9 2
expect msgtext_is_2_CHORDS_HIDDEN_BACK_IF_IT_GROWS 1
wait 5000
cap 3. Change its length | Turn KNOB 1 back to 4. The hidden chords come back.
wait 2500
turn K1 2 400
expect loop_bars9 4
expect loop_ch9_12 406
wait 2500
# ---- 4. the name
cap 4. Name it | Turn KNOB 4 (Name) to "Verse".
wait 2500
turn K4 1
still 4-name
expect loop_name9 Verse
wait 3000
# ---- 5. from anywhere: P1, then P9 again
cap 5. Play it from anywhere | Hold HOME and press white key 1. P1 plays from the next bar.
wait 2500
press HOME
wait 300
note 900 0
release HOME
expect page 5
expect loop_now 9
wait 2600
expect loop_now 1
wait 2500
cap 5. Play it from anywhere | Hold HOME and press white key 9. Your Verse comes back at the next bar.
wait 2500
press HOME
wait 300
still 5-anywhere
note 900 14
release HOME
wait 2600
expect loop_now 9
wait 3000
cap Next: 103 | Your song.
wait 4000
