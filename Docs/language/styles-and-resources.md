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
  then the node's own. A base that comes back around is DUI3015.
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
