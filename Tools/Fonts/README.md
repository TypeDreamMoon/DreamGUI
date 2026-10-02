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
| `/DreamGUI/DefaultFont_DistanceField_CJK` | `DroidSansFallback` | its fallback (`FallbackFontArray`), for Chinese, Japanese and Korean |

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
