---
title: "rows tables"
description: "Many instances of one component as a table — the type, the style and the property names written once, one line per instance, each named from its first value."
---

## `rows`: a table of instances

The same component several times, differing in a few values, is a table: the type, the style and the property names
are written once, then one line per instance.

```
ListPage Page_0 : PageSize {
    Icon = @IconMap
    rows Row : ListRow (Label, Description) {
        "City Ruins",      "The overgrown remains of a city. The Resistance camp lies to the east."
        "Desert Zone",     "Sand and wind as far as the eye can see."
        "Factory",         "An abandoned factory, still running somewhere deep inside." { Kind = Count }
    }
}
```

is the same tree as three unnamed `Row : ListRow { Label = "…"  Description = "…" }` written where the table stands.
Nothing runs at run time: the table is read into those widgets, so a row is a widget like any other -- it builds,
binds and lays out exactly as the line it stands for would.

- **The header** is `rows`, a node type (a tag, a container, an alias, `@Name`, a path), an optional `: Style`, and the
  columns: property names, dotted or not, in parentheses. Each is written once (DUI2020).
- **A row** is its values, in column order, separated by commas -- any value a property takes: a string, a number, a
  tuple, a colour, a word, `@Resource`. One row per line, or several on a line separated by `;`. A row has exactly as
  many values as there are columns (DUI2020), and a row that does not read makes no widget. Values are written, not
  bound: a column is `=`, never `<-`.
- **A block at the end of a row** (`{ Kind = Count }`) holds what that one row needs beyond the columns: more
  properties, `@slot` lines, `+` components, children. A line in it that names a column wins over the cell.
- **Ids come from the first value**, the row's key: `<id of the enclosing node>__<type>_<key>`, the key with every run
  of characters an id cannot hold made one `_` and cut to 32 characters -- `Page_0__Row_City_Ruins`. A row inserted or
  moved therefore leaves every other row's id, and the localization keys made from it (`Page_0__Row_City_Ruins.Label`),
  where they were. Two rows whose keys make the same id are told so (DUI3023, a warning): the second gets `_1`, which
  does move when rows are reordered. A key nothing of which an id can hold is counted like any unnamed node.
- `rows` is a keyword only before a type and a column list: a property called `rows`, or a node whose type is, reads
  as it always did.

Code that needs a row by name writes that row as a node of its own.
