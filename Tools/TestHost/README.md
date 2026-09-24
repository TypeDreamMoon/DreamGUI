# DreamGUI test host

A minimal Unreal project that exists only to build DreamGUI and run its automation suite, away from
any project you actually work in.

## Why

The suite used to be built and run inside the working project (DevTest). That had three costs:

- **The working project's editor had to be closed.** Building links the plugin's DLLs, and a running
  editor holds them open; a run also needs an editor process of its own on the same project.
- **Other sessions share the working tree.** Anything else committing to or editing the same checkout
  changes what a run is testing, half-way through.
- **"The project builds" is not "the plugin builds".** A project that happens to enable other plugins,
  or carries its own config, can hide a dependency the plugin forgot to declare.

The host fixes all three. Its `Plugins/DreamGUI` is a separate **git worktree** of the DreamGUI
repository, with its own `Binaries/` and `Intermediate/`, so it builds and runs while the working
project's editor stays open; it enables nothing but DreamGUI and Enhanced Input; and it has no content
or code of its own.

## Layout

```
DreamGUITestHost/                       (default I:\UnrealProject_Moon\DEV_58\DreamGUITestHost)
  DreamGUITestHost.uproject             from Template/
  Config/DefaultEngine.ini              from Template/, with [CoreRedirects] filled in (see below)
  Config/DefaultInput.ini               from Template/
  Config/DefaultGame.ini                from Template/
  Source/DreamGUITestHost*.Target.cs    from Template/
  Source/DreamGUITestHost/              from Template/ -- an empty primary game module
  Plugins/DreamGUI/                     a git worktree of the DreamGUI repository
  .dreamgui-testhost.json               what the host was made from (template version, repo, branch)
  Binaries/ Intermediate/ Saved/        created by the first build and run
```

The template lives here, in `Tools/TestHost/Template/`. It is not under the plugin's `Source/` or
`Tests/` folder, which are the only places UnrealBuildTool looks for module and target rules inside a
plugin, so no project that has DreamGUI in its `Plugins/` ever picks the template's `.Build.cs` or
`.Target.cs` files up.

## Creating it

```powershell
pwsh -NoProfile -File Tools\TestHost\New-DreamGUITestHost.ps1 -WhatIf   # see what it would do
pwsh -NoProfile -File Tools\TestHost\New-DreamGUITestHost.ps1
```

| Parameter | Default | |
| --- | --- | --- |
| `-Root` | `I:\UnrealProject_Moon\DEV_58\DreamGUITestHost` | The host directory. Refused on drive C. |
| `-RepoPath` | `I:\UnrealProject_Moon\DEV_58\DevTest\Plugins\DreamGUI` | Any working tree of the repository. |
| `-Branch` | `feat/tests-completion` | What the worktree must have (or is created with). |
| `-EngineRoot` | looked up from the `.uproject`'s engine association | |
| `-Force` | off | Replace files that differ from the template; each old one is kept as `<name>.bak-<timestamp>`. |
| `-KeepCurrentHead` | off | Accept an existing worktree on another branch or a detached commit. |
| `-WhatIf` | off | Report only. |

What it does, in order:

1. **Checks before touching anything**: git is on `PATH`; `-Root` is not on drive C (following links,
   so a link from D: into C: is caught too), is not inside the repository, and does not already hold
   another project; the repository is a git repository; the engine exists.
2. **The worktree.** If `Plugins/DreamGUI` exists it must be a worktree of that repository -- the same
   git directory, its own working tree rather than the repository's main one reached through a link,
   registered in `git worktree list` -- with `-Branch` checked out. If it does not exist, it is created
   with `git worktree add`. An existing worktree is never removed, reset or switched.
3. **The template.** Missing files are written; matching files are left alone (line endings and a
   byte-order mark do not count as differences); a file that differs is listed with its first
   differing line and kept, unless `-Force`.
4. **`.dreamgui-testhost.json`**, rewritten only when something in it changed.
5. Prints the command that builds and runs.

It is safe to run again at any time. Exit codes: `0` the host matches the template; `1` a check
failed (printed; nothing after the failing step was done); `2` the host is usable but does not match
the template (files kept without `-Force`, or `-WhatIf` found work to do).

Creating the worktree is the one thing the script writes outside `-Root`: git records every worktree
in the repository's `.git/worktrees/` directory.

## Building and running

The test runner owns building and running; it picks the host up by itself when the host's `.uproject`
exists:

```powershell
pwsh -NoProfile -File I:\UnrealProject_Moon\DEV_58\DreamGUITestHost\Plugins\DreamGUI\Tools\Tests\Invoke-DreamGUITests.ps1 -Project I:\UnrealProject_Moon\DEV_58\DreamGUITestHost\DreamGUITestHost.uproject
```

See `Tools/Tests/README.md` for presets, reports and exit codes.

The editor target (`DreamGUITestHostEditor`) uses the **shared** build environment: engine modules
already built in `Engine/Binaries/Win64` are linked against as they are, and only the host module and
the plugin are compiled. So the first build costs one plugin build, not an engine build -- provided
the engine is up to date. If the engine's source has changed since it was last built, building the
host rebuilds the changed engine modules, exactly as building any other project on this engine would:
that fails with LNK1104 while any editor has those DLLs loaded, and it can leave the other projects'
modules looking out of date to their next launch. Build the engine first in that case. The runner
builds with `-NoEngineChanges`, so instead of doing any of that it stops before the first action and
lists the engine files it would have written.

The generated code of engine modules is shared the same way: UnrealHeaderTool writes it into the
engine's own `Intermediate`, whichever project's build ran it. That only works while UHT's output is
the same for every project, and on this engine it was not: a type-less header included by another
header of its module (NetCore's `PushModel.h`, included by `FastArraySerializer.h`) was left out of
its package whenever the included header's own parse started after the includer's `#include` line --
a race between parse threads -- which changed `/Script/NetCore`'s hash in `NetCore.init.gen.cpp` and
made NetCore look out of date to whichever project built next. UE_Moon's UHT now marks those headers
after all parsing is done (`EpicGames.UHT`, `UhtHeaderFile.Resolve`), and the output is the same on
every run. With an engine that lacks that fix, a host build refused by `-NoEngineChanges` over NetCore
alone is this race, not a stale engine.

Never build the game target (`DreamGUITestHost`) casually: a game target against a source engine is
monolithic with its own build environment, which compiles the whole engine into this project.

## Moving to another branch or commit

The worktree is an ordinary checkout; change it with git, in the worktree:

```powershell
git -C I:\UnrealProject_Moon\DEV_58\DreamGUITestHost\Plugins\DreamGUI switch <branch>
git -C I:\UnrealProject_Moon\DEV_58\DreamGUITestHost\Plugins\DreamGUI switch --detach <commit>
```

- **One branch, one worktree.** git refuses to check out a branch that another worktree already has
  -- the working copy in `DevTest/Plugins/DreamGUI` included. To test a branch that is checked out
  there, detach at it (`switch --detach main`) or make a branch of your own from it.
- Then run `New-DreamGUITestHost.ps1` again with `-Branch <branch>` (or `-KeepCurrentHead` for a
  detached commit). `[CoreRedirects]` in `Config/DefaultEngine.ini` is taken from the worktree's own
  `Config/DefaultEngine.ini`; if that changed between the two branches, the script lists the file as
  different and `-Force` brings it up to date.
- Undoing a fix to see its test go red, then putting it back -- on a detached HEAD, so the branch never
  carries the revert and nothing has to be reset afterwards:

  ```powershell
  git -C <worktree> switch --detach
  git -C <worktree> revert --no-edit <fix commit>
  # build, run only the tests that cover it (the setup script needs -KeepCurrentHead meanwhile)
  git -C <worktree> switch <branch>
  ```

The host builds exactly what is on disk in its worktree, uncommitted changes there included. It never
sees anything in your own working copy that is not committed and checked out into the worktree.

## The derived data cache

The host has no derived-data-cache configuration, on purpose, and shares the cache the working project
already warmed:

- The cache this engine actually reads and writes is the local **Zen** store. Its data directory is
  machine-wide (`[Zen.AutoLaunch] DataPath` in the engine's `BaseEngine.ini`, resolving to
  `%LOCALAPPDATA%\UnrealEngine\Common\Zen\Data`) and so is its namespace (`ue.ddc`): every project on
  this engine uses the same store. On this machine `%LOCALAPPDATA%\UnrealEngine\Common\Zen` is a link to
  `F:\Zen\Zen`, so the data is on F:. The setup script reports where the data physically lands and warns
  if it is on drive C; moving it is a machine-wide decision, not the host's.
- The per-project FileSystem store (`<project>\LocalDerivedDataCache`) is configured `DeleteOnly` by this
  engine, so it takes no reads or writes; the host's lands on the host's own drive anyway.
- A cache hit needs the same cache key, and shader keys include the project's renderer settings and
  targeted shader formats. That is why `Config/DefaultEngine.ini` copies those sections from DevTest
  verbatim. Change one of them and the first launch compiles every shader again.

Two other things write to drive C during a run. Shader compile workers exchange their job files in
`%TEMP%\UnrealShaderWorkingDir\`; the engine accepts `-ShaderWorkingDir=<dir>` on its command line to
put them elsewhere, which matters on a cold cache. And the trace server keeps its store under
`%LOCALAPPDATA%\UnrealEngine\Common\UnrealTrace`.

## How the host differs from DevTest

| | DevTest | Host |
| --- | --- | --- |
| Game viewport client | the engine's `UGameViewportClient` | `UDreamGameViewportClient`, as the plugin's README asks of a game: characters reach text fields through `InputChar`, not the US-only key table |
| Plugins | DreamGUI plus about thirty others | DreamGUI and Enhanced Input |
| Content | the project's own, including `/Game/UI/WBP_ControlsGallery` | none |
| Startup map | a full showcase level | `/Engine/Maps/Entry` (one PlayerStart) |
| `[CoreRedirects]` | the project's own copy, older than the plugin's | the plugin's `Config/DefaultEngine.ini`, entry for entry |
| MoonToon ramp atlases | loaded from the MoonToon project plugin | `None` (the plugin is not there) |
| Editor layouts, per-user settings | whatever you last left them as | engine and plugin defaults |

Things that follow from this:

- **Text input.** A test that depends on which character road is live must set it up itself (the
  driver's switch for "the host delivers characters"). In DevTest the key-table fallback is what runs
  unless a test says otherwise; in the host the viewport client's road is.
- **`DreamGUI.Animation.Playback.ProjectGallery.ButtonSlidesIn`** checks DevTest's own gallery widget
  and says "nothing to check" wherever that asset does not exist. It passes in the host without having
  tested anything.
- **Class sweeps** (tests that walk every loaded class deriving from a DreamGUI base) see only the
  plugin's own classes in the host, and also other plugins' subclasses in DevTest.
- **`[CoreRedirects]` is not optional even here.** The plugin still ships assets saved under their LGUI
  class names (`NavigationSelection_Sprite`, referenced by the default navigation-selection widget, and
  `DefaultFont_Bitmap`), and the engine reads redirects only from the project's config.

## Known limitations

- Only what is checked out into the worktree is tested. Uncommitted work in your own working copy --
  an edited material, an unsaved asset -- is not in the host until it is committed and checked out
  there.
- The first build compiles the whole plugin (every module, the tests included) into the worktree's own
  `Intermediate/`, which takes a while and a few gigabytes on the host's drive.
- Every shader-affecting setting is DevTest's. That is what keeps the cache warm, and it also means the
  host renders with ray tracing, Lumen and Substrate on, like DevTest does.
