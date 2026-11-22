# Vib-Ribbon VR

A free, fan-made version of Vib-Ribbon (NanaOn-Sha, 1999) for VR headsets, Windows monitors and (experimentally) Android phones. It plays the original's songs and courses from **your own disc**, and turns any music you own into courses, as the original does with audio CDs.

The game's logic is our own portable C++, written from how the original behaves and timed by the audio clock rather than the PlayStation's 50 Hz frame, so timing stays steady at any display rate. Vibri, the menus and the courses are drawn as clean, sharp lines at your headset's or monitor's full resolution.

This is an unofficial project, not affiliated with Sony Interactive Entertainment or NanaOn-Sha.

## Versions

| Version | Runs on | Download | State |
|---|---|---|---|
| Vib-Ribbon VR | PC VR through OpenXR: Meta Quest Link, Virtual Desktop, SteamVR | Windows zip | 1.0 |
| Vib-Ribbon | Windows, on a monitor | Windows zip (the same one) | 1.0 |
| Vib-Ribbon VR for Quest | Meta Quest 3, standalone (no PC needed to play) | Quest APK, sideloaded | 1.0 |
| Vib-Ribbon for Android | Android phones and tablets, sideways, touch buttons or a gamepad | Android APK | **Experimental: feedback welcome** |

Downloads are on the [Releases](../../releases) page. What changed is in the [release notes](docs/RELEASE_NOTES.md).

## You need your own disc

Nothing of the game is in this repository or in the downloads: no code, pictures, sounds, music or text from the disc, and no BIOS. The game reads a disc image you made from your own copy of the European release, **SCES-02873**: a `.cue` file with its raw `.bin` tracks (all eight). The Windows launcher checks that the image is the right one.

## Installing

**Windows (monitor or PC VR).** Unzip the download anywhere and run `Vib-Ribbon Launcher.exe`. Choose your disc image, pick the monitor or a VR headset, set the display (window or fullscreen, resolution, refresh rate, VSync, frame limit) and press Play. For VR, start your headset's OpenXR runtime first. Nothing is installed; delete the folder to remove the game. The zip's `README.txt` has the full controls.

**Meta Quest 3.** Sideload the APK with adb (Developer Mode on the headset), then copy your disc image onto the headset once. `QUEST.txt`, beside the APK, walks through every step and command, including your own music from the headset's Music folder.

**Android phones and tablets (experimental).** Install the APK from your phone's Files app. Put your disc files in a folder of their own (for example `Download/Vib-Ribbon`); on first start, tap **Pick folder** and choose it. `ANDROID.txt` has the details. Please report what phone or tablet you used and how it went.

## Playing

- **Options menu on every screen:** Start on a gamepad, P or Tab on the keyboard, the menu button on Touch controllers, or the on-screen button on a phone. In a song it pauses. Every setting shows the original game's value in brackets, so the original is always one glance away.
- **Exit** is on the title menu (B jumps to it).
- **Your own music** becomes courses, as the original does with audio CDs: pick tracks, play all of them, or in order; speed courses too.

## Features

Everything below is new in this port. The original's look and behaviour are the starting point, and **Original look** (Options > Visual) puts every look setting back to the original's in one go; **Restore my look** brings yours back.

### Timing

- **Audio-clock timing.** The game runs from the music, not from the PlayStation's 50 Hz frame, so judging stays steady at 60, 120 or 240 Hz and in any headset.
- **Sound calibration** and **Picture calibration** (Options > Calibration): short tests that measure your speakers' and display's delay and suggest the right **Sound timing** and **Picture timing**. Judging always follows the sound.
- **Timing feedback:** each hit shows how early or late you were, in milliseconds, for a moment near Vibri.
- **Resume:** after a pause, either rewind a little and play on (the original) or count in from the same spot.
- **Low latency** (monitor): fewer frames waiting for the display, for a snappier picture.

### Camera and view

- **Camera beside Vibri** (the default) or **Original**. Beside Vibri, the analog sticks (or the mouse) turn round her and zoom, on every screen including the menus; the right stick click puts the view back. **Zoom**, **View angle** and **Field of view** set it precisely.
- **See ahead** and **See behind:** how far ahead the road is drawn and how long passed obstacles stay, from the original's 8 and 2 seconds up to the **whole song**.
- **Scroll speed** (0.5x to 10x), like a rhythm game's speed modifier: it changes how fast the road comes at you, not the timing. Change it mid-song with the triggers (L2/R2) or Page Up/Page Down.

### The lines

- **Wobble size:** how far the lines shake (0 makes them still; 1 is the original).
- **Wobble hold:** how often the shake changes. **Off** is the original's 50 times a second; 1 to 3 changes it every 1 to 3 frames of your display, which is calmer or livelier at high refresh rates.
- **Smooth motion:** the original steps Vibri and the menus through their poses at 15 to 30 a second; this blends between them for fluid movement at your display's rate.
- **Line width**, **Glow** (a soft halo round every line, faint to strong) and **Custom colors** (a colour for the road, each obstacle and pair, each form of Vibri, the background and the menus).

### Courses and scoring

- **Difficulty** (experimental): Easy, Normal (the original's courses exactly), Hard and Extreme. See below.
- **Consistent courses:** the original's courses vary a little from play to play; this makes each song's course the same every time, for practice.
- **Score in numbers** during play, a **Full Combo** celebration for a song cleared without a miss, and an **Effects volume** for the hit sounds.

### In VR

- **Course around you:** the road wraps round you in a ring (**Circle size** sets how wide); Vibri runs round it, the new road comes round one side and the old fades away behind you.
- **Classic screen:** the game exactly as on a monitor, the original's cameras and all, on a big screen in front of you (**Screen size** sets it).
- **Passthrough background** on the Quest: the lines over your own room.
- **Refresh rate:** Auto (the highest up to 120 Hz) or any rate your headset offers; **Recenter view**; Touch controller **vibration**.

### On a monitor

Window, borderless or fullscreen; any **Resolution** and refresh rate; **VSync** on or off; a **Frame limit**; and **Picture shape**: fill the screen or the original's 4:3 with bars. The Windows launcher sets these before you start, too.

### On a phone or tablet (experimental)

On-screen **Touch buttons**, drawn as the obstacles' shapes or as the PlayStation buttons (L1, Down, R1, X); tap and swipe in the menus, drag and pinch to move the camera. A gamepad works too.

### Also

- A **Performance** overlay (one line or detailed) showing frame times and late frames.
- Sound effects and vibration can be turned off, as in the original.

### Experimental features

The **Difficulty** setting (Options > Modifiers > Difficulty) is experimental, and feedback is welcome. **Normal is the original game's courses exactly**; the other levels are our own changes to how courses are made from the music and may still change. Easy has fewer obstacles and no pairs; Hard has more; Extreme puts an obstacle on nearly every beat, a little faster, with single obstacles only in each song's first half and the same obstacle often several beats running. High scores are kept at Normal only.

The **Android** version is experimental too.

## Feedback and problems

Open an issue on this repository. Please say which version and device you used, and attach the log:

- Windows: `%APPDATA%\VibRibbonVR\VibRibbonVR\log.txt` (`log-vr.txt` for VR)
- Quest and Android: `adb logcat -d -s vibplay:* > log.txt`

## Building from source

| Folder | What it holds |
|---|---|
| `core/` | `vibcore`: the portable game library (disc and file readers, course generator, gameplay, scene, menus, settings, save). No graphics or platform code. |
| `apps/game/` | The game: the monitor, PC VR, Quest and Android builds from one source. |
| `apps/launcher/` | The Windows launcher. |
| `apps/common/` | The Vulkan line renderer and the OpenXR code shared by the builds. |
| `apps/viewer/` | `vibview`, a developer viewer for the disc's models and animations. |
| `android/` | The Gradle project: `quest` and `flat` (phone and tablet) flavours. |
| `tools/` | `vibtool` (inspect your disc), packaging scripts, research prototypes. |
| `tests/` | Unit tests. Disc tests run only when `VIB_TEST_CUE` points at your `.cue` file. |
| `docs/` | Plan, behaviour spec (`SPEC.md`), handoff notes, test matrix, tracker. |

**Windows.** Visual Studio 2022 or later with "Desktop development with C++", CMake 3.24+, Ninja and the Vulkan SDK. The first configure downloads SDL3, OpenXR and Dear ImGui. From a Developer PowerShell for VS:

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The release zip is made by `tools/package/build-windows.ps1`. To run the disc tests:

```powershell
$env:VIB_TEST_CUE = "C:\path\to\Vib-Ribbon (Europe) (En,Fr,De,Es,It).cue"
.\build\dev\tests\vibcore_tests.exe
```

**Android (Quest and phones).** Android Studio (for its JDK and SDK, with the NDK and CMake installed). From PowerShell in `android/`:

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
.\gradlew.bat assembleQuestDebug    # app/build/outputs/apk/quest/debug/app-quest-debug.apk
.\gradlew.bat assembleFlatDebug     # app/build/outputs/apk/flat/debug/app-flat-debug.apk
```

## Licence

The code in this repository is MIT licensed (see `LICENSE`). That covers only what is written here; it grants nothing over Vib-Ribbon itself. The libraries the game is built from, and their licences, are listed in `docs/notices/THIRD_PARTY_NOTICES.txt`, which ships with every download.

## Credits

This project builds on the research of others, especially the open-ribbon project's format documentation and the revib decompilation, which helped us understand the original; none of their code is used here. Vib-Ribbon is © Sony Interactive Entertainment and NanaOn-Sha.
