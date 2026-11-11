# Contributing to DreamGUI

[English](CONTRIBUTING.md) · [简体中文](CONTRIBUTING.zh-CN.md)

Thank you for helping. This page says how to report a problem, how to propose a change, and what a pull request
needs before it can be merged. If you use an AI agent, point it at [AGENTS.md](AGENTS.md) first.

## Ways to contribute

- **Bug reports.** Wrong output, a crash, or behaviour that differs from the docs.
- **Feature and syntax proposals.** Something DreamGUI or `.dui` cannot express or do yet.
- **Docs and translation fixes.** A page that is wrong, unclear, missing, or badly translated.
- **Small fixes.** A typo, a clear bug with an obvious fix, a missing test. Open the pull request directly.
- **Features.** Open a proposal issue first, and agree on the shape there before you write the code.

## Reporting a bug

1. Use the [issue forms](https://github.com/TypeDreamMoon/DreamGUI/issues/new/choose). Blank issues are off.
2. Run the environment command and paste its output into the form. From your game project's folder:

   ```text
   pwsh -NoProfile -File Plugins/DreamGUI/Tools/Get-DreamGUIEnvironment.ps1
   ```

   It starts nothing and prints no paths. `powershell` (Windows PowerShell 5.1) works too. Pass
   `-Project <path to>/<Project>.uproject` when the plugin is not inside the project's `Plugins` folder.
3. Give the smallest `.dui` that shows the problem, complete enough to compile on its own.
4. Give the diagnostic code (`DUInnnn`) if there is one, and the few log lines around the error.

An AI tool wrote `.dui` that does not build, or used names that do not exist? Use the *AI wrote it wrong* form.
Those reports improve the agent guide and the examples.

## Questions and ideas

Usage questions, design ideas and show-and-tell go to
[Discussions](https://github.com/TypeDreamMoon/DreamGUI/discussions), not to issues.

## Development setup

- Unreal Engine 5.8, a source build or a launcher install. Win64 is the only platform built and run.
- PowerShell 7.2 or later (`pwsh`), Python 3.8 or later on `PATH` as `python`, and git.
- A host project with this repository checked out as `Plugins/DreamGUI`. The test host script makes a minimal one,
  with the plugin as a git worktree of your clone:

  ```text
  pwsh -NoProfile -File Tools/TestHost/New-DreamGUITestHost.ps1 -Root <host folder> -RepoPath <your clone> -Branch <your branch> -Detach -EngineRoot <engine root>
  ```

  Its defaults are the maintainer's machine, so pass all four values. Details:
  [Tools/TestHost/README.md](Tools/TestHost/README.md) and [The automation suite](Docs/tools/tests.md).
- The host script and the test runner refuse drive C by default. On a machine with only drive C, set
  `DREAMGUI_ALLOW_DRIVE_C=1`.
- The test runner builds the host before it tests. Build the whole editor target; never use `-Module=`.

## Before you open a pull request

Run the `Quick` preset. It runs the static checks, builds, and runs the headless suite:

```text
pwsh -NoProfile -File Tools/Tests/Invoke-DreamGUITests.ps1 -Project "<Project>/<Project>.uproject" -Engine "<engine root>"
```

Close every editor of that project first. Exit code 0 is green. Paste the line that starts with `RESULT` into the
pull request; the full report is `Saved/DreamGUITestReports/<stamp>-Quick/summary.md` in the project. The pixel
tests need a real RHI and run with `-Preset Rhi`. All presets: [Tools/Tests/README.md](Tools/Tests/README.md).

Update, in the same pull request:

- **Docs.** The page in `Docs/` that describes what you changed. English only; see below.
- **CHANGELOG.md.** A line under `## Unreleased` (`### New`, `### Changed` or `### Fixed`) for anything a project
  can notice.
- **Diagnostics.** A new `DUInnnn` code goes into `EDreamUIDiagnosticCode` and into both tables,
  `Docs/diagnostics/index.md` and `Docs/diagnostics/codes.md`. Codes are never renumbered or reused.
- **The class reference.** `Docs/Reference/` is generated from reflection. When you change a reflected type,
  regenerate it with the `DreamGUIReferenceDocs` commandlet; never edit it by hand.
- **Module ownership.** A new or moved runtime file gets its row in `Tools/Tests/module-owners.csv`.
- **Tests.** A new test is named `DreamGUI.<Area>.<Sentence>`. Raise the preset's floor in
  `Tools/Tests/presets.json` when you add tests.
- **Property meta.** A new meta tag starts with `DreamGUI_` ([Property meta tags](Docs/guides/property-meta.md)).
- **Editor UI text.** Strings are `LOCTEXT`. Their Chinese translations live in `DLoc/` and are kept with
  DreamLoc. If you add or change UI text and do not use DreamLoc, say so in the pull request.

`Tools/Tests/Install-DreamGUIHooks.ps1` installs an optional pre-push hook that runs `Quick` for you.

## Commits and pull requests

- Small commits, one topic each. Subjects read `type(scope): sentence`, for example
  `fix(text): every glyph's underlay draws before any glow`.
- Branch names follow the same types: `feat/…`, `fix/…`, `docs/…`.
- No planning labels (wave, batch, phase numbers) in code, comments, test names or commit messages.
- No absolute local paths and no personal data in any file. Write paths as `Plugins/DreamGUI/...`.
- Fill in the pull request template. Link the issue it fixes.

## AI-assisted contributions

AI-assisted pull requests are welcome, on the same terms as any other.

- Say in the pull request which tool and model you used.
- Paste the result line of the test command you ran. "It should work" is not evidence.
- You are responsible for every line. Read and understand the diff before you submit it.
- Bulk AI-generated pull requests without test evidence will be closed.
- Let the agent read [AGENTS.md](AGENTS.md) before it starts.

## Docs and translations

- English under `Docs/` is the source. The site <https://gui.toolchain.64hz.cn> is built from it.
- `Docs/zh-CN/` is translated by CI with [dreamdocs](https://github.com/TypeDreamMoon/dreamdocs) after a change
  reaches `main`, and arrives as its own pull request. Do not translate in your pull request.
- To fix a Chinese sentence, edit the Chinese page directly. The next translation run keeps your edit.
- Never edit `Docs/zh-CN/.align/`. It records which English paragraph each Chinese one came from.
- `README.zh-CN.md` and `CONTRIBUTING.zh-CN.md` are kept by hand next to their English pages.

## License

DreamGUI is under the [MIT license](LICENSE). By contributing, you agree that your contribution is licensed under
the same terms. There is no CLA. Substantial portions of the code are the work of Lex Liu in
[LGUI / LexUI](https://github.com/liufei2008/LGUI), also MIT; keep its notice.

## Code of conduct and security

- [Code of conduct](https://github.com/TypeDreamMoon/.github/blob/main/CODE_OF_CONDUCT.md)
- [Security policy](https://github.com/TypeDreamMoon/.github/blob/main/SECURITY.md). Do not report a
  vulnerability in a public issue.
