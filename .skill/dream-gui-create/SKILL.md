---
name: dream-gui-create
description: Write a new DreamGUI screen (.dui) from a plain-language description, then check it, lay it out and fix what the layout shows until it is right. Use when asked to create, author, add, design or write a .dui file, a DreamGUI screen, menu, HUD, dialog or widget Blueprint from text in an Unreal project.
---

# dream-gui-create `<what the screen is>`

`.dui` is not in your training data. It looks a little like QML and is not QML. Do not guess names: look them up. A
screen that compiles can still be laid out wrong, so you are not done when it compiles: you are done when `layout`
reports no warnings at the sizes that matter.

Paths are relative to the game project's root. The plugin is `Plugins/DreamGUI/`.

## With DreamMCP (the `dream_gui` tool)

Every call is `dream_gui { "action": "<action>", "args": { ... } }`. `help` lists an action's arguments.

1. **Read the language once.** `reference` lists the pages; read `dui` (the language), then `example` and `pitfalls`.
   Without the tool, read `Plugins/DreamGUI/.skill/reference/dui.md`, `example.md` and `pitfalls.md`.
2. **Look up every name before you write it.** One call per node type and per property you are not sure of:
   - `lookup { "name": "Text" }`: a node type's properties, with their types and defaults.
   - `lookup { "name": "Text.FontSize" }`: one property.
   - `lookup { "name": "VerticalBox", "kind": "component" }`: a layout box for `+ VerticalBox { }`.
   - `lookup { "name": "Widget" }`: what every node takes (anchors, size). `lookup { "name": "slot" }`: what `@slot` takes.
   - `lookup { "name": "Button" }`: every name containing the text, when you do not know the exact one.
3. **Write the file** under `DUI/`, for example `DUI/MainMenu.dui`. Its first line names the widget Blueprint:
   `class /Game/UI/WBP_MainMenu`. One root node, anchored to fill the screen:

   ```
   class /Game/UI/WBP_MainMenu

   Widget Root {
       AnchorData.AnchorMin = (0, 0)
       AnchorData.AnchorMax = (1, 1)
       AnchorData.SizeDelta = (0, 0)
       + Overlay {}
       ...
   }
   ```

   Lay things out with boxes (`+ VerticalBox { Spacing = 16 }`, `+ HorizontalBox`, `+ Overlay`) and `@slot` lines,
   not with absolute positions: a box keeps its children apart and aligned at every screen size.
4. **Check.** `check { "file": "DUI/MainMenu.dui" }`. It writes nothing and answers in a fraction of a second. Fix every
   error at the line it gives (each code has a `doc` link), and check again until it says `0 errors`.
5. **Lay it out.** `layout { "file": "DUI/MainMenu.dui", "sizes": "1280x720, 2560x1440" }`. The answer lists every node
   with its rect in pixels (x, y from the top left), its text and whether that fits, and then the problems:
   - `DUI8001` off the screen, `DUI8002` text that does not fit, `DUI8003` zero size or a button smaller than 24 px,
     `DUI8004` buttons on top of each other, `DUI8006` something that breaks at one of the other sizes: **fix all of
     these**.
   - `DUI8005` (info) edges that miss lining up by 1-3 px: fix them; they look like mistakes.
   Read the tree too: is every node where the description wants it? A node at `w=0` or far from the others is wrong
   even with no warning. Change the file and run `layout` again until there are no warnings.
6. **Look at it** if you can see images: `preview { "file": "DUI/MainMenu.dui" }` draws the screen into a PNG. If you
   cannot see images, read its figures: coverage, mean colour, luminance, and `DUI8007` for text that is hard to read.
7. **Report** the file, the Blueprint (`writes` in the result), what `layout` shows, and anything you could not do.

`layout` and `preview` compile the screen first, as `compile` does: in the user's editor the Blueprint stays in memory
until the user saves it; in a host the server started it is saved.

## Without DreamMCP

The same steps, with the script instead of the tool (each call starts an engine for a few seconds, so write the whole
file first):

```bash
pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 check DUI/MainMenu.dui
pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 layout DUI/MainMenu.dui -Sizes "1280x720, 2560x1440"
```

For names, read `DUI/.dui-symbols.json` (the editor writes it) or the class pages linked from `reference/controls.md`.
There is no picture without DreamMCP.

## Rules

- Copy only from the reference pages and `Plugins/DreamGUI/Content/Samples/`. The `.dui` files under the plugin's
  `Source/` are tests, some of them broken on purpose.
- A component (`use "Card.dui" as Card`) is compiled before the screen that places it; you only compile the screen.
- `Style.X = v` does nothing while the project has a style sheet: write `StyleSource = Inline` first.
- Colours like `#8C8C94` are sRGB. A `RectBlock`'s `CornerRadius` is a fraction of its size unless its unit mode says
  pixels: `preview` shows a pill where you wanted a rounded card.
- A text in a box that gives it its width breaks its lines at `WrapTextAt`; without it, it may be measured one
  character wide (`DUI8002`, or a `w=0` text in the tree).
- Never edit `DUI/.dui-symbols.json` or `DUI/.dui-diagnostics.json`.
