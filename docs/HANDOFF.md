# Vib Ribbon VR: Handoff Document

Last updated 2026-11-19 (session 2). Read this first in any new session, then the tracker.

- Repository: https://github.com/CorrieDivitt/Vib-Ribbon-VR. Master copies of this file, the plan and the tracker source live in its `docs/` folder.

- Tracker (live status, command notes): https://claude.ai/artifact/42iBUS2Aiu81652cgnRM1k (artifact slug `188889c5-f4d7-4ae5-bc71-aeb8011a952b`; its data is the `const T = {...}` block at the top of the page script; source is `docs/tracker/index.html` in the repo. To update: edit that block, publish the file to the artifact with publish_files, commit)
- Plan: `docs/PLAN.md` in the repo, also project doc `claude/PLAN.md`

Confidence labels used below: **[measured]** = computed this session from the disc or repo files. **[source]** = stated by the linked project or page. **[summary]** = from an automated summary of a decompiled source file, not yet read line by line. **[inferred]** = my reasoning from measured data, needs confirming.

## 1. Where things stand

**Latest (2026-11-19):** Phase 2 under way. The course generator reproduces Polaroid exactly; see section 2b. Tasks 2.1 and 2.2 done, 2.3, 2.4 and 2.5 active, 1.6 active (owner recording the remaining songs; they will say when the files are complete). Gameplay rules are written (section 2c). All six songs compared (section 2d): 2.3 and 2.5 done; a repeat recording confirmed the original varies between its own runs on songs 5 and 6. Next: extend the recorder to log score and level so 2.4 can be checked, then 2.7. Owner decided courses vary like the original by default (section 2d, last bullets); fixed courses are an option (Q13). Phase 3 started: first playable desktop build `vibplay` (section 2e), waiting for the owner to try it (Q15). Scoring check passed (2.4 done, section 2c).

Phase 0 is signed off. Phase 1 is under way: 1.1 and 1.3 done, 1.2 waits only on the Vulkan SDK install. 1.4 is done: TMD, ANM, ANC, FSL and VH/VB loaders plus posing are in vibcore, and build on the owner's PC. TIM was dropped (D10: the only images are the Sony and NanaOn-Sha logos). 1.5 is in progress: `apps/viewer` (vibview) is written and compiles in Windows CI (SDL3 3.2.26 via FetchContent, Vulkan 1.0, shaders compiled by glslc with -mfmt=c into .inc files; CI installs Vulkan SDK 1.4.309.0 with jakoch/install-vulkan-sdk-action). It has never been run: the assistant's workspace has no Vulkan and may not execute downloaded shader-compiler binaries, so shaders were first validated by CI's glslc. First run on the owner's PC (2026-11-18) worked: RTX 4090 selected, no validation messages, Vibri upright with limbs attached, smooth lines, animation speed looks sensible with the header rate treated as frames per second. One open observation: from an oblique front view one eye appears edge-on and apart from the head. The owner thinks it is the original's look; unverified. The game's own camera looks from the side (road pieces lie in the YZ plane) and hides back faces, so check eye placement against the emulator in task 1.6 before changing anything. Task 1.5 is marked done. Renderer design: lines are instanced screen-space quads with a feathered edge and a small depth pull toward the camera; fills draw first with depth write, no culling (the original culls back faces, e.g. Vibri's eyes; revisit); validation layer is enabled when present and prints `[vulkan]` lines to stderr; prefers the discrete GPU. Gotcha seen in session 2: a new Developer PowerShell starts 32-bit; building from it compiles but fails at the link step. Run Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64 first. Task 1.2 is done: Vulkan SDK 1.4.363.1 at C:\VulkanSDK confirmed working (VULKAN_SDK set, glslc from shaderc v2026.4).

Owner decisions, 2026-11-18:

- Architecture approved (native port, D1).
- Hardware: Quest 3 only. Steam Frame is a stretch goal; keep the architecture compatible (rules in the plan).
- Levels from your own music are a stretch goal. Failure there is not a blocker. The analysis algorithm is still core, because the official courses appear to come from the same analysis.
- Free public release. Repo: CorrieDivitt/Vib-Ribbon-VR.

What exists in the repo (first commit, session 2):

- `core/` (`vibcore` static library, C++20): `bytes.h` (little-endian readers, `path_from_utf8`), `sha1`, `cue` (CUE parsing, Redump multi-file and single-file layouts, raw 2352 only), `disc_image` (data sectors with Mode 1 / Mode 2 Form 1 / Form 2, audio tracks as int16 stereo PCM, data track SHA-1), `iso9660`, `pak` (reader and a test-only builder), `known_discs` (SCES-02873 hash).
- `tools/vibtool`: `info`, `extract`, `audio`.
- `tests/`: no-dependency harness, 8 tests. The disc test runs only when `VIB_TEST_CUE` is set.
- CI: `.github/workflows/ci.yml`, Windows (MSVC) and Ubuntu, warnings as errors.
- Verified on Linux (GCC 13, Clang) against the real disc: all 8 tests pass; extracted MAIN_G.EXE and SCES_028.73 have the same SHA-1 as the session 1 Python extraction; WAV output is 44.1 kHz stereo with the expected lengths. GitHub Actions CI also builds and tests it with MSVC on Windows and GCC on Ubuntu, warnings as errors: first run passed. Built on the owner's PC (MSVC x64, Ninja, `dev` preset) on 2026-11-18: all 8 tests pass against the disc and `vibtool info` output matches the Linux run exactly. Repo is cloned at `F:\VibRibbonVR\Vib-Ribbon-VR`.

Owner's toolchain (from their output, session 2): git 2.56.0, CMake 4.4.4, Ninja 1.12.1, Python 3.15.2, adb 37.0.1 in `%LOCALAPPDATA%\Android\Sdk\platform-tools`, Vulkan 1.4.357 instance on Windows 10 19045. GPUs: NVIDIA RTX 4090 (driver 621.24) and an AMD Radeon integrated GPU. Overlay layers from RTSS and OBS print harmless loader warnings. Second check (session 2): MSVC 14.44.35207 from VS 2022 Build Tools found, but the Developer PowerShell defaults to the x86 toolset; run `Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64` first. Android NDK 27.3.13750724 and 28.2.13676358 installed. **Vulkan SDK not installed** (no VULKAN_SDK, no glslc); owner to install from LunarG before task 1.5.

Licence position: the project's own code licence is not chosen yet (asked as Q4, MIT suggested). D9 confirmed by the owner: we write our own code and copy nothing from revib or open-ribbon. Reason given to the owner: a decompilation reconstructs Sony's program, so its author's permission would not cover copied code; extra effort is mostly in Phase 2 and the menus.

Folder map on the PC:

```
F:\VibRibbonVR\
  Vib-Ribbon-VR\       the git repo (once the owner clones it)
  docs\, tools\        session 1 copies, superseded by the repo
  research\
    Gamefiles\         the owner's disc image: 1 CUE + 8 BIN        (never commit)
    extracted\disc\    files pulled from the data track             (never commit)
    extracted\pak\     GAME_01, GAME_02, TITLE, KIOSK_02 unpacked   (never commit)
    vib-ribbon-recomp-main\   zip snapshot of Adoregabriel2005's repo (submodule folders are empty)
```

## 2. The disc [measured]

- European release SCES-02873. Data track: 15,680,784 bytes, 6,667 sectors, MODE2/2352, SHA-1 `a2ba376167bda9ed03ab89a11ade12410d75ede7`. This matches the hash the recomp project expects.
- 8 tracks: 1 data plus 7 audio. Audio lengths after pregap: T2 2:36.96, T3 2:31.59, T4 2:30.00, T5 2:32.76, T6 2:44.12, T7 2:30.19, T8 3:00.88. The course script lists six songs, so one audio track is something else. Which track is which song is not yet mapped.
- Executables, all `PS-X EXE`, stack 0x801FFFF0:

| File | Size | Load | Entry | Role [source: open-ribbon, revib] |
|---|---|---|---|---|
| SCES_028.73 | 28,672 | 0x80010000 | 0x80011F2C | Loader. Runs the others in turn and carries parameters between them. |
| MAIN_T.EXE | 196,608 | 0x80019000 | 0x8002BF78 | Title screens, logos, language and mode menus |
| MAIN_K.EXE | 184,320 | 0x80019000 | 0x800294B4 | Kiosk, the how-to-play level |
| MAIN_G.EXE | 385,024 | 0x80019000 | 0x8005405C | Main menu and gameplay |

- In MAIN_G the game's own code runs from 0x800219E4 to 0x8005405C, about 206 KB of MIPS; the rest is Sony's libraries [source: revib translation unit table, arithmetic mine].
- Data files: `GAME\{01,02,04,08,10,20}_FILES.PAK` (one per language: Japanese, English, German, Spanish, French, Italian), `KIOSK\` same six, `TITLE\FILES.PAK`, and three sound banks (`PSJ_SE`, `TITLE`, `KIOSK` as `.VH` + `.VB`).
- `GAME\02_FILES.PAK` holds 242 files: 31 TMD models, 171 ANM animations, 38 ANC cameras, 1 FSL course script, 1 stray `MAKEPAK.PIF`. Folders: `CHARA\{FROG,PEELOO,SNAKE,SUPER}`, `FONT`, `MESSAGE`, `METERS`, `ROAD`, `SCRIPT`, `TITLE\...`.
- Models: across those 31 TMDs (main game archive only, title and kiosk archives not counted) there are 6,489 line primitives (mode 0x40) and 19 flat triangles (mode 0x21). The four character models have 11 to 16 objects and 80 to 156 vertices each.
- Depth: character vertices span all three axes (PEELOO: x -91..203, y -288..278, z -292..151). `ROAD.TMD` has x = 0 for every vertex, so road pieces are flat profiles in one plane. The game is a side-on diorama with real depth in the characters.
- Animation headers: the second header field takes the values 10, 15, 20, 30 and 60 across ANM and ANC files (plus one ANC with 1). Likely the clip's playback rate [inferred].
- Course script `SYSTEM3.FSL` is 26,920 bytes. Header counts: 7 control tracks, 7 pattern tracks, 7 event tracks, 7 track-index entries, 11 fixed patterns, 63 distribution patterns. This matches the open-ribbon format page.
- **Time unit [inferred, strong]:** the custom-CD control track has 774 segments and its last start time is 97,902,000. Divided by 22,050 that is exactly 4,440 seconds, 74:00, the length of a standard audio CD. Speed values such as 11,025 and 4,409 are then 0.5 s and 0.2 s. So course-script time is audio samples at 22,050 Hz.
- Source file names recovered from assert strings in MAIN_G: AnalysisScript, AudioStream, AudioSys, CdSys, FastAllocator, FbStream, FileSys, FontHack, Forms, GameCamera, Gameplay, Glue, InputControl, InputSys, MemorySys, Message, PackedFiles, RoadSelect, SceneDraw, SignalModule, StaticQueue.h, Thread, TitleDraw, TmdLineObj, VideoSys. Build path `C:/psj/dev/locale/game/`.

## 0. Read docs/SPEC.md

`docs/SPEC.md` (session 2) is the behaviour spec written from reading the revib sources: clock, 8 s look-ahead, onset detection, course script semantics, judging, forms, scoring, courses, jitter, scene layout, camera. It supersedes the engine notes in section 3 below wherever they differ. Version 2 added game flow, sound effects, HUD and projection; task 1.7 is done. Menu drawing internals (TitleDraw, 90 KB of source) were read at header level only and are deferred to task 3.5. Sound-effect playback pitch is described but not verified by ear. Courtesy note to revib's author: the owner decided (session 2) not to send it until the project is past the prototype stage; it is now task 6.5. Draft kept at the end of this document.

Tracker notes: the tracker page has a "Your notes" box near the top for the owner. It is stored in the artifact's database (capability `db`, document `tracker/notes`, field `text`), not in the page source. Read it at the start of a session with the artifact db tools (db_get, slug above, path `tracker/notes`). Never overwrite it.

Reference rig (1.6): the owner installed PCSX-Redux; the game freezes at the first loading screen. Leading suspicion: OpenBIOS not handling the game's thread switch inside an interrupt (the recomp needed a runtime patch for exactly that). Update: still freezes with a retail PAL BIOS, so OpenBIOS was not the cause (the owner had the wrong BIOS first, then a correct one). Cause unknown; no log seen yet. DuckStation runs the game well with the owner's PAL BIOS. The PCSX-Redux freeze was at the LOADING screen after language select, i.e. while starting MAIN_G, the only program that uses the sound-chip capture and reverb trick; likely an emulation gap there, not investigated further. Recorder: `tools/reference/capture_events.py` speaks the GDB remote protocol to DuckStation (default port 1234), breaks at Gameplay::AddEvent (0x800466D0) and Gameplay::Start (0x8004549C), checks 16 code bytes against the owner's MAIN_G.EXE before trusting a stop (MAIN_T and MAIN_K load at the same base), and appends JSON lines: type, time, value, length, scale, script start, offset, seed. Works against real DuckStation (owner's run, 2026-11-19): hardware breakpoints (Z1) accepted, register and memory packets as assumed, game stays smooth. First recording `polaroid.jsonl`: 75 parts over 154.6 s, first part 0.51 s after track start, all scales 1.0, script seed 0, smallest gap between parts 2.02 part-lengths (matches the spec's more-than-twice-the-length rule), part length starts at 13,229 (the script's first period). Event times are absolute stream positions; subtract `script_start`. Round start reported 159 s for the first song. Script effects (cameras) are logged when script time passes them, parts only once placed, so the file is not sorted by time. `--keep-alive` writes Vibri's level back to 3 at every event (address 0x800764CC) so game over cannot cut a recording; untested on the real emulator. Recordings go to `F:\VibRibbonVR\research\reference\`, ignored by git. ePSXe was suggested by the owner but has no debugger, so it can only confirm the disc runs.

Corrections it forced, already applied in code:

- TMD triangles are **facing markers**, not fills. Nothing in the game is filled. Quads are drawn as one line (first vertex to last). `TmdObject` now has `marker` and `marker_vertices`; `fills` is gone. The earlier note that "every fill is black" was wrong: triangles are (0,0,0) ×124 and (0,0,1) ×63, quads are white ×6.
- vibview no longer mirrors the model (it used to flip Y, which mirrored depth). It uses PlayStation axes with −Y as up, and hides marker objects with the original's screen-winding rule. Owner confirmed on their PC that the eyes now disappear correctly. The fill pipeline is still in the renderer, unused.
- FSL field names changed to their real meaning: `period`, `select_period`, slopes; `tempo_length`, `tempo_pattern`, `reverse_length` (dead), `screw_length`, `camera`; header lists are rounds / songs / medleys.
- Animation rate is frames per second (confirmed). Rotation order confirmed as scale, X, Y, Z, translate.

Licence: MIT for our code (owner's choice), `LICENSE` in the repo root.

How the reading was done: revib cloned to a temporary folder outside the repo, read, and described in our own words. Nothing was copied.

## 2b. Course generator and first comparison with the original (session 2, 2026-11-19) [measured]

Phase 2 has started. `core/include/vib/course_gen.h` and `core/src/course_gen.cpp` implement SPEC sections 1 to 3: `make_blocks` (44.1 kHz stereo to 22.05 kHz mono blocks of 128), `CourseGenerator` (onset detection, peak picking, course script stepping, selectors, tempo effect, the random generator). `vibtool course <cue> <round> [cue interval]` prints the events as JSON lines. `tools/reference/compare_events.py ours.jsonl recording.jsonl [n]` compares with a recording (n = which round inside the file, by order of first appearance).

Results for Polaroid (round 1, CD track 2), our code fed straight from the disc image, script consulted every block:

- Against `polaroid.jsonl`: 75 of 75 obstacles, every time identical after a constant shift of 168 samples, every type and tempo scale identical. Part lengths differ by up to 4 samples on 28 of 75, only where the period is ramping.
- Against the second run (the owner saved it in `bronze.jsonl`, script_start 4545032, 65 parts when read, still recording): all types identical; shift 56 samples; times exact on 32, within 2 samples on almost all, outliers of 6, 8, 25 and 124 samples.

What this settles:

- Decoded CD samples give the same analysis as the original's sound-chip capture. No resampling or filtering is in the path. Risk R2 is largely retired.
- The original is not repeatable to the sample between runs: the start shift differs (168 vs 56) and individual onsets wobble. The likely cause is that the capture buffer and CD playback are not locked to each other; unverified. Our generator is fully deterministic, which is what the project goal wants. Golden comparisons must allow a tolerance: the script uses 256 samples and requires all types to match.
- The length differences come from the original fetching the script cue once per video frame while we fetch per block. With `cue_interval_blocks` 3 or 4 the count of exact lengths rises from 47 to 54 but one onset time changes at 4. Per-block stays the default; a length error of 4 samples in 13,000 is not perceptible.
- Stream positions do not restart per song: `script_start` was 34184 in the first session and 4545032 in the second, so always subtract it.
- `bronze.jsonl` begins with two parts under script_start 3723144 that match none of the six songs. Probably the recorder attached while something else was playing (tutorial or title demo); not investigated.

Not yet exercised by any comparison: tempo effect patterns, distribution (weighted random) selectors with the script seed, medley tracks. Polaroid uses fixed patterns only. Songs 2 to 6 will show them.

Tests: `tests/test_course_gen.cpp` (synthetic bursts, no game data) and `disc_sces02873_first_song_course` in test_disc.cpp (needs VIB_TEST_CUE). 26 tests in all.

## 2d. All six songs against the recordings (session 2, 2026-11-19) [measured]

Recordings: `polaroid.jsonl` (song 1, no --keep-alive), `bronze.jsonl` (songs 1, 2), `silver.jsonl` (3, 4), `gold.jsonl` (5, 6). script_start values: 4545032, 8233992, 14768136, 18303368, 22918408, 26784136. Part counts original / ours (per-block cue, music starting at time 0): 75/75, 92/92, 110/110, 154/154, 109/113, 113/114.

Findings:

- **Obstacle types and cameras are right on all six.** The type sequences are identical apart from places where one side placed an obstacle the other did not (0, 0, 3, 1, 3, 2 such places). That covers fixed patterns and the seeded weighted draws. Camera events match in number and value. No tempo effect occurs in any of the six songs (all scales 1.0); tempo is exercised only by the audio-CD script and is unverified.
- **Where the music starts relative to the round start changes the result, and it differs every run.** Observed shifts (22.05 kHz samples, ours minus original): 168, 56, 82, -102, 231, -76, 44. Selection windows are counted from the round start, not from the audio, so the same music with a different lead falls differently into the windows. The decimation phase (which of each pair of 44.1 kHz samples is kept) also differs per run. `vibtool course <cue> <round> <interval> <lead>` reproduces this: lead is in 44.1 kHz samples, positive drops samples, negative prepends silence.
- With the lead set to match the run (odd values, so the other decimation phase): song 2 gives 90 to 91 of 92 onsets within 2 samples, song 4 gives 153 of 154 with all types right (cue every 3 blocks). So songs 1, 2 and 4 are reproduced.
- **Songs 3, 5 and 6 are not reproduced exactly.** Their scripts ramp period and select period almost continuously. The original fetches the cue once per video frame, for the first block queued in that frame (SignalSys update), so the window lengths depend on how blocks fall into frames; since window starts accumulate, one different length moves every later window. A frame model was added (`CourseGenerator::set_frame_timing`, `vibtool course ... <lead> <frame phase>`, env `VIB_FRAME_Q16` overrides the frame length). Scanning phase and frame length for song 3 moved the count of exactly shared onsets between 10 and 88 of 110; no setting gave all 110. The exact PAL frame is 22050 x 314 x 3406 / 53203425 = 443.244 samples (q16 29048467); the constant in the code (29041657, from 49.759 Hz) is slightly off and should be corrected if the frame model is ever used for real.
- Conclusion [inferred]: for those songs the original has no single canonical course; it depends on run-to-run timing. Not yet proven: needs two recordings of the same song (asked as Q11, `gold2.jsonl`). If two original runs differ from each other as much as ours differs from one, the per-block deterministic generator stays as the port's definition of the course. If they agree with each other, there is a real discrepancy to find in songs 3, 5, 6.
- Two overflow behaviours of the original are now mirrored exactly: the 1.5x loudness comparison and the scaled part length are computed in 32 bits and wrap.
- `compare_events.py` now pairs parts by nearest time and compares type sequences; verdicts EXACT MATCH / CLOSE / DIFFERENT. Current verdicts with default settings: song 1 exact (first recording), songs 1 (second), 2, 3, 4, 6 close, song 5 different (81 of 113 onsets shared, type sequence differs in 3 places).

- **Confirmed with `gold2.jsonl` (song 5 twice, song 6 once; script_start 30871816, 36045192, 39911432):** the original is not repeatable on these songs. Shared onsets (within 27 ms, after the best constant shift) and extra/missing obstacle types: song 5 original run vs original run 88, 85, 97 of 109 to 112 (3, 3, 2 extra/missing); ours vs each original run 81, 85, 90 of 113 (6, 5, 5). Song 6 original vs original 100 of 113 (2); ours vs originals 105 and 106 of 114 (3, 5). Ours sits inside the original's own spread on shared onsets and slightly above it on extra/missing obstacles (ours places 113 and 114 where the original placed 109 to 113). **Decision: the per-block deterministic generator is the port's definition of each course. Task 2.5 closed.** A remaining small bias toward one or two more obstacles on songs 5 and 6 is unexplained and not pursued.

- **Owner decision, 2026-11-19 (supersedes the "Decision" in the bullet above): being true to the original comes first. Default = courses vary from play to play like the original; the fixed course per song is an option the owner will judge by playing (tracker Q13).** Implemented as `CourseTiming` in course_gen.h: `like_original(seed)` draws a lead of -240 to +500 samples at 44.1 kHz (the recorded rounds spanned -204 to +462, either decimation phase) and a frame phase, and uses the per-frame cue model with the exact PAL frame length (constant corrected to 29048467); `consistent()` is lead 0, cue per block. `make_blocks(pcm, lead)` applies the lead. The app must pass a fresh seed per play (and store it with a replay or score if courses ever need reproducing). `vibtool course|play <cue> <round> original <seed>`.
- Check of the varied mode, 8 seeds per song, share of obstacles in common (within 27 ms) and counts: songs 1, 2, 4 99 to 100% whatever the seed (75, 92, 154 obstacles, as the original). Song 3: 108 to 112 obstacles (original 110), ours vs ours 85 to 96%, ours vs original 87 to 95%. Song 5: 108 to 113 (original 109, 112, 112), ours vs ours 73 to 96%, ours vs original 70 to 93%, original vs original 76 to 87%. Song 6: 110 to 113 (original 113, 113), ours vs ours 78 to 95%, ours vs original 78 to 94%, original vs original 88%. So the varied mode's spread matches the original's, and the small surplus of obstacles seen with the per-block generator is gone with the frame model.

Owner's PC, 2026-11-19: `build\dev` has .pdb files but no .exe for vibtool, vibcore_tests or vibview; .ninja_log ends before the link steps. Same symptom as the 32-bit developer shell. Asked to rebuild after the amd64 switch (Q12).

## 2e. First playable desktop build, vibplay (session 2, 2026-11-19)

Phase 3 started (owner chose it while running the scoring recording). Layout:

- `core/include/vib/scene.h`, `core/src/scene.cpp`: `Clip` (time-based animation stepping, whole-frame display, no blending), `GameCamera` (7 cameras, 6 transition pairs, same state machine as the original), `Scene` (road, POINT.TMD far points, joins, screw lean, four forms with 11 moves and alternates, evolution meter, jitter). Platform-free; outputs `SceneLine`s in PlayStation axes and a `SceneView`. Written after reading revib's SceneDraw, TmdLineObj, GameCamera, ModelMovie, CameraMovie and Movie for behaviour; nothing copied. Disc test `disc_sces02873_scene_draws`.
- `apps/common/` (`vib_render`): the Vulkan line renderer moved out of vibview; fill pipeline removed (nothing in the game is filled). SDL3 FetchContent lives here now.
- `apps/game/` (`vibplay`): `audio.{h,cpp}` (SDL3 stream with a callback mixing music and 24 effect voices; heard position = frames handed over minus two device buffers minus the user offset, smoothed and monotonic), `main.cpp` (course loop). Built by `VIB_BUILD_VIEWER` (on by default on Windows). CI builds it on Windows with warnings as errors.

How a song runs: playback buffer = 8.005 s of silence (the original's analysis look-ahead) then the track, shifted by `CourseTiming::lead`. Each frame: heard position from the audio clock; blocks fed to the generator up to heard + 176,512; generator events go to Gameplay; `quiet` when the script stops selecting; `end_at(total)` when all blocks are in. Presses carry SDL event timestamps, and each one runs `Gameplay::update` at the heard position of its own timestamp, before the frame's own update. Game events drive sounds, Vibri's moves, the evolution meter and the camera. The jitter seed changes 49.759 times a second from wall time; everything else moves at display rate.

Findings while building it:

- **Sound effect pitch settled [measured from the bank]:** the game keys tone t at note t + 1 while centre notes are 25 to 42, so effects play two or more octaves below their centre. With "centre note = 44.1 kHz" that puts program 1's samples at 11,025 Hz (centre 25, note 1, 24 semitones), so the samples are evidently 11 kHz recordings. `vibtool sounds` still writes them as 44.1 kHz WAVs (they will sound fast); fix when convenient. Fine tune (centre_fine, 128ths of a semitone) is subtracted.
- **SPEC section 7 had camera speed backwards (now corrected):** Gameplay passes "home course" as the camera's `fast` flag, so the game's own courses run camera clips at 1.3 and audio-CD courses at 1.0. In fast mode a break always goes to side 0.
- Camera twist is 0 in every ANC on the disc; ignored.
- ROAD.TMD pieces span z 0 to 600 (one part length); POINT.TMD is the far marker the road line starts and ends at (40 lengths behind, 50 ahead).
- The original keeps the join point in the previous piece's local coordinates and draws it in the next piece's frame, so leaning pieces' joins are slightly off in the original; reproduced.
- The original's facing test ignores a piece's z offset (marker vertices unjittered, untranslated); reproduced. Road pieces carry no markers, so it does not matter in practice.

HUD added the same day: `Hud` in scene.h draws the score marks (FONT/MARK.TMD objects swapped into METERS/SCORE.ANM's object 0, 7 marks 20 frames apart on one looping clip run backwards at 1 + 9 x (combo / 18)^2, jitter 4, camera at z -2906 looking along +z, fov 296/4096), the time meter (METERS/TIME, seeked to (1 - time left) x (frames - 1), jitter 3, its own ANC) and the Round 1/2 sign (MESSAGE/02_ROUND.TMD, ROUND01/02.ANM, ROUND.ANC, jitter 3, plays once). `Hud::marks` builds the original's row table from binomials (tested against its first values). `LineBuilder` now holds the jitter and object drawing shared by Scene and Hud. The renderer draws a list of layers, each with its own camera (`vibr::Layer`). `vibtool sounds` now writes each sample at its played rate (11,025 Hz for most effects). Not yet: text, speed meter, title and menus (3.5), effect volume following music loudness (needs the meter's scale), pause, demo. Not run by the assistant (no GPU or audio device in the workspace); the first run is the owner's (tracker Q15). **Owner's first playtest (2026-11-19): looks pretty good; only issue was the default line weight one notch too high, so the default became 0.75; still slightly too strong on the second look, so now 0.5 of the original's one pixel on 288 lines, with - and = stepping 0.125 and printing the value.** No other issues seen by the owner. Not yet reported: timing feel and the audio offset used. Things that were to watch on that run: whether presses feel early or late (latency estimate is two device buffers; calibrate with [ and ]), a pause at start while the whole track is decoded, effects too loud or quiet against the music.

## 2c. Gameplay rules (session 2, 2026-11-19)

`core/include/vib/gameplay.h`, `core/src/gameplay.cpp`, `core/include/vib/fixed.h` (16.16 arithmetic with the original's rounding, including its 8-bit-precision divide). Written after re-reading revib's Gameplay and Score sources for behaviour; nothing copied. `tests/test_gameplay.cpp` has 12 tests on hand-made courses. `vibtool play <cue> <round>` runs a perfect player through the real pipeline (generator 1,379 blocks ahead of the heard position, gameplay updated once per block).

Interface: `Gameplay::start(RoundSetup)`, `add_event(CourseEvent)`, `end_at`, `quiet`, `update(position, pressed bits, move_done)` returning Playing / Clear / GameOver, then `events()` (hit, miss, stray, form changes, camera, select on/off) and state getters. Buttons arrive already folded to four bits (block, pit, loop, wave); the platform layer maps L1/L2, Up/Down, R1/R2, Triangle/Cross.

Perfect-play results, no stray presses (score + end bonus): Polaroid 1408 + 422, song 2 1782 + 534, song 3 2178 + 653, song 4 3146 + 943, song 5 2370 + 711, song 6 2420 + 726. Unverified against the original. The default top scores (6000 for bronze) are above a plain perfect run of two songs, so stray-press bonuses are part of high-score play.

Details that go beyond SPEC section 4 (now the reference for them is the code):

- Points carry between a course's two songs as score + end bonus of the first; the second song's end bonus is a percentage of the whole total.
- Score shown is capped at 116,279; the bonus is computed from the uncapped points.
- A stray press is credited against the earliest part not yet hit or missed. Only the first pressed action in the order block, pit, loop, wave counts in one update.
- 18-hit run does not count in auto-play or speed courses. In the best form hits score double, including the 18th hit that reaches it.
- The round clears (or game over is reported) only when no part is in the hit window and none is within one length behind Vibri.
- Screw effect: a new one only moves the start/end once the running one has ended.
- Original video delay is 882 samples (two frame periods at 49.759 Hz), not about 886 as SPEC says. `RoundSetup::video_delay` defaults to 0: the VR build should predict display time itself (Phase 4 decision).

Deliberate differences from the original, to keep in mind for golden tests:

- Speed courses: the original raises speed 0.2% per video frame. Ours does it per 443 samples of music (one original frame), so it does not depend on headset refresh rate.
- The speed curve's square root uses an exact integer root; the original calls the PlayStation library's `csqrt`, which may differ in the last bit.
- Ties in part position are kept in arrival order (the original's sort leaves them unspecified).
- `move_done` (presses ignored until Vibri's move animation is half over) is an input; the animation side does not exist yet. Open design question for Phase 4: judging with the press's own timestamp instead of the frame's, for consistent timing at any refresh rate.

Checking the rules against the original (added 2026-11-19): `capture_events.py --scores` also breaks at Score::Hit (0x8004BF54; a1 road, a2 multiplier) and Score::Press (0x8004BFE8; a1 road, a2 the coming part's road) and adds to every record the state before the stop: points, strays counted, stray bonus, chain (4 ints at 0x8006D800), level, misses, combo (0x800764CC, D0, D4). `tools/reference/check_scores.py <file>` replays the rules over consecutive records. **Run on a real recording (owner, 2026-11-19, `scores.jsonl`, silver course): 216 records, 88 hits, 19 stray presses, 0 disagreements. SCORING MATCHES.** Covered: chain, stray bonus (results 1 and 2 seen), best-form doubling (13 hits), levels 0 and 3 to 7, misses, and the carried total (song 2 started at 769 = 669 + 15% end bonus at level 7). Not covered: pair obstacles (none hit), stray result 4, a third stray. Task 2.4 done. `--keep-alive` changed: it no longer forces the level to 3; it clears the miss count when the level is 11, which prevents game over without touching forms or scoring. Earlier recordings (bronze, silver, gold, gold2) were made with the old behaviour, which does not matter for obstacle events. More breakpoint stops per second with --scores; whether the game stays smooth is untested.

Not wired yet: speed-course `fixed_period()` and the audio-CD select on/off events are produced by Gameplay but the CourseGenerator does not consume them (both belong to audio-CD play, the stretch goal).

## 2a. Format findings from the loaders (session 2) [measured]

Across all 13 PAKs on the disc (every language):

- 252 TMD models: 63,905 line primitives (mode 0x40, flag 1, two vertex indices), 187 unlit flat triangles (mode 0x21, all facing markers, see section 0) and 6 unlit flat quads (mode 0x29, drawn as one line each). No other primitive types. All TMD flags are 0 (offsets relative to the object table).
- Unlit flat polygon layout: colour word (R, G, B, mode), then 3 or 4 u16 vertex indices padded to 3 words.
- 1,162 ANM files with 87,495 frames, all parsing with zero leftover bytes. Keyframe flag bits beyond 0..2 never occur.
- 266 ANC files with 67,166 camera keys, all exactly 6 + 16 × count bytes.
- FSL: fixed-pattern obstacles are u32 each (the next table starts exactly at the documented 0xEC). Header track list is 0..6. The last distribution pattern's count field is 0xFFF00001. Segment counts per course (control/pattern/event): Polaroid 12/5/4, Sunny Day 35/5/4, Laugh & Peace 28/8/4, Universal Dance 5/5/4, Overflowing Emotions 36/8/4, Roll Along 44/5/4, custom CD 774/90/82.
- **Posing [observed, not yet confirmed in code]:** vertices are scaled, rotated about X then Y then Z, then translated (v' = Rz·Ry·Rx·S·v + p), 4096 = one turn. With the other common order Vibri's feet detach from her legs. Limb objects often have scale Z = 0 (flat). PlayStation Y points down; the OBJ export flips it.
- Assumed: a key without a rotation, scale or position group uses the identity value for it.
- Sound banks (VAB layout, version 7): PSJ_SE 16 programs, 187 tones, 129 samples; TITLE 18 programs, 57 tones, 29 samples; KIOSK 1 program, 16 tones, 4 samples. VH size = 32 + 2048 + programs × 512 + 512 exactly, and the sample-size table adds up to each VB's size exactly. All samples decode as SPU ADPCM (37.6 s in total; 14 of 1.66 M samples at full scale, normal for loud effects). Each tone maps one key (note_min = note_max) to a sample, so effects are triggered by note number. Playback pitch is not yet known: decoded WAVs assume 44.1 kHz at the centre note.
- TIM images exist only in TITLE/FILES.PAK under LOGOS/: Sony and NanaOn-Sha (7ON) logos. Not loaded (D10).

## 3. How the game works

File formats [source: open-ribbon documentation]:

- **PAK**: file count, offset table, then name (padded to 4 bytes), length, data. Uncompressed.
- **TMD**: standard PlayStation model format, used almost entirely for line primitives.
- **ANM**: per frame, a list of keyframes: object index, flags, then optional rotation, scale and position triples (int16; rotation and scale in 1/4096 units). An object with no keyframe in a frame is not drawn.
- **ANC**: camera keyframes: eye position, look-at point, twist, field of view. Applied with `GsSetRefView2`.
- **FSL**: fixed patterns, distribution patterns (seeded weighted random over 10 obstacle types: block, pit, loop, wave and the six pairs), control segments (speed and minimum obstacle gap, with linear ramps), pattern segments (which pattern is active; type 0 is a song break where the camera circles Vibri), event segments (random speed, flip, camera moves), and a per-track index. Track order: Polaroid, Sunny Day, Laugh & Peace, Universal Dance, Overflowing Emotions, Roll Along, then the custom-CD track.

Engine behaviour [summary, from revib source files; confirm in task 1.7]:

- **Audio path.** The game does not read CD sectors. It reads the sound chip's capture buffers while the CD plays, patching Sony's library to allow transfers from that low address range (`FbStream.cpp`). It also writes audio into the reverb work area, which plays it back delayed. `AudioStream.cpp` turns each 256-sample stereo capture into a 128-sample mono block at 22,050 Hz and keeps a delay line so analysis runs ahead of what is heard. The delay length is set elsewhere and is not yet known.
- **Analysis.** `SignalModule.cpp` does onset detection on full-band energy: a short running sum against a long one, an absolute floor, strongest onset per selection window, spacing rules in a peak picker. Accepted onsets become PART events handed to `Gameplay::AddEvent`.
- **Clock.** `Game` passes the music position, in samples, to both the analysis and the gameplay each update. Part times are "in samples as the sound is heard". Constants seen: distance 10.0, Vibri position 1.25, beat length half a second.
- **Gameplay.** Parts (road pieces) with type, time and rate. Four single obstacles and their pairs. A combo of 18 hits moves Vibri toward a better form; misses are counted in threes toward a form change. Chains count up to ten. Hit-window widths are in `Gameplay.cpp`, not yet read.
- **Video.** PAL, 50 Hz, 288 lines; the code's own constant for the real refresh is 49.759 Hz. Ordering table of 2,048 entries. Fades and a blur are built from sprites and VRAM moves.
- **Threads.** MAIN_G uses PlayStation kernel threads; the audio thread has its own stack and is resumed from the transfer callback.

Other facts:

- The original was written in C++ [source: open-ribbon]. revib builds with egcs 2.91.66 and the SGI STL 3.2 [source: revib README].
- TCRF documents debug strings for a parameter editor ("Selector Type", "Pattern Index", "Distribution Index", "Road Part Period", "Send Screw Effect"). revib has a `debug-menu` branch that opens the developer menus.

## 4. Adoregabriel2005/vib-ribbon-recomp [measured from the local copy, plus its README]

- What it is: a Windows x64 build of SCES-02873 on the PSXRecomp framework. Recompiled code plus native overlays plus an interpreter fallback. Version 0.1.0, 5 commits, 6 stars at time of reading.
- Size: about 737 KB, of which 370 KB is an icon and 127 KB is the bundled MP3 decoder. Own code: `native\vib_menu.cpp` (25 KB), `native\music_disc.cpp` (5 KB), build scripts, diagnostics tools, four patches to the framework (55 KB of diffs).
- Submodules pinned: psxrecomp `d35f13c`, recomp-ui `dd47b18`. The local zip has both folders empty.
- Config: OpenBIOS, low-level emulation, `bios_hle = false`, OpenGL renderer, 4:3, native resolution, digital pad.
- What it adds: an MP3 menu (decode to a virtual audio CD so the original analysis runs on it), English and Brazilian Portuguese PC menus, PC video and volume settings.
- Its own status file (12 November 2026) lists as unverified: gameplay fidelity, timing, collision and scoring, audio quality and sync, end-of-track return, pause and resume, controller navigation, memory card. It records audio underruns and drops in probes, and an optional ahead-of-time code path disabled after failing its byte audit.
- Licence: the repo has no LICENSE file. PSXRecomp is PolyForm Noncommercial 1.0.0. recomp-ui is MIT [source: another PSXRecomp project's README].
- Useful technical notes from it:
  - MAIN_G global pointer is 0x80075E24. Scene id at gp+0x5A0. Menu mode at gp+0x580 and gp+0x584. Session mode at gp+0x840 (the session driver reads this copy; changing only the menu copy starts a manual game). Modes 3, 4 and 8 are the custom-music modes; official demo modes are 5 to 7.
  - Hook points it uses, each guarded by the original instruction bytes: 0x8003FBC0 (menu observer) and 0x8003FE54 (mode setter).
  - It had to patch the framework because the game switches kernel threads inside an interrupt, which the framework's low-level path did not follow. Any emulation-based reference must get this right.
  - MP3 handling: decode with minimp3, convert to 44.1 kHz 16-bit stereo, write a CUE/BIN, mount it as an audio CD.

## 5. Why the plan does not fork that repo

1. **It is the wrong foundation for the three goals.** It runs the PS1 program on an emulated PS1. Frames come at the PAL rate of 50 per second; a headset needs 72 to 120. The framework's stereo support replays the game's rendering once per eye, so the game itself still advances 50 times a second however fast the headset refreshes [inferred from the framework's pull request descriptions]. Audio goes CD emulation to sound-chip emulation to host, the path where the repo itself reports underruns.
2. **No route to standalone.** PSXRecomp lists Windows, macOS and Linux. Its OpenXR path has shipped only in Windows builds (the two Medal of Honor projects), whose docs say demanding scenes still miss the 60/90 target. Quest 3 and Steam Frame standalone need Android ARM64 or Linux ARM64.
3. **Nothing to build on legally or practically.** No licence file means no granted right to modify and redistribute. The framework underneath is non-commercial only. And the repo's own contribution is small: a menu, an MP3 converter and four patches.
4. **Its line rendering is the PS1's.** One-pixel lines scaled up. The goal is lines that look right at headset resolution.
5. **What we keep from it:** the technical notes above, the idea of feeding custom music through the original analysis, and the option to build it locally as a second reference.

When forking it would be the right call: if the goal were only "see the original in a PC headset soon". That is a few sessions of work following the Medal of Honor VR projects, and it remains available as a side experiment.

## 6. Tools and projects surveyed [source]

Vib-Ribbon specific:

- **open-ribbon** (github.com/open-ribbon/open-ribbon): work-in-progress C decompilation of the PAL build, 105 commits, plus the documentation site with the format pages.
- **revib** (github.com/pawix25/revib): C++ decompilation; all four executables rebuild byte-identical. Created 2026-10-22, last push 2026-11-03. No licence. Includes symbol files with addresses for every executable, splat configs, and Python tools for PAK, TMD, ANM and disc images.
- **VibRipper** (PAK unpack and repack), **Murugo's Misc-Game-Research** (Blender add-on for the models and animations, written for Blender 2.8), Golan Levin's recovery of the game's stroke font.

PlayStation recompilation and porting:

- **PSXRecomp** (mstan, now under RetroPortingToolKit): MIPS to C plus a hardware runtime. Beta. OpenGL default, Vulkan experimental, software reference. Mods and trusted plugins. Games include Mega Man X4 to X6, Tomba 1 and 2, Ape Escape and Um Jammer Lammy (preview). A community Spyro port combines it with PsyCross.
- **RecompOne** (Flaffy / BlackLabelHQ): MIPS to C#. Used for MediEvilRecomp (November 2026) and a Symphony of the Night test case.
- **Psy-Z** and **PsyCross**: replacements for Sony's PsyQ libraries so a decompiled game compiles natively. PsyCross is MIT and is used by the Driver 2 port.
- Decompilation tooling: splat, maspsx, m2c, asm-differ, decomp-permuter, decomp.me (has a Vib-Ribbon preset).
- Emulators with debuggers for the reference rig: PCSX-Redux, DuckStation.

VR precedents:

- **Medal of Honor** and **Medal of Honor Underground** recomps by FractalEngineer: PSXRecomp with native OpenXR stereo, head tracking and tracked weapons. Windows only. Baseline tested on Quest 3 through Virtual Desktop. Eight framework changes merged upstream into PSXRecomp (pull requests 739 to 746). Built with `-DPSX_OPENXR=ON`.
- Source-level VR ports of other consoles' games that do run standalone on Quest: Perfect Dark VR (from the decompilation), plus PC-only from-source ports such as GEVR (GoldenEye) and a Mario Kart 64 VR build.

Platforms:

- **Steam Frame**: SteamOS on a Snapdragon 8 Gen 3. Runs Windows x86 games through Proton and FEX, Android APKs through Lepton, and native Linux ARM64. Valve's guidance recommends Android or Linux ARM64 for new development.
- **Quest 3**: Android ARM64, OpenXR, Vulkan.

## 7. Open questions

For the owner (also in the tracker): Q4 licence for our code, Q5 run toolchain checks and the first Windows build, Q6 OK to contact revib's author. Q1 to Q3 were answered in session 2.

Technical, to settle in Phase 1:

- Exact look-ahead delay between analysis and heard audio.
- Hit-window widths and the scoring table.
- Whether analysis on decoded file samples matches analysis on sound-chip capture, sample for sample. Answered: yes for Polaroid (section 2b).
- Which audio track is which song, and what the seventh track is.
- Whether the ANM and ANC header field is frames per second.
- Differences between the PAL build and the Japanese NTSC build.
- Licence position of revib and open-ribbon.

## 8. Pages that could not be read

GitHub rate-limited several fetches. The repository landing page for revib and pull request 666 on PSXRecomp were not read directly; facts about them above come from the GitHub API, raw files and search snippets.

## 9. Sources

- https://github.com/Adoregabriel2005/vib-ribbon-recomp
- https://github.com/pawix25/revib and https://raw.githubusercontent.com/pawix25/revib/main/docs/translation_units.md
- https://github.com/open-ribbon/open-ribbon and https://open-ribbon.github.io/documentation
- https://open-ribbon.github.io/documentation/file-types/fsl/ (also `pak/`, `anm/`, `anc/`, `tmd/`)
- https://tcrf.net/Vib-Ribbon
- https://github.com/RetroPortingToolKit/psxrecomp
- https://github.com/BlackLabelHQ/RecompOne
- https://github.com/Fancy2209/psyz and https://github.com/OpenDriver2/PsyCross
- https://github.com/FractalEngineer/Medal-Of-Honor-PSX-Recomp and https://github.com/FractalEngineer/Medal-Of-Honor-Underground-PSX-Recomp
- https://partner.steamgames.com/doc/steamhardware/steamframe

## 10. Draft: courtesy note to revib's author (not sent; task 6.5)

To be posted by the owner as an issue at github.com/pawix25/revib once the project is past the prototype stage.

> **Thank you, and a heads-up about a VR port project**
>
> Hi, I'm working on a free, non-commercial fan project: a native VR version of Vib-Ribbon for PCVR and Meta Quest 3. Repo: https://github.com/CorrieDivitt/Vib-Ribbon-VR
>
> I wanted to say thank you and tell you how your work is being used. We read revib (and open-ribbon's documentation) to understand how the game behaves, and write our own implementation from that understanding. No code from revib is copied into our repo, the repo contains no game data, and both projects are credited.
>
> If you'd prefer to be credited differently, or anything about this use bothers you, please say. If we find mistakes or confirm open details, we're glad to report them back. Two from the disc: times in SYSTEM3.FSL are samples at 22,050 Hz, and fixed patterns store each road as a 32-bit value.
>
> No reply needed if this is all fine with you. Thanks again for the careful work.
