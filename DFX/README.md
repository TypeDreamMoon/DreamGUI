# Niagara sources

These `.dfs` files are the source for the Niagara systems under `Content/Niagara`, `Content/Samples/Niagara` and
`Content/Tests/Niagara`. The `.uasset` files are build output and are committed alongside them, so **DreamGUI has no
dependency on DreamFX**: without it the systems are ordinary `UNiagaraSystem` assets, built from the engine's own Niagara
modules, and everything works. [DreamFX](https://github.com/TypeDreamMoon/DreamFX) is only needed to rebuild them from text.

| Source | Asset | What it is |
| --- | --- | --- |
| `Niagara/NS_DreamUI_FlyTo.dfs` | `/DreamGUI/Niagara/NS_DreamUI_FlyTo` | The flight `UDreamNiagaraSystem::BurstTo` spawns when no flight system is set |
| `Samples/NS_Sample_*.dfs` | `/DreamGUI/Samples/Niagara/` | The effects `Content/Samples/NiagaraEffects.dui` shows |
| `Tests/NS_DreamUITest_Particles.dfs` | `/DreamGUI/Tests/Niagara/` | The test system: every particle placed by its index, nothing random |
| `Tests/NS_DreamUITest_Unsupported.dfs` | `/DreamGUI/Tests/Niagara/` | What is not drawn: a GPU emitter, a mesh and a light renderer |

## Rebuilding

With DreamFX installed in the project, `Plugins/DreamGUI/DFX` is a source root. Build one file:

```bash
pwsh -File Plugins/DreamFX/.skill/dfx.ps1 build Plugins/DreamGUI/DFX/Samples/NS_Sample_ClickSparks.dfs -Force
```

`-All` builds every DreamFX source in the project, not just this plugin's.

## Before committing a rebuilt asset

DreamFX stamps each asset's package with where its source was -- relative to the source root, which stays, and as the full
path on the machine that built it, which has no place in a repository others check out. Take the full path off:

```bash
UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="DreamGUI.GeneratedAssets.ForgetFullPaths /DreamGUI Exit" -unattended -nullrhi
```

The command is the test module's. `DreamGUI.Niagara.TheGeneratedSystemsNameNoMachinesPath` fails while any system under
`/DreamGUI` still carries one.

## Things that will bite

**Only the engine's own modules.** A module of DreamFX's (`/DreamFX/...`) or of the project would make the asset depend on
a plugin DreamGUI does not ship with.

**The CPU VM has no integer remainder and no `smoothstep`.** An `hlsl { }` expression on a CPU emitter compiles to the
Niagara VM: write `fmod(x, 4.0)` and `floor(x * 0.25)` rather than `%` and `/` on integers, and a smoothstep out in full
(`t * t * (3 - 2 * t)` of a saturated `t`). `ExecIndex()` is there, and is what places the test system's particles.

**DreamGUI draws CPU emitters' sprite and ribbon renderers.** A GPU emitter or a mesh renderer builds, and is then reported
and left out by the visual.
