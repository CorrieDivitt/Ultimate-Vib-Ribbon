# Vib-Ribbon behaviour spec

Version 2, 2026-11-18. What the original game does, written in our own words as the target for our own implementation. Sources: the revib and open-ribbon decompilations (read for behaviour, nothing copied) and measurements of the SCES-02873 disc.

Status of each section: **read** means taken from the decompiled code this session; **measured** means checked against the disc; **to do** means not yet read. Nothing here has been checked against the running game yet; that is task 1.6.

Units used throughout: a **sample** is 1/22,050 s. **16.16** means a fixed-point number with 16 fractional bits.

## 1. Clock and audio path (read)

- The game's only gameplay clock is the **music position in samples**, read once per video frame.
- The PlayStation plays the CD. The game does not read the disc's audio directly: it reads back what the sound chip captured, 256 stereo samples at 44.1 kHz at a time.
- Each capture becomes one **block** of 128 mono samples at 22,050 Hz: take every second stereo sample and average left and right, `(L + R) / 2` with C integer division (truncates toward zero).
- Blocks go into a delay line of **1,379 blocks** (8,000 ms rounded up to whole blocks = 176,512 samples, about 8.005 s). The delayed mono block is what the player hears. So on the original the music is heard in **mono at 22,050 Hz, 8 seconds after the game has analyzed it**.
- "Music position" means the position of the sound being heard: capture position minus the delay.
- Loudness meter: sum of absolute sample values per block, scaled down by 2^14, summed over a sliding 400 blocks (about 2.3 s) centred on the block; the game's sound-effect volume follows it (louder music, louder effects, never below 10%).

Consequences for the port: analysis can run on the decoded file directly, eight seconds is the look-ahead to reproduce, and we are free to play the music in full stereo quality as long as it is time-aligned with the mono stream the analysis saw.

## 2. Finding where obstacles go (read)

Input: the stream of 128-sample mono blocks, each with its position. Output: **part events** (time, road type, length, scale).

### 2.1 What is being looked for

At any moment the course script (section 3) supplies the current **cue**: a `period` (how long one road part lasts), a `select period` (the window in which one onset is chosen), a selector that will name the next road type (or none, meaning no obstacles now), and optionally a tempo effect.

On the original the cue is fetched **once per video frame** and used for every block that arrived during that frame (about 3.4 blocks at 50 frames per second). The queue between the audio interrupt and the game holds 31 blocks; if a frame takes longer than about 180 ms, blocks are dropped. This makes the original very slightly dependent on frame timing. Port decision to make in Phase 2: fetch the cue per block (deterministic) and accept tiny differences from the reference, or group blocks on a simulated 50 Hz frame.

### 2.2 Onset measure, per sample

Two running sums over the squared samples:

- **short**: the last 3 blocks plus the current one so far (history of 4, less one block), each square shifted right by 9;
- **long**: the last 31 blocks plus the current one so far (history of 32, less one block), each square shifted right by 12.

Both are updated per sample by adding the new sample's term and subtracting the term of the sample one history length back (silence before the track starts). The history and both sums are cleared when a track starts.

A sample is an onset candidate when `short > 999,999` and its ratio `short / long` beats the best seen so far in the current stretch. Ratios are compared by cross-multiplying in 64 bits, never by dividing.

### 2.3 Windows and picking

- Time is cut into windows, each as long as the `select period` of the current candidate if there is one, else of the current cue. A window boundary can fall inside a block.
- Within a window the sample with the highest ratio becomes the **candidate**, but only while the cue has a selector.
- When a window ends:
  - no pending cue: the candidate becomes the pending cue;
  - pending cue exists and the candidate is more than `2 × length` after it: place the pending cue, the candidate becomes pending;
  - otherwise, if the candidate's ratio is above 1.5 times the pending cue's ratio: the candidate replaces it;
  - otherwise the candidate is dropped.
- After that, if the pending cue is more than `2 × length` before the window boundary, it is placed.
- `length` is the cue's `period` multiplied by the tempo scale (section 3.4), taken when the candidate became pending.

### 2.4 Placing

Placing a cue emits a part event: time = the onset's sample position; road = the selector's next value (this is the moment the selector advances); length = the cue's period; scale = the tempo scale (16.16).

## 3. Course script, SYSTEM3.FSL (read, measured)

Layout is implemented in `core/src/course_script.cpp`. Meaning of the fields:

### 3.1 Tracks and how they are chosen

- Header lists: 6 **rounds** (tracks 0 to 5, the six songs in fixed order), 0 free-order songs, 1 **medley** (track 6, used for audio CDs).
- Game's own disc: round `n` uses track `n - 1`, script time starts at 0.
- Audio CD: every CD track uses the medley track, starting at an **offset** into its 74-minute script. The disc's tracks are put in a shuffled order seeded by the disc's ID (XOR 0xCBF633B1); each track's script span is as long as the track; the leftover script time is shared out as random gaps between them. This is why the same CD always gives the same courses.
- Script time `now` = clamp(position − track start, 0 …) + offset, capped at the last entry's time.
- Disc key: `((lead-out sector << 7) | last track number) ^ 0xCBF633B1`, from the CD's table of contents (the game disc uses 0). It is the shuffle's seed and also the distributions' script seed; the round passed to the distributions is the CD track number (capped at 100).
- Order and offsets: `order = random_picks(0, n−1, n, key)`; track `t` (from 1) takes slot `order[t−1]`, of length `whole seconds of the track × 22,050` (sectors / 75, truncated). The medley's end minus all lengths (at least 0) is shared out as `gaps = random_picks(0, rest, n, key)`, sorted; slot `s` starts at the sum of the earlier slots' lengths plus `gaps[s]`. Up to 100 tracks.
- `random_picks(lo, hi, count, seed)`: the seed is stepped 7 times first; then rounds of up to `hi − lo + 1` picks, each `lo + ((numbers left × next seed) >> 32)` counted over the numbers not yet taken in this round, so each round takes every number once. Implemented as `vib::random_picks` (course_gen.h).
- The player's own files (vibplay --music): taken as a one-track CD, the track from sector 150 and its length rounded up to whole sectors (588 frames each), so the key and offset depend only on the file's length.

### 3.2 Speed list (control segments)

Entry in force = the last one starting at or before `now`. For all but a track's final entry, `period` and `select period` ramp: `value + ((slope × (now − start)) >> 12)`. The final entry does not ramp. Both are then multiplied by a speed factor (1.0 in normal play) and floored at 1.

### 3.3 Part list (pattern segments)

Entry in force chooses the selector: none (no obstacles), a **fixed pattern** (its roads in order, round and round), or a **distribution**. Selectors are created the first time a track needs them and keep their place for the rest of the track.

Distribution draw: state starts at `seed_of_distribution × (100 × script_seed + round)`. Each draw: `state = state × 1664525 + 1013904223` (32-bit), `pick = ((total × state) >> 32) + 1`, result = the first of the ten roads whose cumulative weight is ≥ pick.

Road numbers: 0 block, 1 pit, 2 loop, 3 wave, then pairs 4 block+pit, 5 block+loop, 6 block+wave, 7 pit+loop, 8 pit+wave, 9 loop+wave.

### 3.4 Effect list (event segments)

Each entry, when script time reaches it, can send four things:

- **Tempo** (`tempo_length > 0`): for that long, each newly pending cue gets a length scale by pattern: 2 = 1.25 or 0.5 at random, equally likely; 3 = 1.25 (30%), 1/1.5 (40%), 0.5 (30%); 22 = 0.5 and 1.25 alternately; anything else = 1/1.5, 0.5, 1.25 in turn. Random patterns use the same generator, seeded with the entry's time.
- **Reverse** (`reverse_length > 0`): sent, and ignored by gameplay. Dead feature.
- **Screw** (`screw_length > 0`): parts whose onset falls in the span are flagged; their road pieces lean as the camera passes.
- **Camera** (`camera >= 0`): value = loops × 10 + camera id (section 7).

When a round starts partway into a script (audio CDs), effects still running at that point are sent, and the most recent camera.

### 3.5 Audio CD extras

For audio CDs in the normal courses, obstacles stop during seconds 52 to 59 of every minute of analyzed audio, and the camera changes when the heard audio reaches second 52. Mechanism: gameplay turns the analysis's selection off and on (the script hands out no selector while off), and the lost selector makes the analysis report quiet, as it does at the end of a song. This is what creates the regular breaks on custom music.

## 4. Gameplay (read)

### 4.1 Parts on the road

- A part event becomes a **part**: road, time, rate, screw flag. `rate = 1 / ((scale × length) >> 6)` as 16.16 (in speed courses the scale is ignored). Its time is shifted earlier by the video delay: two frame periods (double buffering) converted to samples, about 886 samples at 49.759 Hz.
- Every frame each part's **position** = `rate × ((time − now) >> 6)`: 1.0 means one part-length ahead of Vibri, 0 is at Vibri, negative is behind. Parts further back than −3 − 1.25 are discarded. The road is drawn out to 10 units; Vibri stands at 1.25.
- Events before the round's start or after its end are rejected.

### 4.2 Buttons

Block = L1 (or L2), pit = Up (or Down), loop = R1 (or R2), wave = Triangle (or Cross). A pair needs both buttons. Presses are edge-triggered and read once per frame.

### 4.3 Judging

- **Hit window:** a part can be pressed for while its position is in [−0.2, +0.2). Because position is measured in part-lengths, the window is 20% of the part's length either side: for a half-second part, 100 ms early to 100 ms late.
- A press only counts once Vibri's current move animation is at least half done.
- With a part in the window: pressed buttons accumulate on the part. Exactly its buttons = **hit**. Any wrong button = **miss** at once.
- With no part in the window: the press is a stray. Up to two strays before the next part are scored (section 4.5) and play a sound.
- A part that passes −0.2 without being hit is a **miss**.
- Round end: when the end time has passed and no part is in the window or just behind Vibri: clear. Game over is signalled the same way once the game-over flag is set.

### 4.4 Vibri's forms

Level 0–11; form = level ÷ 3: 0 Super (angel), 1 Peeloo (rabbit, the start: level 3), 2 Frog, 3 Snake (worm).

- 18 hits in a row while not in the best form: up one form, level = form × 3.
- A miss resets the run of hits. In the best form a single miss drops to rabbit. Otherwise every third miss raises the level by one; at level 11 the third miss is **game over**.
- The stage within a form (level mod 3) makes the whole scene jitter more.

### 4.5 Score

- Hit: `(base + stray bonus + chain) × multiplier`. Base 1, or 3 for a pair. Chain = hits in a row so far, capped at 10. Multiplier 2 in the best form, else 1.
- Stray bonus: each of up to two strays adds 1 if it matched the coming part (or repeated an earlier stray), 2 if it was a different road and the part is single, 4 if different and the part is a pair.
- A miss clears chain and strays.
- End bonus by final form: 30% (best), 20%, 15%, 0%.
- Default high scores: bronze and silver 6000/4000/2000; gold 5500/3500/1500; one CD track 6500/4500/2500; all tracks 50000/35000/20000.

### 4.6 Courses

Nine: bronze, silver, gold (two songs each from the game disc: disc tracks 1–2, 3–4, 5–6 of the audio tracks); one CD track; all CD tracks (shuffled); three speed courses; and an auto-play of a CD in order.

Speed courses: all parts arrive at one speed, which rises with points scored (square-root curve) from 1.5 toward 3.5, 5.0 or 6.2 beats per second over 210, 300 or 372 points; the cue period is fixed at one beat and the select period at three. Reaching top speed clears the course; the result is the time taken.

### 4.7 Demo and auto-play

Auto-play hits every part as it reaches Vibri. The title-screen demo does the same but misses on purpose every 5 to 18 parts.

## 5. Models and animation (read, measured)

- Only lines are drawn (see `core/include/vib/tmd.h`): lines as is, each quad as one line, triangles never. A black triangle marks "draw this object only while this triangle faces the camera": 124 objects on the disc. A second marker type (blue = 1, 63 objects) is recorded and never used.
- Object transform per animation frame: rotate (library "ZYX" order), then scale columns, then translate; equivalent to scale, rotate X, Y, Z, translate.
- Clips carry frames per second (no header: 60). Each video frame the clip advances by `rate × frame period × speed`. Position starts at 0.5; the frame shown is the whole part. No blending between frames. Non-looping clips stop on the last frame.
- **Jitter**, the game's signature wobble: every time an object is drawn, each vertex is moved by an independent random integer offset in [−range, +range) on each axis, from one shared random generator. Ranges: scene base = 10 + 14 × stage; road pieces 1.5 × base; missed pieces 5 × base; Vibri base, up to 9 × base while shaking after a miss; zero for everything while a part is in the window or just hit ("still"), except missed pieces. HUD models use 3, the evolution meter 10 (5 while playing).
- Vibri's move animations: one per road type (two variants for some), plus running. Animation speed follows the current part speed × 0.5.

## 6. Scene layout (read)

- Road axis is Z. One unit of part position = 600 model units. The road spans 10 units; drawing offset = −5 units, so Vibri (at 1.25) sits at Z = −2,250.
- Pieces are joined: each piece starts with a line from the previous piece's last (jittered) vertex, beginning from a point 40 units behind and ending 50 ahead.
- Screw lean: a piece ahead of Vibri is rolled about Z by an angle growing with its distance, sign by camera side.

## 7. Game camera (read)

Seven cameras in three groups: back (b01, b02), side (s01, s02), front (f01, f02, f03), and six transition clips between groups, each in a same-side and a switch-side version, played backwards when only the opposite direction exists. A camera event names the next camera and how many times its clip loops (clip speed scales so total time is constant). A "break" starts the transition to the next camera, switching sides. Clips can be mirrored left to right. Game-disc courses run camera clips at 1.3 speed, others at 1.0 (corrected 2026-11-19: the camera's "fast" flag is set for the game's own courses); in that mode a break always returns to side 0.

Field of view: see section 9c.

## 8. Video timing (read)

PAL: 50 Hz nominal, 49.759 Hz actual, 288 lines, width 512, double buffered. One game update per video frame.

## 9. Game flow (read)

States: **title** (menus), **alert** (asking for a disc), **starting** (track requested), **buffering** (course running, music not yet audible), **playing** (music audible).

- A round starts when the analysis sees the track start. Gameplay begins at once in the buffering state, so obstacles are already being placed; the state becomes playing when the heard position reaches the track start, 8 seconds later. Music volume is set to full on entering playing and to zero on returning to the title.
- The round's time limit shown by the time meter is the track's length in seconds (for all-tracks courses: the disc's length plus one second per track).
- Track end seen by the analysis: for the game's own courses, or the last track of a CD course, the round's end time is set; otherwise the next CD track starts playing.
- **Round clear:** score 0 counts as game over. In a two-song course, clearing the first song adds its score and end bonus to a running total and starts the second. After the last, the total (or seconds, for speed courses) is entered in the high scores and the clear screen shows.
- **Game over** shows the "again" screens. "Again" restarts from the song that was lost in the game's own courses, and from the start otherwise. Start button during play goes to the same screen.
- High scores: three per course, higher is better except speed courses, where fewer seconds is better. Options stored with them: sound effects on/off, vibration on/off; both default on.
- Screens: main; game-disc course select; CD course select; speed course select; CD change; track select; two disc alerts; CD check; options; two high-score screens; clear; again; again select.
- The title demo plays one song of the game's own courses.

Disc handling (lid open, disc check, asking for the game disc back) exists only because of the physical drive and has no equivalent in the port.

## 9a. Sound effects (read, measured)

- A sound is (program, tone) in the PSJ_SE bank; a sound number packs it as program × 16 + tone.
- Played at note = tone + 1, pitch from the tone's centre note and fine tune by the PlayStation library's note-to-pitch rule (44.1 kHz when note equals centre; one octave per 12 notes; fine tune in 128ths of a semitone, subtracted). Centre notes are 25 to 42, so effects play two or more octaves down: program 1's at 11,025 Hz. Volume = bank volume × program volume × tone volume × requested volume. Still to confirm by ear.
- Up to 24 voices. A sound can be queued to start when another voice finishes; key changes are applied once per video frame.
- Gameplay sounds: hitting a part: program 1, tone = road number (0–9), voice 3, full volume. Stray press: program 1, tone 11 (not scored), 12 (different road, single), 13 (different road, pair) or 15 (matching), voice 3. Miss: program 1 tone 10, voice 2. Form up: program 1 tone 14, voice 4. Round start: program 0 tone 11, voice 4.
- Sound-effect volume scales with music loudness, from 10% in silence to 100%.
- Results screens speak the score in Japanese from syllable clips laid out on a beat grid, and play short music sequences, each sound starting as the previous ends.

## 9b. HUD (read)

- **Score** is not shown as digits during play. It is a row of 7 marks, each one of 15 shapes, never increasing along the row; the score is the index of that row among all such rows, which caps it at 116,279. The marks circle on a looping animation whose speed rises with the current run of hits: 1 + 9 × (run / 18)².
- **Time meter**: one animation scrubbed from full to empty as the round's seconds run out.
- **Speed meter** (speed courses): dial, needle at current speed, mark at target speed.
- **Evolution meter**: shows the run of hits toward the next form; plays through on a form change.
- "Round 1 / Round 2", "Auto" and "Demo" signs are small animated line models with their own cameras, jitter 3.
- Text is drawn with a line font, one model object per character, jittered like everything else; `~` selects a character's accented form.

## 9c. Projection (read)

A camera key's `fov` is the **vertical field of view** on a 4:3 picture, 4096 = full turn. The original's projection distance works out to 192 / tan(fov / 2) on a 512-wide frame. Example: the first key of camera b01 has fov 499, about 43.9°.

## 9d. Read only at header level

`TitleDraw` (the drawing of each menu screen: which of its 15 models, 102 animations and 14 cameras play when, and the sounds each screen queues), `Font` and `NumberVoice` internals, `CdSys`, `InputSys`. These matter when we build menus (task 3.5); VR menus will be designed afresh, so they were not transcribed now. `Forms` is a small Lisp-like data reader the shipped game never uses.

## 10. Corrections to earlier notes

- The triangles are not fills that hide lines. Nothing is filled. They are facing markers (section 5).
- FSL "base speed" is the road-part period; "shadow period" is the onset selection window; the "unused duration" is a dead reverse effect; "flip" is the screw (lean) effect.
- The floating eye seen in the first viewer screenshot was an eye the original hides from that angle.
