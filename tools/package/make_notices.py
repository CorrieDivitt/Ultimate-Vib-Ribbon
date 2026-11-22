#!/usr/bin/env python3
"""Writes docs/notices/THIRD_PARTY_NOTICES.txt (task 6.3): what the shipped programs are
built from and the licence of each, with the licence texts taken from the libraries'
own source trees. Run with the source folders of each (the versions CMake fetches):

  make_notices.py <SDL> <imgui> <OpenXR-SDK> <dr_libs> <out.txt>
"""
import pathlib
import sys

sdl, imgui, oxr, drlibs, out = (pathlib.Path(a) for a in sys.argv[1:6])

def text(p):
    return p.read_text(encoding="utf-8").strip() + "\n"

def dr_licence(header):
    s = header.read_text(encoding="utf-8")
    start = s.rfind("This software is available as a choice of the following licenses.")
    end = s.find("*/", start)
    return s[start:end].strip() + "\n"

parts = []
parts.append("""Vib-Ribbon VR: third-party notices
==================================

Vib-Ribbon VR is a free fan project (MIT licence, see LICENSE). It contains no part
of the game: it plays the player's own copy of the PAL disc (SCES-02873). Vib-Ribbon
was made by NanaOn-Sha and published by Sony Computer Entertainment; this project is
not affiliated with or endorsed by either.

Thanks and credits
------------------
- revib (github.com/pawix25/revib), a decompilation of the game, and open-ribbon
  (github.com/open-ribbon), with its file-format documentation: read to understand
  how the original behaves. No code from either is copied into this project.
- The Cutting Room Floor's Vib-Ribbon page, and the projects listed in the repository's
  handoff document, for research.
- DuckStation, used as the reference emulator for checking courses and scoring.

What the programs are built from
--------------------------------
Program                       Library (version)                         Licence
All Windows programs          SDL 3.2.26                                Zlib
Vib-Ribbon, Vib-Ribbon VR     dr_libs (dr_mp3, dr_flac), commit e381981 Unlicense or MIT-0 (choice)
                              minimp3 (inside dr_mp3)                   CC0-1.0
                              Khronos OpenXR loader 1.1.65              Apache-2.0 (some files Apache-2.0 OR MIT)
                              JsonCpp (inside the OpenXR loader)        Public domain / MIT
                              Vulkan headers (compiled in, not shipped) Apache-2.0 OR MIT
Vib-Ribbon Launcher           Dear ImGui 1.92.9b                        MIT
Quest 3 APK (Android)         the above but Dear ImGui, plus:
                              SDL's Android Java code                   Zlib
                              jnipp (inside the OpenXR loader)          MIT
                              android-jni-wrappers (OpenXR loader)      BSL-1.0
                              LLVM libc++ (libc++_shared.so)            Apache-2.0 WITH LLVM-exception
The Windows programs link Microsoft's C++ runtime in (no separate notice is needed).
""")

def section(title, body):
    parts.append("\n" + "=" * 78 + "\n" + title + "\n" + "=" * 78 + "\n\n" + body)

section("SDL 3 (Zlib licence)", text(sdl / "LICENSE.txt"))
section("Dear ImGui (MIT licence)", text(imgui / "LICENSE.txt"))
section("dr_libs: dr_mp3 and dr_flac (Unlicense or MIT No Attribution)", dr_licence(drlibs / "dr_mp3.h"))
section("minimp3 (CC0 1.0)", "https://github.com/lieff/minimp3\nTo the extent possible under law, the author(s) have dedicated all copyright\n"
        "and related and neighboring rights to this software to the public domain worldwide.\n"
        "This software is distributed without any warranty.\nSee <http://creativecommons.org/publicdomain/zero/1.0/>.\n")
section("Khronos OpenXR loader (Apache License 2.0; some files Apache-2.0 OR MIT)",
        "Copyright (c) 2017-2026 The Khronos Group Inc.; Copyright (c) 2017-2019 Valve Corporation;\nCopyright (c) 2017-2019 LunarG, Inc.; Copyright (c) 2020-2021 Collabora, Ltd.; and the other\nauthors named in its files.\n\n" + text(oxr / "LICENSES" / "Apache-2.0.txt"))
section("MIT licence text (the OpenXR loader's and the Vulkan headers' MIT option)", text(oxr / "LICENSES" / "MIT.txt"))
section("JsonCpp (public domain, or MIT where that is not recognised)", text(oxr / "src" / "external" / "jsoncpp" / "LICENSE"))
section("jnipp (MIT licence; Quest APK only)", text(oxr / "src" / "external" / "jnipp" / "LICENSE"))
section("android-jni-wrappers (Boost Software License 1.0; Quest APK only)",
        "Copyright 2020-2021, Collabora, Ltd.\n\n" + text(oxr / "LICENSES" / "BSL-1.0.txt"))
section("LLVM libc++ (Apache License 2.0 with LLVM exception; Quest APK only)",
        "libc++ is part of the LLVM project (https://llvm.org), distributed with the Android NDK.\n"
        "Licensed under the Apache License v2.0 (text above) with the following exception:\n\n"
        "---- LLVM Exceptions to the Apache 2.0 License ----\n\n"
        "As an exception, if, as a result of your compiling your source code, portions\n"
        "of this Software are embedded into an Object form of such source code, you\n"
        "may redistribute such embedded portions in such Object form without complying\n"
        "with the conditions of Sections 4(a), 4(b) and 4(d) of the License.\n\n"
        "In addition, if you combine or link compiled forms of this Software with\n"
        "software that is licensed under the GPLv2 (\"Combined Software\") and if a\n"
        "court of competent jurisdiction determines that the patent provision (Section\n"
        "3), the indemnity provision (Section 9) or other Section of the License\n"
        "conflicts with the conditions of the GPLv2, you may retroactively and\n"
        "prospectively choose to deem waived or otherwise exclude such Section(s) of\n"
        "the License, but only in their entirety and only with respect to the Combined\n"
        "Software.\n")
out.write_text("".join(parts), encoding="utf-8", newline="\r\n")
print("wrote", out)
