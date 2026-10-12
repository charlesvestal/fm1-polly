# Polly 109, keeping your work (docs/course/109-keeping-your-work.md), where 108 ended (hidden: 101 to 108 replayed),
# with a pause on each step's caption before its first move and after its last
# (tools/polly_video.py tools/video/109.story OUT.mp4).
# Nothing plays: saving, undo, songs and Memory, all stopped. C1's sound (EDIT, KNOB 1: 108's Rock Organ, one step
# on) is a change to save and undo. Memory holds three places: S1's Groove A (yours) (107, 108), S1's melodies (105),
# and 106's copy in place 2, which no song plays (given back to the style): the one freed. Polly saves by itself ~4 s after the last move: SAVE goes
# in before that. The backup (step 5) is on the computer: not in the video.
head 109 Keeping your work
# ---- where 108 ended: Funk, stopped on Home, saved; Groove A yours (107's notes, 108's sounds), transpose D
hide
include 108.story
expect playing 0
expect page 0
expect track 5
expect trk5_program 19
expect transpose 2
show
cap Keeping your work | Save, undo, switch songs, free Memory, and back up to your computer.
wait 3500
expect dirty 0
expect lit_SAVE 0
# ---- 1. save
cap 1. Save | Press EDIT and turn KNOB 1 (Sound) one step. SAVE lights, and the header shows "unsaved".
wait 2500
tap EDIT
expect page 8
expect track 5
wait 900
turn K1 1
expect trk5_program 20
expect dirty 1
wait 900
still 1-unsaved
wait 700
expect lit_SAVE 1
cap 1. Save | Press SAVE. When Polly is stopped, it also saves by itself after about four seconds.
wait 1500
tap SAVE
wait 100
expect dirty 0
expect lit_SAVE 0
expect msgtext_is_SAVED 1
wait 2200
# ---- 2. undo, redo
cap 2. Undo and redo | Hold HOME and press REC to undo the last change.
wait 2500
press HOME
wait 300
still 2-undo
press REC
wait 800
release REC
expect msgtext_is_UNDO_C1_SOUND 1
expect trk5_program 19
expect recording 0
wait 400
release HOME
wait 1800
cap 2. Undo and redo | Hold HOME and press PLAY to redo it.
wait 2500
press HOME
wait 300
press PLAY
wait 800
release PLAY
expect msgtext_is_REDO_C1_SOUND 1
expect trk5_program 20
expect playing 0
wait 400
release HOME
wait 2200
# ---- 3. four songs
cap 3. Switch songs | Hold HOME and turn PRESETS: songs S1 to S4.
wait 2500
tap HOME
expect page 0
wait 1000
press HOME
wait 400
turn PRESET 1
expect song 2
still 3-song
wait 2000
cap 3. Switch songs | Turn it back to S1. Each song has its own sections, progressions, melodies, tempo and style.
wait 1500
turn PRESET -1
expect song 1
wait 1500
release HOME
expect page 0
wait 2000
# ---- 4. memory (5: a place freed)
cap 4. Open Memory | Press SEQ, then SEL. Turn SELECT to MEMORY, the last row, and press SEL.
wait 2500
tap SEQ
wait 700
tap SEL
expect list_open 1
expect list_has_MEMORY 1
wait 700
turn SELECT 9 150
wait 500
tap SEL
expect slots_list 1
expect memrow1_is_S1:_Groove_A_(yours)|Funk 1
expect memrow2_is_S1:_melodies 1
expect memrow3_is_No_song:_groove_(yours)|Pop 1
expect memrow4_is_free 1
expect memrow16_is_free 1
still 4-memory
wait 1000
poster
wait 1500
cap 4. Open Memory | Memory's 16 places keep your grooves (yours) and melodies, for all four songs. The header shows how many are used.
wait 2500
cap 5. Free a place | Turn SELECT to place 2. It holds your copy from 106, which no song plays. Press SEL, and Polly asks first.
wait 2500
expect list_sel 0
turn SELECT 2 400
expect list_sel 2
wait 800
tap SEL
expect asking 1
still 5-free
wait 1800
cap 5. Free a place | Press SEL again. The place is free, and the copy's notes are gone.
wait 2000
tap SEL
expect asking 0
expect slottext2_is_free 1
expect memrow3_is_free 1
expect memrow1_is_S1:_Groove_A_(yours)|Funk 1
expect trk5_program 20
wait 2500
tap HOME
wait 400
tap HOME
expect list_open 0
wait 1500
cap Next: 110 | With other gear.
wait 4000
