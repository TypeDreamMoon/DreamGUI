---
title: "安装"
description: "环境要求、把插件克隆进 Plugins/、装完后值得马上决定的两项设置（键盘布局用的游戏视口客户端、Slate 输入源），以及各项设置存在哪里。"
---

DreamGUI 的装法和别的项目插件一样：克隆进项目的 `Plugins/`，重新生成工程文件，编译。
对一个新项目来说，这就是全部。后面两节讲的是装完之后值得马上决定的两件事 ——
都不是必需的，但不做的话，一个会让非美式键盘打错字，一个会让 UI 在引擎自己的 UI-only 输入模式下听不到任何输入。

| | |
| --- | --- |
| 引擎 | Unreal Engine `5.8`（`EngineVersion` 5.8.0），源码引擎和 Epic 启动器安装版都行 |
| 平台 | Win64 是唯一构建并运行过的平台（见[平台](../guides/platforms.md)） |
| 依赖插件 | `EnhancedInput`，`.uplugin` 里自己启用 |
| 运行时模块 | `DreamGUIRenderer`、`DreamGUI`、`DreamGUIInput`、`DreamGUIControls`、`DreamGUIExtensions`、`DreamGUISamples`（`PostConfigInit`）；`DreamTween`（`Default`） |
| 只在未烹饪环境加载 | `DreamGUIEditor`、`DreamGUIK2Nodes`（`UncookedOnly`） |
| 测试 | `DreamGUITests`（`Editor`，`PostEngineInit`） |

<a id="requirements"></a>

## 要求

<a id="engine-58-a-launcher-install-included"></a>

### 引擎 5.8，启动器安装版也可以

插件只用引擎的公开头文件编译：渲染器通过公开的 scene-texture API 读取场景深度，静态检查 `engine-private-path` 会让任何指向
`Runtime/Renderer/Private` 或 `Internal` 的 include 路径直接失败。所以不需要源码引擎，也没有 5.7 的分支。

<a id="the-bundled-msdfgen"></a>

### 自带的 msdfgen

字形光栅器把 [msdfgen](https://github.com/Chlumsky/msdfgen)（Viktor Chlumsky，MIT）编译进它自己的一个
翻译单元，用来从字形轮廓生成多通道距离场。以前这要求源码引擎：上游不提供单文件发行版，而是用
`all-in-one/generate.py` 生成一对 `msdfgen.h` / `msdfgen.cpp`，启动器安装版的
`Engine/Source/ThirdParty/msdfgen/` 下只有一个 `msdfgen.tps`。

现在插件自己带着生成好的那一对，放在 `ThirdParty/msdfgen-single-file/`，已经提交进仓库。所以普通的
`git clone`、甚至 GitHub 的 zip 下载（不带子模块内容）都能直接编译，构建时不需要 Python。
`ThirdParty/msdfgen` 子模块只是 `Tools/UpdateMsdfgen.ps1` 重新生成这对文件时的来源，平时不编译、不包含；
`Tools/UpdateMsdfgen.ps1 -Check` 在提交的那对文件和子模块对不上时以退出码 1 结束。

**提示：**

这份拷贝被包在 `DreamMsdfgen` 命名空间里（`MSDFGEN_PARENT_NAMESPACE`）。SlateCore 也编译了一份自己的
  msdfgen，单体构建里两份符号会冲突 —— 两份"看起来差不多"的库比一个链接错误更糟，所以这层命名空间不是可选的。

<a id="install"></a>

## 安装

```bash
git clone https://github.com/TypeDreamMoon/DreamGUI.git Plugins/DreamGUI
```

重新生成工程文件，编译。不需要源码引擎、不需要引擎私有头文件，也没有要拷贝的设置。

1.0.0 不带任何 CoreRedirects。如果项目里有按 LGUI、LexUI 保存的资产，或者 1.0.0 之前的构建按后来改掉的名字保存的资产，
要借 2.1.0 的 redirect 段把它们重存一次：步骤见[从开发版升级](../guides/upgrading.md#from-21-to-100)，从 LGUI、LexUI
过来的项目从[迁移](../guides/migration.md)开始。

<a id="checking-it-is-there"></a>

### 确认装上了

编辑器起来以后：

- 内容浏览器的 **添加** 菜单最上面一组里有 **DreamGUI Widget**，下面还有一个 **DreamGUI** 子菜单，分 Basic、Fonts、Graphics、Rich Text 几节；
- *Project Settings ▸ Plugins* 下有 **Dream GUI** 和 **DreamUI** 两页（还有一页 **DreamGUI Editor**）；
- 关卡编辑器的 *Tools* 菜单里有 **Open DreamUI Workspace (VSCode)** 和 **Rebuild DUI**。

<a id="keyboard-layouts-give-dreamgui-the-game-viewport-client"></a>

## 键盘布局：把游戏视口客户端交给 DreamGUI

DreamGUI 的文本框不是 Slate 控件，所以它永远不在键盘焦点路径上；而游戏里平台的**字符**事件在引擎中唯一的
落脚点是虚函数 `UGameViewportClient::InputChar` —— 它没有可以订阅的委托。没人接管这个函数时，`UUITextInput` 退回到自己的 `FKey` → 字符对照表，而这张表
只在 **US QWERTY** 下是对的：AZERTY、QWERTZ、Dvorak、西里尔字母、死键和 AltGr 打出来的都是错字。（IME 用户不受影响 ——
组字结果走 TSF，不走这张表。）

两种办法任选其一。两种都没做时，文本框在第一次被编辑时打一条警告。

<a id="1-use-the-plugins-viewport-client"></a>

### 一、直接用插件的视口客户端

在项目的 `Config/DefaultEngine.ini`：

```ini
[/Script/Engine.Engine]
GameViewportClientClassName=/Script/DreamGUIInput.DreamGameViewportClient
```

<a id="2-keep-your-own-viewport-client"></a>

### 二、保留你自己的视口客户端

要么让它从 `UDreamGameViewportClient` 派生，而不是 `UGameViewportClient`；要么保留原来的基类，在它的
`InputChar` 重写里把字符交给 DreamGUI（模块 `DreamGUIInput`，头文件
`Interaction/DreamUITextInputTarget.h`）—— **在控制台之后、在基类之前**：

```cpp
#include "Interaction/DreamUITextInputTarget.h"

bool UMyGameViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
    FString CharacterString;
    CharacterString += Character;
    // An open console takes every character.
    if (ViewportConsole && ViewportConsole->InputChar(FInputDeviceId::CreateFromInternalId(ControllerId), CharacterString))
    {
        return true;
    }
    // Before the base class: in a play-in-editor viewport it answers true for every character, so a
    // field asked after it never sees one there.
    if (!IgnoreInput() && DreamUITextInputRouter::RouteViewportCharacter(this, ControllerId, Character))
    {
        return true;
    }
    return Super::InputChar(InViewport, ControllerId, Character);
}
```

顺序为什么是这样：PIE 视口里，基类会把每个字符都吞掉（返回 true），免得游戏输入漏到编辑器自己的窗口；
先问基类再看它的返回值，就永远到不了文本框 —— PIE 里打字退回对照表，而打包后的游戏里基类返回 false，
又能拿到真字符。

`DreamUITextInputRouter::RouteViewportCharacter` 就是全部契约：它把字符交给打字的那个玩家（`ControllerId`
对应的玩家）正在编辑的文本框；没有文本框要，就作为一个按键字符交给这个玩家当前聚焦的东西；返回两者中是否有一个收下了。
从第一个这样到达的字符起，`FKey` 对照表在这个世界里就不再合成可打印字符，两条路不会重复打字。
项目里需要的模块依赖是 `DreamGUIInput`。

**提示：**

`UDreamGameViewportClient` 还做两件事：在 DreamGUI 自己的 UI-only 输入模式
  （`UDreamUIInputModeLibrary::SetInputModeUIOnly`）下把引擎会忽略的输入照常送给 DreamGUI；在 DreamGUI 有焦点时，不让 Slate 自己的导航（Tab、方向键）把键盘焦点
  从视口带进 UMG。自己写视口客户端时，后一件由世界的输入子系统通过基类的 `OnNavigationOverride` 兜着。

<a id="input-in-every-input-mode-the-slate-input-source"></a>

## 任何输入模式下都有输入：Slate 输入源

默认情况下，DreamGUI 通过它的预设事件系统 Actor 在玩家控制器上的绑定来听输入，所以在引擎自己的
UI-only 输入模式下它什么也听不到：`SetInputMode(FInputModeUIOnly())` 让游戏视口忽略输入，控制器根本收不到。

打开 *Project Settings ▸ Plugins ▸ Dream GUI ▸ Input ▸ Use Slate Input Source*（`bUseSlateInputSource`），
DreamGUI 就改从 Slate 本身听鼠标、触摸、按键和摇杆 —— 一个输入预处理器，排在游戏视口之前 —— 在任何输入模式下都有效。
开着它时，预设 Actor 自动让位，同一个输入不会到两次。

**Slate Input Consume Policy**（`SlateInputConsumePolicy`）决定 UI 从游戏那里扣下什么：

| 取值 | UI 扣下的 |
| --- | --- |
| `Never`（默认） | 什么都不扣，游戏照常听到一切 |
| `WhenOverUI` | 落在 DreamGUI UI 上的按下、松开和滚轮，以及 UI 收下的任何按键 |
| `WhenHandled` | 只有落在"会处理按下"的控件上的按下，以及 UI 收下的按键 |

正在编辑的文本框里打的键，无论哪种策略都会扣下。同一组设置里还有 `SlateInputStickScrollSpeed`（默认 1500）：
右摇杆滚动聚焦内容的速度，单位是画布单位每秒（推满时）。

Slate 输入源目前默认关闭，会在以后的某个版本里变成默认。

<a id="where-settings-live"></a>

## 设置存在哪里

| 页面 | 类 | 写入到 |
| --- | --- | --- |
| *Project Settings ▸ Plugins ▸ Dream GUI* | `UDreamGUISettings` | 项目的 `Config/DefaultDreamGUI.ini`，`[/Script/DreamGUI.DreamGUISettings]` |
| *Project Settings ▸ Plugins ▸ DreamUI* | `UDreamUISettings` | 项目的 `Config/DefaultEngine.ini`（`config=Engine`） |
| *Project Settings ▸ Plugins ▸ DreamGUI Editor* | `UDreamUIEditorSettings` | 项目的 `Config/DefaultEditor.ini`（`config=Editor`） |

两个运行时设置类都是在被使用的地方读取，因此在运行时更改的值会从下一帧起生效。

**提示：**

  插件的 `Config/Game.ini` 会把 `/DreamGUI` 放入 `DirectoriesToAlwaysCook`，因此由显式包列表（`-map=`、chunk）驱动的烹饪仍会包含插件的默认资产。

<a id="next"></a>

## 下一步

  - [你的第一个界面](first-screen.md) — 从一个 .dui 文件到屏幕，以及关卡中的同一个类。
  - [项目结构](project-layout.md) — 模块分层、.dui 文件放在哪里、插件文件夹里有什么。
