#!/bin/sh
# There is no Linux or macOS build of the Shogo recomp yet. The host runs on
# pcrecomp's native32, which is MSVC x86 inline assembly, and a Wine or
# cross-compile route has not been tried. Use Setup.cmd on Windows.
echo "Shogo recomp: no Linux or macOS build yet (native32 needs MSVC on Windows)." >&2
echo "Run Setup.cmd on Windows. See docs/building.md." >&2
exit 1
