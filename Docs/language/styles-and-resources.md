---
title: "Styles and resources"
description: "A style is a named set of lines that can inherit and carry components and slot lines; a resource is a typed, named constant."
---

## Styles

A style is a named set of lines, applied to a node by `: Name`:

```
style Caption {
    FontSize = 14
    Color    = #8C93A6
}

style Warning : Caption {
    Color = #E05A47
}

Text Note : Warning { Text = "Unsaved changes" }
```

- A style may inherit another (`style Warning : Caption`): the base's lines apply first, then the derived style's,
  then the node's own. A base that comes back around is DUI3015. A style may name several bases, and so may a node; see
  [Several styles](#several-styles).
- A style may carry `+ Component` blocks and slot lines, so that a kind of column is one name:

  ```
  style RowColumn {
      + VerticalBox { Spacing = 15 }
      @fill
  }

  Widget Left : RowColumn { … }
  ```

  The style's components come first. A component of the same class the node also writes is one object, the style's
  values first and the node's after; the node's own `@slot` lines win over the style's.
- A local style shadows an imported one of the same name. Two local styles of one name are DUI3005.
- **A control's `Style` is not one of these.** A `Native.Button` and its kin have a property called `Style`, a struct of
  the control's own, written `Style.TickChecked = #FF3355` and chosen by `StyleSource`. Whether such a line takes effect,
  and what the `bOverride_` bits beside the fields do, is on [Styles and style sheets](../controls/styles.md#in-dui-and-at-run-time).


### Several styles

A style, a node and a `rows` header each take any number of styles, every one behind its own colon:

```
style Danger : Button : Loud { Color = #FF4040 }
Text Title : Heading : Glow { }
rows Row : ListRow : Dense (Label, Value) { … }
```

- **Order.** The names apply from left to right, and each name first applies its own bases, recursively, so a base comes
  before the style that inherits it. A style applies once, at the place it first appears. A later style overrides an
  earlier one, and the node's own lines come last. With `style A : Base` and `style B : Base`, a node written `: A : B`
  gets Base, A, B.
- An unknown name is DUI3004, once for each name that is unknown. A style that comes back around to itself through its
  bases is DUI3015.
- **Naming a style that is already applied** changes nothing, and is DUI3025, a warning. It is raised on the later name,
  whether an earlier one applied it as a base (`: A : Base`, where `A` inherits `Base`) or the name is simply written
  twice (`: A : A`). Each clause is checked once -- a node's or a `rows` header's, and a style declaration's, however
  many nodes wear it -- and every redundant name in it is reported. A style imported from another file is never
  warned about here; the file it came from is, when it compiles. The messages say which case it is:
  `style 'X' is already applied through 'Y', so naming it again changes nothing`, and
  `style 'A' is named twice in this clause, so the second changes nothing`.

## Resources

```
resources {
    Color   Accent = #FF6600
    Number  Gap    = 8
    Vector2 Icon   = (24, 24)
    String  Brand  = "Dream"
    Asset   Row    = /Game/UI/WBP_Row
}
```

`@Accent` then stands for `#FF6600` wherever a value goes, and in a binding expression. The type is checked against the
entry's own value (DUI4008); a missing entry is DUI4007. Each entry also becomes a variable of the compiled class, so a
graph can read it.

An `Asset` entry naming a widget class can be a node type: `@Row Audio { … }`. `use … as` does the same with less.

Entries accumulate over several `resources` blocks; one name declared twice is DUI3014.
