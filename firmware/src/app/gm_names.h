/* SPDX-License-Identifier: GPL-3.0-only */
/* The General MIDI level 1 program names and families (the GM specification's names). */
#pragma once

static const char *const GM_PROGRAM[128] = {
    "Acoustic Grand", "Bright Piano", "Electric Grand", "Honky-tonk", "E.Piano 1", "E.Piano 2",
    "Harpsichord", "Clavinet", "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone",
    "Tubular Bells", "Dulcimer", "Drawbar Organ", "Perc. Organ", "Rock Organ", "Church Organ", "Reed Organ",
    "Accordion", "Harmonica", "Tango Accordion", "Nylon Guitar", "Steel Guitar", "Jazz Guitar",
    "Clean Guitar", "Muted Guitar", "Overdrive Gtr", "Distortion Gtr", "Gtr Harmonics", "Acoustic Bass",
    "Finger Bass", "Pick Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1",
    "Synth Bass 2", "Violin", "Viola", "Cello", "Contrabass", "Tremolo Str", "Pizzicato Str",
    "Orch. Harp", "Timpani", "Strings 1", "Strings 2", "Synth Str 1", "Synth Str 2", "Choir Aahs",
    "Voice Oohs", "Synth Voice", "Orch. Hit", "Trumpet", "Trombone", "Tuba", "Muted Trumpet",
    "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2", "Soprano Sax", "Alto Sax",
    "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet", "Piccolo", "Flute",
    "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina", "Square Lead",
    "Saw Lead", "Calliope Lead", "Chiff Lead", "Charang Lead", "Voice Lead", "Fifths Lead",
    "Bass + Lead", "New Age Pad", "Warm Pad", "Polysynth Pad", "Choir Pad", "Bowed Pad", "Metallic Pad",
    "Halo Pad", "Sweep Pad", "Rain", "Soundtrack", "Crystal", "Atmosphere", "Brightness", "Goblins",
    "Echoes", "Sci-fi", "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum",
    "Reverse Cymbal", "Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone", "Helicopter",
    "Applause", "Gunshot"};

static const char *const GM_FAMILY[16] = {"Piano", "Chromatic", "Organ", "Guitar", "Bass", "Strings",
                                          "Ensemble", "Brass", "Reed", "Pipe", "Synth Lead", "Synth Pad",
                                          "Synth FX", "Ethnic", "Percussive", "Sound FX"};
