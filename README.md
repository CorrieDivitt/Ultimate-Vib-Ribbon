# Vib-Ribbon VR

A fan-made VR version of Vib-Ribbon (NanaOn-Sha, 1999) for PCVR and Meta Quest 3, with Steam Frame in mind as a stretch goal. Released free.

The goals are timing that is responsive and consistent, and visuals that do justice to the original's line art. The plan is a native port: the game's logic reproduced in portable C++, driven by the audio clock rather than the PS1's 50 Hz frame, with the original models, animations and course scripts read from your own disc.

**Status:** Phase 1, foundations. Nothing playable yet. See [docs/PLAN.md](docs/PLAN.md) and the tracker.

## You need your own disc

This repository contains no game code, data, music or BIOS, and never will. The tools read a disc image you made from your own copy: the European release **SCES-02873**, as a CUE sheet with raw 2352-byte BIN tracks (all eight). Other releases may be added later.

## Layout

| Folder | What it holds |
|---|---|
| `core/` | `vibcore`: portable game library. Disc, CUE, ISO 9660 and PAK readers so far. No graphics or platform code. |
| `apps/viewer/` | `vibview`: desktop viewer that draws the disc's models and animations as lines. Windows, needs the Vulkan SDK. |
| `tools/vibtool/` | Developer command line for inspecting and extracting your disc. |
| `tools/research/` | Python prototypes from the research phase. |
| `tests/` | Unit tests. Disc tests run only when `VIB_TEST_CUE` points at your CUE file. |
| `docs/` | Plan, handoff notes and the tracker page. |

## Building on Windows

Needs Visual Studio 2022 or later with "Desktop development with C++", CMake 3.24+, Ninja, and the Vulkan SDK from LunarG. The first configure downloads SDL3. Run from a **Developer PowerShell for VS**:

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

To run the tests against your disc and look at what is on it:

```powershell
$env:VIB_TEST_CUE = "F:\VibRibbonVR\research\Gamefiles\Vib-Ribbon (Europe) (En,Fr,De,Es,It).cue"
.\build\dev\tests\vibcore_tests.exe
.\build\dev\tools\vibtool\vibtool.exe info $env:VIB_TEST_CUE
```

## Credits

This project builds on the research of others, especially the open-ribbon project's format documentation and the revib decompilation. Vib-Ribbon is © Sony Interactive Entertainment and NanaOn-Sha. This is an unofficial project, not affiliated with either.
