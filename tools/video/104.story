# Polly 104, changing styles (docs/course/104-changing-styles.md), where 103 ended (hidden: 101 to 103 replayed), with
# a pause on each step's caption before its first move and after its last
# (tools/polly_video.py tools/video/104.story OUT.mp4).
# 103's song (1 P1, 2 P2 on Groove B x2, 3 P1), At end "again", so it plays round. Funk 100 BPM, Rock 120, Pop 105.
head 104 Changing styles
# ---- where 103 ended: Funk, stopped on the Song page's page 2, three sections
hide
include 103.story
expect playing 0
expect page 2
expect tempo 100
expect rows 3
expect row_loop1 1
expect row_loop2 2
expect row_loop3 1
expect knob4_is_stop 1
show
cap Changing styles | Play your song in another style, and choose whose tempo it plays at.
wait 3500
# ---- 1. the song, round and round
cap 1. Loop the song | Turn KNOB 4 (At end) to again. Press PLAY, and the song plays over and over.
wait 2500
turn K4 1
still 1-song
expect song_at_end 1
wait 1500
tap PLAY
wait 300
expect playing 1
expect mode 2
expect tempo 100
wait 6000
# ---- 2. Rock: the same sections and chords, Rock's band and tempo
cap 2. Change the style | Press HOME for the Home page. Turn KNOB 1 (Style) to Rock.
wait 2500
tap HOME
wait 1000
expect page 0
expect knob4_is_Song 1
turn K1 1
still 2-style
expect knob1_is_Rock 1
expect tempo 120
wait 1000
expect mode 2
expect rows 3
expect row_loop1 1
expect row_loop2 2
poster
cap 2. Change the style | The song keeps its sections and chords, but now it has Rock's band and tempo.
wait 9000
expect playing 1
expect mode 2
# ---- 3. a tempo of your own: kept when the style changes
cap 3. Set your own tempo | Turn KNOB 2 (Tempo) to 90.
wait 2500
turn K2 -30 70
expect tempo 90
wait 2000
cap 3. Set your own tempo | Turn KNOB 1 back to Funk. Funk's band plays, and your tempo stays.
wait 2500
turn K1 -1
still 3-tempo
expect knob1_is_Funk 1
expect tempo 90
expect msgtext_is_TEMPO_KEPT_90_(FUNK_100) 1
wait 6000
# ---- 4. the style's tempo again
cap 4. Use the style's tempo | Press SEL to see the page's list. It opens on STYLE TEMPO 100. Press SEL again.
wait 2500
tap SEL
wait 300
still 4-styletempo
expect list_open 1
expect list_has_STYLE_TEMPO 1
wait 2000
tap SEL
wait 300
expect list_open 0
expect tempo 100
expect msgtext_is_TEMPO_100_THE_STYLE'S 1
wait 3000
cap 4. Use the style's tempo | From now on, each style brings its own tempo. Turn KNOB 1 to Pop, and the tempo goes to 105.
wait 2500
turn K1 -1
expect knob1_is_Pop 1
expect tempo 105
wait 4000
expect playing 1
expect mode 2
cap 4. Use the style's tempo | Press PLAY to stop.
wait 2000
tap PLAY
wait 300
expect playing 0
wait 1500
cap Next: 105 | Recording a melody.
wait 4000
