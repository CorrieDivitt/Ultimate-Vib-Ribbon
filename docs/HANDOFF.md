# Vib Ribbon VR: Handoff Document

Last updated 2026-11-18 (session 2). Read this first in any new session, then the tracker.

- Repository: https://github.com/CorrieDivitt/Vib-Ribbon-VR. Master copies of this file, the plan and the tracker source live in its `docs/` folder.

- Tracker (live status, command notes): https://claude.ai/artifact/42iBUS2Aiu81652cgnRM1k (artifact slug `188889c5-f4d7-4ae5-bc71-aeb8011a952b`; its data is the `const T = {...}` block at the top of the page script; source is `docs/tracker/index.html` in the repo. To update: edit that block, publish the file to the artifact with publish_files, commit)
- Plan: `docs/PLAN.md` in the repo, also project doc `claude/PLAN.md`

Confidence labels used below: **[measured]** = computed this session from the disc or repo files. **[source]** = stated by the linked project or page. **[summary]** = from an automated summary of a decompiled source file, not yet read line by line. **[inferred]** = my reasoning from measured data, needs confirming.

## 1. Where things stand

Phase 0 is signed off. Phase 1 is under way: 1.1 and 1.3 done, 1.2 waits only on the Vulkan SDK install. 1.4 is done: TMD, ANM, ANC, FSL and VH/VB loaders plus posing are in vibcore, and build on the owner's PC. TIM was dropped (D10: the only images are the Sony and NanaOn-Sha logos). Next is 1.5, the desktop viewer, which needs the Vulkan SDK. Gotcha seen in session 2: a new Developer PowerShell starts 32-bit; building from it compiles but fails at the link step. Run Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64 first. Vulkan SDK 1.4.363.1 is installed at C:\VulkanSDK; the glslc check still needs a fresh shell.

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

## 2a. Format findings from the loaders (session 2) [measured]

Across all 13 PAKs on the disc (every language):

- 252 TMD models: 63,905 line primitives (mode 0x40, flag 1, two vertex indices), 187 unlit flat triangles (mode 0x21) and 6 unlit flat quads (mode 0x29). No other primitive types. Every fill seen so far is coloured black: they are occluders that hide lines behind a character, so the renderer must write them to depth. All TMD flags are 0 (offsets relative to the object table).
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
- Whether analysis on decoded file samples matches analysis on sound-chip capture, sample for sample.
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
