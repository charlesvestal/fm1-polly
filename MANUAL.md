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
- [Fixing notes](#fixing-notes)
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
- **Tempo**, **Transpose** (the song's, and a section's own on top of it; it moves everything, the keys you play
  too), **Repeat** (a section's), **Mute** (an instrument or a melody; a section's mutes last for that section).
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
| go to bar 1 beat 3 | **ARP**, turn **KNOB 1** (Bar) to 1, **KNOB 2** (Beat) to 3 (**OCT−** / **OCT+**: a beat back / on) |
| fix a chord | **ARP**, **KNOB 1** (Bar) and **KNOB 2** (Beat) to it, turn **KNOB 3** (Root) or **KNOB 4** (Type); its bass: **SELECT**, **KNOB 3** (Bass) |
| delete a chord | **ARP**, **KNOB 1** / **KNOB 2** to it, **SEL** (the list opens on CLEAR CHORD), **SEL** |
| lengthen a progression | **ARP**, **SELECT** (page 2), turn **KNOB 1** (Length) |
| add a section to the song | **HOME**, **SELECT** (the Song page), press white key *n*: a section playing P*n* |
| play from section 3 | on the Song page turn **KNOB 1** (Section) to 3, **SEL** (the list opens on PLAY FROM SECTION 3), **SEL** |
| change an instrument's sound | **EDIT**, turn **ALGORITHM** to the instrument, turn **KNOB 1** (Sound) |
| mute an instrument | **SEQ**, turn **ALGORITHM** to it, turn **KNOB 4** (Mute; again: it plays). In one section only: on the Song page, its black key |
| record a melody | on the Song page **PLAY**; **EDIT**, turn **ALGORITHM** past C4 to M1, **REC**, play, **REC** |
| fix one wrong note | **SEQ**, **SEQ** again (Notes); **ALGORITHM** to the instrument or melody; turn **SELECT** to the note (ringed coral), turn **KNOB 2** (Pitch); **EDIT** deletes it |
| undo / redo | hold **HOME**, press **REC** / **PLAY** |
| save | **SAVE** (Polly also saves by itself, stopped) |
| back up | on a computer: `python3 tools/polly_backup.py backup my-songs.plb` |

### The buttons

- **PLAY**: start, stop. On the Song page: the song.
- **REC**: record what the keys play on this page (chords, an instrument, a melody, sections).
- **SEL**: this page as a list, with its actions; in a question, yes.
- **HOME**: Home; **SELECT** then turns to the Song pages. Held: the shortcuts (keys, SELECT, PRESETS, REC, PLAY); in a
  question, no.
- **ARP**: Progression (the chords: bar, beat, root, type), page 2 its length, beats, bass and name.
- **SEQ**: Band (the groove's notes, instrument by instrument; bar, beat: the cursor), page 2 the groove's length and
  name.
- **EDIT**: Sound (the instrument's sound and range), page 2 its high key and channel (a melody: one page, its sound,
  channel and velocity).
- **LFO**: Mixer, page 2 its tone.
- **FX**: Effects (reverb and chorus).
- **ENV**: Style (which style each groove and extra plays).
- **GLO**: Setup, page 2 Status (the processor's load, the voices, the version).
- **SAVE**: save; held + a white key: copy here.
- **OCT− / OCT+**: the keys' octave (on Progression: the cursor, a beat back / on).
- **SELECT**: a button's next page; in a list, up and down. **ALGORITHM**: the instrument (or melody). **PRESETS**: the
  next one of what the page shows. **KNOB 1–4**: the four values at the bottom of the screen.

The two lines at the bottom of the screen name the controls as pills, the way the panel prints them, each followed by
what it does here: what turning SELECT, ALGORITHM (ALGO) and PRESETS (PRESET) changes, in round pills, "[SELECT] page ·
[ALGO] instrument · [PRESET] family"; then what the keys and buttons do, in square keycaps, "[KEYS] chords · [REC]
write".

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
| SEL | This page as a list, opened on its first action (LOAD PROGRESSION, PLAY FROM SECTION, CLEAR C1…); SELECT up to its values and MORE. The list covers the knobs; the footer says what SELECT, ALGORITHM, SEL and HOME do in it |
| HOME | Home; SELECT for the Song pages. Held: the shortcuts, on screen: + white key 1–16 a progression, + black key 1–5 a groove or an extra, + SELECT the tempo, + PRESETS the song, + REC undo, + PLAY redo |
| ARP | Progression: its chords; page 2 its length, beats, bass, name |
| SEQ | Band (the groove's notes), page 2 Groove length; on them SEQ again: Notes, the note editor (SEQ there: back to Band) |
| EDIT | Sound, page 2 Sound 2 (high key, channel); a melody: Sound alone |
| LFO | Mixer, page 2 Mixer tone |
| FX | Effects |
| ENV | Style: which style each groove and extra plays (two pages) |
| GLO | Setup, page 2 Status |
| SAVE | Save. Held + a white key: copy the progression (Home, Progression) or the groove (Band) there |
| SELECT | The button's next or previous page; in a list, up and down |
| ALGORITHM | The instrument, on any page: D1, D2, PC, BA, C1, C2, C3, C4, then the melodies M1, M2 |
| PRESETS | Depends on the page (Home, Progression: the next progression). With HOME held: the song |
| KNOB 1–4 | The four values at the bottom of the screen |
| OCT− / OCT+ | Move the keys down or up an octave, −3 to +3. On Progression: the cursor, a beat back / on |
| Keys | Chords on Home and Progression, the selected instrument on Band and Sound, the sections on Song |
| Any button held a second | A card says what it does on this page |

### Pages and dots

![Home while a progression plays](docs/img/manual/home.png)

Each page button lights while you're on one of its pages, and always opens its first page. Some buttons
have two pages (HOME three: Home and Song's two): turn **SELECT** for the next one. The dots over the
knobs show which one you're on.

The screen has these bands:

- **The header**: the page's name (on Home, the song: "Home S1"), the tempo on the right (not on Home, whose KNOB 2
  is the tempo; "ext" when MIDI clock drives it), a coral dot while recording, and "unsaved" when there's
  something to save. Nothing a knob already shows.
- **The middle**: what the page is about.
- **The tag line** over the knobs: where you are, from the song down, and nothing else: "S1 › Pop › Groove A ›
  C1 Acoustic Grand" (Band, Sound, Mixer), "S1 › P9 Verse" (Progression), "S1" (Song, and Home in the song),
  "S1 › free", "S1 › M1 Square Lead" (a melody). A long one loses its first steps. Where you are inside the page
  (the bar, the section) is said by the cursor, not here.
- **The knobs**: what KNOB 1–4 do here, with their values. The knob you turned last shows in coral. A page with
  no knobs (Status) has no strip.
- **The two bottom lines**: each control as a pill, like the panel's printed labels, and what it does here after it.
  First the encoders, in round pills: what turning SELECT, ALGORITHM (ALGO) and PRESETS (PRESET) changes: "[SELECT]
  page · [ALGO] instrument · [PRESET] section". SELECT turns the button's pages (the dots show which; nothing where a
  button has one page). ALGO picks the instrument (or melody) the keys play; the line says so where an instrument is
  on screen (Band, Groove length, Sound, the Mixer pages). PRESET steps through the progressions (Home, Progression),
  the sections (Song), the grooves (Band), the sound families (Sound) or the styles (Style). HOME held: "[SELECT]
  tempo · [PRESET] song".
  Then the keys and buttons, in square keycaps: what they do here ("[KEYS] chords · [REC] write · [OCT] cursor",
  "[KEYS] C1 · [REC] record", "[WHITE] progression · [BLACK] mutes"); while something records, "[REC] stop". Two held
  together show as "[HOME]+[KEYS] sections". Where the keys do nothing (Effects, Style, Setup, Status) there is only
  the encoders' part; where everything fits on one line, it's one line. The very first screen says "[REC] new prog ·
  hold a button: help". In a list only the list's line shows ("[SELECT] move · [SEL] do it · [HOME] back"), a
  question says "[SEL] yes · [HOME] no", and while a help card is up "let go: nothing changes".

**Messages** such as "SAVED" or "NEXT GROOVE B" take over the header for a second or so.

**The cursor and the playhead.** Where the knobs and keys write is outlined in thick coral: a chord's cell
(Progression, its bar and beat numbers on coral), a section (Song, "section 3 of 6" by it), a line down the
instruments (Band, "bar 2.3" on coral over it), a melody's section and its first bar (Sound with M1 or M2). What plays is filled teal (a
teal line on Band). Both show at once: a coral outline round a teal fill is the cursor on the beat playing.
Whatever you change flashes coral for half a second where it lands: a chord, a section, an instrument's row, a bar
of notes just recorded, and what undo or redo brings back. On Notes only the notes you changed flash (a small mark
on the song line), so the selected note's ring stays clear while you turn a knob.

**The colours** mean one thing each. **Teal**: sounding now (the bar, beat, section, progression playing; the
playhead) and nothing else. **Coral**: you (the cursor, an edit's flash, the knob you turned, REC, a chord you play
over a progression, questions and messages). **Ink** and its greys: everything that stays put (names, values, a
list's selection, the place PLAY starts: an ink ring). **Slate** and **amber**: Groove A and Groove B, on their pills;
the extras (Intro, Fills, Ending) are outlined pills. **Plum**: the melodies.

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

On Home, turn **KNOB 1**: the knob shows the style. Until you set the song's
tempo yourself, the tempo changes to the style's own. Once you've turned the tempo, it stays yours.
You can change the style while playing.

Each song has its own style, so each of the four songs can sound different.

**A groove you've changed stays yours.** Once you record into a groove (or an extra), or change its
sound, it plays [your own copy](#yours-your-own-copy) instead of the style's. Its pill says so, on Home and
Band: "Groove A (yours)"; the Style page names it ("Pop (yours)"). Turning the Style knob changes only what still
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

Held, Home shows the five (each under its black key's number) and the progressions with chords, big; the empty
ones share a line under them ("P9–P16 empty"). What plays now is teal (a
ring round a groove; a progression's number on teal, its name teal), what you asked for (from the next bar) is coral
(a ring, a number on coral), and, stopped, where PLAY starts is ink (a ring, a number on ink).

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

Turn **KNOB 2** on Home, or hold **HOME** and turn **SELECT** (held, the line over the footer says "SELECT tempo"): 40 to
240 BPM. The tempo is shown at the top right. When Polly follows MIDI clock, it shows "ext" instead.

## Chords

Polly's grooves are written as if the chord were C major 7. As it plays, it moves every note of every
instrument to fit the chord of the moment. There are three ways to give it chords: play them, play a
progression, or play a song.

### Playing chords

On Home and Progression the keys play chords.

- Hold three or more different notes. Polly names the chord (free: big on Home), and the band follows.
- The chord stays after you let go (fingered, as on an arranger keyboard). Play the next one whenever
  you like.
- Inversions work: E G C and G C E are both C. When the notes could be read two ways, the lowest note
  is taken as the root: A C E G is Am7, C E G A is C6, D F A C is Dm7.
- The lowest note is the bass. When it's the root, 3rd or 5th of the chord, you get that chord. Any other
  note at the bottom becomes a slash bass under the chord the notes above it make: D under C E G is
  C/D, F under C E G is C/F, B♭ under C E G is C/B♭. (For C7, put B♭ above the C: C E G B♭.)
- A chord may leave out its 5th: C E B♭ is C7 too.
- Two notes are not a chord: nothing changes.
- Over a progression, a chord you play lasts until the progression's next chord change. Home shows it at the top
  right, coral: yours.
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

**TRANSPOSE** (Home's KNOB 3) moves everything to another key, from −6 to +5 semitones, as on an arranger
keyboard: the progressions, the song, and what you play on the keys too (chords, an instrument's notes, a melody;
in a song, a section's own TRANSPOSE adds to it). Home's bars show the chords you hear. Chords you write and
notes you record are kept as you heard them: they play back the same, and move with TRANSPOSE when you turn it
later. MIDI in isn't transposed, and the keys' octave (**OCT−** / **OCT+**) is separate.

## Progressions and the song

See [Writing a song](#writing-a-song) for the whole path.

![The Progression page](docs/img/manual/progression.png)

- **Progression** (ARP): its bars, a chord a beat (a short one gets taller cells), and the cursor: the thick coral
  ring, its bar's and beat's numbers on coral. While playing, the beat playing is teal. KNOB 1–4: **BAR** and
  **BEAT** move the cursor (BAR keeps the beat; the grid scrolls to it; while
  REC writes, one bar past the end, where a chord makes the progression longer), **ROOT** and **TYPE** change
  the chord there (TYPE one below maj: "·", no chord there). **OCT−** / **OCT+** move the cursor a beat. Page 2:
  **LENGTH**, **BEATS** (a bar's; each chord stays in its bar: a chord on bar 2 beat 1 is still on bar 2 beat 1),
  **BASS** (the cursor's chord's), **NAME** (a preset name, or its own: "P9"). It shows the progression playing, or the last one picked; before
  you've picked one, the first empty one, so you never write over a factory one.
- **Writing at the cursor** (stopped, **REC**): each chord you play goes on the cursor, and the cursor moves on
  a **STEP**: a bar, unless you pick a beat or half a bar in **SEL**'s list.
- **SEL** on Progression: the knobs, the name's letters, then **CLEAR CHORD** (the list opens on it: the chord at
  the cursor goes, "·", one undo step), **STEP** (beat, half, bar: ALGORITHM picks), **LOAD PROGRESSION**,
  **CLEAR PROGRESSION**.
- **Shorter, then longer again:** made shorter (LENGTH, or fewer BEATS), the chords that no longer fit are
  hidden ("2 CHORDS HIDDEN"): not played, and back if you make it longer again while you're still on the
  Progression pages. Leave them and they're gone (never saved). A chord written past the end grows the
  progression into empty bars. Undo brings back a shortening, chords and all.
- **Home's KNOB 4, PROG**: "–" (free: the band follows your chords) or P1–P16 (that progression plays; while
  playing, from the next bar). In a song it shows "Song"; turning it leaves the song.
- **Song** (HOME, SELECT): the sections, top to bottom. A white key writes a section: on END it adds one, on a
  section it sets that section's progression. A section's row: its number, its progression ("P3 Fifties"), its
  groove (a slate A or amber B, "+ fill" for a fill into the next section), its repeat ("x2") and its own transpose
  ("+2"; nothing at 0). Under the rows, the cursor's section's mutes: D1 D2 PC BA, C1–C4, then the melodies M1 M2
  (plum), filled plays, an outline is muted; black keys 1–8 mute the section's instruments, 9 and 10 its melodies.
  KNOB 1–4 SECTION, PROG, GROOVE (A or B, "+ fill"), REPEAT; page 2 TRANSPOSE (the cursor's section's own, on top of
  the song's; **PRESETS** steps the sections), and the song's INTRO, ENDING, AT END. (The song itself: hold
  **HOME**, turn **PRESETS**.) Over the sections, "section 3 of 6" says where the cursor (the coral ring) is; on
  END, "a white key adds a section". While the song plays, the section playing is filled teal (inside the ring when
  the cursor follows it); during its Intro, a fill and its Ending the line over the sections says so ("Intro · then
  section 1", "Fill AB · into section 3", "Ending · then stop").
- **Home** shows the progression as bars, the same whether you listen or write chords: a cell a bar with its
  chords, the bar playing filled teal with its beats along its foot (in a bar of several chords, the one sounding
  bright, the others pale); stopped, bar 1 ringed in ink (PLAY starts there). Eight bars show at once, two rows of
  four; a longer one (the 12-bar blues) scrolls to the bar playing. Over them, the groove's pill (and NEXT with the
  one waiting). In a song the top line counts "section 2 of 4 · repeat 1 of 2", the bars are the section's
  progression, and the line under it says what comes next, with its first chord: "next: P4 Chorus (Am) · Groove
  B", "next: Fill · P4 Chorus (Am) · Groove B" (a fill in this section's last bar), "next: P3 Verse again", "next:
  Ending", "next: stop". Free, the chord is alone, big (teal while the band plays it), "groove bar 1.1 / 2" at the
  top right while playing, and "the band follows your chords" under it. While REC writes chords (or a new
  progression's take) the top line is "REC P9 · bar 3.2", "chords land on the nearest beat" until the first, and the
  chord you write flashes its bar coral.

![The Song page](docs/img/manual/song.png)

### The style's grooves and extras (ENV)

![The Style page](docs/img/manual/style.png)

Every song chooses what each of its grooves and extras plays. ENV shows Intro, Groove A and Groove B
(KNOB 1–3, on a panel, bold); SELECT turns to Fill AB, Fill BA and Ending. Each knob goes through your own
copies ("Funk (yours)"),
then the styles by name. So you can play Funk's Groove A with Rock's fills.

**KNOB 4** (and PRESETS) is the song's style, like Home's KNOB 1: it changes what plays a style, not what is
yours.

## Recording an instrument

Polly records into the selected instrument of the groove (or extra) playing, going round.

![Recording into C1](docs/img/manual/recording.png)

1. Press **SEQ** for the Band page. It shows the eight instruments of the groove playing, with their
   notes (in ink, a drum's hits as short ticks; a muted instrument's pale, its name struck through; one with no
   notes faded), the playhead (a teal line) and the cursor: a coral line down the instruments, its place on coral
   over it ("bar 2.3"). The selected instrument's row is darker, outlined. At the top left the groove's pill ("Groove
   A (yours)"; its length is on Groove length). **KNOB 2** (Bar) and
   **KNOB 3** (Beat) move the cursor; **KNOB 4** (Mute: muted, plays). To fix a note, press **SEQ** again:
   [Fixing notes](#fixing-notes).

   ![The Band page](docs/img/manual/band.png)

2. Turn **ALGORITHM** to pick the instrument. The keys play its sound.
3. Press **REC**. The coral dot shows in the header and PLAY turns red. The screen shows the groove as
   bars, the instrument's notes in each: "REC C1 · starts with PLAY". The footer says "[REC] stop".
4. Press **PLAY** and play. The groove goes round, and each pass adds to what's there (overdub). You hear
   a new note from the next pass. The lines say where you are: "REC C1 · pass 2 · adding", and under it
   "groove bar 1 / 2 · prog bar 3 / 4" (the groove's bar, and the progression's while one plays).
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
  Polly says "C1 FOLLOWS CHORDS" and the footer "[SEL] clear C1: plays as played". What you record
  there follows the chords too.
- **REC while playing** starts at the next bar line, wherever the groove is: the screen rings that bar and
  counts the beats down in it. A note played just before the bar line lands on it. The passes count from
  there: started at bar 3 of a 4-bar groove, a pass is bars 3, 4, 1, 2.
- **A groove shorter than the progression** (a style's 2-bar Groove A over a 4-bar progression): REC's footer
  says "[SEL] 4 bars, as the prog". Press **SEL** and the groove is as long as the progression,
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

- The page shows the groove's bars as a picture, a cell a bar, its number at the top and its beats along its
  foot; the bars it could still have (to 16) are outlines. Under them its name as a word, the letter PLACE points
  at in a coral box (the places still free after it: dots).
- **KNOB 1** bars, 1 to 16. **KNOB 2** beats a bar, 2 to 7. Longer, the new bars repeat the groove's,
  each instrument its own way (a 1-bar drum groove repeats bar 1, a 2-bar bass line bars 1 and 2), so the band
  goes on. Notes past a shortened length are kept, just not played; made longer again, they come back
  (nothing is repeated over them). While a progression plays, the groove's bar stays the progression's: turned
  at the progression's bar 3, a 4-bar groove plays its bar 3. The Band page shows a groove longer than 8 bars by
  halves, "bars 9-16 of 16", following the playhead.
- **KNOB 3** (Place) picks a letter of the name, **KNOB 4** (Letter) changes it. Names are eight letters,
  digits, "-" and "#".

### The groove's actions (SEL)

On the Band, Sound and Mixer pages, **SEL**'s list opens on its actions: **CLEAR C1** (the selected instrument's
notes), **CLEAR GROOVE A** (or the extra playing) and **BACK TO STYLE**, for the groove playing (and on SEQ and
EDIT, **MEMORY**; **BARS AS THE PROG** first when the groove is shorter than the progression). With a melody
selected they are the melody's: **CLEAR M1 IN SECTION n**, **CLEAR M1**, **QUANTISE M1 IN SECTION n** and **COPY M1
TO SECTION n**. (Effects is every song's: its list is its knobs.) To copy the groove into a place of memory, hold **SAVE** and press a white key on SEQ.

## Melodies

Each song has two melodies, **M1** and **M2**, for lines that run along the whole song instead of
going round with a groove. They play only in a song (Song on Home's PROG knob, or PLAY on the Song
page). A note belongs to its section, and it is kept as you heard it with the chord's root under it: when
you change that section's chord or TRANSPOSE, or the song's, the melody moves with the root and keeps its
shape (over the same chord at the same TRANSPOSE it plays exactly as heard).

**Record a melody over your song:**

1. On the Song page, press **PLAY**.
2. Press **EDIT** and turn **ALGORITHM** past C4 to **M1**. The keys play its sound (a square lead; M2
   is strings).
3. Press **REC**. The click counts in to the next bar line ("REC M1 · starts at the next bar", the beats
   left big in that bar); during the Intro it waits for section 1. Play along. Under "REC M1" the screen says where the
   next note lands, "song bar 6 · Chorus bar 2.1", and shows the song as one line with your notes landing
   on it (see *Where it is* below).
4. Press **REC** again to stop recording. The song plays on. Stopping the song stops recording too
   ("RECORD OFF SONG STOPPED"), so the next PLAY doesn't record over the song.

Notes go into the section playing, at their place on the Band page's grid. You hear them the next time
the section plays. Recording again over a section (a punch in) adds notes to what's there; to replace a
section's melody, clear it first (**SEL**: the list opens on **CLEAR M1 IN SECTION n**), then record.

- **Where it is:** with M1 or M2 picked, the Sound page shows the song as one line, whether it plays or not:
  - **The whole song** across the top, its sections end to end, each as wide as its bars (REPEAT counted),
    named after its progression ("Verse", "Chorus x2"; shortened when narrow, and a run of the same progression
    named once), slate for Groove A, amber for Groove B. The melody's notes are plum ticks along it (the other
    melody's pale), and while the song plays a teal line runs through it. The section you're in is outlined: teal
    while it plays, coral for the Song page's cursor when stopped. A short line under it marks the four bars shown
    below.
  - **Four bars** under it, running on from section to section: the bar before the place, the place and two
    after. A section's first bar carries its name and a divider; the others their number in the section. The
    notes are placed by time and pitch (the other melody's pale), the bar playing is teal with its beats, notes
    just recorded flash their bar coral.
  - **One readout** over them: "song bar 9 · Chorus bar 1". Stopped, it's the Song page's cursor section's
    first bar, ringed coral: where PLAY FROM SECTION and REC's punch in start. Playing, the bar and beat in teal
    ("Chorus bar 3.2"); recording, where the next note lands, in coral.

  ![A melody recording along the song](docs/img/manual/melody.png)
- **Punch in:** on the Song page, move the cursor (SECTION) to the section; then on **EDIT**, **REC** and **PLAY**:
  the song starts at that section (REC's screen rings it) and records from its first downbeat. (Or, stopped,
  REC while free or on a progression puts the song on first.)
- **A second melody:** turn ALGORITHM to **M2**; recording goes on into it. M1's notes show dim under
  M2's, to play against.
- **Clear:** on EDIT, **SEL**'s list opens on **CLEAR M1 IN SECTION 3** (the section playing, else the Song page's
  cursor); under it **CLEAR M1** (the whole song's). Each asks first.
- **Copy a section's melody:** **COPY M1 TO SECTION 3** (the same section): **ALGORITHM** picks the section it
  comes from ("from 2"), **SEL** copies its M1 notes in place of section 3's (asking first when section 3 has
  some). They play as played over section 3's chords, moved with its root. One undo step.
- **Quantise:** **SEL**'s **QUANTISE M1 IN SECTION 3** moves that section's M1 notes to the nearest line of the
  Band page's grid (shown at the right: 16, 8T, ...). Record with the grid off to play freely, then quantise.
  HOME + REC undoes it in one step.
- **Sound:** a melody's Sound is one page, its sound's family at the top ("M1 Synth Lead", plum). Turning
  **KNOB 1** (or **PRESETS**, a family) shows the family's eight sounds there for a moment, the sound ringed, as for
  an instrument. **KNOB 1** its sound, **KNOB 2** its channel, **KNOB 4 Velocity** how
  hard the notes you record from then on are (the keys have no velocity; 100 at first). It always plays "as
  played" over the whole keyboard: no type, range or high key to set. The Mixer and Mixer tone pages have its strip
  (black keys 9 and 10 pick M1, M2).
- **Mutes:** on the Song page, black keys 9 and 10 mute M1 and M2 in the cursor's section.
- **On Home**, in a song, M1 and M2 (each with notes) sit at the right of the "next:" line: plum, teal while a
  note sounds, an outline struck through while muted.
- REC while a progression plays, or free, says "MELODIES RECORD IN A SONG". Deleting a section that has melody
  notes asks first ("SECTION 3 HAS MELODY NOTES: DELETE?").
- Melody notes share the 16 places of memory: a song's melodies take one place (about 420 notes), a second
  when they outgrow it. "MEMORY FULL" or "MELODY FULL" means none is left: see [Memory](#memory).

## Fixing notes

One wrong note doesn't mean recording again. **Notes** is a note editor for the selected instrument's notes in the
groove playing, or a melody's along the song: press **SEQ** for Band, then **SEQ** again. (**SEQ** there goes back
to Band; any other page button leaves it.) **ALGORITHM** picks whose notes, as everywhere: D1 … C4, M1, M2.

![Notes with a melody: E4 made F4, longer and softer](docs/img/manual/notes.png)

The screen shows the notes as a piano roll: by time from left to right and by pitch from bottom to top, a melody's
plum (the other melody's pale under them), an instrument's ink, drums as short ticks. A melody's song line runs over
it, the stretch shown marked under it. Bars carry their number, a section's first bar its name.

- **The cursor** is a coral line at a place on the grid (Band's **GRID**: 8, 8T, 16, 16T, 32; off: a 32nd), its step
  capped coral along the foot. **The selected note** is ringed coral. **The playhead**, while playing, is teal.
- **The readout** over it says where the cursor is and the note: "M1 · song bar 1 · Verse bar 1.1 · F4", or
  "D1 · bar 1.1 · Pedal hat" (drums are named by their sound).
- **SELECT** goes from note to note in time order, the notes of a chord one by one (lowest first). The cursor
  follows.
- **KNOB 1 (Place)** moves the cursor a grid step a detent, through empty places too ("1.1.2": bar 1, beat 1, its
  second step). The first note starting in that step is selected; none: an empty place.
- **KNOB 2 (Pitch)** changes the selected note's pitch (a drum's sound: Snare, Closed hat …). **KNOB 3 (Length)** its
  length, a grid step a detent, in beats ("1/2", "1 1/2"); hold **SEL** for fine steps. **KNOB 4 (Velocity)** how hard
  it plays. Over an empty place they show "–".
- **Move:** hold **SEL** and turn **SELECT** (or **KNOB 1**): the selected note moves a grid step a detent, keeping its
  length and velocity. A melody's note moved past the end of its section goes into the next section, and takes the
  root of the chord there, as if it had been recorded there.
- **Delete:** **EDIT** deletes the selected note.
- **Enter:** at an empty place, play a key: the note goes in there (a grid step long; a melody's with its velocity,
  and with the root of the chord sounding there, so it follows chord changes as recorded notes do). Keys held together
  go in as a chord. When you let go, the cursor moves a step on, so you can write a line note by note. On a note, the
  keys only play. An instrument's entered notes are written as over C major 7, the way the styles keep theirs.
- **PRESETS** zooms: 8, 4, 2 or 1 bars (an instrument: down to one beat). Two bars at first.
- **Erase while recording:** with **REC** on and playing, hold **EDIT**: the notes the playhead passes go, from the
  step it is in, until you let go. The readout says "ERASING". (Keys still record meanwhile.)
- **Undo:** every edit is one step: hold **HOME** and press **REC** (redo: **PLAY**). A recording pass with its
  erasing is one step too.
- **Your copy:** the first edit of a style's groove makes it yours first, as recording does ("GROOVE A NOW YOURS").
  A groove holds 600 notes: past that, "GROOVE FULL".
- While recording, Notes shows the recording: the window follows the playhead, and the keys record.

![Notes with the drums: a hit's sound named](docs/img/manual/notes_band.png)

## Sound

The sound pages show and change the groove playing, instrument by instrument: ALGORITHM picks the
instrument.

### Sound (EDIT)

![The Sound page](docs/img/manual/sound.png)

The page is a browser: the sound's General MIDI family of eight as its heading ("Chromatic"), the family's
sounds in two columns of four (Celesta to Dulcimer), the sound the instrument plays ringed coral, its General
MIDI number small at the right ("GM 9"). Turning KNOB 1, the ring moves through the family, so you see where
you are going. Under them, the range it plays in as a bar over the whole MIDI keyboard (its ends: the Low and
High knobs').

- **KNOB 1** the sound: the 128 General MIDI sounds, by name. **PRESETS** jumps a family of eight
  (pianos, organs, guitars, basses...). A drum instrument plays the drum kit: the page shows "Drums" and the kit,
  no family, no range, no Low or High.
- **KNOB 2** the type: how the instrument follows the chords ("as played" once Polly has switched it so).
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
  so a bass line doesn't climb out of reach as the chords rise. The page draws it: an octave of keys, the high key
  coral, the roots above it shaded ("roots above F#: an octave down"). Drums have none.
- **KNOB 2** the MIDI channel, 1 to 16. Drums are on 10.

### Mixer (LFO)

![The Mixer](docs/img/manual/mixer.png)

Ten strips: the eight instruments, then the melodies M1 and M2 (plum), each its volume and pan; the selected one
darker, outlined, its volume big under it; a muted one pale. Black keys 1–10 pick the instrument. For the selected
one: **KNOB 1** volume, **KNOB 2** pan, **KNOB 3** reverb send, **KNOB 4** chorus send (every instrument's sends:
the Effects page). Instruments on the same channel share these (D1, D2 and PC are all on 10).

**Mute:** the Band page's KNOB 4 mutes or unmutes the selected instrument. A mute is the song's: the
instrument stays muted in every groove (and through fills), it's saved with the song, and each song has
its own. Muting never makes a groove yours.

### Mixer tone (LFO, SELECT)

![Mixer tone](docs/img/manual/mixer_tone.png)

**KNOB 1** Bright (the filter's cutoff, the strip's dark bar) and **KNOB 2** Reso (its resonance, the grey one) for
the selected instrument; its two numbers, big under its strip (64 is the sound as it is, the line across). Lower Bright is darker; higher Reso is more pronounced.

### Effects (FX)

![Effects](docs/img/manual/effects.png)

The effects all the instruments share, in every song: **KNOB 1** the reverb (Off, Large hall, Hall, Chamber,
Room), **KNOB 2** its level, **KNOB 3** the chorus (1 to 4), **KNOB 4** its level. The page shows how much each
instrument of the groove playing (and each melody) sends to them: a dark bar the reverb, a grey one the chorus
(the Mixer's KNOB 3 and 4 set them).


## Songs and saving

### Four songs

Polly keeps four songs. Each has its own sections, progressions, melodies, tempo, TRANSPOSE, style and mutes.

To change songs, hold **HOME** and turn **PRESETS**, on any page. While
playing, the new song waits until you stop ("SONG S2 AFTER THE STOP"): loading a song saves first, and saving
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
or "NOTHING TO UNDO", and what came back flashes coral: the Progression page's cursor goes to its chord, the Song
page's cursor to its section (a melody's notes: their section). It covers the song you're on: its progressions (chords, length, name, loading, clearing,
copying), its sections and settings (style, tempo, TRANSPOSE, mutes, the melodies' sounds), its melody notes (a
take is one step; clearing, copying a section's), and its grooves as you change them (sounds, mixer, types, CLEAR
C1, CLEAR GROOVE A, BACK TO STYLE, and each pass of a recording). A step is what you changed before a
pause of about a second, a whole melody take, or one pass of an instrument's recording. Polly keeps up to 32 steps
(fewer after big ones) while it's on; switching songs or turning it off starts afresh. It works while playing.
Not undone: the tempo turned while playing, and the Setup and Effects pages. What undo brings back is saved like
any change.

### Memory

![Memory](docs/img/manual/memory.png)

The 16 places of memory hold your grooves (yours), the songs' melodies and each song's own progressions (once
they differ from the factory's). When they are full, Polly says **MEMORY FULL** and the footer says **[SEL] memory:
free one**: press SEL then, or pick **MEMORY** at the end of SEL's list on SEQ or EDIT, to see what fills them, a
place a line, numbered, each the same way round, the songs first: "S1 · Groove A (yours) · Funk" (a groove of yours:
the songs playing it, what they play it as, its name), "no song · groove (yours)", "S2 · melodies 1/2", "S1 ·
progressions", "free". The list covers the whole screen; the header counts the free ones. The numbers are
the white keys SAVE copies a groove to on SEQ ("COPIED TO MEMORY 7").

On a place, **SEL** frees it (stopped; it asks first): a groove of yours goes back to the style in the songs that
play it (its notes are cleared, and that can't be undone), a song's melody notes are cleared, a song's progressions
go back to the factory's ("S1'S PROGRESSIONS BACK TO FACTORY?"). HOME goes back. A change MEMORY FULL left playing
but not kept (a groove's sound, a recording, the song's progressions) takes the place freed at once ("GROOVE A
KEPT IN MEMORY 14").

### The song's actions (SEL)

On the Song pages, **SEL**'s list opens on **PLAY FROM SECTION** (the section the cursor is on), then **INSERT
SECTION**, **DELETE SECTION**, **CLEAR SONG** and **COPY SONG TO**. A section's melody notes go with it (DELETE
SECTION asks first); CLEAR SONG clears them; COPY SONG TO ("S2") doesn't copy them.

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

The page says what the knob you turned last does, in words ("Click: a click on every beat."), over the QY70 bank's
line. The knobs:

| Knob | Sets |
| --- | --- |
| KNOB 1 Clock | **MIDI** (the default): Polly keeps its own time, and follows MIDI clock, start and stop whenever they come in. **internal**: it ignores MIDI clock |
| KNOB 2 MIDI out | **on** (the default): the instruments go out over USB on their channels, with their sounds and mixer, and MIDI clock with start and stop while Polly keeps time. **off**: nothing goes out |
| KNOB 3 Lights | **on**: keys light as you play, and the other buttons glow. **notes**: keys light, no glow. **off**: neither |
| KNOB 4 Click | **rec** (the default): a woodblock on every beat while you record, high on the bar's first. **on**: whenever Polly plays. **off**: only the count-in |

The click is on MIDI channel 16 (it goes to MIDI out too), a little under the band, and the instruments' mutes
don't silence it. REC's count-in, the beats before a take starts at the next bar line (an instrument, or a new
progression on Home), always clicks, even with the click off.

The last line shows the QY70 bank: none, the number of styles, "damaged" or "old format".

Turn SELECT for Status: the processor's load, the voices sounding and the version (no knobs).

**MIDI in** (USB and the TRS jack) plays Polly's sounds on all 16 channels, as a General MIDI sound
module, drums on channel 10. It plays alongside the arranger, not transposed (TRANSPOSE moves the FM-1's keys only). Controllers 74 (brightness) and 71
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
- **Melodee** by Kerem Kilic (Ellic Studio, keremimo; GPL-3.0, on Felucca too): the design of Notes, its piano roll and its way of selecting notes
  (SEQ > NOTES: SELECT walks the notes, the knobs edit, EDIT deletes or erases while held, a held button + SELECT moves).
  <https://github.com/keremimo/melodee>

Polly is inspired by the Yamaha QY70; it is not affiliated with Yamaha. "M-VAVE" and "FM-1" are
trademarks of their owners. Polly is GPL-3.0 (see LICENSING.md).

## Page by page

ALGORITHM picks the instrument on every page (D1 … C4, then the melodies M1, M2; the encoders' line says so where
one is on screen). HOME held + PRESETS picks the song
on every page. SELECT turns a button's pages.

| Page | Button | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 | PRESETS |
| --- | --- | --- | --- | --- | --- | --- |
| Home | HOME | Style | Tempo | Transpose | Prog | The next progression |
| Song | HOME, SELECT | Section | Prog | Groove | Repeat | The next section |
| Song 2 | HOME, SELECT ×2 | Transpose (the section's) | Intro | Ending | At end | The next section |
| Progression | ARP | Bar | Beat | Root | Type | The next progression |
| Progression 2 | ARP, SELECT | Length | Beats | Bass | Name | The next progression |
| Band | SEQ | Grid | Bar (the cursor) | Beat (the cursor) | Mute | What the groove plays |
| Groove length | SEQ, SELECT | Bars | Beats | Place | Letter | What the groove plays |
| Notes | SEQ, SEQ | Place (the cursor) | Pitch | Length | Velocity | Zoom |
| Sound | EDIT | Sound | Type (a melody: Channel) | Low (drums, a melody: none) | High (drums: none; a melody: Velocity) | Sound family |
| Sound 2 | EDIT, SELECT (not a melody) | High key (drums: none) | Channel | | | |
| Mixer | LFO | Volume | Pan | Reverb | Chorus | |
| Mixer tone | LFO, SELECT | Bright | Reso | | | |
| Effects | FX | Reverb | Level | Chorus | Level | |
| Style | ENV | Intro | Groove A | Groove B | Style | Style |
| Style 2 | ENV, SELECT | Fill AB | Fill BA | Ending | Style | Style |
| Setup | GLO | Clock | MIDI out | Lights | Click | |
| Status | GLO, SELECT | | | | | |

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
