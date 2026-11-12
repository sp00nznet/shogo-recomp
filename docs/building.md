# Building and the command line

`Setup.cmd` does all of this for you. This is what it does, by hand, and the
rest of the details.

## The setup script

`Setup.cmd` (`tools/setup.ps1`) checks for Python 3.10+, the `pefile` and
`capstone` packages, the pcrecomp toolkit and Visual Studio 2022 with the C++
x86 tools, and **asks** before installing any of them (`-Yes` installs without
asking). The toolkit is `..\pcrecomp`, cloned on its **`pcrecomp-lithtech`**
branch if missing (`PCRECOMP` points elsewhere); `main` lacks the DirectInput
`HINSTANCE` shim, and setup refuses a checkout without it. It looks for Shogo
in the usual install folders or asks (`-GameDir DIR`), links it as `game\`,
relocates the modules, catalogs, lifts and builds `build\shogo.exe`, and
leaves a `Shogo` shortcut. A rerun skips finished steps; the details are in
`setup.log`.

There is no Linux or macOS build: `native32` is MSVC x86 inline assembly, and
`setup.sh` says so and exits.

## Step by step

Prerequisites: Windows 10/11, **Python 3.10+** (`py -3 --version`), **git**,
**Visual Studio 2022** (or its Build Tools) with *Desktop development with
C++*, and pcrecomp cloned **beside** this repository as `pcrecomp`, on the
`pcrecomp-lithtech` branch:

```
some-folder\
  pcrecomp\        <- git clone -b pcrecomp-lithtech https://github.com/sp00nznet/pcrecomp
  shogo\           <- this repository
```

1. Python packages:
   ```
   py -3 -m pip install --user pefile capstone
   ```
2. Point `game` at your 2.2 folder:
   ```
   mklink /J game D:\Games\Shogo
   ```
3. Relocate the modules (the game DLLs come out of `SHOGOP3.REZ`):
   ```
   py -3 tools\rebase.py
   ```
   Expected, at the end: `soft.ren     0x10700000-0x1076b000`.
4. The function catalog, all 8 modules in parallel:
   ```
   py -3 tools\catalog.py
   ```
   Each module's log is `work\<module>.catalog.log`; `Client.exe`'s ends
   `[*] Functions: 3335  (thunks=5, leaves=935)`.
5. Recompile to C, then build (MSVC's 64-bit-hosted x86 compiler, CMake and
   Ninja; all three come with the C++ workload):
   ```
   py -3 run_lift.py
   build.cmd
   ```
6. Check it without opening a window:
   ```
   build\shogo.exe
   ```
   Expected: eight `[bind] ... 0 unresolved` lines, then
   `(dry run: mapped and bound; --run enters 0x00467ED0)`.
7. Play: `build\shogo.exe --run -- +windowed 1 +RGBRast 1`.

The usual trip-ups: `python` opening the Microsoft Store (Windows' alias
placeholder; use `py -3`, or turn the alias off in *Settings > Apps > Advanced
app settings > App execution aliases*); a freshly installed Python not on
`PATH` until you open a new window; and a pcrecomp on `main` instead of
`pcrecomp-lithtech` (DirectInput then fails and the engine quits at once).

## The command line

```
build\shogo.exe --run -- +windowed 1 +RGBRast 1 +soundenable 0
build\shogo.exe --run --key 0x20@25 --key 0x0D@35 --key 0x0D@45 --key 0x0D@55 ^
    --key 0x0D@65 --key 0x0D@80 --key 0x20@115 --shot 130 ambush.bmp ^
    -- +windowed 1 +RGBRast 1 +soundenable 0
```

The second line is the scripted path into the first mission: Space, Enter five
times (single player, new game, normal, past the two intro captions), Space
past the briefing.

| Flag | |
|---|---|
| `--run` | enter the game (without it: map, bind and stop) |
| `--game DIR` | the game folder (default `game`) |
| `--rebased DIR` | the relocated modules (default `work\rebased`) |
| `--key VK@S` | press a virtual key S seconds in (DirectInput and `WM_KEYDOWN`) |
| `--shot S FILE.bmp` | save the renderer's back buffer at S seconds (up to 8) |
| `--watchdog S` | stop after S seconds and say where every thread was |
| `--trace-at S` | one second of the native-call trace at S seconds |
| `--native-trace`, `--callbacks` | one line per call into Windows, or back from it |
| `--no-16bit` | skip the relaunch under the `16BITCOLOR` compatibility layer |
| `-- ...` | everything after goes to the engine as console variables |

The host runs itself again under Windows' "reduced color mode" compatibility
layer: the renderers only draw 16-bit surfaces, and in a window they take the
desktop's format.

## Build options

`PCRECOMP` (Python) and `-DPCRECOMP=` (CMake, through
`set CMAKE_ARGS=...` for `build.cmd`) point at a toolkit checkout other than
`..\pcrecomp`; the lifter and the runtime must come from the same tree.
The generated C is compiled at `/Od` (`-DPCRECOMP_GEN_OPT=ON` for `/O2`, which takes about 25 minutes more). On the farm: `netlab build shogo-recomp`. What the game needed, in the order it
turned up, is [docs/bringup.md](docs/bringup.md).

