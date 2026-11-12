# .skill — DreamGUI agent skills

Skills for an AI agent writing `.dui` screens in a game project that has DreamGUI. Each `SKILL.md` is plain Markdown:
any agent can read it and run the same steps. With a [DreamMCP](../Docs/tools/dreammcp.md) server connected the steps
call the `dream_gui` tool; without one they run `dui.ps1`.

| Skill | Use it when |
| --- | --- |
| [`dream-gui-create`](dream-gui-create/SKILL.md) | writing a new screen from a description, and laying it out until it fits |
| [`dream-gui-verify`](dream-gui-verify/SKILL.md) | checking, compiling or laying out one file or all of them |
| [`dream-gui-diagnose`](dream-gui-diagnose/SKILL.md) | a `DUInnnn` diagnostic needs explaining, or a screen compiles and looks wrong |

| File | What it is |
| --- | --- |
| `dui.ps1` | `check <file>` / `check -All` / `compile <file>` / `layout <file>` / `info`: the endpoint's actions from the command line. It uses an editor that already serves the project, or starts a headless host for the call. Exit 0 clean, 1 source errors, 2 not run |
| `sync-skills.ps1` | publishes the skills and `reference/` into the project's `.claude/skills/` (as `dream-gui-*` and `dream-gui-reference`), where Claude Code loads them; `-Check` reports a stale copy, `-Prune` removes what has no source |
| `build-reference.ps1` | builds `reference/` from `Docs/`; `-Check` reports drift |
| `reference/` | the pages an agent reads first, each standing alone (its links lead to the online manual): `dui.md` the language, `example.md`, `pitfalls.md`, `controls.md`, `diagnostics.md`. DreamMCP serves them through its `reference` action |

Publish once per project, from the project root:

```bash
pwsh -NoProfile -File Plugins/DreamGUI/.skill/sync-skills.ps1
```

Edit the skills here, never the published copy. After editing a page under `Docs/` that a reference page is made from,
run `pwsh -NoProfile -File .skill/build-reference.ps1` in the plugin.
