# Polly 103, your song (docs/course/103-your-song.md), from a fresh boot, with a pause on each step's caption before
# its first move and after its last (tools/polly_video.py tools/video/103.story OUT.mp4).
# Two ready-made progressions, P1 Pop and P2 Ballad (4 bars each). Pop, 105 BPM: a bar 2285.714 ms.
# The song: 1 P1 (drums muted), 2 P2 on Groove B x2, 3 P1; Intro and Ending on. Anchor S: PLAY.
head 103 Your song
barms 2285.714
cap Your song | Build a song from two progressions, remove a section, shape the others, and play it.
wait 3500
# ---- 1. sections
cap 1. Add sections | Start with a new song: on the Home page, press HOME for the Song page. Press SEL, turn SELECT to NEW SONG and press SEL twice.
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
cap 1. Add sections | Press white keys 1, 2, 1 and 2.
wait 2500
expect page 1
tapkey w0
wait 700
tapkey w1
wait 700
still 1-sections
tapkey w0
wait 700
tapkey w1
wait 1000
expect rows 4
expect row_loop1 1
expect row_loop2 2
expect row_loop3 1
expect row_loop4 2
cap 1. Add sections | Each white key adds a section that plays its progression: P1 Pop, P2 Ballad, P1 Pop, P2 Ballad.
wait 3000
# ---- 2. remove the last: PROG below P1 ("-"); it goes when the Song page is left (HOME, HOME again)
cap 2. Remove a section | Turn KNOB 1 (Section) to section 4. Turn KNOB 2 (Prog) down, past P1, to "-".
wait 2500
turn K1 -1
expect cursor 4
wait 800
turn K2 -2 400
expect knob2_is_- 1
still 2-remove
wait 2000
cap 2. Remove a section | The row reads "section removed". It goes when you leave the Song page.
wait 3000
cap 2. Remove a section | Press HOME twice, for the Home page and back to the Song page. Section 4 is gone.
wait 2500
tap HOME
wait 1200
expect page 0
expect rows 3
tap HOME
wait 200
expect page 1
expect rows 3
expect cursor 4
wait 2500
# ---- 3. shape section 2
cap 3. Shape a section | Turn KNOB 1 (Section) to section 2.
wait 2500
turn K1 -2
expect cursor 2
wait 1200
cap 3. Shape a section | Turn KNOB 3 (Groove) to B, the busier groove, and KNOB 4 (Repeat) to x2: it plays twice.
wait 2500
turn K3 3 400
expect row_sect2 2
wait 800
turn K4 1
still 2-shape
expect row_rep2 2
wait 2500
# ---- 4. a mute in section 1
cap 4. Mute the drums | Turn KNOB 1 to section 1. Press black keys 1, 2 and 3: the drums and percussion rest there.
wait 2500
turn K1 -1
expect cursor 1
wait 1000
tapkey b0
wait 500
tapkey b1
wait 500
still 3-mute
note 900 b2
wait 600
expect row_mute1 7
wait 2000
# ---- 5. page 2: the Intro, the Ending, At end
cap 5. Check the Intro and the Ending | Turn SELECT to page 2. KNOB 2 (Intro) and KNOB 3 (Ending) are on.
wait 2500
turn SELECT 1
still 4-page2
wait 800
expect page 2
expect knob2_is_on 1
expect knob3_is_on 1
expect knob4_is_stop 1
expect song_intro 1
expect song_ending 1
wait 2000
cap 5. Check the Intro and the Ending | KNOB 4 (At end) is on stop: the song stops after the Ending.
wait 3000
# ---- 6. play: Intro (2 bars), 1 (4), 2 (x2: 8), 3 (4), the Ending
cap 6. Play the song | Press PLAY. The Intro's clock fills at the top.
wait 2500
anchor S
tap PLAY
wait 300
expect playing 1
expect sect 1
at S 1 0
at S 2 400
expect row 1
expect mutes 7
cap 6. Play the song | Then each section's clock fills as it plays. Section 1: no drums.
at S 6 400
expect row 2
expect sect 3
expect mutes 0
cap 6. Play the song | Section 2: Ballad on Groove B, twice.
at S 9 0
poster
still 5-play
at S 14 400
expect row 3
cap 6. Play the song | Section 3: Pop again. Then the Ending, and the song stops.
at S 18 400
expect sect 6
at S 20 300
expect playing 0
wait 1000
cap Next: 104 | Changing styles.
wait 4000
