# Notice

The MIT license in [LICENSE](LICENSE) covers what is ours in this repository:
the host, the setup scripts, the tools and the documentation. It does not
cover the game.

*Shogo: Mobile Armor Division* and the LithTech engine are © 1998-1999
Monolith Productions, and are not ours to license. Their executables, `.rez`
archives, renderers, game DLLs, music and art are not in this repository and
are excluded by `.gitignore`.

- You need your own copy of the game, patched to 2.2, to use any of this.
  Setup links your install as `game\`; the pipeline reads it from there.
- Lifted code is a derivative work of the binary it came from. Anything the
  pipeline writes into `work/` (the rebased modules, the catalogs) or
  `src/recomp/gen*/` (the lifted C) carries whatever license the original
  carries, not this one. The MIT grant stops at our own work.

The host's module loader comes from the
[Unreal Tournament recomp](https://github.com/sp00nznet/ut99-recomp) and the
layout from the [Crazy Taxi recomp](https://github.com/sp00nznet/crazytaxi-recomp),
both ours and MIT. There is no third-party code in this repository.
