---
title: 网络与多人游戏
description: 在网络游戏中创建 UI 的位置——只在本地玩家能看到它的地方；专用服务器和监听服务器分别如何处理 DreamUI 控件、编辑器中运行时的联机模式，以及服务器世界给出的警告。
---

UI 属于其玩家正在观看它的那台机器。DreamUI 控件不会被复制：每个客户端构建自己的界面，而不同时作为玩家的服务器则不构建任何 UI。

<a id="who-has-a-ui-manager"></a>

## 谁拥有 UI 管理器

| 世界 | UI 管理器 | 在那里创建的控件会做什么 |
| --- | --- | --- |
| 单机、客户端 | 有 | 全部：布局、绘制、输入、tick |
| 监听服务器（主机也参与游戏） | 有 | 全部，显示在主机屏幕上 |
| 专用服务器 | **没有**，这是设计使然 | 什么都不做：它从不被布局、绘制、tick，也不会接收输入 |

专用服务器不绘制任何内容，也没有玩家，因此它的世界不会获得管理器（`UDreamUIManagerWorldSubsystem::ShouldRunForWorld`），也得不到管理器所产生的东西：绘制数据、渲染资源、绘制行。关卡中持有的控件树仍然会在那里构建，就像在 commandlet 中一样，而请求管理器的代码会得到 null。

<a id="make-ui-where-a-local-player-is"></a>

## 在存在本地玩家的地方创建 UI

玩家控制器的 `BeginPlay` 会在**每一**台拥有该控制器的机器上运行——而服务器为每个加入的玩家都有一个控制器。因此，在 `BeginPlay` 中不加任何前置判断地调用 `Create Dream Widget`，也会在服务器上运行，每个玩家一次：

- 在**专用服务器**上，控件什么也不做，DreamGUI 会针对该世界提示一次（见下文）；
- 在**监听服务器**上，它会被绘制在主机屏幕上——每个加入的玩家各一份。

因此，把 UI 放在“这是谁的机器”这一判断之后：

- **在玩家控制器中**，创建控件前先检查 `Is Local Controller`。把该控制器作为 `Owning Player` 传入也能起到保护作用：`Create Dream Widget` 会拒绝非本地控制器并返回 null，与 UMG 的 Create Widget 行为一致。
- **在 HUD 中。** `AHUD` 只会为本地玩家生成，绝不会为服务器上远程玩家的控制器生成，因此在 HUD 的 `BeginPlay` 中构建的 UI 已经位于正确的机器上。
- **基于复制状态**，在每个客户端上更新控件——在 `RepNotify` 中，或通过读取复制属性——而不是从服务器发送 UI 变更。

<a id="play-in-editor"></a>

## 在编辑器中运行

| PIE 联机模式 | 世界 | UI |
| --- | --- | --- |
| Play Standalone | 一个单机世界 | 有 |
| Play As Listen Server | `UEDPIE_0` 是主机，其余是客户端 | 在每个窗口中 |
| Play As Client | `UEDPIE_0` 是**专用服务器**，客户端从 `UEDPIE_1` 开始 | 仅在客户端窗口中 |

`Play As Client` 就是在编辑器中运行专用服务器的模式——启用 *Run Under One Process* 时在编辑器自己的进程中运行，未启用时作为独立的服务器进程运行——也是在没有本地检查的情况下创建的 UI 会以如下警告形式出现的模式。

<a id="the-warning"></a>

## 警告

在专用服务器的世界中注册的控件会记录一条日志，每个世界记录一次：

```text
<widget>: created in <world>, a dedicated server's world. A dedicated server has no DreamUI manager by design ...
```

它会指出第一个控件；之后在同一世界中创建的控件不会再被报告。这是一条警告，而不是错误：该控件在那里无害，只是无用。而一个*本应*拥有管理器却没有的世界——客户端、单机游戏、编辑器世界——仍会触发 ensure，因为那属于 DreamGUI 需要解释的情况。

<a id="see-also"></a>

## 另请参阅

- [迁移与升级](migration.md#servers)——专用服务器规则最初进入更新日志时的样子。
- [平台与状态](platforms.md)——尚未构建或运行过任何专用服务器目标。
