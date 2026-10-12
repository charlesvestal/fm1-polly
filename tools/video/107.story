# Polly 107, fixing notes (docs/course/107-fixing-notes.md), from a fresh boot, with a pause on each step's caption
# before its first move and after its last (tools/polly_video.py tools/video/107.story OUT.mp4).
# Pop, 105 BPM, the band playing Groove A over C. Notes on BA (the bass): C2 at bar 1.1 (tick 0), C2 at 1.1.4 (360),
# ... 480 ticks a beat, the grid 16ths (120). Keys (octave 0): key K is MIDI 53 + K: G3 2.
head 107 Fixing notes
barms 2285.714
cap Fixing notes | Change, move, delete and enter single notes, without recording again.
wait 3500
# ---- 1. open Notes
cap 1. Open Notes | Start with a new song: press HOME for the Song page, press SEL, turn SELECT to NEW SONG and press SEL twice. Press HOME to go back.
wait 2500
expect page 0
tap HOME
wait 900
expect page 1
tap SEL
wait 700
turn SELECT 4 200
wait 700
expect list_sel 12
tap SEL
wait 900
expect asking 1
tap SEL
wait 900
expect asking 0
expect rows 0
expect msgtext_is_NEW_SONG_S1 1
tap HOME
wait 1000
expect page 0
cap 1. Open Notes | Press PLAY. Press SEQ for the Band page, turn ALGORITHM to BA, then press SEQ again.
wait 2500
tap PLAY
expect playing 1
wait 1500
tap SEQ
expect page 6
wait 1000
turn ALGO -1
expect track 4
wait 1000
tap SEQ
expect page 17
expect tag_is_S1>Pop>Groove_A>BA_Finger_Bass 1
still 1-notes
wait 2500
# ---- 2. pick a note
cap 2. Pick a note | Turn SELECT. It steps from note to note, and the line at the top names it.
wait 2500
turn SELECT 1 600
turn SELECT 1 600
expect ne_at 1200
turn SELECT -2 600
expect ne_at 0
expect ne_note 36
expect notes_where_is_BA|bar_1.1|C2 1
still 2-pick
wait 2500
# ---- 3. change it
cap 3. Change its pitch | Turn KNOB 2 (Pitch) one step. Here the bass plays the chord's root, so Polly asks.
wait 2500
turn K2 1
expect slot1_trk4_note0 37
expect asking 1
still 3-ask
wait 2500
cap 3. Change its pitch | Press SEL (yes): the bass plays your notes as written. Keep turning to E2.
wait 2000
tap SEL
expect asking 0
expect slot1_trk4_xtype 2
wait 1000
turn K2 3 300
expect ne_note 40
expect notes_where_is_BA|bar_1.1|E2 1
wait 2000
cap 4. Change its length and velocity | KNOB 3 (Length) makes it longer, KNOB 4 (Velocity) softer.
wait 2500
turn K3 2 250
wait 800
turn K4 -6 80
still 3-change
poster
wait 2500
# ---- 4. move it
cap 5. Move it | Hold SEL and turn SELECT. The note moves one grid step per detent.
wait 2500
press SEL
wait 300
turn SELECT 2 500
still 4-move
wait 1000
release SEL
expect ne_at 240
expect ne_note 40
wait 2500
# ---- 5. delete and enter
cap 6. Delete a note, enter one | Press EDIT to delete the selected note.
wait 2500
tap EDIT
expect page 17
expect ne_note -1
wait 2500
cap 6. Delete a note, enter one | Turn KNOB 1 (Place) to an empty place, then play a key. The note goes in there.
wait 2500
turn K1 -2 400
expect ne_t 0
expect ne_note -1
wait 800
key 2 down
wait 100
still 5-enter
wait 900
key 2 up
expect ne_t 120
expect slot1_trk4_count 10
wait 2500
# ---- 6. undo
cap 7. Undo | Hold HOME and press REC. The last edit is undone, and the header says what.
wait 2500
press HOME
wait 300
tap REC
expect msg_is_UNDO 1
still 6-undo
wait 900
release HOME
expect ne_note -1
expect slot1_trk4_count 9
wait 2500
cap Next: 108 | Making it sound yours.
wait 4000
