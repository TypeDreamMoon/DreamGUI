---
title: Platforms and status
description: What is claimed and what has been run, listed apart — Win64 is the only platform built and run; the release gate, what mobile and dedicated servers actually amount to, test coverage and the known gaps.
---

What is *claimed* and what has been *run* are different lists, so both are here. **Win64 is the only platform built and
run.**

| | Compiled | Run |
| --- | --- | --- |
| **Win64** | The editor; the game target in Development and Shipping | **The editor with the whole automation suite**; a packaged Development game, once the release gate has run the packaged text smoke test for that version |
| Mac, Linux | Never | Never — there is no machine or toolchain for them here |
| iOS, Android | Never | Never — no SDK here, and never on a device |
| Consoles | No | Not in any module's `PlatformAllowList` |
| Dedicated server | Never — no server target has been built | No |

## The release gate

What a version goes through before it is tagged:

1. **BuildPlugin**, which compiles the plugin for the editor and for the game target in Development and Shipping;
2. **the test host's packaged text smoke test**, which cooks and packages a Development game, runs it, and holds what it
   draws to the same build run on uncooked content (see the plugin's `Tools/TestHost/README.md` and
   [Fonts and packaging](fonts-and-packaging.md#5-checking-a-package)).

Until the gate has run for a version, no packaged game of that version has been run.

## What the listed platforms mean

The per-module `PlatformAllowList` and the descriptor's `SupportedTargetPlatforms` name the five platforms the code is
written for — Win64, Mac, Linux, IOS, Android — and a test keeps them in step with each other; `DreamGUIEditor` and
`DreamGUIK2Nodes` list Win64, Mac and Linux only. They say what the build tools will *try* to compile and what the cooker
will cook, not that anyone has compiled, run or shipped on those platforms: the shaders, the engine's third-party
libraries the text uses and the C++ (which clang, unlike MSVC, builds with warnings as errors) have only ever been built
for Win64.

## Mobile

There are code paths that have never run:

- touch is routed end to end (`BindTouch` → the standalone input module);
- the virtual keyboard is implemented (`FPlatformApplicationMisc::RequiresVirtualKeyboard` → `ShowVirtualKeyboard`);
- the one live platform branch in the renderer flips culling for Android GLES.

MSAA is *not* available on GLES, and the renderer falls back rather than pretending — see `AntiAliasingMethod` in
*Project Settings ▸ Plugins ▸ DreamUI*.

## Dedicated server

There is no server target in this project, so the server configuration has never been compiled. What has been done is
the part that can be checked by reading:

- the `#if !UE_SERVER` blocks are balanced;
- the thing that actually breaks a server build — **no member or function referenced outside a server guard is declared
  inside one**. `UDreamUMGWidget` is the only class with `UE_SERVER` blocks, and every field they touch (`SlateWindow`,
  `SlateWidget`, `WidgetRenderer`) is declared unconditionally;
- `WITH_FREETYPE=0` / `WITH_HARFBUZZ=0` are handled the same way: every function the text path exposes is *defined*
  unconditionally with the body guarded, so nothing goes undefined at link time.

At run time a dedicated server draws nothing, and the UI manager does not start in its worlds
(`UDreamUIManagerWorldSubsystem::ShouldRunForNetMode`), a play-in-editor server included: no canvas is drawn, and no
renderer or paint rows are made. Six interaction subsystems already decline to exist in a server process
(`ShouldCreateSubsystem` → `!IsRunningDedicatedServer()`); in a play-in-editor server's worlds they find no manager and
stand down.

## Test coverage

The automation suite is part of the plugin — upstream had none. Run it with `Automation RunTests DreamGUI`, or by preset
with `Tools/Tests/Invoke-DreamGUITests.ps1`, which says how many tests ran; the presets are in
[Tests](../tools/tests.md). A packaged game is checked by the test host's packaged text smoke test, which the release
gate runs.

## Known gaps

- `LineHeightPercentage` and `WrapTextAt` are only reachable through a real font asset, so they are not covered by
  tests.
- A panel that offers a wrapping text no width — a horizontal box measuring along its own axis — is answered with the
  paragraph's one-line width; the text wraps once it is arranged narrower than that. (Offered a width, as a vertical box
  offers its own, a wrapping text answers with its height at that width.)
- One content asset still carries `Lex` in its name (`Content/Blueprints/LexEventSystemActor_EnhancedInput`). Renaming a
  `.uasset` file does not rename the object inside it, so only an editor-side rename can change it; the code points at
  what is actually on disk.
- The editor work is verified by tests, not by eye. Expect rough edges in the designer.

Reports: https://github.com/TypeDreamMoon/DreamGUI/issues
