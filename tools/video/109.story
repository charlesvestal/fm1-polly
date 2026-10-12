# Polly 109, keeping your work (docs/course/109-keeping-your-work.md), from a fresh boot, with a pause on each step's
# caption before its first move and after its last (tools/polly_video.py tools/video/109.story OUT.mp4).
# Nothing plays: saving, undo, songs and Memory, all stopped. C1's sound (EDIT, KNOB 1) makes Groove A yours: a
# change to save and undo, and a place in Memory to free. Polly saves by itself ~4 s after the last move: SAVE goes
# in before that. The backup (step 5) is on the computer: not in the video.
head 109 Keeping your work
cap Keeping your work | Save, switch songs, undo, free memory, and back up to your computer.
wait 3500
expect dirty 0
expect lit_SAVE 0
# ---- 1. save
cap 1. Save | Press EDIT and turn KNOB 1 (Sound). SAVE lights: the change isn't saved yet.
wait 2500
tap EDIT
expect page 8
expect track 5
wait 900
turn K1 1
expect trk5_program 2
expect dirty 1
wait 900
still 1-unsaved
wait 700
expect lit_SAVE 1
cap 1. Save | Press SAVE. Stopped, Polly also saves by itself after about four seconds.
wait 1500
tap SAVE
wait 100
expect dirty 0
expect lit_SAVE 0
expect msgtext_is_SAVED 1
wait 2200
# ---- 2. undo, redo
cap 2. Undo | Hold HOME and press REC to undo the last change.
wait 2500
press HOME
wait 300
still 2-undo
press REC
wait 800
release REC
expect msgtext_is_UNDO_C1_SOUND 1
expect trk5_program 1
expect recording 0
wait 400
release HOME
wait 1800
cap 2. Undo | Hold HOME and press PLAY to redo it.
wait 2500
press HOME
wait 300
press PLAY
wait 800
release PLAY
expect msgtext_is_REDO_C1_SOUND 1
expect trk5_program 2
expect playing 0
wait 400
release HOME
wait 2200
# ---- 3. four songs
cap 3. Switch songs | On Home, hold HOME and turn PRESETS: songs S1 to S4.
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
cap 3. Switch songs | Each song has its own sections, progressions, melodies, tempo and style.
wait 1500
turn PRESET -1
expect song 1
wait 1500
release HOME
expect page 0
wait 2000
# ---- 4. memory
cap 4. Memory | Press SEQ, then SEL. Turn SELECT to MEMORY, the last row, and press SEL.
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
expect slottext1_is_S1|Groove_A_(yours)|Pop 1
expect slottext2_is_free 1
expect slottext16_is_free 1
still 4-memory
wait 1000
poster
wait 1500
cap 4. Memory | 16 places for your grooves, melodies and progressions. To free one, select it and press SEL.
wait 2500
expect list_sel 0
tap SEL
expect asking 1
wait 1800
cap 4. Memory | Polly asks first. Press SEL again: the groove goes back to the style.
wait 2000
tap SEL
expect asking 0
expect slottext1_is_free 1
expect trk5_program 1
wait 2500
tap HOME
wait 400
tap HOME
expect list_open 0
wait 1500
cap Next: 110 | With other gear.
wait 4000
