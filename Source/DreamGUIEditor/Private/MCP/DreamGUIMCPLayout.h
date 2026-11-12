// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
//
// `layout` in words: a screen laid out on the stage, read back node by node -- where each one is in viewport pixels,
// whether it shows, what it says and whether that fits -- and the problems a person sees at a glance and a model that
// cannot see the picture does not: something off the screen, a text that is cut, a button too small to hit or under
// another, edges that miss each other by a pixel or two, a size of the viewport that breaks what another size kept,
// text that does not stand out from what is behind it.
//
// The rules have codes of their own, DUI8001 to DUI8007 (EDreamUIDiagnosticCode, the 8xxx layout range), and their
// thresholds are below and in Docs/tools/dreammcp.md. They never fail a compile: they come out of `layout` and
// `preview` alone, as warnings and infos.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

#include "MCP/DreamGUIMCPResult.h"

struct FDreamUIAst;

namespace DreamGUIMCP
{
	class FScreenStage;

	/** The thresholds the layout rules hold a screen to. */
	namespace LayoutLint
	{
		/** A node reaching this far (pixels) past an edge of the viewport is off the screen (DUI8001). */
		inline constexpr double OffScreenTolerance = 1.0;
		/** A node narrower or lower than this (pixels) is of zero size (DUI8003). */
		inline constexpr double ZeroSize = 1.0;
		/** The smallest a control the pointer presses may be on each side, in pixels (DUI8003): WCAG 2.2's minimum target. */
		inline constexpr double MinTargetSize = 24.0;
		/** Two controls overlapping by at least this on both axes, in pixels, are on top of each other (DUI8004). */
		inline constexpr double OverlapTolerance = 2.0;
		/**
		 * ...unless one lies at least LayeredShare inside the other and the other is LayeredAreaRatio times its area or more:
		 * a button on a pressable card, a dialog on a click-away backdrop, layered on purpose.
		 */
		inline constexpr double LayeredShare = 0.95;
		inline constexpr double LayeredAreaRatio = 4.0;
		/** Edges or gaps differing by more than the first and at most the second, in pixels, nearly line up (DUI8005). */
		inline constexpr double NearMissLow = 0.5;
		inline constexpr double NearMissHigh = 3.0;
		/** Text against what is behind it: below the first ratio a warning, below the second an info for text under LargeText px (DUI8007). */
		inline constexpr double ContrastPoor = 3.0;
		inline constexpr double ContrastLow = 4.5;
		inline constexpr double LargeText = 24.0;
		/** A control's own part (not a node the file writes) below this ratio is hidden on purpose, not hard to read. */
		inline constexpr double HiddenByControl = 1.1;
	}

	/** One widget of the screen as it was laid out. */
	struct FLayoutNode
	{
		int32 Parent = INDEX_NONE;
		int32 Depth = 0;
		TArray<int32> Children;
		/** The node's id, as the file names it. */
		FString Name;
		/** Ids from the screen's root down, joined with '/': `Root/Card/Heading`. */
		FString Path;
		/** The node type as written (`Text`, `Native.Button`, `Card`), or the class when the file did not write the node. */
		FString Type;
		/** Written in the file (has a source line) rather than made by a control or a component for itself. */
		bool bAuthored = false;
		int32 Line = 0;
		int32 Column = 0;
		/** The file the node is written in, project-relative. */
		FString File;

		/** In viewport pixels, top left origin, Y down. */
		double X = 0, Y = 0, W = 0, H = 0;
		/** The part that can show at all: the rect cut to its clipping ancestors. Empty when cut away completely. */
		double VisX = 0, VisY = 0, VisW = 0, VisH = 0;
		bool bClipped = false;
		bool bClipsChildren = false;
		/**
		 * Turned out of the canvas plane (depth, yaw or pitch), sheared, or inside a perspective: the rect is the bound of
		 * its outline as the canvas's camera draws it.
		 */
		bool b3D = false;
		/** A corner of a node turned in 3D lies behind the eye, so its rect is the outline seen straight on instead. */
		bool bApproximate = false;

		bool bActive = true;
		bool bShown = true;
		float Opacity = 1.0f;
		/** Takes the pointer: carries an enabled selectable (a button, a toggle, a slider...). */
		bool bInteractive = false;
		/** Draw order within its canvas (the flattened hierarchy index). */
		int32 Order = 0;
		/** The canvas it draws in, when it is not the screen's own. */
		FString Canvas;
		/** Its own layout container (`VerticalBox`) when it has one. */
		FString Container;
		/** Its parent's container, which places it; empty when its anchors do. */
		FString PlacedBy;
		/** How its anchors hold it, in words, when nothing places it: `fills its parent`, `centred`, `top left`... */
		FString Anchors;
		/** The visual's class (`Text`, `Image`, `RectBlock`...), empty for a plain widget. */
		FString Visual;
		FColor Color = FColor::White;

		bool bHasText = false;
		FString Text;
		float FontSize = 0.0f;
		bool bTextCut = false;
		bool bTextOverflows = false;
		/** What the text needs and the box it has, pixels. */
		FVector2f TextNeeded = FVector2f::ZeroVector;
		FVector2f TextBox = FVector2f::ZeroVector;
		/** Where text that does not fit spills: -1 left / 0 both sides / 1 right, and 1 down from the top / 0 both ways / -1 up. */
		int8 TextSpillX = 0;
		int8 TextSpillY = 0;

		bool IsDrawn() const { return bActive && bShown && Opacity > 0.0f; }
	};

	/** A screen laid out at one viewport size. */
	struct DREAMGUIEDITOR_API FLayoutSnapshot
	{
		FIntPoint Viewport = FIntPoint::ZeroValue;
		TArray<FLayoutNode> Nodes;
		/** The node the caller asked to look at (`root`), or 0. */
		int32 Focus = 0;

		int32 FindByPath(const FString& InPathOrName) const;
	};

	/** The screen on InStage, node by node. InAst (the file's) gives types and lines; may be null. */
	DREAMGUIEDITOR_API void CaptureLayout(const FScreenStage& InStage, const FDreamUIAst* InAst, const FString& InSourceFile, FLayoutSnapshot& OutSnapshot);

	/** One problem a layout rule found. */
	struct FLayoutIssue
	{
		int32 Code = 0;
		/** `warning` / `info`. */
		FString Severity = TEXT("warning");
		int32 Node = INDEX_NONE;
		int32 Other = INDEX_NONE;
		FString Message;
		FString Hint;
		/** What the issue is about regardless of the viewport, for telling sizes apart: `8001|Root/Title|right`. */
		FString Key;
		/** How much it matters within its severity, for picking the one the summary names. */
		double Weight = 0.0;
	};

	/** Rules 1 to 5 on one snapshot. */
	DREAMGUIEDITOR_API void LintLayout(const FLayoutSnapshot& InSnapshot, TArray<FLayoutIssue>& OutIssues);

	/** Rule 7 on a picture of the snapshot: each text against the pixels around it. */
	DREAMGUIEDITOR_API void LintContrast(const FLayoutSnapshot& InSnapshot, const TArray<FColor>& InPixels, FIntPoint InSize, TArray<FLayoutIssue>& OutIssues);

	/** An issue on the wire: its node's file and line, its path, its doc. */
	DREAMGUIEDITOR_API FDiagnostic MakeLayoutDiagnostic(const FLayoutSnapshot& InSnapshot, const FLayoutIssue& InIssue);

	/**
	 * The compact tree, one line per node from InSnapshot.Focus down to InMaxDepth levels below it:
	 * `  Text Heading  x=774 y=440 w=372 h=34  text="Hello, DreamGUI"`. OutHidden counts the nodes left out by depth.
	 */
	DREAMGUIEDITOR_API FString FormatLayoutTree(const FLayoutSnapshot& InSnapshot, int32 InMaxDepth, int32 InMaxLines, int32& OutShown, int32& OutHidden);

	/** The same tree as data: nested objects, the same depth. */
	DREAMGUIEDITOR_API TSharedRef<FJsonObject> LayoutTreeToJson(const FLayoutSnapshot& InSnapshot, int32 InMaxDepth);

	/** How the node's text reads in a line: `"Hello, DreamGUI"`, shortened past 40 characters. */
	DREAMGUIEDITOR_API FString QuoteText(const FString& InText);
}
