---
title: Contributing
description: How to report a problem, propose a change and get a pull request merged — the issue forms and the environment command, the repository's layout, building in a host project, the Quick test gate, what to update with a change, the docs and translation flow, and AI-assisted contributions.
---

DreamGUI takes bug reports, proposals, docs fixes and pull requests on
[GitHub](https://github.com/TypeDreamMoon/DreamGUI). This page is the overview. The checklist a pull request is held
to is [CONTRIBUTING.md](../../CONTRIBUTING.md) in the repository. An AI agent starts at [AGENTS.md](../../AGENTS.md).

## Reporting a problem

Issues are opened through forms; a blank issue is not offered.

| Form | For |
| --- | --- |
| Bug report | Wrong output, a crash, or behaviour that differs from these docs |
| Feature or syntax proposal | Something DreamGUI or `.dui` cannot express or do yet. Open one before a feature pull request |
| AI wrote it wrong | An AI tool wrote `.dui` that did not build, looked wrong, or used names that do not exist |
| Docs or translation | A page here that is wrong, unclear, missing, or badly translated |

The bug and AI forms ask for the output of the environment command. Run it from your game project's folder:

```text
pwsh -NoProfile -File Plugins/DreamGUI/Tools/Get-DreamGUIEnvironment.ps1
```

It prints the plugin's version and commit, the engine's version and kind, the system, its UI language and the shell.
It starts nothing and prints no paths. Pass `-Project <path to>/<Project>.uproject` when the plugin is not inside the
project's `Plugins` folder. Questions and ideas go to
[Discussions](https://github.com/TypeDreamMoon/DreamGUI/discussions).

## The repository

| Folder | What it holds |
| --- | --- |
| `Source/` | The thirteen modules: eight runtime, four editor-side (`UncookedOnly`) and the test module ([module layers](../getting-started/project-layout.md#module-layers)) |
| `Shaders/`, `Config/`, `Content/`, `Resources/` | What the modules load: shaders, default settings, fonts and other assets, icons and the UMG parity tables |
| `Docs/` | This manual. English is the source; `Docs/zh-CN/` is its translation |
| `Tools/Tests/` | The test runner, its presets, the static checks |
| `Tools/TestHost/` | The minimal host project the suite builds and runs in |
| `DLoc/` | The editor UI's translations, kept with DreamLoc |
| `ThirdParty/` | msdfgen, committed as a single-file copy, so a plain clone builds |

The whole layout is under [What is in the plugin folder](../getting-started/project-layout.md#what-is-in-the-plugin-folder).

## Building

You need Unreal Engine 5.8 (source or launcher), PowerShell 7.2 or later, Python 3.8 or later and git. Win64 is the
only platform built and run. Build in a project that has the checkout as `Plugins/DreamGUI`; the
[test host](../tools/tests.md#the-test-host) is a minimal one, with the plugin as a git worktree of your clone:

```text
pwsh -NoProfile -File Tools/TestHost/New-DreamGUITestHost.ps1 -Root <host folder> -RepoPath <your clone> -Branch <your branch> -Detach -EngineRoot <engine root>
```

Its defaults are the maintainer's machine, so pass all four values. Build the whole editor target, never with
`-Module=`: a restricted build leaves the plugin's module manifest stale, and the plugin then fails to load as a whole.

## The test gate

Every pull request runs the `Quick` preset first:

```text
pwsh -NoProfile -File Tools/Tests/Invoke-DreamGUITests.ps1 -Project "<Project>/<Project>.uproject" -Engine "<engine root>"
```

It runs the static checks, builds, checks the module manifest, and runs the headless suite in an unattended editor.
Close every editor of that project first. Exit code `0` is green, `1` is red, `2` means the run could not be judged.
It prints one line that starts with `RESULT`: paste that line into the pull request. The other presets (`Rhi` for
the pixel tests, `Designer`, `Pie`, `All`), the report, known issues and the static checks' rules are on
[The automation suite](../tools/tests.md). `python Tools/Tests/static_checks.py` runs the static checks alone,
without an engine.

## What to update with a change

- The page here that describes the behaviour you changed.
- `CHANGELOG.md`: a line under `## Unreleased`, in `### New`, `### Changed` or `### Fixed`, for anything a project can
  notice.
- A new diagnostic code: `EDreamUIDiagnosticCode`, and both tables, [Diagnostics](../diagnostics/index.md) and
  `Docs/diagnostics/codes.md`. Codes are never renumbered or reused.
- A changed reflected type: regenerate the [class reference](../Reference/index.md) with the `DreamGUIReferenceDocs`
  commandlet. It is never edited by hand.
- A new or moved runtime file: its row in `Tools/Tests/module-owners.csv`.
- New tests: a name of the form `DreamGUI.<Area>.<Sentence>`, and a higher floor for the preset in
  `Tools/Tests/presets.json`.
- A new property meta tag: the `DreamGUI_` prefix ([Property meta tags](../guides/property-meta.md)).

## Docs and translations

English under `Docs/` is the source of this site. After a change reaches `main`, CI translates what changed into
`Docs/zh-CN/` with [dreamdocs](https://github.com/TypeDreamMoon/dreamdocs) and opens a pull request of its own, so a
contribution changes the English only. A Chinese sentence is corrected by editing the Chinese page; the next run keeps
the edit. `Docs/zh-CN/.align/` records which English paragraph each Chinese one came from and is never edited by hand.

## AI-assisted contributions

They are welcome on the same terms as any other. Name the tool and model in the pull request, paste the `RESULT` line
of the test run, and review every line before you submit: the person who opens the pull request is responsible for
it. Bulk AI-generated pull requests without test evidence are closed. [Working with AI agents](../tools/ai-agents.md)
describes what an agent has to work with.

## License and conduct

Contributions are licensed under the plugin's [MIT license](../../LICENSE); there is no CLA. The
[code of conduct](https://github.com/TypeDreamMoon/.github/blob/main/CODE_OF_CONDUCT.md) and the
[security policy](https://github.com/TypeDreamMoon/.github/blob/main/SECURITY.md) apply to every repository of the
project.
