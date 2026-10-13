# Polly 107, fixing notes (docs/course/107-fixing-notes.md), where 106 ended (hidden: 101 to 106 replayed), with a
# pause on each step's caption before its first move and after its last
# (tools/polly_video.py tools/video/107.story OUT.mp4).
# Funk (106's), 100 BPM: a bar 2400 ms, the band free on Groove A. Funk's Groove A on BA (the bass), 18 notes: C2 at
# bar 1.1 (tick 0), C2 at 1.1.3 (240), C3 at 1.1.4 (360), B1 at 1.2.3 (720), ... 480 ticks a beat, the grid 16ths
# (120). C2 at 1.1 goes to E2 and moves one step, to 1.1.2 (an empty place), then is deleted.
# Keys (octave 0): key K is MIDI 53 + K: G3 2.
head 107 Fixing notes
# ---- where 106 ended: Funk, stopped on the Band page on C2, free, Groove A Funk's again
hide
include 106.story
expect playing 0
expect page 6
expect tempo 100
expect track 6
expect song1_style1 39
show
barms 2400
cap Fixing notes | Change, move, delete and enter single notes, without recording again.
wait 3500
# ---- 1. open Notes
cap 1. Open Notes | Press PLAY. On the Band page, turn ALGORITHM to BA for the bass, then press SEQ.
wait 2500
tap PLAY
expect playing 1
expect sect 2
wait 1500
expect page 6
expect track 6
turn ALGO -2 300
expect track 4
wait 1000
tap SEQ
expect page 17
expect tag_is_S1>Funk>Groove_A>BA_Finger_Bass 1
still 1-notes
wait 2500
# ---- 2. pick a note
cap 2. Pick a note | Turn SELECT. It steps from note to note, and the line at the top names it.
wait 2500
turn SELECT 1 600
turn SELECT 1 600
expect ne_at 360
turn SELECT -2 600
expect ne_at 0
expect ne_note 36
expect notes_where_is_BA|bar_1.1|C2 1
still 2-pick
wait 2500
# ---- 3. change it
cap 3. Change its pitch | Turn KNOB 2 (Pitch) one step. On this note the bass always plays the chord's root, so Polly asks.
wait 2500
turn K2 1
expect slot1_trk4_note0 37
expect asking 1
still 3-ask
wait 2500
cap 3. Change its pitch | Press SEL for yes, and the bass plays your notes as written. Keep turning to E2.
wait 2000
tap SEL
expect asking 0
expect slot1_trk4_xtype 2
wait 1000
turn K2 3 300
expect ne_note 40
expect notes_where_is_BA|bar_1.1|E2 1
wait 2000
cap 4. Change its length and velocity | Turn KNOB 3 (Length) to make it longer, and KNOB 4 (Velocity) to make it softer.
wait 2500
turn K3 2 250
wait 800
turn K4 -6 80
still 3-change
poster
wait 2500
# ---- 4. move it
cap 5. Move it | Hold SEL and turn SELECT. The note moves one grid step for each click of the knob.
wait 2500
press SEL
wait 300
turn SELECT 1 500
still 4-move
wait 1000
release SEL
expect ne_at 120
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
turn K1 -1 400
expect ne_t 0
expect ne_note -1
wait 800
key 2 down
wait 100
still 5-enter
wait 900
key 2 up
expect ne_t 120
expect slot1_trk4_count 18
wait 2500
# ---- 6. undo
cap 7. Undo | Hold HOME and press REC. Your last edit is undone, and the header tells you what changed.
wait 2500
press HOME
wait 300
tap REC
expect msg_is_UNDO 1
still 6-undo
wait 900
release HOME
expect ne_note -1
expect slot1_trk4_count 17
wait 2500
cap 7. Undo | Press PLAY to stop.
wait 2000
tap PLAY
wait 300
expect playing 0
wait 1500
cap Next: 108 | Making it sound yours.
wait 4000
