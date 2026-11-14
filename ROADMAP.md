# Roadmap

## Next
- **Play the first mission**: move and fire in Ambush, past the first
  encounter, and report what breaks.
- **Sound**: runs are muted (`+soundenable 0`); turn Miles on and check it.
- **A conformance harness** (REPO_RULES section 9): a fixed set of milestones
  (dry-run bind counts, boot to the main menu, the scripted path into Ambush),
  each checked by a screenshot or a log line, with a pass count in the README
  and a `QA` line in the netlab recipe. Today there is no harness; the dry run
  (`build\shogo.exe` without `--run`) is the only automatic check.
- **Headless mode** (section 10): `--headless --record out.mp4`, an offscreen
  window piped to ffmpeg, so runs work over RDP and on the test VMs.
- **Finish a farm build**: the first `netlab build shogo-recomp` stalled on the
  generated C at `/O2` under clang-cl; it is now compiled at `/Od`, so build
  it on the farm again and add a `QA` line.

## Later
- The Direct3D hardware path (only the RGB software rasterizer has run).
- Fullscreen, and the 32-bit desktop without the `16BITCOLOR` relaunch.
- Multiplayer (`ShogoSrv.exe` is not lifted).
- A game-file helper that builds the 2.2 folder from the CD and the patch zip.

## Out of scope
- Shipping any game file, the patch, or the generated C.
