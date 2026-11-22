# Releasing

How a release is made (tasks 6.7 to 6.13). Commands are for PowerShell on the owner's PC.

## Version

One number for everything, in the top `CMakeLists.txt` (`project(VibRibbonVR VERSION x.y.z)`): the launcher's About tab and the game's log (`Vib-Ribbon x.y.z (commit)`) show it. The APKs carry it in `android/app/build.gradle`: `versionName` the same, `versionCode` = major * 100 + minor * 10 + patch (1.0.0 is 100). Raise `versionCode` for every APK handed out, or Android refuses the update.

## The signing key (once, kept forever)

Android only installs an update over an app signed with the same key. The debug key differs on every PC, so release APKs are signed with the owner's own key, which never goes into the repository (`.gitignore` keeps `keystore.properties`, `*.jks` and `*.keystore` out).

1. Make the key (it asks for a password and a name; the name can be anything):

   ```powershell
   mkdir F:\VibRibbonVR\keys
   & "C:\Program Files\Android\Android Studio\jbr\bin\keytool.exe" -genkeypair -v -keystore F:\VibRibbonVR\keys\vib-ribbon-release.jks -alias vibribbon -keyalg RSA -keysize 4096 -validity 36500
   ```

2. Tell the build where it is: `android\keystore.properties` (not committed):

   ```
   storeFile=F:/VibRibbonVR/keys/vib-ribbon-release.jks
   storePassword=the password
   keyAlias=vibribbon
   keyPassword=the password
   ```

3. **Back up the .jks file and the password** somewhere safe (a second drive or cloud storage, and a password manager). If either is lost, no later version can update an installed one: players would have to uninstall, which deletes their saves and the disc copy on the headset or phone.

Without `keystore.properties` a release build is signed with the debug key and Gradle warns; such an APK is for testing only.

## Building the release APKs

```powershell
cd F:\VibRibbonVR\Vib-Ribbon-VR\android
git pull
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
.\gradlew.bat assembleQuestRelease assembleFlatRelease
```

Outputs: `app\build\outputs\apk\quest\release\app-quest-release.apk` and `app\build\outputs\apk\flat\release\app-flat-release.apk`. Release builds are fully optimised and not debuggable (`adb logcat` still shows the game's log).

A device that has a debug build installed must uninstall it before the first release build (the keys differ). On the Quest that deletes the game's folder, so back up the save and push the disc again afterwards (QUEST.txt has the push commands):

```powershell
adb pull /sdcard/Android/data/io.github.corriedivitt.vibribbonvr/files/save.txt F:\VibRibbonVR\quest-save.txt
adb uninstall io.github.corriedivitt.vibribbonvr
adb install app\build\outputs\apk\quest\release\app-quest-release.apk
```

## Before going public

Checked for 1.0 (task 6.10, every commit in the history): no game files (no disc images, PAK, TMD, sound or text files from the disc), no large blobs (the largest are HANDOFF.md and main.cpp), no byte or number tables copied from the game, no keys or passwords; the only binary files are our own icons, the Gradle wrapper and the licence. The `.gitignore` keeps game data, reference recordings (`*.jsonl`) and signing keys out. Every commit is authored as Claude <noreply@anthropic.com>, so no personal e-mail address is published.

## The release

1. GitHub Actions green (needs the billing fixed while private; public repositories build free).
2. Windows zip: `tools\package\build-windows.ps1` (or CI's `vib-ribbon-vr-windows`).
3. Rename the APKs for players: `Vib-Ribbon-VR-Quest-1.0.0.apk` (with QUEST.txt) and `Vib-Ribbon-Android-1.0.0-experimental.apk` (with ANDROID.txt).
4. Tag `v1.0.0`, and make the GitHub release from docs/RELEASE_NOTES.md with the three downloads and their SHA-256 checksums (`Get-FileHash`).
