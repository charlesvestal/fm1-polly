# 109 Keeping your work

Save, undo, switch songs, free Memory, and back up to your computer.

Starts where [108](108-making-it-sound-yours.md) ended: Polly stopped, with your grooves and melodies from 105 to 108 in Memory.

[![109 Keeping your work, the video (1 minute)](../video/109.jpg)](../video/109.mp4)

<table>
<tr><td width="38%" valign="top"><h3>1. Save</h3>Press <b>EDIT</b> and turn <b>KNOB 1</b> (Sound) one step. <b>SAVE</b> lights, and the header shows "unsaved".<br><br>Press <b>SAVE</b>. When Polly is stopped, it also saves by itself after about four seconds.</td><td><img src="img/109/1-unsaved.jpg" alt="A new sound for C1: SAVE lit, unsaved in the header" width="100%"></td></tr>
<tr><td width="38%" valign="top"><h3>2. Undo and redo</h3>Hold <b>HOME</b> and press <b>REC</b>. The sound goes back, and the header shows "UNDO C1 SOUND".<br><br>Hold <b>HOME</b> and press <b>PLAY</b> to redo it.</td><td><img src="img/109/2-undo.jpg" alt="HOME held, REC pressed: UNDO C1 SOUND" width="100%"></td></tr>
<tr><td width="38%" valign="top"><h3>3. Switch songs</h3>Hold <b>HOME</b> and turn <b>PRESETS</b>. The header shows the song, S1 to S4. Turn it back to S1.<br><br>Each song has its own sections, progressions, melodies, tempo and style.</td><td><img src="img/109/3-song.jpg" alt="HOME held, PRESETS turned: SONG S2" width="100%"></td></tr>
<tr><td width="38%" valign="top"><h3>4. Open Memory</h3>Press <b>SEQ</b>, then <b>SEL</b>. Turn <b>SELECT</b> to MEMORY, the last row, and press <b>SEL</b>.<br><br>Memory's 16 places keep your grooves (yours) and melodies, for all four songs. The header shows how many are used.</td><td><img src="img/109/4-memory.jpg" alt="Memory: S1's Groove A (yours), S1's melodies and a groove no song plays, 3 of 16 used" width="100%"></td></tr>
<tr><td width="38%" valign="top"><h3>5. Free a place</h3>Turn <b>SELECT</b> to place 2. It holds your copy from 106, which no song plays. Press <b>SEL</b>, and Polly asks first. Press <b>SEL</b> again.<br><br>The place is free, and the copy's notes are gone.</td><td><img src="img/109/5-free.jpg" alt="Memory: FREE MEMORY 2? NOTES CLEARED" width="100%"></td></tr>
<tr><td width="38%" valign="top"><h3>6. Back up</h3>Stop Polly, and connect the FM-1 to your computer over USB. In a terminal, in Polly's repository, install the tools once, then run the backup.</td><td><pre><code>pip3 install mido python-rtmidi
python3 tools/polly_backup.py backup my-songs.plb</code></pre></td></tr>
<tr><td width="38%" valign="top"><h3>7. Put it back</h3>Stop Polly and run the restore. It replaces everything on Polly.<br><br>Type y, and when Polly shows "RESTORE FROM USB?", press <b>SEL</b>.</td><td><pre><code>python3 tools/polly_backup.py restore my-songs.plb</code></pre></td></tr>
</table>

## More in the manual

[Four songs](../../MANUAL.md#four-songs), [Saving](../../MANUAL.md#saving), [Undo](../../MANUAL.md#undo) (what it
covers), [Memory](../../MANUAL.md#memory) (what each place holds), [Backup](../../MANUAL.md#backup) (what a backup
holds, and restoring safely).

Next: [110 With other gear](110-with-other-gear.md)
