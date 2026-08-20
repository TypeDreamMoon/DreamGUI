<p align="center">
  <img width="112" src="./Resources/Icon128.png" alt="DreamGUI logo">
</p>

<h1 align="center">DreamGUI</h1>

<p align="center">
  <strong>3D UI System for Unreal Engine 5.8</strong><br>
  Event Framework · Prefab Workflow · Tween Animation<br>
  事件框架 · 预制体工作流 · 补间动画
</p>

<p align="center">
  A fork of <a href="https://github.com/liufei2008/LGUI"><strong>LGUI / LexUI</strong></a> by Lex Liu, MIT licensed.<br>
  基于 Lex Liu 的 <a href="https://github.com/liufei2008/LGUI">LGUI / LexUI</a>，MIT 许可。
</p>

---

## Overview / 概览

| Capability | Description |
| --- | --- |
| 3D UI | UI rendering and interaction for Unreal Engine / 面向 Unreal Engine 的 UI 渲染与交互 |
| Event Framework | Reusable event dispatch and input workflow / 可复用的事件分发与输入流程 |
| Prefab Workflow | Hierarchical UI authoring with nested Prefabs and overrides / 支持嵌套 Prefab 与 Override 的层级化 UI 编辑 |
| Tween Animation | Tween-based UI motion and transitions / 基于补间的 UI 动画与过渡 |

## Prefab Workflow / Prefab 工作流

> [!IMPORTANT]
> The Prefab Editor works on a loaded preview hierarchy. Preview changes are separate from the serialized asset until **Apply** or **Save**.<br>
> Prefab Editor 修改的是已加载的预览层级；执行 **Apply** 或 **Save** 之前，预览修改尚未进入资产序列化数据。

| Action / 操作 | In-memory asset / 内存资产 | Package on disk / 磁盘 Package |
| --- | --- | --- |
| Edit preview / 编辑预览 | Unchanged / 不变 | Unchanged / 不变 |
| **Apply** | Serializes the complete preview hierarchy / 完整序列化预览层级 | Controlled by **Save on Apply**; default: **Never** / 由 **Save on Apply** 控制；默认：**Never** |
| **Save** | Applies the complete preview hierarchy / Apply 完整预览层级 | Saves the Prefab package / 保存 Prefab package |
| Runtime `LoadPrefab` | Creates an instance only / 仅创建实例 | Unchanged / 不变 |

### Nested Prefabs / 嵌套 Prefab

Saving a child Prefab refreshes its instances in loaded parent Prefabs. Registered parent overrides are restored after the child data is reloaded. Changes that were not recorded as overrides can be replaced during that refresh.

保存子 Prefab 后，已加载父 Prefab 中的对应实例会刷新；子 Prefab 数据重载后会恢复父级已登记的 Override。未被记录为 Override 的实例修改可能在刷新时被替换。

| Intent / 目的 | Edit here / 编辑位置 |
| --- | --- |
| Shared change / 所有实例共享 | Source child Prefab / 源子 Prefab |
| Parent-specific change / 仅当前父级生效 | Nested instance override / 嵌套实例 Override |

### Extension and Migration Safety / 扩展与迁移安全

> [!WARNING]
> `UDreamUIPrefab::SavePrefab` and `UDreamUIPrefabHelperObject::SavePrefab` perform **full Prefab serialization**, not a property-level patch. Calling `ClearLoadedPrefab` followed by `Init` rebuilds the loaded hierarchy and can discard unapplied editor changes.<br>
> `UDreamUIPrefab::SavePrefab` 与 `UDreamUIPrefabHelperObject::SavePrefab` 执行的是**完整 Prefab 序列化**，不是属性级补丁。调用 `ClearLoadedPrefab` 后再 `Init` 会重建已加载层级，可能丢弃尚未 Apply 的编辑器修改。

Prefab upgrade and migration tools must follow this checklist:

1. Block execution while the target Prefab Editor has unapplied changes, or use the existing dirty-editor confirmation path before refreshing it.<br>目标 Prefab Editor 存在未 Apply 修改时拒绝执行，或在刷新前使用现有的 dirty-editor 确认流程。
2. Build and validate changes on a transient duplicate before replacing target serialization data.<br>先在 Transient 副本上构建并验证修改，再替换目标资产的序列化数据。
3. Use source control or a backup, and report every Prefab package marked dirty.<br>修改前使用版本控制或备份，并明确报告所有被标记 dirty 的 Prefab package。
4. Do not reapply hard-coded hierarchy, slot, size, padding, or spacing values to an already-migrated Prefab.<br>不要对已迁移 Prefab 重复写入硬编码的层级、Slot、尺寸、Padding 或 Spacing。
5. Refresh loaded parent Prefabs after child changes while preserving registered overrides.<br>子 Prefab 修改后刷新已加载父 Prefab，同时保留已登记的 Override。
6. Use transient duplicates in automation tests; never call `SavePrefab` on production assets.<br>自动化测试必须使用 Transient 副本，禁止对正式资产调用 `SavePrefab`。

---

---

## Divergence from Upstream / 与上游的差异

> [!IMPORTANT]
> This fork has diverged substantially and **is not a drop-in replacement for upstream LGUI/LexUI**.
> Do not expect upstream patches to apply, or upstream documentation to describe this behaviour.<br>
> 本分支与上游差异较大，**不能直接替换上游 LGUI/LexUI**。上游的补丁通常无法套用，上游文档也不能用来描述这里的行为。

Forked from upstream `LexUI/5.7` at `765efeaf1` (2026-07-13). 214 commits since,
2026-07-18 to 2026-08-20.

从上游 `LexUI/5.7` 的 `765efeaf1`（2026-07-13）分出，此后 214 个提交。

### Why the branches cannot easily merge / 为什么两边已经很难合并

| | Upstream / 上游 | Here / 本分支 |
| --- | --- | --- |
| Engine / 引擎 | UE 5.7 (`LexUI/5.7`); no 5.8 branch on this line | **UE 5.8** |
| Layout / 布局 | FlexBox + Grid layout family, still actively developed | **Family deleted**; UMG-shaped panels only |
| Widget base / 控件基类 | — | Unchanged, but invalidation and geometry write-back are rebuilt |

The layout divergence is the sharpest: upstream's 2026-08-08 fix for an infinite loop in
`ULexLayoutContainerFlexBox` has no meaning here, because that class no longer exists.

布局这一条分歧最深：上游 2026-08-08 修的 `ULexLayoutContainerFlexBox` 死循环，在这里没有意义
—— 那个类已经不存在了。

### What changed / 改了什么

**Layout / 布局** — rebuilt along the lines Blink and Yoga use. The legacy Lex layout family
(`ULexLayoutContainerFlexBox`, `ULexLayoutContainerGrid`, `ULexLayoutSelfFlexBox`,
`ULexLayoutSelfGrid`, and the `ELexUILayoutMode` switch) was deleted; the UMG-shaped panel
family is the only layout path. Measurement is `const` and separated from application; panels
arrange into an immutable fragment which is then committed in one write; desired size is
memoised for the duration of a pass; invalidation carries a reason, so a move no longer
re-measures the whole ancestor chain.

按 Blink / Yoga 的形状重建。legacy 布局家族整体删除，UMG 家族成为唯一路径。测量变 const 并与
应用拆开；面板排布进不可变的 fragment 再一次性提交；每遍 pass 内缓存 desired size；失效带原因，
移动不再触发整条祖先链重新测量。

**Text / 文字** — `Margin`, `LineHeightPercentage`, `WrapTextAt` (a wrap width independent of
the box) and Best Fit (shrink the font until it fits). The first three match UMG's text
controls; Best Fit matches uGUI's, and neither UMG nor Slate has an equivalent.

**Perspective / 透视** — a per-widget perspective inherited by its subtree, in the shape CSS
uses. Requires a screen-space canvas with a perspective projection; inert otherwise.

**Render transform / 渲染变换** — widened to three dimensions, so a widget can be animated
inside a layout without the layout fighting it.

**Prefab editor / 预制体编辑器** — reviewed against UMG's widget designer and largely rebuilt.
Viewport picking is by widget **rect** rather than by rendered triangles, because layout-only
panels have no mesh and were unhittable. Added: hover feedback, per-axis resize handles, an
anchor medallion, marquee selection, drag-to-reparent on the canvas, Content-Browser drops onto
the design surface, palette favourites, and type-aware search. Create, paste and drop now enter
the undo stack.

视口命中改为按控件**矩形**判定 —— 布局面板没有网格，射线永远打不中。新增 hover 反馈、逐轴缩放
手柄、锚点 medallion、框选、画布内改父级、Content Browser 拖入、调色板收藏、按类型搜索。新建 /
粘贴 / 拖入进入撤销栈。

**Tests / 测试** — 279 automation tests, from none. Run with
`Automation RunTests DreamGUI`.

### Known gaps / 已知缺口

- `LineHeightPercentage` and `WrapTextAt` are exercised only through a real font asset and are
  not covered by tests.<br>
  这两项需要真实字体资产才能跑到，没有测试覆盖。
- 25 content assets still carry `Lex` in their names. Renaming a `.uasset` file does not rename the
  object inside it, so only an editor-side rename can change those; the code points at what is
  actually on disk.<br>
  25 个内容资产名字里仍有 `Lex`。改 `.uasset` 文件名不会改包内对象名，只有在编辑器里改才行；代码指向的
  是磁盘上真实存在的名字。

## Installing / 安装

> [!IMPORTANT]
> This plugin was renamed from LGUI/LexUI. Assets saved against the old names need CoreRedirects,
> and **the engine reads those from the project's config only** -- a plugin's own
> `Config/DefaultEngine.ini` is not consulted for them. Copy the `[CoreRedirects]` block from
> [`Config/DefaultEngine.ini`](./Config/DefaultEngine.ini) into your project's
> `Config/DefaultEngine.ini`.<br>
> 本插件由 LGUI/LexUI 改名而来。按旧名字保存的资产需要 CoreRedirects，而**引擎只从工程的 config 读取**
> —— 插件自己的 `Config/DefaultEngine.ini` 不会被读。请把
> [`Config/DefaultEngine.ini`](./Config/DefaultEngine.ini) 里的 `[CoreRedirects]` 段落复制到你工程的
> `Config/DefaultEngine.ini`。

Only needed if you have assets authored against LGUI/LexUI. A fresh project does not.

只有当你已有基于 LGUI/LexUI 制作的资产时才需要；全新工程不需要。

## License / 许可

MIT. See [LICENSE](./LICENSE).

Copyright (c) 2026-present TypeDreamMoon<br>
Copyright (c) 2019-present Lex Liu

Substantial portions of this software remain the work of Lex Liu and are used under
the MIT terms of the [original project](https://github.com/liufei2008/LGUI). The MIT
notice must travel with any copy or substantial portion of this code, including yours.

本项目大量代码仍属 Lex Liu 的原创工作，依据[原项目](https://github.com/liufei2008/LGUI)的
MIT 条款使用。任何拷贝或实质性部分都必须随附 MIT 声明，你的分发也一样。
