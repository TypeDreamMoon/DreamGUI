# Default font

`make_default_fonts.py` builds DreamGUI's default text font, `/DreamGUI/DefaultFont_DistanceField`, and the four
font assets it is made of.

## What it does

| Asset | Engine font face | What it is |
|---|---|---|
| `/DreamGUI/DefaultFont_DistanceField` | `RobotoRegular` | the default font: Roboto, with the three style faces below and the CJK font as its fallback |
| `/DreamGUI/DefaultFont_DistanceField_Bold` | `RobotoBold` | its bold face (`BoldFont`) |
| `/DreamGUI/DefaultFont_DistanceField_Italic` | `RobotoItalic` | its italic face (`ItalicFont`) |
| `/DreamGUI/DefaultFont_DistanceField_BoldItalic` | `RobotoBoldItalic` | its bold-italic face (`BoldItalicFont`) |
| `/DreamGUI/DefaultFont_DistanceField_CJK` | `DroidSansFallback` | its fallback (an entry of `Fallbacks`, with no ranges or cultures), for Chinese, Japanese and Korean |

Every one of them uses the multi-channel field from the glyph outlines (`SdfSource` = Outline Multi Channel), which
keeps corners sharp; the single-channel field derived from a bitmap rounds them by a texel or two.

The four new assets start as copies of the existing default, so they keep its sample size, field radius, bold ratio,
emoji data and the rest. On the default itself only the engine font face, `SdfSource`, the fallback list and the three
style faces are set; every other property stays as it is. All five packages are saved.

The script is idempotent. An asset that already exists is kept and its properties are set again; nothing is copied
twice, so it can be rerun after any of the assets has been edited or deleted.

## Why

This is how Slate's own default font is put together: the engine's `Roboto` composite font is Roboto in real
regular, bold, italic and bold-italic faces, with DroidSansFallback behind it for CJK. Text in DreamGUI then has the
same faces as the editor's and UMG's text: real bold and italic outlines rather than a regular face made bolder or
slanted, and CJK from the same fallback.

Switching the default from DroidSansFallback to Roboto re-lays-out every text that uses the default font, since the
two faces' Latin widths differ. To go back, point the default's engine font face at `DroidSansFallback` again.

## Colour emoji

The default font has no colour emoji face, on purpose. The engine's `NotoColorEmoji.ttf` lives under
`Engine/Content/Editor/Slate/Fonts`, which is editor content that a packaged game does not get (Slate itself loads it
only in the editor), and embedding it in the plugin's font asset would add 7.8 MB to every project that uses DreamGUI.
Without one, an emoji draws from the text's emoji data (`EmojiData`, an image per emoji) when it has an entry, and
otherwise from whatever monochrome face has the code point, or as the missing-glyph box. DreamGUI's tests and the
text parity corpus load the engine's file directly, so they need nothing from here.

A game that wants colour emoji ships a colour emoji font of its own and adds it as a fallback:

1. Pick a font whose colour data DreamGUI draws: CBDT/CBLC or sbix bitmap strikes (Noto Color Emoji, OFL), or COLRv0
   layers. A glyph that only has COLRv1 or SVG data counts as missing. Mind the licence: Noto is OFL, Twemoji's art is
   CC-BY, and Segoe UI Emoji may not be redistributed.
2. Make a font asset for the file (a distance-field font). Its own settings hardly matter: a fallback's glyphs are drawn
   into the atlas of the font the text uses, colour glyphs at their own pixel size rather than as a field. That text font
   has to be on the outline field (`SdfSource` Outline Multi Channel, the default) or be a bitmap font: the single-channel
   field's atlas is R8, holds no colour, and draws no colour glyph.
3. Add it to the text font's `Fallbacks` as an entry whose `Ranges` cover the emoji and nothing else, so its digits,
   space and symbols never stand in for the text font's own:

   | Range | What |
   |---|---|
   | `0x23`, `0x2A`, `0x30`-`0x39` | the keycap bases (`#`, `*`, digits), for sequences like `1` U+FE0F U+20E3 |
   | `0xA9`, `0xAE` | (c) and (r) |
   | `0x203C`-`0x3299` | the emoji among the symbols, arrows, dingbats and enclosed ideographs |
   | `0x1F000`-`0x1FAFF` | the emoji blocks, flags included |

   A range is matched against a cluster's first code point; the joiners, variation selectors, skin tones and tags after
   it need no range of their own. Leave `Cultures` empty and `Scale` at 1.

Clusters that ask for emoji presentation -- a pictograph that is drawn as emoji by default, or anything followed by
U+FE0F, a flag, a keycap, a skin tone or a ZWJ sequence -- try the colour faces first (the font's `bPreferColorEmoji`,
on by default). Everything else is in text presentation -- ordinary text, and anything followed by U+FE0E -- and tries
the monochrome faces first, so the digits and symbols in those ranges still come from the text font wherever it has
them. An `EmojiData` entry for the exact sequence still wins over the colour face, and one for the cluster's first code
point only loses to it.

## How to run it

The Python Editor Script Plugin (`PythonScriptPlugin`) must be enabled in the project. Nothing else is needed: the
script uses only what the core editor exposes to Python (`unreal.load_asset`, the asset registry, AssetTools and
`EditorLoadingAndSavingUtils`), not the Editor Scripting Utilities plugin, which is off by default.

- From the editor's command line, which runs the script once the editor has started:

  ```
  UnrealEditor.exe "<Project>.uproject" -ExecutePythonScript="<absolute path>/Tools/Fonts/make_default_fonts.py"
  ```

- Or in a running editor, from the Output Log's console: `py "<absolute path>/Tools/Fonts/make_default_fonts.py"`
  in the `Cmd` console, or, switched to `Python`:

  ```
  exec(open(r"<absolute path>/Tools/Fonts/make_default_fonts.py").read())
  ```

Every step is printed with a `[make_default_fonts]` prefix: which assets were created or kept, each property set,
and each package saved. The DreamGUI plugin the project loads is the one whose `/DreamGUI` content is changed, so
copy the five `.uasset` files back into the repository from that plugin's `Content` folder afterwards.
