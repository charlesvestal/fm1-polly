# Polly 104, changing styles (docs/course/104-changing-styles.md), from a fresh boot, with a pause on each step's
# caption before its first move and after its last (tools/polly_video.py tools/video/104.story OUT.mp4).
# A song of P1 Pop and P2 Ballad, At end "again", so it plays round. Pop 105 BPM, Funk 100, Rock 120.
head 104 Changing styles
cap Changing styles | Play your song in another style, and choose whose tempo it plays at.
wait 3500
# ---- 1. a song that plays round
cap 1. Set up a song | On the Home page, press HOME for the Song page. Press white keys 1 and 2.
wait 2500
tap HOME
wait 1000
expect page 1
tapkey w0
wait 700
tapkey w1
wait 1000
expect rows 2
expect row_loop1 1
expect row_loop2 2
cap 1. Set up a song | Turn SELECT to page 2, and KNOB 4 (At end) to again. Press PLAY: the song plays round.
wait 2500
turn SELECT 1
wait 800
turn K4 1
still 1-song
expect song_at_end 1
wait 1500
tap PLAY
wait 300
expect playing 1
expect mode 2
expect tempo 105
wait 6000
expect row 1
# ---- 2. Funk: the same sections and chords, Funk's band and tempo
cap 2. Change the style | Press HOME for the Home page. Turn KNOB 1 (Style) to Funk.
wait 2500
tap HOME
wait 1000
expect page 0
expect knob4_is_Song 1
turn K1 1
still 2-style
expect knob1_is_Funk 1
expect tempo 100
wait 1000
expect mode 2
expect rows 2
expect row_loop1 1
expect row_loop2 2
poster
cap 2. Change the style | The song keeps its sections and chords. The band and the tempo are Funk's.
wait 9000
expect playing 1
expect mode 2
# ---- 3. a tempo of your own: kept when the style changes
cap 3. Set your own tempo | Turn KNOB 2 (Tempo) to 90.
wait 2500
turn K2 -10 120
expect tempo 90
wait 2000
cap 3. Set your own tempo | Turn KNOB 1 to Rock. Rock's band plays, and your tempo stays.
wait 2500
turn K1 1
still 3-tempo
expect knob1_is_Rock 1
expect tempo 90
expect msgtext_is_TEMPO_KEPT_90_(ROCK_120) 1
wait 6000
# ---- 4. the style's tempo again
cap 4. Use the style's tempo | Press SEL for the page's list. It opens on STYLE TEMPO 120. Press SEL again.
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
expect tempo 120
expect msgtext_is_TEMPO_120_THE_STYLE'S 1
wait 3000
cap 4. Use the style's tempo | From now on, each style brings its own tempo. Turn KNOB 1 back to Funk: 100.
wait 2500
turn K1 -1
expect knob1_is_Funk 1
expect tempo 100
wait 4000
expect playing 1
expect mode 2
cap Next: 105 | Recording a melody.
wait 4000
