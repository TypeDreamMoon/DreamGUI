---
title: Lyrics
description: UDreamLyricsView — a word-by-word, spring-driven lyrics view, its two inputs (LRC and TTML), and its parameters by group.
---

`UDreamLyricsView` (`DreamGUIExtensions`) is a lyrics control that **lights up word by word** — in the plugin's own words,
**Apple Music style**.

## What it looks like

- Each lyric line is a **text line**;
- **the current line is pinned at one height**, and the other lines slide in and out above and below, driven by
  **springs**;
- **the word being sung lights up through the text's fill channel**;
- **the further a line is from the current one, the softer and fainter it gets**.

It goes on **the widget that serves as the lyrics box** — the width wraps the lyric lines, and the height is the visible
area.

## Driving it

The proper way is to let an **audio clock** drive it through `SetTime`; for previews, `Play()` lets it run on its own.

| Entry point | Does |
| --- | --- |
| `LoadLRC(Text)` / `LoadTTML(Xml)` | Parse the two lyric text formats — these two are on **`UDreamLyricsLibrary`** (a `UBlueprintFunctionLibrary`), not just for the view |
| `SetLyrics` / `RebuildLines` | Change the lyrics; rebuild the line widgets from them (also called on `Awake`) |
| `SetTime` / `GetTime` / `Seek` | Driven by an external clock; `Seek` jumps **without animating** |
| `Play` / `Pause` / `bPlaying` / `PlaybackRate` | Self-running mode — `bPlaying` defaults to **false**, so **by default it is driven by an external clock** |
| `OnActiveLineChanged` | Fires when the current line changes, with the line index |
| `GetActiveLineIndex` / `GetLineCount` / `GetLineText` | Queries |

## Parameters by group

Defaults in brackets.

| Group | Parameters |
| --- | --- |
| Text | Font size **36**; background-vocal lines scale by `BackgroundLineScale` **0.7**; a word sung long enough (`EmphasisMinDuration` **1 second**) gets **3×** glow from `EmphasisGlowBoost`; translations scale by `TranslationScale` **0.6** and are on by default (`bShowTranslation`) |
| Layout | The current line is pinned at `ActiveLineAnchor` **0.25** (25% down from the top); line spacing **24** |
| Motion | **One set of spring parameters each** for position and scale; lines are staggered by `StaggerSeconds` **0.04 s**, accumulating over at most **8 lines** |
| Other lines | Scale **0.92**, opacity **0.55**, **0.35 for lines already sung**; each line further away adds **0.04 em** of softening (up to 4 lines) |

It is built on **springs** (see the spring section of [Animation](animation.md)) — this is exactly where
"a retarget keeps the velocity" earns its keep: a lyric line sliding to a new place **carries on at the speed it had**
instead of starting over.

## It was written in 2026

The file header's copyright is `Copyright 2026-Present TypeDreamMoon`, **with no `Lex Liu` line** — in this plugin it is
one of the few modules whose code comments and copyright both come from the new author.
