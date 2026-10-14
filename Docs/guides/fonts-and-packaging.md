# Fonts in a packaged game

What a DreamGUI text needs to look in a packaged game the way it looks in the editor: which fonts ship by themselves and
which you add, how to add a colour emoji font, which ICU data the game has to be packaged with, and how to check a
package. Everything here is what the plugin does on Win64; no other platform has been built (see the README's
[Platforms](https://gui.toolchain.64hz.cn/en/docs/guides/platforms)).

## 1. What ships by itself

**The default font.** `/DreamGUI/DefaultFont_DistanceField` is Roboto in four real faces (regular, bold, italic, bold
italic) with DroidSansFallback behind it for Chinese, Japanese and Korean, every face on the outline multi-channel field
(`Tools/Fonts/README.md` says how it is built). All five assets name an engine font face (`FontType` Engine Font):

- At cook, `BeginCacheForCookedPlatformData` copies each face's bytes into its asset. Outside the editor a font reads
  only those bytes, so nothing has to be staged.
- `/DreamGUI` is always cooked: the plugin's `Config/Game.ini` puts it into `DirectoriesToAlwaysCook`, so even a cook
  of an explicit package list (`-map=`, chunks) gets it.
- DroidSansFallback adds about 3.9 MB to the package.

**Faces per language.** A font's `Fallbacks` entries can name the cultures they are for (`Cultures`, `"zh-Hans"`,
`"ja"`): a text in that language (its `Language`, or a rich text's `<lang=xx>`) tries that face before the font's other
fallbacks, and before the font's own face too when the entry has `bPreferOverPrimary`. Prefer these
entries to `bCultureFont` and its `CultureFontMap`. That switch swaps the font's own face when the game's culture
changes, and it only works for engine font faces with the Inline loading policy: a cooked Lazy Load or Stream face has
no face data to swap to, so the font logs a warning and keeps the face it was cooked with.

## 2. Colour emoji

The default font has no colour emoji face, on purpose. The engine's `NotoColorEmoji.ttf` lives under
`Engine/Content/Editor/Slate/Fonts`, which is editor content a packaged game does not get (Slate loads it only in the
editor), and embedding it in the plugin's font would add 7.8 MB to every project that uses DreamGUI. Without one, an emoji
draws from the text's emoji data (`EmojiData`, an image per emoji) when it has an entry, and otherwise from whatever
monochrome face has the code point, or as the missing-glyph box.

### Pick a font, and mind its licence

DreamGUI draws colour from CBDT/CBLC and sbix bitmap strikes (Noto Color Emoji is one) and from COLRv0 layers. A glyph
that only has COLRv1 or SVG data counts as missing, and a WOFF2 font does not load at all (the engine's FreeType has no
Brotli). Licences differ: Noto Color Emoji is OFL -- ship its licence with the game -- Twemoji's art is CC-BY, and Segoe UI
Emoji may not be redistributed.

### Make a font asset for it, one of three ways

Copy the font file into your project first. Then make a distance-field font asset for it (a *DreamUI FontData
DistanceField* asset in the Content Browser). Its own settings hardly matter: a fallback's glyphs are drawn into the
atlas of the font the text uses, colour glyphs at their own pixel size rather than as a field.

1. **Custom font file, embedded** (`FontType` Custom Font File, `bUseExternalFileOrEmbedInToUAsset` off: the default).
   The editor reads the file and saves its bytes into the asset. Since 2.1 the cook reads the file again when it is
   there, so an asset saved before the file changed does not ship stale bytes, and a cook that finds nothing to embed
   is an error rather than a font that silently has no face.
2. **Engine font face** (`FontType` Engine Font). Import the file as a Font Face and name it in `EngineFont`; the cook
   copies its bytes, as it does the default font's. The most robust of the three.
3. **External file** (`bUseExternalFileOrEmbedInToUAsset` on). The game reads the file at run time, so it has to be
   staged -- `+DirectoriesToAlwaysStageAsUFS=(Path="Fonts")` under `[/Script/UnrealEd.ProjectPackagingSettings]` in
   `Config/DefaultGame.ini` for files in `Content/Fonts` -- and `FontFilePath` has to be relative to the project
   directory (`bUseRelativeFilePath`). An absolute path never works in a packaged game.

### Add it as a fallback of the text font, for the emoji only

Add it to the text font's `Fallbacks` as an entry whose `Ranges` cover the emoji and nothing else, so that its digits,
space and symbols never stand in for the text font's own:

| Range | What |
|---|---|
| `0x23`, `0x2A`, `0x30`-`0x39` | the keycap bases (`#`, `*`, digits), for sequences like `1` U+FE0F U+20E3 |
| `0xA9`, `0xAE` | (c) and (r) |
| `0x203C`-`0x3299` | the emoji among the symbols, arrows, dingbats and enclosed ideographs |
| `0x1F000`-`0x1FAFF` | the emoji blocks, flags included |

A range is matched against a cluster's first code point; the joiners, variation selectors, skin tones and tags after it
need no range of their own. Leave `Cultures` empty and `Scale` at 1.

**The text font has to hold colour.** It must be on the outline field (`SdfSource` Outline Multi Channel, the default)
or be a bitmap font. A font on the single-channel field keeps its atlas in R8 -- one channel, no colour -- and draws no
colour glyph at all: its emoji fall back to `EmojiData` or a monochrome face.

Clusters that ask for emoji presentation -- a pictograph drawn as emoji by default, anything followed by U+FE0F, a flag,
a keycap, a skin tone, a ZWJ sequence -- try the colour faces first (the font's `bPreferColorEmoji`, on by default).
Everything else is in text presentation and tries the monochrome faces first, so the digits and symbols in those ranges
still come from the text font wherever it has them. An `EmojiData` entry for the exact sequence still wins over the
colour face.

**A material of your own.** Colour glyphs, like small-text coverage glyphs and gradient paints, are decoded by
DreamGUI's own shading (`MF_DreamUI_Shade`); a material shades through it when it carries the function's
`DreamUI_ShadeMarker` parameter, as DreamGUI's own materials do. Since 2.1 a text whose material does not lays its emoji
out as if the font had no colour face, rather than drawing a colour bitmap as a distance field; it also gets no coverage
glyphs, and paints its gradients by vertex colour.

### What it costs

About 7.8 MB of memory for Noto Color Emoji's bytes, held by its font asset, and the colour glyphs themselves in the text
font's atlas, one per emoji and size bucket. An atlas slice is 2048 x 2048 BGRA by default: 16 MiB on the GPU and as much
again for the copy the font keeps on the CPU, up to `MaxFontAtlasSlices` (8) slices per font. The console command
`DreamGUI.Memory` prints what every font's atlas holds -- slices, GPU and CPU bytes, cells, field, colour and coverage
glyphs -- and `DreamGUI.Memory Json` the same as JSON.

## 3. What the libraries do in a cooked game

A packaged game links the same libraries as the editor:

- **FreeType 2.14.1** (the engine's Win64 Release library): PNG strikes (CBDT, sbix) and COLRv0 layers work; there is
  no Brotli, so WOFF2 fonts do not load; SVG glyphs are skipped and COLRv1-only glyphs count as missing.
- **HarfBuzz 2.4.0** shapes text on every target but a dedicated server, which draws no text.
- **ICU 64** breaks lines and words and answers which names a culture falls back to. `Emoji_Presentation` and
  `Extended_Pictographic` are compiled into the library, so emoji detection works under any packaging preset; newer
  emoji come from the plugin's own tables.

Only the Win64 libraries have been used. The other platforms' builds of these libraries have never been linked into
DreamGUI. What a cooked game does with them is what the packaged text smoke test (section 5) checks; until the release
gate has run it for a version (see [Platforms](https://gui.toolchain.64hz.cn/en/docs/guides/platforms)), what this section says of a
cooked game is what the editor does with the same libraries.

## 4. ICU data: package EFIGSCJK, or All

How much of ICU's data a game carries is the packaging preset's choice: **Project Settings > Packaging >
Internationalization Support** (`InternationalizationPreset`), English by default; `-I18NPreset=` on `BuildCookRun`
overrides it. The editor always has all of it, so text can lay out one way in the editor and another in the package.

**Package with EFIGSCJK** for any game with Chinese, Japanese or Korean text, **or All** for Thai, Lao, Khmer or
Burmese. Under the English preset:

- **The game cannot switch its culture** to Chinese or Japanese at all.
- **Line breaking loses its dictionaries.** English carries the basic character, word and line rules only. CJK text then
  breaks between any two characters with no dictionary for phrases (`PhraseWrap`, CSS `word-break: auto-phrase`, falls
  back to per-character breaks) and without the Japanese and Chinese line tailorings; Thai, Lao, Khmer and Burmese,
  which need the dictionaries only `All` has, break only at spaces.

**Chinese fallbacks by script.** The engine finds the script a culture is written in from ICU's likely subtags, which
neither the English nor the EFIGSCJK data carries: in a packaged game `"zh-CN"` falls back to `zh-CN, zh` only, where
the editor has `zh-Hans-CN, zh-CN, zh-Hans, zh`. Since 2.1 DreamGUI puts the script back for Chinese names -- `zh-CN`,
`zh-SG` and `zh` get `Hans`, `zh-TW`, `zh-HK` and `zh-MO` get `Hant` -- so fallback entries for `"zh-Hans"` and
`"zh-Hant"` match in the package as in the editor. Before, they never matched a zh-CN text in a package, and a font with
a Japanese and a Simplified Chinese fallback could draw Chinese ideographs in their Japanese forms. Entries for other
languages are matched by language and region, which every preset has.

**Line breaking follows the game's culture.** Since 2.1 the line and word iterators are made for the current culture's
locale (FInternationalization's current culture) and made again when it changes. Before, they followed the operating
system's language, so the same game broke Japanese lines differently on a Japanese and an English Windows.

## 5. Checking a package

- `DreamGUI.Memory` in a packaged game's console (Development) prints what the fonts, sprite atlases and canvases hold;
  `DreamGUI.Memory File=<path>` writes it as JSON, under `Saved/` for a relative path.
- The packaged text smoke test in the plugin's test host puts a screen of these cases -- small text, a mixed line,
  Chinese and Japanese from culture fallbacks, colour emoji, a justified paragraph, a Japanese paragraph, a safe zone --
  on a packaged game and on the same build run on uncooked content, and holds the two to each other field by field --
  the safe zone each against its own viewport (`Tools/TestHost/README.md`, "The packaged text smoke test"). The release
  gate runs it before a version is tagged.
