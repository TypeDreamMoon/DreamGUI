---
title: 类参考
description: DreamGUI 每个公开类的属性、函数与事件，由反射生成，并附与 UMG 的对照。
---

这里每一页对应一个类：它的属性（类型、分类、是否出现在细节面板、蓝图怎么读写）、可调用的函数、事件，以及页尾和对应 UMG 类的命名对照。页面由插件的 `DreamGUIReferenceDocs` commandlet 从反射打印出来，所以永远不会落后于头文件：

```powershell
UnrealEditor-Cmd.exe <project>.uproject -run=DreamGUIReferenceDocs
```

命名为什么和 UMG 一致、哪些成员刻意不同，见 [与 UMG 的命名对照](../controls/umg-parity.md)。

<a id="控件"></a>

## 控件

| | |
| --- | --- |
| [`UDreamBorder`](DreamBorder.md) | A border whose hierarchy is code, not an asset: a face, a padding, and a hole to put things in. |
| [`UDreamButton`](DreamButton.md) | A button whose hierarchy is code, not an asset. |
| [`UDreamDialog`](DreamDialog.md) | A dialog whose hierarchy is code, not an asset: a dimmer, a centred panel, a title, a content |
| [`UDreamDropdown`](DreamDropdown.md) | A dropdown whose hierarchy is code, not an asset. |
| [`UDreamEditableText`](DreamEditableText.md) | A text field with no box: UMG's EditableText, where Native.TextInput is its EditableTextBox. |
| [`UDreamExpandableArea`](DreamExpandableArea.md) | A header that toggles, and the content it hides. |
| [`UDreamInputKeySelector`](DreamInputKeySelector.md) | A button whose label is a bound key, and which listens for the next one when clicked. |
| [`UDreamListView`](DreamListView.md) | A list whose hierarchy is code, not an asset: rows built from a source, in a scrolling viewport. |
| [`UDreamListViewBase`](DreamListViewBase.md) | Everything a list and a tree have in common, which is nearly all of it. |
| [`UDreamMenuAnchor`](DreamMenuAnchor.md) | A place a menu opens from, and the thing that puts it away again. |
| [`UDreamMultiLineEditableText`](DreamMultiLineEditableText.md) | The same field over several lines: UMG's MultiLineEditableText. |
| [`UDreamNativeWidgetHost`](DreamNativeWidgetHost.md) | A hole in a DreamGUI hierarchy that a UMG widget fills. |
| [`UDreamProgressBar`](DreamProgressBar.md) | A progress bar whose hierarchy is code, not an asset. |
| [`UDreamRadioButton`](DreamRadioButton.md) | A radio button whose hierarchy is code, not an asset. |
| [`UDreamRichTextBlock`](DreamRichTextBlock.md) | A rich text block whose hierarchy is code, not an asset: one paragraph that reads markup. |
| [`UDreamRingMenu`](DreamRingMenu.md) | A ring menu whose hierarchy is code, not an asset: wedges around a hub, picked by direction. |
| [`UDreamScrollBar`](DreamScrollBar.md) | A scroll bar whose hierarchy is code, not an asset. |
| [`UDreamScrollBox`](DreamScrollBox.md) | A scroll box whose hierarchy is code, not an asset. |
| [`UDreamSlider`](DreamSlider.md) | A slider whose hierarchy is code, not an asset. |
| [`UDreamSpinBox`](DreamSpinBox.md) | A spin box whose hierarchy is code, not an asset. |
| [`UDreamTabView`](DreamTabView.md) | A tab view whose hierarchy is code, not an asset: a strip of tabs over a switcher of pages. |
| [`UDreamTextInput`](DreamTextInput.md) | A text field whose hierarchy is code, not an asset. |
| [`UDreamThrobber`](DreamThrobber.md) | A throbber whose hierarchy is code, not an asset: N pieces that pulse, saying only "something is |
| [`UDreamTileView`](DreamTileView.md) | A tile view whose hierarchy is code, not an asset: a list whose rows are laid out across as well |
| [`UDreamToggle`](DreamToggle.md) | A toggle whose hierarchy is code, not an asset. |
| [`UDreamTreeView`](DreamTreeView.md) | A tree whose hierarchy is code, not an asset: a list whose rows carry an indent and a twisty. |
| [`UDreamUIControl`](DreamUIControl.md) | A control whose hierarchy is code, not an asset. |

<a id="面板与插槽"></a>

## 面板与插槽

| | |
| --- | --- |
| [`UDreamLayoutContainerBorder`](DreamLayoutContainerBorder.md) | UMG's UBorder: one child, padded, aligned by the BORDER rather than by the child's slot. |
| [`UDreamLayoutContainerCanvasPanel`](DreamLayoutContainerCanvasPanel.md) | UMG Canvas Panel |
| [`UDreamLayoutContainerGridPanel`](DreamLayoutContainerGridPanel.md) | UMG Grid Panel |
| [`UDreamLayoutContainerHorizontalBox`](DreamLayoutContainerHorizontalBox.md) | UMG Horizontal Box |
| [`UDreamLayoutContainerMenuAnchor`](DreamLayoutContainerMenuAnchor.md) | UMG's UMenuAnchor, layout side. |
| [`UDreamLayoutContainerOverlay`](DreamLayoutContainerOverlay.md) | UMG Overlay |
| [`UDreamLayoutContainerSafeZone`](DreamLayoutContainerSafeZone.md) | UMG Safe Zone |
| [`UDreamLayoutContainerScaleBox`](DreamLayoutContainerScaleBox.md) | UMG Scale Box |
| [`UDreamLayoutContainerScrollBox`](DreamLayoutContainerScrollBox.md) | UMG Scroll Box |
| [`UDreamLayoutContainerSizeBox`](DreamLayoutContainerSizeBox.md) | UMG Size Box |
| [`UDreamLayoutContainerStackBox`](DreamLayoutContainerStackBox.md) | UMG Stack Box |
| [`UDreamLayoutContainerUniformGridPanel`](DreamLayoutContainerUniformGridPanel.md) | UMG Uniform Grid Panel |
| [`UDreamLayoutContainerVerticalBox`](DreamLayoutContainerVerticalBox.md) | UMG Vertical Box |
| [`UDreamLayoutContainerWidgetSwitcher`](DreamLayoutContainerWidgetSwitcher.md) | UMG Widget Switcher |
| [`UDreamLayoutContainerWrapBox`](DreamLayoutContainerWrapBox.md) | UMG Wrap Box |
| [`UDreamPanelLayoutBase`](DreamPanelLayoutBase.md) | Dream Panel Layout Base |
| [`UDreamPanelSlot`](DreamPanelSlot.md) | UMG-style per-child layout data owned by the child widget. |

<a id="控件基类与用户控件"></a>

## 控件基类与用户控件

| | |
| --- | --- |
| [`UDreamUserWidget`](DreamUserWidget.md) | A widget hierarchy that is a CLASS rather than an asset instance -- the counterpart to UMG's |
| [`UDreamWidget`](DreamWidget.md) | Base class for almost all UI related things. |

<a id="函数库"></a>

## 函数库

| | |
| --- | --- |
| [`UDreamLyricsLibrary`](DreamLyricsLibrary.md) | Dream Lyrics Library |
| [`UDreamTextPaintLibrary`](DreamTextPaintLibrary.md) | Tweens that move a text's paints (FDreamTextPaint) without repainting it: each drives one of UDreamText's paint |
| [`UDreamUIAnimationLibrary`](DreamUIAnimationLibrary.md) | The Blueprint-side helpers for FDreamUIAnimationHandle, which a struct cannot carry itself. |
| [`UDreamUIBPLibrary`](DreamUIBPLibrary.md) | Dream UIBPLibrary |
| [`UDreamUICaptureLibrary`](DreamUICaptureLibrary.md) | Pictures of what DreamGUI draws, written out as PNG files -- a render target, a canvas that renders into one, or a |
| [`UDreamUIInputModeLibrary`](DreamUIInputModeLibrary.md) | Input mode and cursor control, addressed by DreamGUI's user index rather than by a raw controller. |
| [`UDreamUILayoutLibrary`](DreamUILayoutLibrary.md) | The viewport, the mouse and the screen -- this framework's UWidgetLayoutLibrary. |
| [`UDreamUIWidgetGeometryLibrary`](DreamUIWidgetGeometryLibrary.md) | Coordinate conversions between a widget's own space, the viewport and the screen -- this |
| [`UDreamUIWidgetLibrary`](DreamUIWidgetLibrary.md) | Finding widgets, driving a drag, and building an image brush from Blueprint -- the parts of |

<a id="样式"></a>

## 样式

| | |
| --- | --- |
| [`FDreamBorderStyle`](DreamBorderStyle.md) | A border: a brush, a padding and one child. |
| [`FDreamButtonStyle`](DreamButtonStyle.md) | A button: one face, three pointer states, a label. |
| [`FDreamDialogStyle`](DreamDialogStyle.md) | A dialog: a dimmer over the screen, a panel on it, a title, content and buttons. |
| [`FDreamDropdownStyle`](DreamDropdownStyle.md) | A dropdown: a button-shaped face, and the list it opens. |
| [`FDreamExpandableAreaStyle`](DreamExpandableAreaStyle.md) | An expandable area: a header that toggles, and the content it hides. |
| [`FDreamInputKeySelectorStyle`](DreamInputKeySelectorStyle.md) | A key binder: a button whose label is the bound key, and which listens when pressed. |
| [`FDreamListStyle`](DreamListStyle.md) | A list: rows built from a source, in a scrolling viewport. |
| [`FDreamMenuAnchorStyle`](DreamMenuAnchorStyle.md) | A menu anchor: the popup's look, and how it is placed against whatever opened it. |
| [`FDreamProgressBarStyle`](DreamProgressBarStyle.md) | A progress bar: a track and the filled part of it. |
| [`FDreamRadioButtonStyle`](DreamRadioButtonStyle.md) | A radio button: the toggle's anatomy -- a box, a mark inside it, a label beside it -- with the |
| [`FDreamRichTextStyle`](DreamRichTextStyle.md) | A rich text block: one paragraph that reads markup. |
| [`FDreamRingMenuStyle`](DreamRingMenuStyle.md) | A ring menu: wedges around a hub, and the geometry that decides where they sit. |
| [`FDreamScrollBarStyle`](DreamScrollBarStyle.md) | A scroll box: a clipped viewport, the content that slides inside it, and the bars beside it. |
| [`FDreamScrollBoxStyle`](DreamScrollBoxStyle.md) | Dream Scroll Box Style |
| [`FDreamSliderStyle`](DreamSliderStyle.md) | A slider: a track, the filled part of it, and the handle riding it. |
| [`FDreamSpinBoxStyle`](DreamSpinBoxStyle.md) | A spin box: a numeric field between a decrement face and an increment face. |
| [`FDreamTabViewStyle`](DreamTabViewStyle.md) | A tab view: a strip of tabs over a switcher of pages. |
| [`FDreamTextInputStyle`](DreamTextInputStyle.md) | A text field: the box, the text in it, and the placeholder shown while there is none. |
| [`FDreamTextStyle`](DreamTextStyle.md) | How the built-in shader draws a text's glyphs beyond the plain face: outline, underlay (drop |
| [`FDreamThrobberStyle`](DreamThrobberStyle.md) | A throbber: N identical pieces, animating, saying only "something is happening". |
| [`FDreamTileViewStyle`](DreamTileViewStyle.md) | A tile view: a list whose rows are a grid of tiles. |
| [`FDreamToggleStyle`](DreamToggleStyle.md) | What a toggle looks like, separated from what it is. |
| [`FDreamTreeViewStyle`](DreamTreeViewStyle.md) | A tree: a list whose rows carry an indent and a twisty. |
| [`FDreamUIFaceBrush`](DreamUIFaceBrush.md) | An optional skin for a control part's procedural-rect face. |
| [`FDreamUIImageBrush`](DreamUIImageBrush.md) | Dream UIImage Brush |
| [`FDreamUIStateFaces`](DreamUIStateFaces.md) | One face drawn five ways -- Slate's FButtonStyle shape, which is the thing a control style here |
| [`FDreamUIWidgetGeometry`](DreamUIWidgetGeometry.md) | A widget's measured rect, and the handle every coordinate conversion below is asked through -- |
