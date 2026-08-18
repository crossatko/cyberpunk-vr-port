# Building on Linux

Both plugins are Windows DLLs, but they can be built on Linux by cross-compiling with
`clang-cl` against the real Windows SDK. Nothing about the source is Linux-specific — the
output is the same MSVC-ABI DLL the Windows build produces — so this is only about getting a
Windows toolchain onto a Linux box.

The game itself runs under Proton, so the whole edit/build/test loop stays on Linux.

## Prerequisites

    clang lld llvm cmake ninja python3        # llvm provides llvm-rc / llvm-lib / llvm-mt

**Clang 19 or newer.** The MSVC standard library shipped with the current Windows SDK refuses
anything older, and says so rather than failing obscurely:

    yvals_core.h: error STL1000: Unexpected compiler version, expected Clang 19.0.0 or newer

Rolling distributions (Arch, Fedora, openSUSE Tumbleweed) are already past that. Debian and
Ubuntu are not: 24.04 ships Clang 18, and installs `clang-cl` and `lld-link` only under
`/usr/lib/llvm-<ver>/bin` with no unversioned symlinks, so a newer toolchain has to come from
[apt.llvm.org](https://apt.llvm.org) and that directory has to be on `PATH`.

CI cross-compiles in an `archlinux:latest` container for the same reason.

Then fetch the Windows SDK and CRT with [`xwin`](https://github.com/Jake-Shadle/xwin):

    cargo install xwin
    xwin --accept-license splat --output ~/winsdk

`~/winsdk` is where the toolchain file looks by default (~640 MB). Set the `XWIN` environment
variable to override it:

    export XWIN=/opt/winsdk

## One-time SDK fixups

    ./tools/linux/apply-sdk-clang-fixes.sh

RED4ext.SDK needs two adjustments to compile under clang-cl. The script is idempotent; re-run
it after updating the submodule.

1. **Case-sensitive filesystem.** Sources include `RED4ext/rtti/...` while the directory is
   `RTTI/`. Windows does not care, ext4 does. The script symlinks it.
2. **`Handle<T>` on forward-declared types.** `Handle`'s destructor carries a `static_assert`
   that clang instantiates where MSVC does not, so seven generated headers need the real
   definition of the type they hold. The script inserts those includes.

## Build

Two separate CMake roots:

    # stereo module (OpenXR + Dear ImGui arrive via FetchContent)
    cmake -B build-stereo -S . -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE=tools/linux/toolchain-clang-cl.cmake \
          -DCMAKE_BUILD_TYPE=Release
    ninja -C build-stereo CyberpunkVR_Stereo.dll

    # hands plugin
    cmake -B build-hands -S src/red4ext_plugin -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE=../../tools/linux/toolchain-clang-cl.cmake \
          -DCMAKE_BUILD_TYPE=Release
    ninja -C build-hands CyberpunkVR_Hands.dll

Copy the results into the game's `red4ext/plugins/<name>/` directories.

## What the toolchain file does, and why

Most of it exists because a cross build has to be told to ignore the host system:

- **`/X` plus `/imsvc`** — `/X` drops the default include search, and each SDK directory is
  re-added as a system include. Without it, glibc headers leak into the Windows build and fail
  in confusing ways (`_G_fpos_t` errors inside MSVC headers).
- **`CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL`** — xwin ships only the release CRT. Anything
  that pulls in the debug runtime fails to link on missing `msvcrtd.lib`, so the release runtime
  is pinned for every configuration, `try_compile` included.
- **`CMAKE_FIND_ROOT_PATH*`** — without this, `find_package()` searches the host. OpenXR's
  loader finds the system `jsoncpp` and drags `/usr/include` into a Windows target.
- **`CMAKE_RC_FLAGS_INIT`** — `llvm-rc` does not honour `/imsvc`, so the resource compiler gets
  its include paths separately, or `winres.h` is not found.

## Testing under Proton

Diagnostics are written relative to the process working directory, which is `bin/x64/`, not the
game root — `cyberpunkvrport.log` and the VRIK dumps land there.

Some values worth knowing while debugging:

- Steam launch options: `-modded --launcher-skip -skipStartScreen`
- `vrport.ini` and `vrport-launcher.ini` are CRLF; `sed` patterns written for LF silently no-op
  on them
- `vrport-launcher.ini` is generated at runtime. Deleting it stops the game launching
- `VRIK_LOG_ENABLED=1` at build time turns on the per-step breadcrumb log, which is the only
  practical way to localise a fault that kills the process before a logger can flush

If the game hard-crashes rather than exiting, the process stays alive in its crash handler and
holds the Wine prefix; kill it before relaunching.
