# Polly 108, making it sound yours (docs/course/108-making-it-sound-yours.md), from a fresh boot, with a pause on each
# step's caption before its first move and after its last (tools/polly_video.py tools/video/108.story OUT.mp4).
# Pop, 105 BPM, the band playing Groove A over C. C2 (track 6, black key 6 = b5): Clean Guitar, to Rock Organ.
head 108 Making it sound yours
barms 2285.714
cap Making it sound yours | Change an instrument's sound, mix it, set the effects and move the key.
wait 3500
# ---- 1. an instrument
cap 1. Pick an instrument | Press PLAY. Press EDIT for the Sound page, then turn ALGORITHM to C2.
wait 2500
tap PLAY
expect playing 1
wait 1500
tap EDIT
expect page 8
wait 1000
turn ALGO 1
expect track 6
expect knob1_is_Clean_Guitar 1
still 1-instrument
wait 2500
# ---- 2. a sound
cap 2. Pick a sound | Turn KNOB 1 (Sound) down to Rock Organ.
wait 2500
turn K1 -8 250
turn K1 -1 50
still 2-sound
wait 300
expect trk6_program 19
expect knob1_is_Rock_Organ 1
poster
wait 2500
# ---- 3. level and pan
cap 3. Mix it | Press LFO for the Mixer. Press black key 6 to select C2.
wait 2500
tap LFO
expect page 10
wait 1000
note 600 b5
expect track 6
wait 1500
cap 3. Mix it | KNOB 1 sets its volume, KNOB 2 its pan.
wait 2000
turn K1 10 90
expect trk6_vol 80
expect knob1_is_80 1
wait 600
turn K2 -12 90
expect knob2_is_R10 1
still 3-mix
wait 2500
# ---- 4. the sends
cap 4. Add space | KNOB 3 sets how much it sends to the reverb, KNOB 4 to the chorus.
wait 2500
turn K3 20 70
wait 600
turn K4 20 70
expect knob3_is_70 1
expect knob4_is_60 1
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
cap 6. Move the key | Press HOME, then turn KNOB 3 (Transpose) to D.
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
cap 6. Move the key | Play C on the keys: the band plays D.
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
cap Next: 109 | Keeping your work.
wait 4000
