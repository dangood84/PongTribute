# Pong Tribute

A small **Pong** match in a hardware-accelerated window: two paddles, a ball, a dashed net, and a first-to-11 score.

Written in **C11** and drawn with **Raylib**. There is no game engine, no scene editor, and no asset folder. The tones are square waves built in memory. Raylib opens a native window (OpenGL underneath) and the program draws into it from `main`. It is not a terminal program: nothing useful happens in the console except Raylib's own log lines.

The same source is meant to build on:

- Apple Silicon Mac (this tree was compiled on macOS 14 with Homebrew Raylib 6.0)
- Windows 10
- Raspberry Pi OS desktop
- macOS Catalina on a mid-2014 Intel MacBook Pro Retina, by compiling Raylib from source on that machine

How the pieces fit together (same style as the other tributes): `WORKINGS.md` for responsibilities and the bounce math, `EXECUTION_FLOW.md` for a frame-by-frame trace.

## Requirements

- A **C11** compiler (`clang`, `gcc`, or Visual Studio)
- **CMake** 3.16 or newer
- **Raylib** 4.5 or newer if it is already installed (5.x and 6.0 both work with this file)
- **Git**, only when Raylib is not installed and CMake has to download it

`make` is the everyday command, as in the other projects. The Makefile asks CMake to find Raylib or to fetch the pinned tag. It does not cross-compile: run it on the machine you want the binary for.

### macOS — Apple Silicon

Homebrew:

```bash
brew install raylib cmake
```

From the project folder:

```bash
make
make run
```

That writes `build/pong` and opens the window. `./run.sh` does the same.

Homebrew's CMake searches `/opt/homebrew`, so an M-series Mac finds the Raylib you just installed. The binary links the shared library; the OpenGL, Cocoa, and IOKit frameworks are already recorded in that library.

### macOS — Catalina, Intel, mid-2014 Retina

Current Homebrew does not support macOS 10.15, so do not try `brew install raylib` on that laptop. Build on the machine itself:

1. Install the Command Line Tools that still belong to Catalina (Xcode 12.4 era), from Apple's developer downloads if `xcode-select --install` no longer offers them.
2. Install a CMake that still runs on 10.15. CMake 3.31 is a safe choice. CMake 4 may refuse to open.
3. Install Git.
4. From the project folder: `make` then `make run`.

The first configure downloads Raylib **5.5** and compiles it with a deployment target of **10.15**. That takes a few minutes on a 2014 machine. Later builds only recompile `src/main.c`.

If 5.5 does not compile with that old toolchain:

```bash
make CMAKE_ARGS="-DPONG_RAYLIB_TAG=4.5.0"
```

This Apple Silicon Mac has not launched the binary on the 2014 machine. The Catalina path is the source build above, not a copy of `build/pong` from an M-series Mac (that binary is arm64).

### Windows 10

Install Visual Studio 2022 with the **Desktop development with C++** workload (it includes CMake), or install CMake and a MinGW toolchain yourself. Install Git if Raylib is not already on the machine (vcpkg's `raylib` is fine; CMake will use it when `find_package` can see it).

From the project folder in Command Prompt:

```bat
build-win.bat
build\pong.exe
```

If GNU make is on PATH, `make` and `make run` are the same build. A console window may sit beside the game when the toolchain uses the console subsystem. Raylib's log goes there. Close the game window (or press Esc) and both go away.

### Linux / Raspberry Pi OS

Use the **desktop** image. A Lite or SSH-only session has no window to open.

```bash
sudo apt update
sudo apt install build-essential cmake git \
  libgl1-mesa-dev libglu1-mesa-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libxext-dev libxrender-dev \
  libasound2-dev
```

If `libasound2-dev` is missing on a newer Debian, install `libasound-dev` instead. `libraylib-dev` is optional: when the package is new enough, CMake uses it and skips the download.

```bash
make
make run
```

Or `./build/pong` from a terminal on the Pi's own desktop. Raspberry Pi OS Bookworm often runs Wayland; Raylib's window goes through X11 or XWayland. If no window appears, switch the session to X11 in Raspberry Pi configuration, then run it again.

32-bit (`armhf`) and 64-bit (`aarch64`) Pi OS both work. Compile on the Pi. An Apple Silicon `build/pong` will not run there.

## Run

From the project root:

```bash
make
make run
```

```bash
./run.sh
```

```bash
make clean
```

## Using it

The right paddle starts as a computer opponent, so one person can play immediately. First to 11 wins. There is no "win by two".

| Key | Action |
|-----|--------|
| `W` / `S` | Move the left paddle |
| Up / Down | Move the right paddle, once it is a human |
| `A` | Hand the right paddle to the computer, or take it back. The score stays |
| `P` or Space | Pause. The serve clock waits with everything else |
| `R` | New match |
| Esc, or the window's close button | Quit. Esc is Raylib's built-in exit key: the window closes and the process ends. It is not a separate "exit" command |

The same line is drawn along the bottom of the window, including **Esc quit**. That hint is the only menu.

`F` is not a key. Nothing toggles fullscreen, and the window is not resizable. The court stays 960×540 for the whole run.

`A` does nothing after a winner is on screen, so the result line does not change. `R` starts again.

A hit away from the paddle's centre sends the ball off at an angle. Each hit speeds the ball up a little, until it reaches a cap. The computer is slower than the left paddle and wanders back to the middle while the ball is travelling away from it.

## Where it appears

| OS | How you build | What you see |
|----|---------------|--------------|
| **macOS** (Apple Silicon or Catalina) | `make` | A 960×540 window titled Pong Tribute |
| **Windows 10** | `build-win.bat` | The same window, plus a console if the toolchain opens one |
| **Raspberry Pi OS** | `make` on the Pi | The same window on the desktop session |

Closing the window **quits** the process. This is a desk toy, not an OS screensaver module.

## Project layout

```
src/main.c           # the whole match: state, step, draw, main loop
CMakeLists.txt       # finds Raylib, or downloads the pinned tag
Makefile             # make / make run / make clean
run.sh               # macOS and Pi: build if needed, then open the window
build-win.bat        # Windows 10 configure and compile
README.md
WORKINGS.md          # who owns state, and the bounce math
EXECUTION_FLOW.md    # from main to one drawn frame
```
