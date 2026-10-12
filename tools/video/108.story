# Polly 108, making it sound yours (docs/course/108-making-it-sound-yours.md), where 107 ended (hidden: 101 to 107
# replayed), with a pause on each step's caption before its first move and after its last
# (tools/polly_video.py tools/video/108.story OUT.mp4).
# Funk (106's), 100 BPM, the band free on Groove A (yours: 107's notes). C1 (track 5): Clavinet, to Rock Organ (+11).
# Its Mixer: volume 78, pan L24, reverb 20, chorus 0. Effects: Hall, level 90.
head 108 Making it sound yours
# ---- where 107 ended: Funk, stopped in Notes on BA, Groove A yours
hide
include 107.story
expect playing 0
expect page 17
expect track 4
expect tempo 100
expect rev_type 2
show
barms 2400
cap Making it sound yours | Change an instrument's sound, mix it, set the effects and move the key.
wait 3500
# ---- 1. the Sound page, on C1 (BA selected in 107; Funk's C1 plays a clavinet in Groove A)
cap 1. Open the Sound page | Press HOME for the Home page, then PLAY. Press EDIT for the Sound page. It opens on BA. You picked it in 107.
wait 2500
expect page 17
tap HOME
wait 900
expect page 0
tap PLAY
expect playing 1
wait 1500
tap EDIT
expect page 8
expect track 4
wait 1500
cap 1. Open the Sound page | Turn ALGORITHM one step to C1. In Funk, C1 plays a clavinet.
wait 2500
turn ALGO 1
expect track 5
expect knob1_is_Clavinet 1
still 1-instrument
wait 2500
# ---- 2. a sound
cap 2. Pick a sound | Turn KNOB 1 (Sound) up to Rock Organ. The clavinet part now plays on an organ.
wait 2500
turn K1 11 120
still 2-sound
wait 300
expect trk5_program 19
expect knob1_is_Rock_Organ 1
poster
wait 3500
# ---- 3. level and pan
cap 3. Mix it | Press LFO for the Mixer, on C1. KNOB 1 sets its volume, KNOB 2 its pan.
wait 2500
tap LFO
expect page 10
expect track 5
wait 1500
turn K1 22 90
expect trk5_vol 100
expect knob1_is_100 1
wait 600
turn K2 34 90
expect knob2_is_R10 1
still 3-mix
wait 2500
# ---- 4. the sends
cap 4. Add space | KNOB 3 sets how much it sends to the reverb, KNOB 4 to the chorus.
wait 2500
turn K3 50 70
wait 600
turn K4 40 70
expect knob3_is_70 1
expect knob4_is_40 1
still 4-sends
wait 2500
# ---- 5. the effects
cap 5. Set the effects | Press FX. KNOB 1 picks the reverb, KNOB 2 sets its level.
wait 2500
tap FX
expect page 12
wait 1200
turn K1 -1 300
expect rev_type 1
expect knob1_is_L.hall 1
wait 800
turn K2 -10 90
expect knob2_is_80 1
still 5-effects
wait 2500
# ---- 6. transpose
cap 6. Move the key | Press HOME for the Home page, then turn KNOB 3 (Transpose) to D.
wait 2500
tap HOME
expect page 0
wait 1200
turn K3 1 600
turn K3 1 50
wait 300
expect transpose 2
expect knob3_is_D 1
wait 2000
cap 6. Move the key | Play a C chord: the band plays D.
wait 2000
key 7 down
key 11 down
key 14 down
wait 300
expect chord_root 2
expect home_chord_root 2
still 6-transpose
wait 1500
key 7 up
key 11 up
key 14 up
wait 2500
cap 6. Move the key | Press PLAY to stop.
wait 2000
tap PLAY
wait 300
expect playing 0
wait 1500
cap Next: 109 | Keeping your work.
wait 4000
