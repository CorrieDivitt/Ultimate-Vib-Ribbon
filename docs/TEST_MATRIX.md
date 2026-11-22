# Release test matrix (task 6.4)

What to check before a release, on each place the game runs. Tick a cell when it passes
on the build being released; write the build (the launcher's version, which names the
commit) at the top of the copy you fill in. Automatic checks (CI and the disc tests) are
listed last.

Build tested: ____________________   Date: __________

## Where it runs

| Id | Platform | Runtime / display | Controller | Who |
|----|----------|-------------------|------------|-----|
| M1 | Windows, monitor | window, 60 Hz | Xbox pad | |
| M2 | Windows, monitor | Fullscreen, high refresh (120-240 Hz), VSync on | Xbox pad | |
| M3 | Windows, monitor | Borderless, VSync off, frame limit | keyboard and mouse | |
| M4 | Windows, monitor | 4:3 picture, a second PC without the developer tools | any | |
| V1 | PCVR, Quest 3 | Virtual Desktop (OpenXR) | Xbox pad | |
| V2 | PCVR, Quest 3 | Meta Quest Link (OpenXR) | Touch | |
| V3 | PCVR, any headset | SteamVR (OpenXR) | Touch or Index | |
| Q1 | Quest 3 standalone | APK, 120 Hz | Touch | |
| Q2 | Quest 3 standalone | APK, passthrough background | Bluetooth Xbox pad | |

## The checks

| # | Check | M1 | M2 | M3 | M4 | V1 | V2 | V3 | Q1 | Q2 |
|---|-------|----|----|----|----|----|----|----|----|----|
| 1 | Install from the download (launcher or APK steps), disc picked or pushed, game starts | | | | | | | | | |
| 2 | Title intro, language menu, mode menu; Exit closes the game cleanly (B jumps to it) | | | | | | | | | |
| 3 | Bronze, silver, gold: both songs each, results, high-score table | | | | | | | | | |
| 4 | Game over, "again?", back to the course select | | | | | | | | | |
| 5 | Options menu opens on every screen (Start / P / Tab / menu button); pause and resume (rewind and count-in) | | | | | | | | | |
| 6 | Camera beside Vibri: sticks, mouse and keys zoom and turn, reset; Camera Original | | | | | | | | | |
| 7 | Settings change and are kept after a restart (save.txt) | | | | | | | | | |
| 8 | Difficulty Easy / Normal / Hard: fewer / original / more obstacles; scores only kept on Normal | | | | | | | | | |
| 9 | Own music: pick tracks, all tracks, in order, speed courses, own-CD high scores | | | | | | | | | |
| 10 | Sound and picture calibration give steady results; timing feels right | | | | | | | | | |
| 11 | Performance overlay: no late frames in play at the display's rate | | | | | | | | | |
| 12 | Idle demo and the kiosk film | | | | | | | | | |
| 13 | Window modes, resolution, VSync, frame limit, low latency (monitor) | | | | — | — | — | — | — | — |
| 14 | Recenter, refresh rate, course around you, classic screen (headset) | — | — | — | — | | | | | |
| 15 | Passthrough background | — | — | — | — | — | — | — | | |
| 16 | Taking the headset off and on, the runtime closing: no crash | — | — | — | — | | | | | |
| 17 | log.txt / log-vr.txt / logcat show no errors after a full session | | | | | | | | | |

## Automatic (every push)

- CI: Windows (MSVC, warnings as errors) and Ubuntu builds with the unit tests; the
  Android APK (holds the game, SDL, libc++ and the notices); the Windows download (the
  three programs need no runtime DLLs we do not ship).
- Disc tests (need your disc: `VIB_TEST_CUE`): courses against the original's recordings,
  scoring, every menu and screen, settings words in every language, difficulty.
