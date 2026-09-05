# Third-Party Notices

This file records third-party software and prior work used by the ESR plugin
suite. It does not replace or broaden any upstream license or permission. The
repository `LICENSE` applies only to code authored here.

## D2RLoader PluginSDK

The plugins target and vendor the D2RLoader PluginSDK.

- upstream: https://github.com/D2RLoader/PluginSDK
- tag / commit: `v4` (`6eb8f8b6192868214706bd6d528c5294f2f551b7`)
- license: MIT, copyright 2026 D2RLoader contributors
- retained paths: `third_party/PluginSDK/include/D2RLPlugin/`,
  `third_party/PluginSDK/cmake/`, `third_party/PluginSDK/LICENSE`

The complete MIT text is retained at `third_party/PluginSDK/LICENSE`. Plugins
built against this pin use the PluginSDK ABI; a different SDK major version
will not load.

## D2MOO

Used as a behavioural reference for the original Diablo II game logic while
reverse engineering Diablo II: Resurrected. No D2MOO code is compiled into
these plugins.

- upstream: https://github.com/ThePhrozenKeep/D2MOO
- license: MIT
- The D2MOO README states the source is intended for non-commercial use and
  that credits to the team are appreciated. Credit is given here gladly. This
  project is non-commercial.

## RuffnecKk D2RLoader Suite

Used as a cross-reference for locating functions in the current Diablo II:
Resurrected build. Where a function address in this project matches one
published there, that prior work is acknowledged.

- upstream: https://github.com/RuffDood/RuffnecKk-D2RLoader-Suite
- license: MIT, copyright 2026 RuffnecKk and contributors

## Diablo II: Resurrected

This project is a modification. It contains no Blizzard Entertainment code,
binaries, executable dumps, disassembly databases, or game assets, and none
will ever be committed to this repository. A legally obtained and separately
installed copy of the game is required to use anything here.

Diablo, Diablo II, Diablo II: Resurrected, Battle.net and Blizzard
Entertainment are trademarks or registered trademarks of Blizzard
Entertainment, Inc. in the U.S. and other countries.

This project and its authors are in no way associated with or endorsed by
Blizzard Entertainment, Inc.
