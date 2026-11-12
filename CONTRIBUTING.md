# Contributing

Issues and pull requests are welcome.

## The one rule

**Nothing from the game goes in the repository.** No game files, no rebased
modules or catalogs (`work/`), no lifted C (`src/recomp/gen/`), no
disassembly listings, nothing generated from `Client.exe` or the game DLLs.
The tool ships; its output is made on each person's machine from their own
copy. `.gitignore` covers the usual paths; check `git status` before you
commit anyway. Screenshots of the game running are fine.

## Before a pull request

1. Build from a clean lift: `py -3 tools\rebase.py`, `py -3 tools\catalog.py`,
   `py -3 run_lift.py` (expected: 0 errors), then `build.cmd`.
2. The dry run must bind everything: `build\shogo.exe` prints eight
   `[bind] ... 0 unresolved` lines.
3. The scripted path into the first mission must still get there
   ([docs/testing.md](docs/testing.md)). There is no conformance harness yet;
   that and the dry run are the checks.

## Where your code comes from

Original work or MIT-compatible only. Don't port anything from GPL or
unlicensed projects (Wine, ReactOS, DOSBox, emulators, leaked SDKs), and say
where anything non-obvious came from.

## Where a fix belongs

- A bug in the lifter, the catalog or `native32` belongs in
  [pcrecomp](https://github.com/sp00nznet/pcrecomp), not here.
- A fix to the game itself is a patch in `tools/rebase.py` or `run_lift.py`,
  with a comment that says what it fixes and why.
- A wall you got past goes in `docs/bringup.md`: what it looked like, what it
  was, what moved it.
