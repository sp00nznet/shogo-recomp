# Bring-up

What Shogo needed, in the order it turned up.

## 1. The build and the modules

The US CD carries the 1.0 game in plain folders (`Game\`, `Data\SHOGO.REZ`);
no installer has to run. The last official patch, `shogo_2214_full.zip`
("Version 2.2", March 1999), is on the CD Gaming 16 magazine disc on
archive.org (`cd-gaming-16`, `Patches/shogomad/`). Unzipped over the game it
replaces `Client.exe`, `server.dll`, `d3d.ren` and adds `SHOGOP2..4.REZ`.

`Client.exe` is the engine (linked at `0x00400000`, no relocations). Every
other module Monolith wrote was linked at `0x10000000`, and four of them live
inside the rez files rather than on disk:

| Module | Base here | Functions |
|---|---|---|
| `Client.exe` | `0x00400000` | 3,335 |
| `server.dll` | `0x10000000` | 2,537 |
| `d3d.ren` | `0x10100000` | 987 |
| `CSHELL.DLL` (in `SHOGOP3.REZ`) | `0x10200000` | 2,287 |
| `CRES.DLL` (in `SHOGOP3.REZ`) | `0x10300000` | 97 |
| `OBJECT.LTO` (in `SHOGOP3.REZ`) | `0x10400000` | 4,323 |
| `SRES.DLL` (in `SHOGOP3.REZ`) | `0x10600000` | 97 |
| `soft.ren` | `0x10700000` | 904 |

`tools/rez.py` reads LithTech's "RezMgr Version 1" archives; `tools/rebase.py`
pulls the game DLLs out of `SHOGOP3.REZ` (2.2's; `SHOGO.REZ` and `ShogoP.rez`
carry the 1.0 and 2.0 builds) and applies each module's base relocations for
its new base. `tools/catalog.py` (pcrecomp `rtti.py` + `disasm32.py`) and
`run_lift.py` are the Unreal Tournament recomp's multi-module drivers: one
`src/recomp/gen/<module>/` per module, one dispatch table over all of them.
The lift has 0 errors.

## 2. Loading the game DLLs

The engine copies `cshell.dll`, `cres.dll`, `object.lto` and `sres.dll` out of
the rez to `%TEMP%` and `LoadLibrary`s the copies. The host's `LoadLibraryA`
matches by file name, then by the PE timestamp in the file named, and attaches
the lifted module (its DllMain, after the modules it imports). A PE it did not
lift (`de_msg.dll`, `ima.dll`) loads natively.

## 3. DirectInput

`DirectInputCreateA` with the guest's `HINSTANCE` (`0x00400000`, a mapped
image, not a module Windows loaded) fails with `E_INVALIDARG`, and the engine
quits at once. KotOR and Messiah hit the same and fixed it in their hosts; it
is now a native32 built-in (`pcrecomp-lithtech`).

## 4. Direct3D

On this machine DirectDraw has no hardware 3D (a 3D-device surface in video
memory fails with `DDERR_NODIRECTDRAWHW`), so `d3d.ren` found no device and
the engine fell back to `soft.ren` (which is lifted too, and also needs
Direct3D). `+RGBRast 1` makes `d3d.ren` use Direct3D's RGB software
rasteriser with system-memory surfaces.

Then "Unknown DirectDraw surface format": windowed, the render surface takes
the desktop's 32-bit format and the renderers only draw 16-bit. The host
re-runs itself under Windows' `16BITCOLOR` compatibility layer (reduced color
mode), which gives DirectDraw a 16-bit primary.

The runs then died at random inside Direct3D (from `d3d.ren`'s model
drawing): an execute fault in heap memory, sometimes heap corruption first.
Direct3D's RGB rasteriser (`d3dim700.dll`) builds span code in memory it got
from the heap and jumps to it; a 1998 process could run that, this
`/NXCOMPAT` host cannot (the bridge needs DEP for its callbacks). The host
makes such a region executable on its first fetch, as the UT99 recomp does
for Galaxy's mixers. Even then the span code sometimes read or wrote wild: `d3d.ren` passes
`D3DDP_DONOTCLIP` to most of its `DrawPrimitive` calls (it clips itself, and
the hardware of the day had a guard band), and the RGB rasteriser has none.
The host gives the device a copy of its vtable whose `DrawPrimitive` and
`DrawIndexedPrimitive` clear that flag. With both, a run plays the menus into
the first mission.

## 5. Miles

`AIL_startup`, called from the lifted engine, faulted inside Windows on a wild
read in most runs. Called once from the host's own stack before the guest
starts, it has not; the engine's own call is answered by a shim.

## 6. Input in the harness

DirectInput's keyboard asked for `DISCL_FOREGROUND`, and a test run's window
is never in front, so `--key` presses never arrived (the title screen waits
for a key). `tools/rebase.py` patches the two `SetCooperativeLevel` flag
pushes in `Client.exe` to `DISCL_BACKGROUND | DISCL_NONEXCLUSIVE`.

## 7. Menu keys

The title screen's "any key" is read from DirectInput, but the menus read
`WM_KEYDOWN`, which a window that is not in front never gets. `--key` sends
both: `SendInput` for DirectInput and a posted `WM_KEYDOWN`/`WM_KEYUP` to the
game window. Space, then Enter five times (single player, new game, normal,
past the two intro captions), then Space past the briefing is the mission.

## 8. Screenshots

A GDI copy of the screen is black. `--shot` locks `d3d.ren`'s back buffer,
right after the renderer's present (which the engine calls through the
renderer's function table, so the host wraps it in `recomp_lookup_manual`):
any other moment catches the frame half drawn, without the menu text.

## Debugging aids

The renderer (`sub_10110200`) and the engine console (`sub_00416000`) print
nowhere. During bring-up a `printf` was spliced into the start of both in the
generated C; that is how the messages quoted above were read.
