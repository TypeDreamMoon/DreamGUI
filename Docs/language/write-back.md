---
title: "Designer write-back"
description: "The designer is a front end for the .dui — which line an edit is written into, how a hand-written file is left alone, and which edits it refuses (DUI7xxx)."
---

## What the designer writes back

The designer is a front end for the file: a change made there is a change to the text, written into the line that
holds the value -- or a new line after the node's others -- with everything else byte for byte as it was. A few
things have no line to write into, and the designer says so (DUI7004) instead of inventing one:

- the visibility of a widget an `if` shows or hides -- change the condition, or move the widget out of the branch;
- a `SizeRule` other than Fill on `@fill 2` -- write `@slot SizeRule = …` and `@slot FillWeight = 2` in its place.
  A new weight replaces the number in `@fill 2`, and a bare `@fill` given another rule becomes `@slot SizeRule = …`;
- anything on a row of a `rows` table but a column's value or a line of the row's own block: the row's line spells only
  its columns. A column's value is replaced in its cell. A row is not removed, moved, named or given a `+` block from
  the designer, and no node is placed beside a table (the place would be inside it) -- edit the text.

A value a node takes from its style's `+ Component` line is not written either (DUI7005): the style is shared, and the
node has no `+` line of its own to hold the change. Add one (`+ VerticalBox { Spacing = 20 }`) or change the style.

A widget added in the designer is written with the type this file would use: its `use … as` alias when the class has
one here, a container type for a plain widget that lays out children (`VerticalBox Column`). One dropped into a named
slot of a component instance is written into that slot's fill, `slot Detail { … }`, which is written first when the
instance has none. An unnamed node renamed in the designer gets its first id, written after its type. The template of a
`for` or an `each` is not removed or moved out of its loop: a loop with nothing to repeat does not build.

A turn -- made with the 2D view's rotate handle, the 3D view's gizmo or the details panel -- is written as the euler,
`RelativeRotationEuler = (0, 0, 30)`: pitch, yaw and roll in degrees, a turn in the canvas plane being the roll,
clockwise for a positive one. The quaternion the asset keeps has no spelling and is never written.

If a source file is read-only or a write fails, the document keeps the edited text in memory. A later flush retries
that write even when no value changed. Compile retries it too, and reports DUI6023 while it still cannot write; the
current hierarchy and edits are retained. Check the file out or resolve the write failure, then compile again.

### From designer values back to text

`FDreamUITextWriteBack` turns designer edits into `.dui` changes. Its central rule is to **compare values, not source
spellings**. `FDreamUITextPatcher` edits text slices; giving it every property the designer might have touched would
rewrite unchanged values whenever the printer's formatting differs from the author's. For example, `(400,240)` and
`(400, 240)` describe the same value.

A flush therefore asks whether rebuilding the current text would produce the value held by the live tree. It builds
a second tree from that text with the compiler's own `FDreamUITextBuilder`, then compares the values. It does not
maintain a separate interpretation of the language for write-back.

One flush is one undo step, or no step when nothing changed. A no-op does not call `SetContent` or create an empty
transaction. Undo marks the tree for regeneration **on the next tick**: the notification arrives inside
`FTransaction::Apply` while it is still restoring objects, so other widgets in the transaction may not yet have
their restored values.

### Where the text lives: `UDreamUIDocument`

For text-backed widgets, the text is the source of truth and the designer is its graphical front end.
`UDreamUIDocument`, a `UCLASS(Transient)` `UObject` in `DreamGUIEditor`, holds the full text of one `.dui` file so
Unreal's transaction system can undo it.

The `UObject` matters because undo restores objects. If the text were outside the transaction, Ctrl+Z could restore
the widget while leaving the new value in the file. A recompile, a reopen or the next unrelated edit would then
regenerate the tree from that file and bring the supposedly undone change back.

Callers must preserve three contracts:

1. **The caller owns the transaction.** `SetContent` calls `Modify()` but never opens a transaction itself. It reports
   a missing transaction instead of silently producing a non-undoable edit.
2. **Memory is authoritative; disk may lag.** A failed write, such as a read-only file that has not been checked out,
   keeps the edit in memory, reports the failure and retries on a later flush.
3. **`FlushToDisk` never clears the read-only bit.** Check out the file through source control rather than making it
   writable behind the source-control system's back.

There is **one document per file, not per editor window**. Two toolkits holding separate documents for the same file
would have independent text and disk hashes: an undo in one could look like an external edit to the other, causing
the windows to keep reloading and overwriting each other's state.

`FDreamUIDocumentRegistry` prevents that by using the normalized absolute path as its key. The registry holds weak
references; callers retain documents through `FDreamUIDocumentHandle`, a move-only, non-copyable scoped handle.
The document API and this ownership contract are declared in
[`Source/DreamGUIEditor/Public/Text/DreamUIDocument.h`](../../Source/DreamGUIEditor/Public/Text/DreamUIDocument.h).
