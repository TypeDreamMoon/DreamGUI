---
title: Networking and multiplayer
description: Where UI is made in a networked game — only where a local player sees it; what a dedicated server and a listen server each do with DreamUI widgets, the play-in-editor net modes, and the warning a server world gives.
---

UI belongs to the machine whose player looks at it. DreamUI widgets are not replicated: each client builds its own
screens, and a server that is not also a player builds none.

## Who has a UI manager

| World | UI manager | What a widget made there does |
| --- | --- | --- |
| Standalone, client | Yes | Everything: layout, drawing, input, ticking |
| Listen server (the host plays too) | Yes | Everything, on the host's screen |
| Dedicated server | **No**, by design | Nothing: it is never laid out, drawn, ticked or given input |

A dedicated server draws nothing and has no player, so its worlds get no manager
(`UDreamUIManagerWorldSubsystem::ShouldRunForWorld`), and none of what one makes: draw data, render resources, paint rows.
Widget trees a level holds are still built there, as in a commandlet, and code that asks for the manager gets null.

## Make UI where a local player is

A player controller's `BeginPlay` runs on **every** machine that has that controller — and the server has one for every
player that joins. A `Create Dream Widget` in `BeginPlay` with nothing in front of it therefore runs on the server too,
once per player:

- on a **dedicated server** the widget does nothing, and DreamGUI says so once for the world (below);
- on a **listen server** it is drawn on the host's screen — one copy for every player who joins.

So put UI behind the question of whose machine this is:

- **In a player controller**, check `Is Local Controller` before creating the widget. Passing the controller as
  `Owning Player` also guards it: `Create Dream Widget` refuses a controller that is not local and returns null, as UMG's
  Create Widget does.
- **In the HUD.** An `AHUD` is spawned only for a local player, never for a remote player's controller on the server, so
  UI built in the HUD's `BeginPlay` is already on the right machine.
- **From replicated state**, update the widgets on each client — in a `RepNotify`, or by reading the replicated property
  — rather than sending UI changes from the server.

## Play in editor

| PIE net mode | Worlds | UI |
| --- | --- | --- |
| Play Standalone | One standalone world | Yes |
| Play As Listen Server | `UEDPIE_0` is the host, the others are clients | In every window |
| Play As Client | `UEDPIE_0` is a **dedicated server**, the clients start at `UEDPIE_1` | In the client windows only |

`Play As Client` is the one that runs a dedicated server in the editor — in the editor's own process with *Run Under One
Process*, as a separate server process without it — and the one where UI made without a local check shows up as the
warning below.

## The warning

A widget registered in a dedicated server's world logs, once for that world:

```text
<widget>: created in <world>, a dedicated server's world. A dedicated server has no DreamUI manager by design ...
```

It names the first widget; widgets made in the same world after it are not reported again. It is a warning, not an
error: the widget is harmless there, only useless. A world that *should* have a manager and does not — a client, a
standalone game, an editor world — still raises an ensure, because that is DreamGUI's to explain.

## See also

- [Migration and upgrades](migration.md#servers) — the dedicated-server rule as it entered the changelog.
- [Platforms and status](platforms.md) — no dedicated server target has been built or run.
