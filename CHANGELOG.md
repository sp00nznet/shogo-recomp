# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[SemVer](https://semver.org/).

## [Unreleased]

## [0.1.0] - 2026-11-20

- Builds against pcrecomp main; the `pcrecomp-lithtech` branch is merged.
  `CMakeLists.txt` uses main's `cmake/pcrecomp_gen.cmake` again.

### Fixed
- The build configures against `pcrecomp-lithtech` again: the generated C was
  added through pcrecomp-hostfixes' `cmake/pcrecomp_gen.cmake`, which that
  branch lacks; without it `CMakeLists.txt` adds the files itself, at `/Od`.

### Changed
- The generated C is compiled at `/Od` (`-DPCRECOMP_GEN_OPT=ON` for `/O2`):
  minutes instead of about 25 more.
- README in the redalert2-recomp order.

### Added
- `NOTICE.md` (replaces `NOTICE`), `CONTRIBUTING.md`, `CONTRIBUTORS.md`,
  `docs/building.md`, `docs/testing.md`, and a `setup.sh` that says there is
  no Linux or macOS build yet.
- `Setup.cmd` (`tools/setup.ps1`): one-click setup from your installed, 2.2-patched
  copy. Checks Python, `pefile`/`capstone`, pcrecomp (`pcrecomp-lithtech`) and
  Visual Studio, links the game as `game\`, rebases, catalogs, lifts, builds and
  leaves a `Shogo` shortcut.
- `launcher/launcher.json` and a hand-run `installer` workflow for
  recomp-launcher.
- `CHANGELOG.md`, `ROADMAP.md`; README rewritten in the pcrecomp house style.
- Into the first mission: menus, new game, difficulty, the briefing and the
  Ambush level, driven by `--key` (`docs/bringup.md` sections 6 and 7).
- The `--shot` back-buffer capture.
- First boot of Shogo 2.2 on pcrecomp `native32`: 8 modules, 14,567 functions,
  0 lift errors, the main menu drawn by the lifted `d3d.ren`.
