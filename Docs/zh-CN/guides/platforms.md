---
title: "平台与状态"
description: "声称支持的平台和真正运行过的平台分开列出——Win64 是唯一构建并运行过的平台；发布闸门、移动端与专用服务器的实际状况、测试覆盖范围以及已知缺口。"
---

*声称*支持的平台和*真正运行*过的平台是不同的清单，因此两者都列在这里。**Win64 是唯一构建并运行过的平台。**

| | 编译过 | 运行过 |
| --- | --- | --- |
| **Win64** | 编辑器；游戏目标的 Development 与 Shipping | **编辑器加整套自动化测试**；打包的 Development 游戏——在发布闸门为该版本运行过打包文本冒烟测试之后 |
| Mac、Linux | 从未 | 从未——这里没有对应的机器或工具链 |
| iOS、Android | 从未 | 从未——这里没有 SDK，也从未在真机上运行过 |
| 主机 | 否 | 不在任何模块的 `PlatformAllowList` 中 |
| 专用服务器 | 从未——没有构建过服务器目标 | 否 |

<a id="the-release-gate"></a>

## 发布闸门

一个版本打标签之前要经过的流程：

1. **BuildPlugin**，为编辑器和 Development 与 Shipping 的游戏目标编译插件；
2. **测试宿主的打包文本冒烟测试**，它烹饪并打包一个 Development 游戏，运行它，并将其绘制内容与在未烹饪内容上运行的同一构建进行比对（见插件的 `Tools/TestHost/README.md` 和
   [字体与打包](fonts-and-packaging.md#5-checking-a-package)）。

在一个版本经过闸门之前，该版本没有任何打包后的游戏被运行过。

<a id="what-the-listed-platforms-mean"></a>

## 列出的平台意味着什么

每个模块的 `PlatformAllowList` 和描述符的 `SupportedTargetPlatforms` 列出了代码所面向的五个平台——Win64、Mac、Linux、IOS、Android——并且有一个测试让两者保持一致；`DreamGUIEditor` 和
`DreamGUIK2Nodes` 只列出 Win64、Mac 和 Linux。它们说明的是构建工具会*尝试*编译什么、烹饪器会烹饪什么，而不是有人在这些平台上编译、运行或发布过：着色器、文本所使用的引擎第三方库以及 C++（clang 与 MSVC 不同，会将警告视为错误）都只为 Win64 构建过。

<a id="mobile"></a>

## 移动端

有些代码路径从未运行过：

- 触摸已端到端接通（`BindTouch` → 独立输入模块）；
- 虚拟键盘已实现（`FPlatformApplicationMisc::RequiresVirtualKeyboard` → `ShowVirtualKeyboard`）；
- 渲染器中唯一一处实际生效的平台分支会为 Android GLES 翻转剔除设置。

GLES 上*不*提供 MSAA，渲染器会回退而不是假装支持——见 *Project Settings ▸ Plugins ▸ DreamUI* 中的 `AntiAliasingMethod`。

<a id="dedicated-server"></a>

## 专用服务器

此项目中没有服务器目标，因此服务器配置从未编译过。已完成的是可以通过阅读来检查的部分：

- `#if !UE_SERVER` 块是配平的；
- 真正会破坏服务器构建的问题——**没有任何在服务器守卫之外被引用的成员或函数是在守卫之内声明的**。`UDreamUMGWidget` 是唯一带有 `UE_SERVER` 块的类，而它们接触的每个字段（`SlateWindow`、
  `SlateWidget`、`WidgetRenderer`）都是无条件声明的；
- `WITH_FREETYPE=0` / `WITH_HARFBUZZ=0` 也以同样方式处理：文本路径暴露的每个函数都是无条件*定义*的，只有函数体被守卫，因此链接时不会出现未定义的内容。

在运行时，专用服务器什么都不绘制，UI 管理器不会在其世界中启动
（`UDreamUIManagerWorldSubsystem::ShouldRunForNetMode`），包括 play-in-editor 服务器：不绘制画布，也不创建
渲染器或绘制行。六个交互子系统本就拒绝在服务器进程中存在
（`ShouldCreateSubsystem` → `!IsRunningDedicatedServer()`）；在 play-in-editor 服务器的世界中，它们找不到管理器，于是
自动让位。

<a id="test-coverage"></a>

## 测试覆盖

自动化测试套件是插件的一部分——上游没有。用 `Automation RunTests DreamGUI` 运行它，或通过预设
使用 `Tools/Tests/Invoke-DreamGUITests.ps1`，后者会说明运行了多少个测试；预设见
[测试](../tools/tests.md)。打包后的游戏由测试宿主的打包文本冒烟测试检查，发布
闸门会运行它。

<a id="known-gaps"></a>

## 已知缺口

- `LineHeightPercentage` 和 `WrapTextAt` 只能通过真实的字体资产触达，因此测试未覆盖
  它们。
- 一个为换行文本提供宽度的面板——一个沿自身轴向测量的水平框——得到的回答是段落的单行宽度；当文本排布得比这更窄时才会换行。（提供了宽度时，
  如垂直框提供自身宽度，换行文本会以该宽度下的高度作答。）
- 还有一个内容资产的名字中带有 `Lex`（`Content/Blueprints/LexEventSystemActor_EnhancedInput`）。重命名
  `.uasset` 文件不会重命名其中的对象，因此只有编辑器侧的重命名能改变它；代码指向的是
  磁盘上实际存在的内容。
- 编辑器方面的工作由测试验证，而非肉眼检查。设计器中可能存在粗糙之处。

报告：https://github.com/TypeDreamMoon/DreamGUI/issues
