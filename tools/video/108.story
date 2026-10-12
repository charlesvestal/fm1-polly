# Polly 108, making it sound yours (docs/course/108-making-it-sound-yours.md), from a fresh boot, with a pause on each
# step's caption before its first move and after its last (tools/polly_video.py tools/video/108.story OUT.mp4).
# Pop, 105 BPM, the band playing Groove A over C. C1 (track 5, the Sound page's first): Acoustic Grand, to Rock Organ
# (+18). Its Mixer: volume 86, pan L12, reverb 45, chorus 0.
head 108 Making it sound yours
barms 2285.714
cap Making it sound yours | Change an instrument's sound, mix it, set the effects and move the key.
wait 3500
# ---- 1. the Sound page, on C1 (the band's piano: it plays in Pop's Groove A; C2 has no notes there)
cap 1. Open the Sound page | Press PLAY. Press EDIT for the Sound page. It opens on C1, the band's piano.
wait 2500
tap PLAY
expect playing 1
wait 1500
tap EDIT
expect page 8
expect track 5
expect knob1_is_Acoustic_Grand 1
still 1-instrument
wait 2500
# ---- 2. a sound
cap 2. Pick a sound | Turn KNOB 1 (Sound) up to Rock Organ. The piano part now plays on an organ, in Groove A (yours).
wait 2500
turn K1 18 120
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
turn K1 14 90
expect trk5_vol 100
expect knob1_is_100 1
wait 600
turn K2 22 90
expect knob2_is_R10 1
still 3-mix
wait 2500
# ---- 4. the sends
cap 4. Add space | KNOB 3 sets how much it sends to the reverb, KNOB 4 to the chorus.
wait 2500
turn K3 25 70
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
cap Next: 109 | Keeping your work.
wait 4000
