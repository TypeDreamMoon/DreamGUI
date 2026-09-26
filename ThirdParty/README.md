# Third-party code carried by DreamGUI

## msdfgen

[msdfgen](https://github.com/Chlumsky/msdfgen) by Viktor Chlumsky (MIT) turns glyph outlines into
multi-channel signed distance fields. `FDreamGlyphSdf`
(`Source/DreamGUI/Private/Core/Text/DreamGlyphSdf.cpp`) compiles it into the one translation unit
that rasterises a glyph.

| | |
|---|---|
| Upstream | <https://github.com/Chlumsky/msdfgen> |
| Submodule | `ThirdParty/msdfgen`, branch `all-in-one`, pinned to `b32de12` (2025-07-29) |
| Compiled from | `ThirdParty/msdfgen-single-file/msdfgen.cpp` (the generated pair, committed) |
| Used by | the `DreamGUI` runtime module, under `WITH_FREETYPE` |
| License | MIT; the text is embedded in the generated `msdfgen.cpp` |

### Why there are two directories

Upstream does not ship a single-file distribution to download. `all-in-one/generate.py` concatenates
`core/` and `ext/` into one header plus one source file, and `all-in-one/.gitignore` ignores both.
That is also how the engine carries it: `Engine/Source/ThirdParty/msdfgen/` is where a **source**
build ends up with the generated pair, while a launcher (installed) engine ships only `msdfgen.tps`
there — which is why the glyph rasteriser could not be built against a binary engine.

So the repository keeps both halves:

- **`msdfgen/`** — the submodule. Upstream sources, `all-in-one/generate.py`, the license. Never
  compiled, never included: it exists so the exact revision behind the generated pair is pinned and
  so the pair can be regenerated.
- **`msdfgen-single-file/`** — the generated `msdfgen.h` + `msdfgen.cpp`, committed. This is what
  `DreamGlyphSdf.cpp` includes (`#include "msdfgen.cpp"`, via the include path `DreamGUI.Build.cs`
  adds). The header is pulled in by the source file.

Committing the generated pair is what keeps a plain `git clone` — and a GitHub zip download, which
carries no submodule contents — a buildable checkout: nothing has to be generated, and no Python is
needed at build time. The cost is that the pair can drift from the submodule, which is what
`Tools/UpdateMsdfgen.ps1 -Check` exists to catch.

### Why the pin is `all-in-one` and not a release tag

The `all-in-one` branch is upstream's amalgamation branch: it holds `generate.py` and the source list
it concatenates. `master` does not carry it, and the pair has to be generated from a revision whose
API matches the calls in `DreamGlyphSdf.cpp`.

`b32de12` (2025-07-29) is the last of those before msdfgen 1.13, and the API there is the one the
engine's own copy has: `generateMTSDF(const BitmapRef<float, 4> &, ...)`, `Shape::inverseYAxis`,
`ErrorCorrectionConfig` with an optional caller buffer. 1.13 replaced `BitmapRef` with
`BitmapSection` and reworked Y-axis orientation, so moving the pin forward means editing the
rasteriser, not just regenerating.

### `MSDFGEN_PARENT_NAMESPACE`

`DreamGlyphSdf.cpp` defines `MSDFGEN_PARENT_NAMESPACE` as `DreamMsdfgen` before including the pair,
which the generator wraps the whole library in when the macro is set. SlateCore compiles its own copy
of msdfgen for its SDF fonts, and in a monolithic build every one of those symbols would otherwise
collide with ours. Two copies that only look similar is worse than a link error, which is why the
namespacing is not optional — it is why every call in the rasteriser reads `msdfgen::`.

## Regenerating the pair

```powershell
git submodule update --init ThirdParty/msdfgen
pwsh -NoProfile -File Tools\UpdateMsdfgen.ps1            # rewrite the pair from the pinned commit
pwsh -NoProfile -File Tools\UpdateMsdfgen.ps1 -Check     # exit 1 if the committed pair is stale
```

Moving to another upstream revision is `-Ref <branch|tag|commit>` on the same script, followed by
editing `Source/DreamGUI/Private/Core/Text/DreamGlyphSdf.cpp` if the API moved, then committing the
regenerated pair **and** the submodule pin in `ThirdParty/msdfgen` together.
