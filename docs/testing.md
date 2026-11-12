# Testing

There is no conformance harness yet ([ROADMAP.md](../ROADMAP.md)). Two checks
stand in for it.

## The dry run

`build\shogo.exe` with no `--run` maps all eight modules, binds every import
and stops, without opening a window:

```
[bind] 0x00400000: 209 native, 0 guest, 12 shimmed, 0 unresolved
[bind] 0x10000000: 104 native, 0 guest, 9 shimmed, 0 unresolved
...
[bind] 0x10700000: 66 native, 0 guest, 6 shimmed, 0 unresolved
(dry run: mapped and bound; --run enters 0x00467ED0)
```

Eight `0 unresolved` lines is a pass.

## The scripted path into Ambush

```
build\shogo.exe --run --key 0x20@25 --key 0x0D@35 --key 0x0D@45 --key 0x0D@55 ^
    --key 0x0D@65 --key 0x0D@80 --key 0x20@115 --shot 130 ambush.bmp ^
    -- +windowed 1 +RGBRast 1 +soundenable 0
```

Space, Enter five times (single player, new game, normal, past the two intro
captions), Space past the briefing. `ambush.bmp` should show the mech's HUD in
the first level, as `docs/screenshots/ambush.png` does. It opens a window, so
run it through offstage at the console. `--key` reaches a window that is not
in front (bringup.md sections 6 and 7), and `--shot` reads the renderer's back
buffer (section 8), since a GDI copy of the screen is black.

`--watchdog S` stops a stuck run and prints where every thread was.
