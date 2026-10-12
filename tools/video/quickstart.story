# Polly: the quick start's "Write a song" (QUICKSTART.md), step by step from a fresh boot, with a pause on each
# step's caption before its first move and after its last (tools/polly_video.py tools/video/quickstart.story OUT.mp4).
# Pop, 105 BPM: a bar 2285.714 ms. Keys (octave 0): F3 0, G3 2, A3 4, B3 6, C4 7, D4 9, E4 11, F4 12, G4 14.
# Am 4 7 11, F 0 4 7, C 7 11 14, G 2 6 9. The band plays from step 1 on, so the steps sit on bars of anchor A.
head quick start
barms 2285.714
cap Polly quick start | Write a song: a verse, a chorus, four sections. Fresh from boot, on the Home page.
wait 3500
# ---- 1. a style
cap 1. Pick a style | Turn KNOB 1 (Style) to pick a style. Here: a look round, then back to Pop.
wait 2500
turn K1 1 600
turn K1 1 600
turn K1 1 700
turn K1 -3 300
wait 1500
expect tempo 105
cap 1. Pick a style | Press PLAY, then play a chord with three keys or more. The band follows your chord.
wait 2500
anchor A
tap PLAY
expect playing 1
at A 0 400
chord 1700 4 7 11
at A 1 30
chord 1900 0 4 7
at A 2 30
chord 1900 7 11 14
at A 3 30
chord 1900 2 6 9
at A 4 30
chord 900 4 7 11
# ---- 2. the verse
at A 6
cap 2. Write a verse | Press REC. It counts in to the next bar.
at A 7 1050
tap REC
expect take 2
cap 2. Write a verse | Play Am, F, C and G, one chord per bar.
at A 8 30
chord 1900 4 7 11
at A 9 30
chord 1900 0 4 7
at A 10 30
chord 1900 7 11 14
at A 11 30
chord 1900 2 6 9
at A 12 -250
tap REC
expect take 0
wait 300
expect mode 1
expect loop_bars9 4
cap 2. Write a verse | REC again to stop. Your chords are progression P9, the first empty one, and the band loops them.
# ---- 3. name it
at A 14
cap 3. Name it | Press ARP: the Progression page, the chord editor.
wait 2000
tap ARP
wait 2000
cap 3. Name it | Turn SELECT to page 2, then KNOB 4 (Name) to "Verse".
wait 1500
turn SELECT 1
wait 1200
turn K4 1
wait 2500
expect loop_name9 Verse
# ---- 4. the chorus
at A 19
cap 4. Write a chorus | Press HOME. Turn KNOB 4 (Prog) anticlockwise, past P1, to "-": the band follows your hands again.
wait 2500
tap HOME
wait 1000
turn K4 -9 110
wait 1200
expect mode 0
at A 22 1050
cap 4. Write a chorus | Press REC, play C, G, Am and F, then REC again. That's P10, the next empty one.
tap REC
expect take 2
at A 23 30
chord 1900 7 11 14
at A 24 30
chord 1900 2 6 9
at A 25 30
chord 1900 4 7 11
at A 26 30
chord 1900 0 4 7
at A 27 -250
tap REC
expect take 0
wait 300
expect mode 1
expect loop_bars10 4
cap 4. Write a chorus | Name it "Chorus" the same way: ARP, SELECT to page 2, KNOB 4 (Name).
wait 2000
tap ARP
wait 1300
turn SELECT 1
wait 1000
turn K4 2 400
wait 2500
expect loop_name10 Chorus
# ---- 5. the song
at A 32
cap 5. Build the song | Press HOME twice: Home, then the Song page.
wait 2000
tap HOME
wait 1000
tap HOME
wait 1500
expect page 1
cap 5. Build the song | Each white key adds a section that plays its progression: 9, 10, 9, 10.
wait 2000
tapkey w8
wait 800
tapkey w9
wait 800
tapkey w8
wait 800
tapkey w9
wait 2500
expect rows 4
expect row_loop1 9
expect row_loop2 10
# ---- 6. Groove B on the chorus
at A 38
cap 6. Make the chorus bigger | Turn KNOB 1 (Section) to section 2, the first chorus.
wait 1500
turn K1 -3 350
wait 1200
expect cursor 2
cap 6. Make the chorus bigger | Turn KNOB 3 (Groove) to B, the busier groove.
wait 1200
turn K3 2 450
wait 1800
expect row_sect2 2
expect row_sect4 0
cap 6. Make the chorus bigger | Turn KNOB 1 to section 4 and KNOB 3 to B too.
wait 1500
turn K1 2 350
wait 600
turn K3 2 450
wait 2500
expect cursor 4
expect row_sect4 2
# ---- 7. play the song
at A 45
cap 7. Play the song | Press PLAY on the Song page. The song takes over at the next bar.
at A 46 1000
tap PLAY
at A 47 400
expect row 1
cap 7. Play the song | Section 1: the verse.
at A 51 400
expect row 2
cap 7. Play the song | Section 2: the chorus, on Groove B. A fill leads in.
at A 55 400
expect row 3
cap 7. Play the song | Section 3: the verse again.
at A 59 400
expect row 4
cap 7. Play the song | Section 4: the chorus again, then an Ending, and the song stops by itself.
at A 67
expect playing 0
# ---- 8. save
cap 8. Save | Press SAVE, or just stop: Polly saves by itself.
wait 2000
tap SAVE
wait 3000
cap Made a mistake? | Hold HOME and press REC to undo. Hold HOME and press PLAY to redo.
wait 4000
