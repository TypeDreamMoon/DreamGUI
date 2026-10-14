---
title: 平台与现状
description: 声称支持的平台和真正跑过的平台分开列：Win64 是唯一构建并运行过的平台；发布闸门、移动端与专用服务器的实际情况，以及测试覆盖和已知缺口。
---

"声称支持"和"真正跑过"是两张不同的单子，所以两张都列在这里。**Win64 是唯一构建并运行过的平台。**

| | 编译过 | 运行过 |
| --- | --- | --- |
| **Win64** | 编辑器；游戏目标的 Development 与 Shipping | **编辑器加整套自动化测试**；打包的 Development 游戏 —— 在发布闸门为这个版本跑过打包文本冒烟测试之后 |
| Mac、Linux | 从没有 | 从没有 —— 这里没有对应的机器和工具链 |
| iOS、Android | 从没有 | 从没有 —— 这里没有 SDK，也从没上过真机 |
| 主机 | 否 | 不在任何模块的 `PlatformAllowList` 里 |
| 专用服务器 | 从没有 —— 没有构建过服务器目标 | 否 |

<a id="发布闸门"></a>

## 发布闸门

一个版本打标签之前要过的东西：

1. **BuildPlugin**，为编辑器、以及 Development 和 Shipping 的游戏目标编译插件；
2. **测试宿主的打包文本冒烟测试**：烹饪并打包一个 Development 游戏，运行它，把它画出来的东西和同一构建在未烹饪内容上的运行逐字段比对
   （见插件的 `Tools/TestHost/README.md` 和[字体与打包](fonts-and-packaging.md#检查一个包)）。

在闸门为一个版本跑过之前，那个版本没有任何打包后的游戏运行过。

<a id="列出来的平台意味着什么"></a>

## 列出来的平台意味着什么

每个模块的 `PlatformAllowList` 和描述文件的 `SupportedTargetPlatforms` 列出了代码面向的五个平台 —— Win64、Mac、Linux、IOS、Android ——
有一个测试让两者保持一致；`DreamGUIEditor` 和 `DreamGUIK2Nodes` 只列 Win64、Mac、Linux。这些列表说的是构建工具会**尝试**编译什么、
烹饪器会烹饪什么，而不是有人在这些平台上编译过、运行过或发布过：着色器、文本所用的引擎第三方库，以及 C++ 本身
（clang 和 MSVC 不同，它把警告当错误）都只为 Win64 构建过。

<a id="移动端"></a>

## 移动端

有从没运行过的代码路径：

- 触摸端到端都接上了（`BindTouch` → 独立输入模块）；
- 虚拟键盘已实现（`FPlatformApplicationMisc::RequiresVirtualKeyboard` → `ShowVirtualKeyboard`）；
- 渲染器里唯一一处活的平台分支，为 Android GLES 翻转剔除。

GLES 上**没有** MSAA，渲染器会退回而不是假装有 —— 见 *Project Settings ▸ Plugins ▸ DreamUI* 的 `AntiAliasingMethod`。

<a id="专用服务器"></a>

## 专用服务器

这个项目里没有服务器目标，所以服务器配置从没编译过。做过的是能靠阅读检查的那部分：

- `#if !UE_SERVER` 块是配平的；
- 真正会弄坏服务器构建的那件事 —— **没有任何在服务器守卫之外被引用的成员或函数是在守卫之内声明的**。`UDreamUMGWidget` 是唯一有
  `UE_SERVER` 块的类，而它们碰到的每个字段（`SlateWindow`、`SlateWidget`、`WidgetRenderer`）都是无条件声明的；
- `WITH_FREETYPE=0` / `WITH_HARFBUZZ=0` 按同样的方式处理：文本路径暴露的每个函数都无条件**定义**，只有函数体被守卫，所以链接时不会有未定义的东西。

运行时，专用服务器什么都不画，UI 管理器也不在它的世界里启动（`UDreamUIManagerWorldSubsystem::ShouldRunForNetMode`），PIE 服务器也一样：
不画画布，不创建渲染器或绘制行。有六个交互子系统本来就拒绝在服务器进程里存在（`ShouldCreateSubsystem` → `!IsRunningDedicatedServer()`）；
在 PIE 服务器的世界里，它们找不到管理器，于是自动让位。

<a id="测试覆盖"></a>

## 测试覆盖

自动化测试套件是插件的一部分 —— 上游没有。用 `Automation RunTests DreamGUI` 运行，或者用 `Tools/Tests/Invoke-DreamGUITests.ps1` 按预设运行，
它会报告实际跑了多少个测试；预设见[测试](../tools/tests.md)。打包后的游戏由测试宿主的打包文本冒烟测试检查，发布闸门会跑它。

<a id="已知缺口"></a>

## 已知缺口

- `LineHeightPercentage` 和 `WrapTextAt` 只能通过真实的字体资产触达，所以测试没有覆盖它们。
- 一个不给换行文本宽度的面板 —— 一个沿自身轴向测量的水平框 —— 得到的回答是段落的单行宽度；文本排布得比这更窄时才会换行。
  （给了宽度时，比如竖排框给出自己的宽度，换行文本会回答它在那个宽度下的高度。）
- 还有一个内容资产名字里带着 `Lex`（`Content/Blueprints/LexEventSystemActor_EnhancedInput`）。重命名 `.uasset` 文件并不会重命名其中的对象，
  所以只有编辑器侧的重命名能改它；代码指向的是磁盘上实际存在的那个。
- 编辑器方面的工作是由测试验证的，而不是靠眼睛。设计器里会有粗糙的地方。

遇到问题请报告：https://github.com/TypeDreamMoon/DreamGUI/issues
