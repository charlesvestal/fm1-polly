# Polly 110, with other gear (docs/course/110-with-other-gear.md), from a fresh boot, with a pause on each step's
# caption before its first move and after its last (tools/polly_video.py tools/video/110.story OUT.mp4).
# Only the device's part: Setup's MIDI out and Clock (both on from a fresh boot: the knob turned right keeps them on
# and the box says what they do). The keyboard, the DAW and the browser's mirror are on the page, not in the video
# (MIDI in and clock in were checked with polly_host's midi / clock commands, which a storyboard can't send).
head 110 With other gear
cap With other gear | Send Polly's band to a computer, follow its clock, play from a keyboard and mirror the screen.
wait 3500
# ---- 1. MIDI out
cap 1. Send MIDI out | Press GLO for Setup. KNOB 2 (MIDI out) is on. Turn it, and the screen says what it does.
wait 2500
tap GLO
expect page 15
expect knob2_is_on 1
wait 1200
turn K2 1
expect knob2_is_on 1
still 1-midi-out
poster
wait 2000
cap 1. Send MIDI out | Every instrument goes out over USB on its own channel, with the clock.
wait 4000
# ---- 2. Clock
cap 2. Follow MIDI clock | KNOB 1 (Clock) is on MIDI. Turn it, and the screen says what it does.
wait 2500
turn K1 1
expect knob1_is_MIDI 1
expect clock 1
still 2-clock
wait 2000
cap 2. Follow MIDI clock | When clock comes in, Polly follows its tempo, start and stop.
wait 4000
