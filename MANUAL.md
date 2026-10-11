# Polly manual

Polly turns the M-VAVE FM-1 into a pocket arranger, inspired by the Yamaha QY70. A style plays a band
of eight instruments, and the chords you give it steer the band. Hold a chord and the bass, the guitars and
the strings follow. Write a few bars of chords and a whole song plays.

Its sounds are the 128 General MIDI instruments and a drum kit, from Sonivox EAS. They play through the
speaker and the headphone jack, and MIDI coming in plays them too.

- [Cheat sheet](#cheat-sheet)
- [Writing a song](#writing-a-song)
- [Getting around](#getting-around)
- [Styles and grooves](#styles-and-grooves)
- [Chords](#chords)
- [Progressions and the song](#progressions-and-the-song)
- [Recording an instrument](#recording-an-instrument)
- [Melodies](#melodies)
- [Sound](#sound)
- [Songs and saving](#songs-and-saving)
- [Setup and MIDI](#setup-and-midi)
- [The QY70 bank](#the-qy70-bank)
- [Updating and safety](#updating-and-safety)
- [Credits](#credits)
- [Page by page](#page-by-page)

## Cheat sheet

(It comes first because the rest of the manual, "Writing a song" included, uses its words.)

### The words

![A song holds sections, progressions, melodies and a style](docs/img/manual/hierarchy.svg)

```
Song (S1-S4)
├─ Sections: progression + groove × repeat (+ mutes, transpose)
├─ Progressions P1-P16
├─ Melodies M1, M2
└─ Style: Groove A, Groove B, Intro, Fills, Ending → Instruments
```

- **Song** (S1–S4): one piece of music, with its own style, progressions, sections, melodies, tempo and transpose.
- **Section**: a stretch of the song (a verse, a chorus): a progression, played with a groove, so many times; a song
  is a list of sections, top to bottom.
- **Progression** (P1–P16; "Prog" where the screen is short): your chords, 1 to 16 bars, going round. P1–P8 come
  ready-made, P9–P16 are for yours.
- **Chord**: one chord in a progression.
- **Melody** (M1, M2): your own lines, recorded along the song's sections.
- **Style**: the band's way of playing (Pop, Funk…), one per song.
- **Groove**: how the band plays a section: **Groove A** calmer, **Groove B** busier.
- **Intro, Fill, Ending**: the style's extras, played by themselves: at the start, between grooves (or when you ask),
  at the end.
- **Instrument**: one player in the band: D1, D2 drums, PC percussion, BA bass, C1–C4 chords, riffs and pads. Each has
  a sound and its notes.
- **(yours)**: a groove you changed plays your own copy, "Pop · Groove A (yours)"; BACK TO STYLE gives it back to the
  style.
- **Tempo**, **Transpose** (the song's, and a section's own on top of it), **Repeat** (a section's), **Mute** (an
  instrument or a melody; a section's mutes last for that section).
- **Free**: no progression playing; the band follows the chords you play (Home's KNOB 4 "Prog: –").
- **Memory**: the 16 places your grooves, melodies and progressions are kept in, shared by the four songs.
- **Undo / Redo**, **Click** (the woodblock while you record), **Backup** (your songs in a file), **Mirror** (the
  screen on a computer).

### How do I…

White keys count from the left, 1 to 16; black keys too, 1 to 11. "Hold HOME" means keep it down while you press the
next thing.

| How do I… | Press |
| --- | --- |
| pick a style | **HOME**, turn **KNOB 1** |
| play a progression | hold **HOME**, press white key *n* (P*n* goes round, from the next bar while playing) |
| change the groove | hold **HOME**, press black key 2 (Groove A) or 3 (Groove B); 4 a fill, 1 the Intro, 5 the Ending |
| write a progression | **HOME**, turn **KNOB 4** anticlockwise to "–" (free), **REC**, **PLAY**, a chord a bar, **REC** |
| fix a chord | **ARP**, **OCT+** / **OCT−** to its beat, turn **KNOB 1** (root), **KNOB 2** (type) or **KNOB 3** (bass) |
| lengthen a progression | **ARP**, **SELECT** (page 2), turn **KNOB 1** (Length) |
| add a section to the song | **HOME**, **SELECT** (the Song page), press white key *n*: a section playing P*n* |
| play from section 3 | on the Song page turn **KNOB 1** (Section) to 3, **SEL** (the list opens on PLAY FROM SECTION 3), **SEL** |
| change an instrument's sound | **EDIT**, turn **ALGORITHM** to the instrument, turn **KNOB 1** (Program) |
| mute an instrument | **SEQ**, turn **ALGORITHM** to it, turn **KNOB 4** (Mute; again: it plays). In one section only: on the Song page, its black key |
| record a melody | on the Song page **PLAY**; **EDIT**, turn **ALGORITHM** past C4 to M1, **REC**, play, **REC** |
| undo / redo | hold **HOME**, press **REC** / **PLAY** |
| save | **SAVE** (Polly also saves by itself, stopped) |
| back up | on a computer: `python3 tools/polly_backup.py backup my-songs.plb` |

### The buttons

- **PLAY**: start, stop. On the Song page: the song.
- **REC**: record what the keys play on this page (chords, an instrument, a melody, sections).
- **SEL**: this page as a list, with its actions; in a question, yes.
- **HOME**: Home; **SELECT** then turns to the Song pages. Held: the shortcuts (keys, SELECT, PRESETS, REC, PLAY); in a
  question, no.
- **ARP**: Progression (the chords), page 2 its length and name.
- **SEQ**: Band (the groove's notes, instrument by instrument), page 2 the groove's length and name.
- **EDIT**: Sound (the instrument's sound and range), page 2 its high key and channel.
- **LFO**: Mixer, page 2 its tone.
- **FX**: Effects (reverb and chorus).
- **ENV**: Style (which style each groove and extra plays).
- **GLO**: Setup, page 2 the Panel.
- **SAVE**: save; held + a white key: copy here.
- **OCT− / OCT+**: the keys' octave (on Progression: the cursor).
- **SELECT**: a button's next page; in a list, up and down. **ALGORITHM**: the instrument (or melody). **PRESETS**: the
  next one of what the page shows. **KNOB 1–4**: the four values at the bottom of the screen.

Hold any button for a second and its card says what it does on the page you're on.

## Writing a song

The white keys are counted from the left, 1 to 16: progression P9 is white key 9.

1. **Find a groove.** Turn **KNOB 1** for a style (Pop, Funk, Bossa…). Press **PLAY**. Play a chord:
   three keys or more. The band follows whatever you play.
2. **Write your verse.** Press **REC**, play Am, F, C, G, one per bar, then press **REC** again. Your
   chords are now progression P9 and the band plays them round. Name it: press **ARP**, **SELECT** to page 2,
   and turn **NAME** to "Verse". (Or, stopped: press **ARP**, which opens the first empty progression, and
   write on its grid: **REC**, then a chord a bar.)
3. **Write your chorus.** Press **HOME** and turn **KNOB 4** anticlockwise, past P1, to "–": the band is
   free again. Press **REC**, play C, G, Am, F, then press **REC**. That's P10. Name it: **ARP**, **SELECT**,
   **NAME** to "Chorus".
4. **Build the song.** Press **HOME**, then **SELECT** to the Song page. Tap white keys in order: 9,
   10, 9, 10. Each tap adds a section: verse, chorus, verse, chorus.
5. **Shape it.** On a chorus (**SECTION**, KNOB 1, moves between sections), turn **GROOVE** to B for
   the busier groove (the other chorus sections follow). Turn **REPEAT** for ×2. On the last section, **SELECT**
   to the next page and turn **TRANSPOSE** to +2 for the last chorus a tone up. For a breakdown, tap black
   keys to mute instruments in a section (black keys 1, 2 and 3 are the drums and the percussion: D1, D2, PC).
6. **Play it.** Press **PLAY** on the Song page. The band is still playing your chorus, so the song
   takes over at the next bar, from section 1: your sections with fills between them, then an Ending.
   (From a stop it opens with an Intro. To start on another section: move the cursor there with **SECTION**,
   press **SEL**: the list opens on **PLAY FROM SECTION**; **SEL** again starts it.)
7. **Add a melody.** On the Song page press **PLAY**, then press **EDIT**, turn **ALGORITHM** past C4 to
   M1 and press **REC**. The click counts you in to the next bar line; play along: your notes go into the
   sections as they play. (See [Melodies](#melodies).)
8. **Save.** Press **SAVE**, or just stop: Polly saves by itself.

## Getting around

### The panel

| Control | What it does |
| --- | --- |
| PLAY | Start and stop. On a Song page: the song from section 1 (while a progression plays, from the next bar); playing, it stops it |
| REC | Write what the keys play here: chords into the progression (Home, Progression; free on Home: a new progression), the selected instrument (Band, Sound), sections (Song) |
| SEL | This page as a list, opened on its first action (LOAD PROGRESSION, PLAY FROM SECTION, ERASE INSTRUMENT…); SELECT up to its values and MORE |
| HOME | Home; SELECT for the Song pages. Held: the shortcuts, on screen: + white key 1–16 a progression, + black key 1–5 a groove or an extra, + SELECT the tempo, + PRESETS the song, + REC undo, + PLAY redo |
| ARP | Progression: its chords; page 2 its length, beats, name |
| SEQ | Band (the groove's notes), page 2 Groove length |
| EDIT | Sound, page 2 Sound 2 (high key, channel) |
| LFO | Mixer, page 2 Mixer tone |
| FX | Effects |
| ENV | Style: which style each groove and extra plays (two pages) |
| GLO | Setup, page 2 Panel |
| SAVE | Save. Held + a white key: copy the progression (Home, Progression) or the groove (Band) there |
| SELECT | The button's next or previous page; in a list, up and down |
| ALGORITHM | The instrument, on any page: D1, D2, PC, BA, C1, C2, C3, C4, then the melodies M1, M2 |
| PRESETS | Depends on the page (Home, Progression: the next progression). With HOME held: the song |
| KNOB 1–4 | The four values at the bottom of the screen |
| OCT− / OCT+ | Move the keys down or up an octave, −3 to +3. On Progression: the cursor |
| Keys | Chords on Home and Progression, the selected instrument on Band and Sound, the sections on Song |
| Any button held a second | A card says what it does on this page |

### Pages and dots

![Home while a progression plays](docs/img/manual/home.png)

Each page button lights while you're on one of its pages, and always opens its first page. Some buttons
have two pages (HOME four: Home and Song's three): turn **SELECT** for the next one. The dots over the
knobs show which one you're on.

The screen has these bands:

- **The header**: the page's name, the tempo on the right ("ext" when MIDI clock drives it), a red dot
  while recording, and "unsaved" when there's something to save. On Home it also shows the song and its
  style.
- **The middle**: what the page is about.
- **The tag line** over the knobs: where you are, from the song down: "S1 › Pop › Groove A › C1 Acoustic
  Grand" (Band, Sound, Mixer), "S1 › P9 Verse · bar 2.1" (Progression), "S1 › section 3" (Song), "S1 › M1 Square
  Lead" (a melody). A long one loses its first steps.
- **The knobs**: what KNOB 1–4 do here, with their values. The knob you turned last shows in coral.
- **The footer**: what the keys and REC do here ("KEYS: CHORDS · REC: WRITE"); while something records,
  "REC: STOP".

**Messages** such as "SAVED" or "NEXT GROOVE B" take over the header for a second or so.

### The instruments

A groove has eight instruments, named as on the QY70:

| Instrument | Usually |
| --- | --- |
| D1, D2 | Drums |
| PC | Percussion |
| BA | Bass |
| C1–C4 | Chords, riffs and pads |

ALGORITHM picks the instrument on any page. The screen says its name and sound. The keys play that
instrument's sound (C1 at first, a piano), and REC records into it. Past C4 come the song's two melodies, M1
and M2 (see [Melodies](#melodies)).

### The keys

The 27 keys run from F3 to G5 at octave 0. OCT− and OCT+ move them an octave at a time. They play at a
fixed velocity, because the FM-1's keys don't sense how hard you press.

### The lights

- **PLAY** is green while playing, and red while recording.
- **REC** lights while recording, and **SAVE** while something isn't saved.
- The button of the page you're on is lit.
- Keys light while you hold them.
- While you hold HOME, the black key of the groove or extra playing (or coming next) lights, and the white
  key of the progression playing.
- With Lights "on" (Setup), the other buttons glow dimly, so you can read them in the dark.

## Styles and grooves

A **style** is the band's way of playing: two grooves and the extras around them.

| | What it does |
| --- | --- |
| Intro | Plays once at the start, then Groove A |
| Groove A | The calmer groove. Goes round |
| Groove B | The busier groove. Goes round |
| Fill AB | One bar, from Groove A into Groove B |
| Fill BA | One bar, from Groove B back to Groove A |
| Ending | Plays once, then stops |

Polly has 16 styles of its own: Pop, Funk, Rock, Ballad, Bossa, Swing, Shuffle, Reggae, Hip hop, House,
Disco, Country, Gospel, Waltz, Latin and Ambient. Their drums are real drummers' performances from the
Groove MIDI Dataset. With a [QY70 bank](#the-qy70-bank) installed, the QY70's 128 preset styles follow
them on the Style knob, with "QY" before their names.

### Picking a style

On Home, turn **KNOB 1**. The header shows the style. Until you set the song's
tempo yourself, the tempo changes to the style's own. Once you've turned the tempo, it stays yours.
You can change the style while playing.

Each song has its own style, so each of the four songs can sound different.

**A groove you've changed stays yours.** Once you record into a groove (or an extra), or change its
sound, it plays [your own copy](#yours-your-own-copy) instead of the style's. The header then says so:
"Pop · Groove A (yours)", "Pop · 3 (yours)", "Pop · all (yours)". Turning the Style knob changes only what still
plays the style; yours stays as it is. So you can keep your Groove A and try the band around it in Funk, then
Rock.

To give a groove back to the style, play it (or pick it with HOME + its black key), press **SEL** on the
Band page and run **BACK TO STYLE** ([the groove's actions](#the-grooves-actions-sel)). Your copy is kept in
memory, so you can pick it again later.

### Changing grooves

Hold **HOME** and press a black key:

| Black key | |
| --- | --- |
| 1 | Intro |
| 2 | Groove A |
| 3 | Groove B |
| 4 | Fill |
| 5 | Ending |

![HOME held: the grooves and extras, and the progressions](docs/img/manual/home_held.png)

While playing, the change waits for the next bar. Home shows NEXT and the groove until then.
While stopped, you choose what PLAY starts with ("FIRST GROOVE B").

**The fill rules:**

- Fill picks the right fill for you: Fill AB from Groove A (it then goes on to Groove B), Fill BA from
  Groove B (it then goes back to Groove A).
- A fill asked for during a fill adds another bar of fill.
- There's no fill in the Intro or the Ending: they play out ("NO FILL").
- If a style has no Fill BA, Fill AB plays instead, and the other way round.

**Starting and ending:**

- PLAY starts on Groove A. The Intro plays first in a song with INTRO on started from a stop, or when
  you pick it (HOME + black key 1) before PLAY.
- The Ending plays once and stops. Without an Ending, Polly stops at the bar line. Stopped so, the pages
  show what PLAY starts with again: in a song section 1's groove, else Groove A (not the Ending).

### Intros and endings hold a chord

The Intro and the Ending play over one held chord, the QY70's way: they carry their own lead-in and
cadence. The chord on their first beat stays until they end. A chord you play on the keys still
changes it at once.

### Tempo

Turn **KNOB 2** on Home, or hold **HOME** and turn **SELECT** (held, the footer says "SELECT: TEMPO"): 40 to
240 BPM. The tempo is shown at the top right. When Polly follows MIDI clock, it shows "ext" instead.

## Chords

Polly's grooves are written as if the chord were C major 7. As it plays, it moves every note of every
instrument to fit the chord of the moment. There are three ways to give it chords: play them, play a
progression, or play a song.

### Playing chords

On Home and Progression the keys play chords.

- Hold three or more different notes. Polly names the chord, shows it big on Home, and the band
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
- Over a progression, a chord you play lasts until the progression's next chord change.
- **Writing in time** (REC while a progression plays, or a new progression's take): a chord lands on the nearest
  beat, except a bar's first chord played within its first beat: that lands on the bar's downbeat (a little late
  on the one is still the one).

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

"---" is no chord: every instrument just moves with the root.

### Ready-made progressions

P1–P8 start as eight factory progressions (Pop, Ballad, Fifties, ii–V–I, Blues, Minor, Flamenco,
Canon); P9–P16 start empty, for your own. Hold **HOME** and press a white key to play a progression. On Home or
Progression, **SEL**'s list has **LOAD PROGRESSION**: any of Polly's 16 progressions into the current one.

| | | | |
| --- | --- | --- | --- |
| 1 Pop (I–V–vi–IV) | 5 Blues (12 bars) | 9 Canon | 13 Jazz (I–VI7–ii–V) |
| 2 Ballad (vi–IV–I–V) | 6 MinBlues (minor blues) | 10 Flamenco (Andalusian) | 14 Vamp (ii–V) |
| 3 Fifties (I–vi–ii–V) | 7 One-four | 11 Mixo (I–♭VII–IV) | 15 Gospel (IV–I) |
| 4 ii–V–I | 8 Minor (i–VI–III–VII) | 12 Doo-wop | 16 Waltz (3/4) |

**TRANSPOSE** (Home's KNOB 3) moves the progressions and the song to another key, from −6 to +5 semitones.
The chords you play on the keys aren't moved.

## Progressions and the song

See [Writing a song](#writing-a-song) for the whole path.

![The Progression page](docs/img/manual/progression.png)

- **Progression** (ARP): its bars, a chord a beat; KNOB 1–4 ROOT, TYPE, BASS, STEP at the cursor
  (OCT− / OCT+ move it). Page 2: LENGTH, BEATS, NAME. It shows the progression playing, or the last one
  picked; before you've picked one, the first empty one, so you never write over a factory one.
- **Home's KNOB 4, PROG**: "–" (free: the band follows your chords) or P1–P16 (that progression plays; while
  playing, from the next bar). In a song it shows "Song"; turning it leaves the song.
- **Song** (HOME, SELECT): the sections, top to bottom. A white key writes a section: on END it adds one, on a
  section it sets that section's progression. Black keys 1–8 mute the section's instruments, 9 and 10 its melodies
  M1 and M2 (the squares after the gap in MUTE: filled plays, a coral outline is muted). KNOB 1–4 SECTION, PROG,
  GROOVE (A or B, "+ fill" for a fill into the next section), REPEAT; page 2 TRANSPOSE (the section's own, on top of
  the song's); page 3 INTRO, ENDING, AT END, SONG. While the song plays, the section playing is tinted teal; during
  its Intro, a fill and its Ending the line under the sections says so ("Intro · then section 1", "Fill AB · into
  section 3", "Ending · then the stop").
- **Home** says where the band is: "progression bar 2.3 / 4" (or "groove bar 1.1 / 2" with no progression);
  stopped, bar 1 and the chords PLAY starts with.

![The Song page](docs/img/manual/song.png)

### The style's grooves and extras (ENV)

![The Style page](docs/img/manual/style.png)

Every song chooses what each of its grooves and extras plays. ENV shows Intro, Groove A and Groove B
(KNOB 1–3); SELECT turns to Fill AB, Fill BA and Ending. Each knob goes through your own copies ("yours"),
then the styles by name. So you can play Funk's Groove A with Rock's fills.

**KNOB 4** (and PRESETS) is the song's style, like Home's KNOB 1: it changes what plays a style, not what is
yours.

## Recording an instrument

Polly records into the selected instrument of the groove (or extra) playing, going round.

![Recording into C1](docs/img/manual/recording.png)

1. Press **SEQ** for the Band page. It shows the eight instruments of the groove playing, with their
   notes, and the playhead.
2. Turn **ALGORITHM** to pick the instrument. The keys play its sound.
3. Press **REC**. The red dot shows in the header and PLAY turns red. The screen shows the groove as
   bars, the instrument's notes in each: "REC C1 · starts with PLAY". The footer says "REC: STOP".
4. Press **PLAY** and play. The groove goes round, and each pass adds to what's there (overdub). You hear
   a new note from the next pass. The lines say where you are: "REC C1 · pass 2 · adding", and under it
   "groove bar 1/2 · prog bar 3/4" (the groove's bar, and the progression's while one plays).
5. Press **REC** again to stop recording. The groove plays on.

Good to know:

- **The click:** a woodblock counts the beats while you record, and always during REC's count-in to the next
  bar line (Setup's CLICK: off, rec, on).
- **The grid** (Band page KNOB 1) moves notes to the nearest 8th, 8th
  triplet, 16th, 16th triplet or 32nd, or "off" for none. Clockwise is finer: 8, 8T, 16, 16T, 32, off.
- **As played.** The first time you record into an empty Chord 1, Chord 2 or Bass instrument, Polly
  switches it to Parallel "as played" and says so: "C1: AS PLAYED". A note plays back as you played it
  over the chord it was played on, and moves with the root over another chord, keeping its shape, by
  the shorter way: a line played over Am goes up a third over C. Its range opens to the whole keyboard.
  The Sound page (EDIT) shows the type; set it back there if you want (Polly won't switch it again).
- **An instrument with notes** (a style's comping) keeps its type, so its notes still follow the chords:
  Polly says "C1 FOLLOWS CHORDS" and the footer "ERASE INSTRUMENT (SEL) TO PLAY AS PLAYED". What you record
  there follows the chords too.
- **REC while playing** starts at the next bar line, wherever the groove is: the screen rings that bar and
  counts the beats down in it. A note played just before the bar line lands on it. The passes count from
  there: started at bar 3 of a 4-bar groove, a pass is bars 3, 4, 1, 2.
- **A groove shorter than the progression** (a style's 2-bar Groove A over a 4-bar progression): REC's screen
  says "2-bar groove · prog 4 bars · SEL: 4 bars". Press **SEL** and the groove is as long as the progression,
  its bars repeated into the new ones (SEL's list has it too: **BARS AS THE PROG**). Then what you play over the
  progression's bar 3 stays with bar 3.
- **A fill takes a section's last bar** in a song (the section's "+ fill", or a groove change after it): that bar
  plays the Fill, not the groove, so what you record in the groove's last bar isn't heard there.
- **Press REC before PLAY** and, with no song, the groove you're recording into plays at once,
  without the Intro.
- **A change of groove ends the take**: a fill, an ending, or a song's next section. Polly says "RECORD OFF
  THE BAND MOVED ON". Record with no song, or on a groove that goes round.
- **A groove holds 600 notes.** Past that, new notes are dropped.

### (yours): your own copy

![GROOVE A NOW YOURS](docs/img/manual/yours.png)

The styles can't be changed. The first time you record into a style's groove or extra, or change it (its
sound, its mixer, its length), Polly copies it into a free place in [memory](#memory) and says so: "GROOVE A
NOW YOURS", or "RECORD C1 GROOVE A (YOURS)". From then on that groove plays your copy, "(yours)" on the screen,
whatever the Style knob says, until you give it BACK TO STYLE. Muting an instrument doesn't count: mutes are the
song's, not the groove's.

Your copies share the 16 places of memory with the melodies and the songs' own progressions. When none is free,
Polly says **MEMORY FULL** "not kept": your change plays, but it isn't kept once something else is picked. Free a
place ([Memory](#memory)): the change is kept there at once ("GROOVE A KEPT IN MEMORY 14"). Or hold SAVE and press
a white key on SEQ to keep a copy in that place of memory.

The Band page's **PRESETS** picks what the groove playing plays: your copies, then the styles'.

### Groove length (SEQ, SELECT)

![Groove length](docs/img/manual/groove_length.png)

- **KNOB 1** bars, 1 to 16. **KNOB 2** beats a bar, 2 to 7. Longer, the new bars repeat the groove's,
  each instrument its own way (a 1-bar drum groove repeats bar 1, a 2-bar bass line bars 1 and 2), so the band
  goes on. Notes past a shortened length are kept, just not played; made longer again, they come back
  (nothing is repeated over them). While a progression plays, the groove's bar stays the progression's: turned
  at the progression's bar 3, a 4-bar groove plays its bar 3. The Band page shows a groove longer than 8 bars by
  halves, "bars 9-16 of 16", following the playhead.
- **KNOB 3** picks a letter of the name, **KNOB 4** changes it. Names are eight letters, digits, "-"
  and "#".

### The groove's actions (SEL)

On the Band, Sound, Mixer and Effects pages, **SEL**'s list opens on its actions: **ERASE INSTRUMENT**,
**CLEAR GROOVE A** (or the extra playing) and **BACK TO STYLE**, for the groove playing (and on SEQ and EDIT,
**MEMORY**; **BARS AS THE PROG** first when the groove is shorter than the progression). With a melody selected
they are the melody's: **ERASE M1 IN SECTION n**, **ERASE M1**, **QUANTISE M1 IN SECTION n** and **COPY M1 TO
SECTION n**. To copy the groove into a place of memory, hold **SAVE** and press a white key on SEQ.

## Melodies

Each song has two melodies, **M1** and **M2**, for lines that run along the whole song instead of
going round with a groove. They play only in a song (Song on Home's PROG knob, or PLAY on the Song
page). A note belongs to its section, and it is kept as you played it with the chord's root under it: when
you change that section's chord or TRANSPOSE, or the song's, the melody moves with the root and keeps its
shape (over the same chord it plays exactly as played).

**Record a melody over your song:**

1. On the Song page, press **PLAY**.
2. Press **EDIT** and turn **ALGORITHM** past C4 to **M1**. The keys play its sound (a square lead; M2
   is strings).
3. Press **REC**. The click counts in to the next bar line ("REC M1 · starts at the next bar", the beats
   left big in that bar); during the Intro it waits for section 1. Play along. The screen says where you are,
   "REC M1 · section 3 · bar 2.3", shows the section as bars with your notes in it, and the song's sections
   underneath, the one playing in teal.
4. Press **REC** again to stop recording. The song plays on. Stopping the song stops recording too
   ("RECORD OFF SONG STOPPED"), so the next PLAY doesn't record over the song.

Notes go into the section playing, at their place on the Band page's grid. You hear them the next time
the section plays. Recording again over a section (a punch in) adds notes to what's there; to replace a
section's melody, erase it first (**SEL**: the list opens on **ERASE M1 IN SECTION n**), then record.

- **Punch in:** on the Song page, move the cursor (SECTION) to the section; then on **EDIT**, **REC** and **PLAY**:
  the song starts at that section (REC's screen rings it) and records from its first downbeat. (Or, stopped,
  REC while free or on a progression puts the song on first.)
- **A second melody:** turn ALGORITHM to **M2**; recording goes on into it. M1's notes show dim under
  M2's, to play against.
- **Erase:** on EDIT, **SEL**'s list opens on **ERASE M1 IN SECTION 3** (the section playing, else the Song page's
  cursor); under it **ERASE M1** (the whole song's). Each asks first.
- **Copy a section's melody:** **COPY M1 TO SECTION 3** (the same section): **ALGORITHM** picks the section it
  comes from ("from 2"), **SEL** copies its M1 notes in place of section 3's (asking first when section 3 has
  some). They play as played over section 3's chords, moved with its root. One undo step.
- **Quantise:** **SEL**'s **QUANTISE M1 IN SECTION 3** moves that section's M1 notes to the nearest line of the
  Band page's grid (shown at the right: 16, 8T, ...). Record with the grid off to play freely, then quantise.
  HOME + REC undoes it in one step.
- **Sound:** the Sound page shows the melody's program and channel; its type is always "as played" over
  the whole keyboard (no Low, High or high key to set). **KNOB 4 Velocity** sets how hard the notes you record
  from then on are (the keys have no velocity; 100 at first). The Mixer and Mixer tone pages have its strip
  (black keys 9 and 10 pick M1, M2).
- **Mutes:** on the Song page, black keys 9 and 10 mute M1 and M2 in the cursor's section.
- **On Home**, in a song, M1 and M2 (each with notes) sit at the right of the song's line: plum, coral
  while a note sounds, an outline struck through while muted.
- REC while a progression plays, or free, says "MELODIES RECORD IN A SONG". Deleting a section that has melody
  notes asks first ("SECTION 3 HAS MELODY NOTES: DELETE?").
- Melody notes share the 16 places of memory: a song's melodies take one place (about 420 notes), a second
  when they outgrow it. "MEMORY FULL" or "MELODY FULL" means none is left: see [Memory](#memory).

## Sound

The sound pages show and change the groove playing, instrument by instrument: ALGORITHM picks the
instrument.

### Sound (EDIT)

![The Sound page](docs/img/manual/sound.png)

- **KNOB 1** the program: the 128 General MIDI sounds. **PRESETS** jumps a family of eight
  (pianos, organs, guitars, basses...). A drum instrument plays the drum kit.
- **KNOB 2** the type: how the instrument follows the chords.
- **KNOB 3, KNOB 4** the low and high limits. A note outside them moves by octaves until it fits.

| Type | For | What it does |
| --- | --- | --- |
| Bypass | Drums | Never changes |
| Parallel | Riffs | Moves with the chord's root, keeping its shape |
| Chord 1 | Chords, arpeggios | Each note moves to the matching note of the new chord |
| Chord 2 | Pads | As Chord 1, and each note stays close to the one before, for smooth voice leading |
| Bass | Bass lines | As Chord 1, and the first note after a chord change is the root (or the slash bass) |

### Sound 2 (EDIT, SELECT)

- **KNOB 1** high key (C to B, or off): the highest root before a Chord 1 or Bass line drops an octave,
  so a bass line doesn't climb out of reach as the chords rise.
- **KNOB 2** the MIDI channel, 1 to 16. Drums are on 10.

### Mixer (LFO)

![The Mixer](docs/img/manual/mixer.png)

Ten strips: the eight instruments, then the melodies M1 and M2. Black keys 1–10 pick the instrument. For the
selected one: **KNOB 1** volume, **KNOB 2** pan, **KNOB 3** reverb send,
**KNOB 4** chorus send. Instruments on the same channel share these (D1, D2 and PC are all on 10).

**Mute:** the Band page's KNOB 4 mutes or unmutes the selected instrument. A mute is the song's: the
instrument stays muted in every groove (and through fills), it's saved with the song, and each song has
its own. Muting never makes a groove yours.

### Mixer tone (LFO, SELECT)

![Mixer tone](docs/img/manual/mixer_tone.png)

**KNOB 1** Bright (the filter's cutoff) and **KNOB 2** Reso (its resonance) for the selected instrument. 64
is the sound as it is. Lower Bright is darker; higher Reso is more pronounced.

### Effects (FX)

![Effects](docs/img/manual/effects.png)

The effects all the instruments share, in every song: **KNOB 1** the reverb (Off, Large hall, Hall, Chamber,
Room), **KNOB 2** its level, **KNOB 3** the chorus (1 to 4), **KNOB 4** its level.


## Songs and saving

### Four songs

Polly keeps four songs. Each has its own sections, progressions, melodies, tempo, TRANSPOSE, style and mutes.

To change songs, hold **HOME** and turn **PRESETS**, on any page, or turn **SONG** on Song's page 3. While
playing, the new song waits until you stop ("SONG 2 AFTER THE STOP"): loading a song saves first, and saving
pauses the sound for a moment.

### Saving

- **SAVE** lights, and "unsaved" shows in the header, while something isn't saved.
- Press **SAVE** to save now: the songs, your grooves you've changed and the settings.
- Polly also saves by itself after about four seconds at rest: stopped, no key or button held, and
  the last notes faded.

Saving pauses the FM-1 for a moment, which is why it waits until you stop.

If two grooves of a song play the same copy of yours and both were changed, the newest change is kept,
and Polly says "SHARED GROOVE KEPT GROOVE B".

### Undo

Hold **HOME** and press **REC** to undo; **HOME** + **PLAY** redoes (neither records nor starts). The header
says what: "UNDO P9 CHORD", "UNDO SECTION 3", "UNDO TAKE M1", "UNDO PASS C1", "UNDO CLEAR PROG", "REDO C1 SOUND",
or "NOTHING TO UNDO". It covers the song you're on: its progressions (chords, length, name, loading, clearing,
copying), its sections and settings (style, tempo, TRANSPOSE, mutes, the melodies' sounds), its melody notes (a
take is one step; erasing, copying a section's), and its grooves as you change them (sounds, mixer, types, ERASE
INSTRUMENT, CLEAR GROOVE A, BACK TO STYLE, and each pass of a recording). A step is what you changed before a
pause of about a second, a whole melody take, or one pass of an instrument's recording. Polly keeps up to 32 steps
(fewer after big ones) while it's on; switching songs or turning it off starts afresh. It works while playing.
Not undone: the tempo turned while playing, and the Setup and Effects pages. What undo brings back is saved like
any change.

### Memory

![Memory](docs/img/manual/memory.png)

The 16 places of memory hold your grooves (yours), the songs' melodies and each song's own progressions (once
they differ from the factory's). When they are full, Polly says **MEMORY FULL** and the footer says **SEL:
MEMORY**: press SEL then, or pick **MEMORY** at the end of SEL's list on SEQ or EDIT, to see what fills them, a
place a line, numbered: "Pop · Groove A (yours) · S1" (a groove of yours, the groove the song plays it as, and the
songs playing it), "S2 melodies 1/2", "S1 progressions", "free". The header counts the free ones. The numbers are
the white keys SAVE copies a groove to on SEQ ("COPIED TO MEMORY 7").

On a place, **SEL** frees it (stopped; it asks first): a groove of yours goes back to the style in the songs that
play it (its notes are erased, and that can't be undone), a song's melody notes are erased, a song's progressions
go back to the factory's ("S1'S PROGRESSIONS BACK TO FACTORY?"). HOME goes back. A change MEMORY FULL left playing
but not kept (a groove's sound, a recording, the song's progressions) takes the place freed at once ("GROOVE A
KEPT IN MEMORY 14").

### The song's actions (SEL)

On the Song pages, **SEL**'s list opens on **PLAY FROM SECTION** (the section the cursor is on), then **INSERT
SECTION**, **DELETE SECTION**, **CLEAR SONG** and **COPY SONG TO**. A section's melody notes go with it (DELETE
SECTION asks first); CLEAR SONG erases them; COPY SONG TO doesn't copy them.

### Backup

Polly's songs can be kept in a file on your computer and put back, over USB. From this repository:

```sh
pip3 install mido python-rtmidi
python3 tools/polly_backup.py backup my-songs.plb
python3 tools/polly_backup.py restore my-songs.plb
```

A backup holds everything Polly keeps: the four songs, the 16 places of memory (your grooves, melodies and the
songs' progressions) and the settings (Setup, the channels' sounds, Effects). Not in it: the QY70 styles (install
them again from your ROM) and the button calibration. Polly plays on while it is backed up; stop first if you want
the changes it hasn't saved yet in the file too.

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
| KNOB 2 MIDI out | **on** (the default): the instruments go out over USB on their channels, with their sounds and mixer, and MIDI clock with start and stop while Polly keeps time. **off**: nothing goes out |
| KNOB 3 Lights | **on**: keys light as you play, and the other buttons glow. **notes**: keys light, no glow. **off**: neither |
| KNOB 4 Click | **rec** (the default): a woodblock on every beat while you record, high on the bar's first. **on**: whenever Polly plays. **off**: only the count-in |

The click is on MIDI channel 16 (it goes to MIDI out too), a little under the band, and the instruments' mutes
don't silence it. REC's count-in, the beats before a take starts at the next bar line (an instrument, or a new
progression on Home), always clicks, even with the click off.

The last line shows the QY70 bank: none, the number of styles, "damaged" or "old format".

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

The QY70 styles follow Polly's 16 on the Style knob (Home's KNOB 1, or the Style pages, ENV), marked "QY". They
play with their own bends, swells and filter moves, and change grooves and styles without a gap. A song plays one
QY70 style at a time: picking a groove from another one moves the song's other QY70 grooves with it.

### What doesn't carry over

- **Drum kits.** Polly has one General MIDI kit. The QY70's brush, jazz, room and analog kits all play
  it.
- **XG voices.** Variations play their General MIDI instrument, and sound-effect voices a General MIDI
  effect.
- **Pan on the drum channel.** The drum instruments share one pan and one set of effects.
- **Editing.** Recording into or changing a QY70 groove makes it yours, as with Polly's own
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
- **Safe mode:** if the FM-1 fails to start twice running, it starts in safe mode with only USB on, so the web installer
  can reach it. PLAY tries a normal start again.
- **Back to M-VAVE's firmware:** the web installer can put the original firmware back. M-VAVE's own
  updater, M-UPGRADE, works too.
- **Button calibration:** hold OCT− and OCT+ while switching on, then follow the screen. Only needed if
  buttons do the wrong thing.

Your songs and grooves stay through a Polly update. Putting M-VAVE's firmware back can wipe them: make a
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

ALGORITHM picks the instrument on every page (D1 … C4, then the melodies M1, M2). HOME held + PRESETS picks the song
on every page. SELECT turns a button's pages.

| Page | Button | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 | PRESETS |
| --- | --- | --- | --- | --- | --- | --- |
| Home | HOME | Style | Tempo | Transpose | Prog | The next progression |
| Song | HOME, SELECT | Section | Prog | Groove | Repeat | The next section |
| Song 2 | HOME, SELECT ×2 | Section | Transpose | | | The next section |
| Song 3 | HOME, SELECT ×3 | Intro | Ending | At end | Song | The next section |
| Progression | ARP | Root | Type | Bass | Step | The next progression |
| Progression 2 | ARP, SELECT | Length | Beats | Name | | The next progression |
| Band | SEQ | Grid | | | Mute | What the groove plays |
| Groove length | SEQ, SELECT | Bars | Beats | Letter | Character | What the groove plays |
| Sound | EDIT | Program | Type | Low (a melody: none) | High (a melody: Velocity) | Program family |
| Sound 2 | EDIT, SELECT | High key (a melody: none) | Channel | | | |
| Mixer | LFO | Volume | Pan | Reverb | Chorus | |
| Mixer tone | LFO, SELECT | Bright | Reso | | | |
| Effects | FX | Reverb | Reverb level | Chorus | Chorus level | |
| Style | ENV | Intro | Groove A | Groove B | Style | Style |
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
