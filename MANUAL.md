# Polly manual

Polly turns the M-VAVE FM-1 into a pocket arranger, inspired by the Yamaha QY70. A style plays a band
of eight tracks, and the chords you give it steer the band. Hold a chord and the bass, the guitars and
the strings follow. Write a few bars of chords and a whole song plays.

Its sounds are the 128 General MIDI instruments and a drum kit, from Sonivox EAS. They play through the
speaker and the headphone jack, and MIDI coming in plays them too.

- [Writing a song](#writing-a-song)
- [Getting around](#getting-around)
- [Styles and sections](#styles-and-sections)
- [Chords](#chords)
- [Loops and songs](#loops-and-songs)
- [Recording a part](#recording-a-part)
- [Lead tracks](#lead-tracks)
- [Sound](#sound)
- [Songs and saving](#songs-and-saving)
- [Setup and MIDI](#setup-and-midi)
- [The QY70 bank](#the-qy70-bank)
- [Updating and safety](#updating-and-safety)
- [Credits](#credits)
- [Page by page](#page-by-page)

## Writing a song

The white keys are counted from the left, 1 to 16: loop L9 is white key 9.

1. **Find a groove.** Turn **KNOB 1** for a style (Pop, Funk, Bossa…). Press **PLAY**. Play a chord:
   three keys or more. The band follows whatever you play.
2. **Write your verse.** Press **REC**, play Am, F, C, G, one per bar, then press **REC** again. Your
   chords are now loop L9 and the band plays them round. Name it: press **ARP**, **SELECT** to page 2,
   and turn **NAME** to "Verse". (Or, stopped: press **ARP**, which opens the first empty loop, and
   write on its grid: **REC**, then a chord a bar.)
3. **Write your chorus.** Press **HOME** and turn **KNOB 4** anticlockwise, past L1, to "–": the band is
   free again. Press
   **REC**, play C, G, Am, F, then press **REC**. That's L10. Name it: **ARP**, **SELECT**, **NAME**
   to "Chorus".
4. **Build the song.** Press **HOME**, then **SELECT** to the SONG page. Tap white keys in order: 9,
   10, 9, 10. Each tap adds a row: verse, chorus, verse, chorus.
5. **Shape it.** On a chorus row (**ROW**, KNOB 1, moves between rows), turn **SECTION** to Main B for
   the busier groove (the other chorus rows follow). Turn **REPEAT** for ×2. On the last row, **SELECT**
   to the next page and turn **KEY** to +2 for the last chorus a tone up. For a breakdown, tap black
   keys to mute tracks on a row (black keys 1, 2 and 3 are the drums and the percussion: D1, D2, PC).
6. **Play it.** Press **PLAY** on the SONG page. The band is still playing your chorus, so the song
   takes over at the next bar, from row 1: your rows with fills between sections, then an Ending.
   (From a stop it opens with an Intro. To start on another row: move the cursor there with **ROW**,
   press **SEL**: the list opens on **PLAY FROM ROW**; **SEL** again starts it.)
7. **Add a melody.** On the SONG page press **PLAY**, then press **EDIT**, turn **ALGORITHM** past C4 to
   LD1 and press **REC**. The click counts you in to the next bar line; play along: your notes go into the
   rows as they play. (See [Lead tracks](#lead-tracks).)
8. **Save.** Press **SAVE**, or just stop: Polly saves by itself.

## Getting around

### The panel

| Control | What it does |
| --- | --- |
| PLAY | Start and stop. On a SONG page: the song from row 1 (while a loop plays, from the next bar); playing, it stops it |
| REC | Write what the keys play here: chords into the loop (HOME, LOOP; in FREE on HOME a new loop), the selected track (SEQ, EDIT), rows (SONG) |
| SEL | This page as a list, opened on its first action (LOAD PROGRESSION, PLAY FROM ROW, ERASE TRACK…); SELECT up to its values and MORE |
| HOME | HOME; SELECT for the SONG pages. Held: the shortcuts, on screen: + white key 1–16 a loop, + black key 1–5 a section, + SELECT the tempo, + PRESETS the song, + REC undo, + PLAY redo |
| ARP | LOOP: the loop's chords; page 2 its length, beats, name |
| SEQ | Pattern, page 2 Pattern setup |
| EDIT | Track, page 2 Track keys |
| LFO | Mixer, page 2 Mixer tone |
| FX | Master FX |
| ENV | Style: the six sections' patterns (two pages) |
| GLO | Setup, page 2 Panel |
| SAVE | Save. Held + a white key: copy the loop (HOME, LOOP) or the pattern (SEQ) there |
| SELECT | The button's next or previous page; in a list, up and down |
| ALGORITHM | The track, on any page: D1, D2, PC, BA, C1, C2, C3, C4 |
| PRESETS | Depends on the page (HOME, LOOP: the next loop). With HOME held: the song |
| KNOB 1–4 | The four values at the bottom of the screen |
| OCT− / OCT+ | Move the keys down or up an octave, −3 to +3. On LOOP: the cursor |
| Keys | Chords on HOME and LOOP, the selected track on SEQ and EDIT, the rows on SONG |
| Any button held a second | A card says what it does on this page |

### Pages and dots

Each page button lights while you're on one of its pages, and always opens its first page. Some buttons
have two pages (HOME four: HOME and SONG's three): turn **SELECT** for the next one. The dots over the
knobs show which one you're on.

The screen has three bands:

- **The header**: the page's name, the tempo on the right ("ext" when MIDI clock drives it), a red dot
  while recording, and "unsaved" when there's something to save. On HOME it also shows the song and its
  style.
- **The middle**: what the page is about.
- **The knobs**: what KNOB 1–4 do here, with their values. The knob you turned last shows in coral.
- **The footer**: what the keys and REC do here ("KEYS: CHORDS · REC: WRITE"); while something records,
  "REC: STOP".

**Messages** such as "SAVED" or "NEXT Main B" take over the header for a second or so.

### The tracks

A pattern has eight tracks, named as on the QY70:

| Track | Usually |
| --- | --- |
| D1, D2 | Drums |
| PC | Percussion |
| BA | Bass |
| C1–C4 | Chords, riffs and pads |

ALGORITHM picks the track on any page. The screen says its name and sound. The keys play that track's
sound (C1 at first, a piano), and REC records into it. Past C4 come the song's two lead tracks, LD1 and
LD2 (see [Lead tracks](#lead-tracks)).

### The keys

The 27 keys run from F3 to G5 at octave 0. OCT− and OCT+ move them an octave at a time. They play at a
fixed velocity, because the FM-1's keys don't sense how hard you press.

### The lights

- **PLAY** is green while playing, and red while recording.
- **REC** lights while recording, and **SAVE** while something isn't saved.
- The button of the page you're on is lit.
- Keys light while you hold them.
- While you hold HOME, the black key of the section playing (or coming next) lights, and the white key
  of the loop playing.
- With Lights "on" (Setup), the other buttons glow dimly, so you can read them in the dark.

## Styles and sections

A **style** is six patterns, one for each **section**:

| Section | What it does |
| --- | --- |
| Intro | Plays once at the start, then Main A |
| Main A | The main groove. Loops |
| Main B | A busier groove. Loops |
| Fill AB | One bar, from Main A into Main B |
| Fill BA | One bar, from Main B back to Main A |
| Ending | Plays once, then stops |

Polly has 16 styles of its own: Pop, Funk, Rock, Ballad, Bossa, Swing, Shuffle, Reggae, Hip hop, House,
Disco, Country, Gospel, Waltz, Latin and Ambient. Their drums are real drummers' performances from the
Groove MIDI Dataset. With a [QY70 bank](#the-qy70-bank) installed, the QY70's 128 preset styles follow
them on the Style knob, with "QY" before their names.

### Picking a style

On HOME, turn **KNOB 1**. The header shows the style. Until you set the song's
tempo yourself, the tempo changes to the style's own. Once you've turned the tempo, it stays yours.
You can change the style while playing.

Each song has its own style, so each of the four songs can sound different.

**Sections you've recorded into keep your part.** Once you record into a section (or change its
sound), it plays your own [user pattern](#your-own-patterns-copy-on-write) instead of the style's.
The header then says so: "Pop, A user" (Main A is yours), "Pop, 3 user", "Pop, all user". Turning
the Style knob changes only the sections still playing the style; yours stay as they are. So you can
keep your Main A and try the band around it in Funk, then Rock.

To put a section back to the style, play it (or pick it with HOME + its black key), press **SEL** on the
Pattern page and run **BACK TO STYLE** ([the pattern's actions](#the-patterns-actions-sel)).
Your pattern is kept in its P number, so you can pick it again later.

### Changing sections

Hold **HOME** and press a black key:

| Black key | Section |
| --- | --- |
| 1 | Intro |
| 2 | Main A |
| 3 | Main B |
| 4 | Fill |
| 5 | Ending |

While playing, the change waits for the next bar. HOME shows NEXT and the section until then.
While stopped, you choose the section PLAY starts with ("FIRST Main B").

**The fill rules:**

- Fill picks the right fill for you: Fill AB from Main A (it then goes on to Main B), Fill BA from
  Main B (it then goes back to Main A).
- A fill asked for during a fill adds another bar of fill.
- There's no fill in the Intro or the Ending: they play out ("NO FILL").
- If a style has no Fill BA, Fill AB plays instead, and the other way round.

**Starting and ending:**

- PLAY starts on Main A. The Intro plays first in a song with INTRO on started from a stop, or when
  you pick it (HOME + black key 1) before PLAY.
- The Ending plays once and stops. Without an Ending, Polly stops at the bar line. Stopped so, the pages
  show what PLAY starts with again: in a song row 1's section, else Main A (not the Ending).

### Intros and endings hold a chord

The Intro and the Ending play over one held chord, the QY70's way: they carry their own lead-in and
cadence. The chord on the section's first beat stays until it ends. A chord you play on the keys still
changes it at once.

### Tempo

Turn **KNOB 2** on HOME, or hold **HOME** and turn **SELECT** (held, the footer says "SELECT: TEMPO"): 40 to
240 BPM. The tempo is shown at the top right. When Polly follows MIDI clock, it shows "ext" instead.

## Chords

Polly's patterns are written as if the chord were C major 7. As it plays, it moves every note of every
track to fit the chord of the moment. There are three ways to give it chords: play them, play a loop,
or play a song.

### Playing chords

On HOME and LOOP the keys play chords.

- Hold three or more different notes. Polly names the chord, shows it big on HOME, and the band
  follows.
- The chord stays after you let go (fingered, as on an arranger keyboard). Play the next one whenever
  you like.
- Inversions work: E G C and G C E are both C. When the notes could be read two ways, the lowest note
  is taken as the root: A C E G is Am7, C E G A is C6, D F A C is Dm7.
- The lowest note is the bass. When it's the root, 3rd or 5th of the chord, you get that chord. Any other
  note at the bottom becomes a slash bass under the chord the notes above it make: D under C E G is
  C/D, F under C E G is C/F, B♭ under C E G is C/B♭. (For C7, put B♭ above the C: C E G B♭.)
- A chord may leave out its 5th: C E B♭ is C7 too.
- Two notes are not a chord: nothing changes.
- Over a loop, a chord you play lasts until the loop's next chord change.
- **Writing in time** (REC while a loop plays, or a new loop's take): a chord lands on the nearest beat, except
  a bar's first chord played within its first beat: that lands on the bar's downbeat (a little late on the
  one is still the one).

**The chord types** (25, plus "---", no chord):

| Major | Dominant | Minor | Other |
| --- | --- | --- | --- |
| maj | 7 | m | aug |
| add9 | 7(9) | madd9 | dim |
| M7 | 7(#9) | m7 | sus4 |
| M7(9) | 7(b9) | m7(9) | 7sus4 |
| 6 | 7(13) | m7(11) | --- |
| | 7(b13) | m6 | |
| | 7(#11) | mM7 | |
| | 7(#5) | m7(b5) | |

"---" is no chord: every track just moves with the root.

### Progressions

L1–L8 start as eight factory progressions (Pop, Ballad, Fifties, ii–V–I, Blues, Minor, Flamenco,
Canon); L9–L16 start empty, for your own. Hold **HOME** and press a white key to play a loop. On HOME or
LOOP, **SEL**'s list has **LOAD PROGRESSION**: any of Polly's 16 progressions into the current loop.

| | | | |
| --- | --- | --- | --- |
| 1 Pop (I–V–vi–IV) | 5 Blues (12 bars) | 9 Canon | 13 Jazz (I–VI7–ii–V) |
| 2 Ballad (vi–IV–I–V) | 6 MinBlues (minor blues) | 10 Flamenco (Andalusian) | 14 Vamp (ii–V) |
| 3 Fifties (I–vi–ii–V) | 7 One-four | 11 Mixo (I–♭VII–IV) | 15 Gospel (IV–I) |
| 4 ii–V–I | 8 Minor (i–VI–III–VII) | 12 Doo-wop | 16 Waltz (3/4) |

**TRANSPOSE** (HOME's KNOB 3) moves the loops and the song to another key, from −6 to +5 semitones.
The chords you play on the keys aren't moved.

## Loops and songs

See [Writing a song](#writing-a-song) for the whole path.

- **LOOP** (ARP): the loop's bars, a chord a beat; KNOB 1–4 ROOT, TYPE, BASS, STEP at the cursor
  (OCT− / OCT+ move it). Page 2: LENGTH, BEATS, NAME. It shows the loop playing, or the last one
  picked; before you've picked one, the first empty loop, so you never write over a factory one.
- **HOME's KNOB 4, LOOP**: "–" (FREE: the band follows your chords) or L1–L16 (that loop plays; while
  playing, from the next bar). In a song it shows "Song"; turning it leaves the song.
- **SONG** (HOME, SELECT): the rows. A white key writes a row: on END it adds one, on a row it sets
  that row's loop. Black keys 1–8 mute the row's tracks, 9 and 10 its lead tracks LD1 and LD2 (the
  squares after the gap in MUTES: filled plays, a coral outline is muted). KNOB 1–4 ROW, LOOP, SECTION,
  REPEAT; page 2 KEY; page 3 INTRO, ENDING, AT END, SONG. While the song plays, the row playing is tinted
  teal; during its Intro, a fill and its Ending the line under the rows says so ("Intro · then row 1",
  "Fill AB · into row 3", "Ending · then the stop").
- **HOME** says where the band is: "loop bar 2.3 / 4" (or "pattern bar 1.1 / 2" with no loop); stopped, bar 1
  and the chords PLAY starts with.

### The style's patterns (ENV)

![The song's patterns](docs/img/manual/song_patterns.png)

Every song chooses a pattern for each of its six sections. ENV shows Intro, Main A and Main B
(KNOB 1–3); SELECT turns to Fill AB, Fill BA and Ending. Each knob goes through the user patterns P01 to
P16, then the styles' patterns for that section by name ("Funk A", "Funk B"). So you can play Funk's
Main A with Rock's fills.

**KNOB 4** (and PRESETS) is the song's style, like HOME's KNOB 1: it changes the sections playing a
style, not the ones on user patterns.

## Recording a part

Polly records into the selected track of the section playing, as a loop.

![Recording into C1](docs/img/manual/recording.png)

1. Press **SEQ** for the Pattern page. It shows the eight tracks of the section playing, with their
   notes, and the playhead.
2. Turn **ALGORITHM** to pick the track. The keys play its sound.
3. Press **REC**. The red dot shows in the header and PLAY turns red. The screen shows the pattern as
   bars, the track's notes in each: "REC C1 · starts with PLAY". The footer says "REC: STOP".
4. Press **PLAY** and play. The pattern loops, and each pass adds to what's there (overdub). You hear
   a new note from the next pass. The lines say where you are: "REC C1 · pass 2 · adding", and under it
   "pattern bar 1/2 · loop bar 3/4" (the pattern's bar, and the loop's while one plays).
5. Press **REC** again to stop recording. The pattern plays on.

Good to know:

- **The click:** a woodblock counts the beats while you record, and always during REC's count-in to the next
  bar line (Setup's CLICK: off, rec, on).
- **The grid** (Pattern page KNOB 1) moves notes to the nearest 8th, 8th
  triplet, 16th, 16th triplet or 32nd, or "off" for none. Clockwise is finer: 8, 8T, 16, 16T, 32, off.
- **As played.** The first time you record into an empty Chord 1, Chord 2 or Bass track, Polly
  switches it to Parallel "as played" and says so: "C1: AS PLAYED". A note plays back as you played it
  over the chord it was played on, and moves with the root over another chord, keeping its shape, by
  the shorter way: a line played over Am goes up a third over C. Its range opens to the whole keyboard.
  The Track page (EDIT) shows the type; set it back there if you want (Polly won't switch it again).
- **A track with notes** (a style's comping) keeps its type, so its notes still follow the chords:
  Polly says "C1 FOLLOWS CHORDS" and the footer "ERASE TRACK (SEL) TO PLAY AS PLAYED". What you record
  there follows the chords too.
- **REC while playing** starts at the next bar line, wherever the pattern is: the screen rings that bar and
  counts the beats down in it. A note played just before the bar line lands on it. The passes count from
  there: started at bar 3 of a 4-bar pattern, a pass is bars 3, 4, 1, 2.
- **A pattern shorter than the loop** (a style's 2-bar Main A over a 4-bar loop): REC's screen says
  "2-bar pattern · loop 4 bars · SEL: 4 bars". Press **SEL** and the pattern is as long as the loop, its bars
  repeated into the new ones (SEL's list has it too: **BARS AS THE LOOP**). Then what you play over the
  loop's bar 3 stays with bar 3.
- **A fill takes a row's last bar** in a song (the row's "+fill", or a section change after it): that bar
  plays the Fill, not the Main, so a part recorded in the Main's last bar isn't heard there.
- **Press REC before PLAY** and, with no song, the section you're recording into plays at once,
  without the Intro.
- **A section change ends the take**: a fill, an ending, or a song's next row. Polly says "RECORD OFF
  section ended". Record with no song, or on a section that loops.
- **A pattern holds 600 notes.** Past that, new notes are dropped.

### Your own patterns (copy-on-write)

The styles can't be changed. The first time you record into a style's section, or change it (its
sound, its mixer, its length), Polly copies it into a free **user pattern** of the song's own, and
says so: "RECORD C1 into new P01" or "NEW PATTERN P01". From then on that section plays your copy,
whatever the Style knob says, until the job Back to style. Muting a track doesn't count: mutes are the
song's, not the pattern's.

![RECORD C1 into new P01](docs/img/manual/record_msg.png)

There are 16 user patterns, P01 to P16, shared by the four songs. A free one is one that no song plays
and that holds no notes. When none is free, Polly says **NO FREE SLOT** "not kept": your change plays,
but it isn't kept once another pattern is picked. Free a slot ([Slots](#slots)): the change is kept there
at once ("MAIN A KEPT IN P14"). Or hold SAVE and press a white key on SEQ to keep it in a P number.

The Pattern page's **PRESETS** picks the pattern for the section playing: P01 to P16, then the styles'.

### Pattern setup (SEQ, SELECT)

![Pattern setup](docs/img/manual/pattern_setup.png)

- **KNOB 1** bars, 1 to 16. **KNOB 2** beats a bar, 2 to 7. Longer, the new bars repeat the pattern's,
  each track its own way (a 1-bar drum groove repeats bar 1, a 2-bar bass line bars 1 and 2), so the band
  goes on. Notes past a shortened length are kept, just not played; made longer again, they come back
  (nothing is repeated over them). While a loop plays, the pattern's bar stays the loop's: turned at the
  loop's bar 3, a 4-bar pattern plays its bar 3. The Pattern page shows a pattern longer than 8 bars by
  halves, "bars 9-16 of 16", following the playhead.
- **KNOB 3** picks a letter of the name, **KNOB 4** changes it. Names are eight letters, digits, "-"
  and "#".

### The pattern's actions (SEL)

On the Pattern, Track, Mixer and Master FX pages, **SEL**'s list opens on its actions: **ERASE TRACK**,
**CLEAR PATTERN** and **BACK TO STYLE**, for the section playing (and on SEQ and EDIT, **SLOTS**; **BARS AS THE
LOOP** first when the pattern is shorter than the loop). With a lead track selected they are the lead's:
**ERASE LD1 IN ROW n**, **ERASE LD1**, **QUANTISE LD1 ROW n** and **COPY LD1 TO ROW n**. To copy the pattern into
P01–P16, hold **SAVE** and press a white key on SEQ.

## Lead tracks

Each song has two lead tracks, **LD1** and **LD2**, for melodies that run along the whole song instead of
going round with a pattern. They play only in a song (SONG on HOME's LOOP knob, or PLAY on the SONG
page). A note belongs to its row, and it is kept as you played it with the chord's root under it: when
you change that row's chord or KEY, or the song's TRANSPOSE, the melody moves with the root and keeps its
shape (over the same chord it plays exactly as played).

**Record a melody over your song:**

1. On the SONG page, press **PLAY**.
2. Press **EDIT** and turn **ALGORITHM** past C4 to **LD1**. The keys play its sound (a square lead; LD2
   is strings).
3. Press **REC**. The click counts in to the next bar line ("REC LD1 · starts at the next bar", the beats
   left big in that bar). Play along. The screen says where you are, "REC LD1 · row 3 · bar 2.3", shows
   the row as bars with your notes in it, and the song's rows underneath, the one playing in teal.
4. Press **REC** again to stop recording. The song plays on. Stopping the song stops recording too
   ("RECORD OFF SONG STOPPED"), so the next PLAY doesn't record over the song.

Notes go into the row playing, at their place on the Pattern page's grid. You hear them the next time
the row plays. Recording again over a row (a punch in) adds notes to what's there; to replace a row's
part, erase it first (**SEL**: the list opens on **ERASE LD1 IN ROW n**), then record.

- **Punch in:** on the SONG page, move the cursor (ROW) to the row; then on **EDIT**, **REC** and **PLAY**: the
  song starts at that row (REC's screen rings it) and records from its first downbeat. (Or, stopped,
  REC in FREE or LOOP puts the song on first.)
- **A second part:** turn ALGORITHM to **LD2**; recording goes on into it. LD1's notes show dim under
  LD2's, to play against.
- **Erase:** on EDIT, **SEL**'s list opens on **ERASE LD1 IN ROW 3** (the row playing, else SONG's cursor row);
  under it **ERASE LD1** (the whole song's). Each asks first.
- **Copy a row:** **COPY LD1 TO ROW 3** (the same row): **ALGORITHM** picks the row it comes from ("from 2"),
  **SEL** copies its LD1 notes in place of row 3's (asking first when row 3 has some). They play as played
  over row 3's chords, moved with its root. One undo step.
- **Quantise:** **SEL**'s **QUANTISE LD1 ROW 3** moves that row's LD1 notes to the nearest line of the Pattern
  page's grid (shown on the row: 16, 8T, ...). Record with the grid off to play freely, then quantise. HOME + REC
  undoes it in one step.
- **Sound:** the Track page shows the lead's program and channel; its type is always "as played" over
  the whole keyboard (no Low, High or high key to set). **KNOB 4 Velocity** sets how hard the notes you record
  from then on are (the keys have no velocity; 100 at first). The Mixer and Mixer tone pages have its strip
  (black keys 9 and 10 pick LD1, LD2).
- **Mutes:** on the SONG page, black keys 9 and 10 mute LD1 and LD2 on the cursor's row.
- **On HOME**, in a song, LD1 and LD2 (each with notes) sit at the right of the song's line: plum, coral
  while a note sounds, an outline struck through while muted.
- REC while a loop plays, or in FREE, says "LEADS RECORD IN A SONG". Deleting a row that has lead notes asks
  first.
- Lead notes share the 16 user-pattern slots: a song's melodies take one slot (about 420 notes), a second
  when they outgrow it. "NO FREE SLOT" or "LEAD FULL" means none is left: see [Slots](#slots).

## Sound

The sound pages show and change the section playing, track by track: ALGORITHM picks the track.

### Track (EDIT)

![The Track page](docs/img/manual/track.png)

- **KNOB 1** the instrument: the 128 General MIDI programs. **PRESETS** jumps a family of eight
  (pianos, organs, guitars, basses...). A drum track plays the drum kit.
- **KNOB 2** the type: how the track follows the chords.
- **KNOB 3, KNOB 4** the low and high limits. A note outside them moves by octaves until it fits.

| Type | For | What it does |
| --- | --- | --- |
| Bypass | Drums | Never changes |
| Parallel | Riffs | Moves with the chord's root, keeping its shape |
| Chord 1 | Chords, arpeggios | Each note moves to the matching note of the new chord |
| Chord 2 | Pads | As Chord 1, and each note stays close to the one before, for smooth voice leading |
| Bass | Bass lines | As Chord 1, and the first note after a chord change is the root (or the slash bass) |

### Track keys (EDIT, SELECT)

- **KNOB 1** high key (C to B, or off): the highest root before a Chord 1 or Bass part drops an octave,
  so a bass line doesn't climb out of reach as the chords rise.
- **KNOB 2** the MIDI channel, 1 to 16. Drums are on 10.

### Mixer (LFO)

![The Mixer](docs/img/manual/mixer.png)

Ten strips: the eight tracks, then the lead tracks LD1 and LD2. Black keys 1–10 pick the track. For the
selected track: **KNOB 1** volume, **KNOB 2** pan, **KNOB 3** reverb send,
**KNOB 4** chorus send. Tracks on the same channel share these (D1, D2 and PC are all on 10).

**Mute:** the Pattern page's KNOB 4 mutes or unmutes the selected track. A mute is the song's: the
track stays muted in every section (and through fills), it's saved with the song, and each song has
its own. Muting never takes a user pattern.

### Mixer tone (LFO, SELECT)

![Mixer tone](docs/img/manual/mixer_tone.png)

**KNOB 1** Bright (the filter's cutoff) and **KNOB 2** Reso (its resonance) for the selected track. 64
is the instrument as it is. Lower Bright is darker; higher Reso is more pronounced.

### Master FX (FX)

![Master FX](docs/img/manual/master_fx.png)

The effects all the tracks share: **KNOB 1** the reverb (Off, Large hall, Hall, Chamber, Room),
**KNOB 2** its level, **KNOB 3** the chorus (1 to 4), **KNOB 4** its level.


## Songs and saving

### Four songs

Polly keeps four songs. Each has its own rows, tempo, TRANSPOSE, style (its six patterns) and mutes.

To change songs, hold **HOME** and turn **PRESETS**, on any page, or turn **SONG** on SONG's page 3. While playing, the new song waits until you stop ("SONG 2 after the stop"): loading a song saves
first, and saving pauses the sound for a moment.

### Saving

- **SAVE** lights, and "unsaved" shows in the header, while something isn't saved.
- Press **SAVE** to save now: the songs, the user patterns you've changed and the settings.
- Polly also saves by itself after about four seconds at rest: stopped, no key or button held, and
  the last notes faded.

Saving pauses the FM-1 for a moment, which is why it waits until you stop.

If two sections of a song play the same user pattern and both were changed, the newest change is kept,
and Polly says "SHARED PATTERN kept Main B".

### Undo

Hold **HOME** and press **REC** to undo; **HOME** + **PLAY** redoes (neither records nor starts). The header
says what: "UNDO L9 CHORD", "UNDO ROW 3", "UNDO TAKE LD1", "UNDO PASS C1", "UNDO CLEAR LOOP", "REDO C1 SOUND", or "NOTHING
TO UNDO". It covers the song you're on: its loops (chords, length, name, loading, clearing, copying), its
rows and settings (style, tempo, TRANSPOSE, mutes, the lead tracks' sounds), its lead notes (a take is one
step; erasing, copying a row), and its patterns as you change them (sounds, mixer, types, ERASE TRACK, CLEAR PATTERN, BACK
TO STYLE, and each pass of a recording). A step is what you changed before a pause of about a second, a whole
lead take, or one pass of a pattern recording. Polly keeps up to 32 steps (fewer after big ones) while it's
on; switching songs or turning it off starts afresh. It works while playing. Not undone: the tempo turned
while playing, and the Setup page and Master FX. What undo brings back is saved like any change.

### Slots

The 16 slots hold the user patterns (P01–P16), the songs' lead notes and each song's own loops (once they
differ from the factory's). When they are full, Polly says **NO FREE SLOT** and the footer
says **SEL: SLOTS**: press SEL then, or pick **SLOTS** at the end of SEL's list on SEQ or EDIT, to see what fills
them: "P03 · Funk A · S1" (a pattern, its name and section, and the songs playing it), "S2 leads 1/2", "S1
loops", "free". The header counts the free ones.

On a slot, **SEL** frees it (stopped; it asks first): a pattern goes back to the style in the songs that play it
(its notes are erased, and that can't be undone), a lead part's notes are erased, a song's loops go back to the
factory's ("S1'S LOOPS BACK TO FACTORY?"). HOME goes back. A change NO FREE SLOT left playing but not kept
(a section's sound, a recording, the song's loops) takes the slot freed at once ("MAIN A KEPT IN P14").

### The song's actions (SEL)

On the SONG pages, **SEL**'s list opens on **PLAY FROM ROW** (the row the cursor is on), then **INSERT ROW**,
**DELETE ROW**, **CLEAR SONG** and **COPY SONG TO**. A row's lead notes go with it (DELETE ROW asks first);
CLEAR SONG erases them; COPY SONG TO doesn't copy them.

### Backup

Polly's songs can be kept in a file on your computer and put back, over USB. From this repository:

```sh
pip3 install mido python-rtmidi
python3 tools/polly_backup.py backup my-songs.plb
python3 tools/polly_backup.py restore my-songs.plb
```

A backup holds everything Polly keeps: the four songs, the 16 slots (your patterns, lead notes and the songs'
loops) and the settings (Setup, the channels' sounds, Master FX). Not in it: the QY70 styles (install them again
from your ROM) and the button calibration. Polly plays on while it is backed up; stop first if you want the changes
it hasn't saved yet in the file too.

A restore **replaces everything** with the backup. Stop first: while playing, Polly answers **STOP FIRST**. Polly
receives the whole file and checks it before anything changes, then asks on its screen: **RESTORE FROM USB?**
**SEL** restores (and reloads the songs), **HOME** keeps what you have. A cut cable or a damaged file changes
nothing; even a power cut while it writes leaves either the old songs or the new, never a mix.

`python3 tools/polly_backup.py info my-songs.plb` shows what a backup holds.

## Setup and MIDI

Press **GLO** for Setup.

![Setup](docs/img/manual/setup.png)

| Knob | Sets |
| --- | --- |
| KNOB 1 Clock | **MIDI in** (the default): Polly keeps its own time, and follows MIDI clock, start and stop whenever they come in. **internal**: it ignores MIDI clock |
| KNOB 2 MIDI out | **on** (the default): the tracks go out over USB on their channels, with their sounds and mixer, and MIDI clock with start and stop while Polly keeps time. **off**: nothing goes out |
| KNOB 3 Lights | **on**: keys light as you play, and the other buttons glow. **notes**: keys light, no glow. **off**: neither |
| KNOB 4 Click | **rec** (the default): a woodblock on every beat while you record, high on the bar's first. **on**: whenever Polly plays. **off**: only the count-in |

The click is on MIDI channel 16 (it goes to MIDI out too), a little under the band, and the tracks' mutes don't
silence it. REC's count-in, the beats before a take starts at the next bar line (a track, or a new loop on HOME),
always clicks, even with the click off.

The last row shows the QY70 bank: none, the number of styles, "damaged" or "old format".

Turn SELECT for the Panel: the processor's load, the voices sounding and the version.

**MIDI in** (USB and the TRS jack) plays Polly's sounds on all 16 channels, as a General MIDI sound
module, drums on channel 10. It plays alongside the arranger. Controllers 74 (brightness) and 71
(resonance) move each channel's filter, as the Mixer tone page does.

**USB:** the FM-1 shows up as a class-compliant MIDI device named "Polly FM-1". The FM-1 has no MIDI
out jack: MIDI out is USB only.

## The QY70 bank

The QY70 came with 128 preset styles. If you have its main ROM (a dump of the chip, the file MAME calls
`qy70main.bin`), Polly can build all 128 from it and play them next to its own 16. Polly contains none
of Yamaha's music: you bring the ROM, and the bank is for your own use.

### On the FM-1

Connect the FM-1 over USB and run, from this repository:

```sh
pip3 install mido python-rtmidi
python3 tools/qy70_install.py qy70main.bin
```

It builds the bank, asks before writing (`--yes` skips the question), and writes it in about 30
seconds. The FM-1 stops playing meanwhile. Polly checks everything it was sent before it uses it, so a
cut cable leaves no bank, never half of one: just run it again. Setup then shows "128 styles".

### In the browser

On the emulator's page, press **Load QY70 ROM** (or drop the file on the box). Your ROM stays on your
computer: the page builds the bank in your browser and keeps it there for your next visit. **Forget
QY70 styles** removes it.

### Playing them

The QY70 styles follow Polly's 16 on the Style knob (HOME's KNOB 1, or the Style pages, ENV), marked "QY". They play with their own bends, swells and filter moves, and change
sections and styles without a gap. A song plays one QY70 style at a time: picking a section from
another one moves the song's other QY70 sections with it.

### What doesn't carry over

- **Drum kits.** Polly has one General MIDI kit. The QY70's brush, jazz, room and analog kits all play
  it.
- **XG voices.** Variations play their General MIDI instrument, and sound-effect voices a General MIDI
  effect.
- **Pan on the drum channel.** The drum tracks share one pan and one set of effects.
- **Editing.** Recording into or changing a QY70 section makes it a user pattern, as with Polly's own
  styles. Then the 600-note limit applies (the quietest short notes go first), and its bends and
  controllers are left out. Played as they are, QY70 styles have no note limit.

### Keeping it

The bank lives in the FM-1's memory next to your songs. Restoring M-VAVE's firmware wipes it. If
Setup shows "none" or "old format" after an update, install it again.

The QY70's styles are © Yamaha Corporation. Don't share the bank or anything built from your ROM.

## Updating and safety

These come from Felucca, the platform Polly is built on, and work as in the other FM-1 firmwares:

- **Update mode:** hold OCT− and OCT+ for 5 seconds. The screen counts down, then the FM-1 restarts
  ready for an update. Let go before then to cancel.
- **Safe mode:** if the FM-1 fails to start twice in a row, it starts in safe mode with only USB on, so
  the web installer can reach it. PLAY tries a normal start again.
- **Back to M-VAVE's firmware:** the web installer can put the original firmware back. M-VAVE's own
  updater, M-UPGRADE, works too.
- **Button calibration:** hold OCT− and OCT+ while switching on, then follow the screen. Only needed if
  buttons do the wrong thing.

Your songs and patterns stay through a Polly update. Putting M-VAVE's firmware back can wipe them: make a
[backup](#backup) first.

## Credits

- **Felucca** by Leo Kuroshita (Hügelton Instruments): the platform under Polly (GPL-3.0), by way of
  X0X and FoMni. <https://github.com/hugelton/Felucca>
- **Sonivox EAS** (Sonic Network, The Android Open Source Project): the synth and its General MIDI
  sound set (Apache-2.0). <https://github.com/EmbeddedSynth/sonivox>
- **The Groove MIDI Dataset** by Jon Gillick, Adam Roberts, Jesse Engel, Douglas Eck and David Bamman
  (Google Magenta), © Google LLC: the drum performances in Polly's styles (CC BY 4.0), excerpted and
  quantised. <https://magenta.tensorflow.org/datasets/groove>
- **Groove OS** by Peter Gombos: the idea of running FM-1 firmware in the browser.
  <https://www.groove-os.com/>
- **qy100-toolkit**: its decoding of the QY70's pattern format helped read the ROM's styles.
  <https://github.com/doffu0000/qy100-toolkit>
- **Pyodide** (MPL-2.0) builds the QY70 bank in the browser.
- **Barlow** (The Barlow Project Authors, SIL OFL 1.1): the screen's typeface.

Polly is inspired by the Yamaha QY70; it is not affiliated with Yamaha. "M-VAVE" and "FM-1" are
trademarks of their owners. Polly is GPL-3.0 (see LICENSING.md).

## Page by page

ALGORITHM picks the track on every page (D1 … C4, then LD1, LD2). HOME held + PRESETS picks the song on every page. SELECT turns a
button's pages.

| Page | Button | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 | PRESETS |
| --- | --- | --- | --- | --- | --- | --- |
| HOME | HOME | Style | Tempo | Transpose | Loop | The next loop |
| SONG | HOME, SELECT | Row | Loop | Section | Repeat | The next row |
| SONG 2 | HOME, SELECT ×2 | Row | Key | | | The next row |
| SONG 3 | HOME, SELECT ×3 | Intro | Ending | At end | Song | The next row |
| LOOP | ARP | Root | Type | Bass | Step | The next loop |
| LOOP 2 | ARP, SELECT | Length | Beats | Name | | The next loop |
| Pattern | SEQ | Grid | | | Mute | The section's pattern |
| Pattern setup | SEQ, SELECT | Bars | Beats | Letter | Character | The section's pattern |
| Track | EDIT | Program | Type | Low (a lead: none) | High (a lead: Velocity) | Program family |
| Track keys | EDIT, SELECT | High key (a lead: none) | Channel | | | |
| Mixer | LFO | Volume | Pan | Reverb | Chorus | |
| Mixer tone | LFO, SELECT | Bright | Reso | | | |
| Master FX | FX | Reverb | Reverb level | Chorus | Chorus level | |
| Style | ENV | Intro | Main A | Main B | Style | Style |
| Style 2 | ENV, SELECT | Fill AB | Fill BA | Ending | Style | Style |
| Setup | GLO | Clock | MIDI out | Lights | Click | |
| Panel | GLO, SELECT | | | | | |

## Screen mirroring

Polly can show its screen and panel on a computer, live, over the USB cable: for teaching, filming or a big screen.
It only watches; nothing on the page plays the FM-1.

1. Plug the FM-1 into the computer.
2. Open `web/mirror/index.html` in Chrome or Edge (the browsers with Web MIDI).
3. Press **Connect** and allow MIDI with SysEx. Polly is picked by itself (its ports are called *Polly FM-1*); if
   not, pick them in the In and Out lists.

The screen shows twice or three times its size (×2, ×3), and the panel below it lights as the FM-1 does: lit
buttons and keys as their LEDs, the ones held outlined, a knob turned flashes with the detents it moved. **Save PNG**
saves the screen; `python3 tools/polly_mirror.py screen.png` does the same without a browser.

The mirror costs nothing until a page asks for it, and stops when the page closes, goes quiet for six seconds or
the cable comes out. Notes and clock go out first: when the computer is slow to take it all, the page skips frames,
then catches up by itself.
