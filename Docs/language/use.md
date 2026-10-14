---
title: "use"
description: "Merging a library, giving a component or a class a short name, putting a library under a namespace, aliases that travel with their library, and the older @Name."
---

## `use`

`use` makes another file, or a class, available to this one. Four forms:

```
use "UI/Library.dui"                     // a library, merged
use "UI/Components/Row.dui" as Row       // a component, by a short name
use /Game/UI/WBP_Slider as Slider        // a class with no .dui, by a short name
use "UI/NieR_Common.dui" as nier         // a library, under a namespace
```

**Merged.** A plain `use` of a file brings in its styles, its resources and the component aliases it declares. The
file's own `use` lines come along, so libraries layer. A name this file declares shadows an imported one.

**A component.** `use "…" as Row` on a file that has a root node names that file's class, and takes nothing else from
it: a screen does not borrow a component's styles. The class is the one the file's `class` line names; for a file with
no `class` line, the editor finds the Blueprint whose Source File it is (without the editor -- a commandlet -- that is
DUI5018, and a `class` line is the fix). Then `Row` is a node type:

```
Row Audio { Label = "Audio" }
```

**A class.** `use /Game/UI/WBP_Slider as Slider` names a class that has no `.dui` behind it. The `as` is required here.

**A namespace.** `use "…" as nier` on a file with no root is a namespace: everything the library declares or brought in
is entered as `nier.X`. Its styles are `: nier.Label`, its resources `@nier.Ink`, its component aliases `nier.Row`. Two
libraries that both say `Label` can then be used side by side. A style that came in under a namespace still finds its
own library's names: `Color = @Ink` written in the library means the library's `Ink`.

**Re-export.** A library's own `use … as Row` lines travel with it. A library that names a family of components gives
every screen that uses it those names -- `Row` after a plain `use`, `nier.Row` after `use … as nier`:

```
// UI/NieR_Common.dui
use "UI/Components/NieR_Row.dui" as Row
use "UI/Components/NieR_Tab.dui" as Tab

// UI/NieR_Menu.dui
use "UI/NieR_Common.dui"
Widget Root {
    Row Item_0 : ListRow { Label = "Warped Wire" }
}
```

The name after `as` is one word, not a keyword (DUI2015), not the name of a tag or a container (DUI3018), and given
once per file (DUI3017). A `ns.` no `use … as ns` declares is DUI3021. An import that cannot be read, does not parse,
or comes back around to a file already being imported is DUI2012.
