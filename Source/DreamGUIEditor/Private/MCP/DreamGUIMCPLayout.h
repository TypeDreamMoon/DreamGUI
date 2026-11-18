// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
//
// `layout` in words: a screen laid out on the stage, read back node by node -- where each one is in viewport pixels,
// whether it shows, what it says and whether that fits -- and the problems a person sees at a glance and a model that
// cannot see the picture does not: something off the screen, a text that is cut, a button too small to hit or under
// another, edges that miss each other by a pixel or two, a size of the viewport that breaks what another size kept,
// text that does not stand out from what is behind it, something drawn outside its panel or over a text, a panel whose
// content keeps to one side.
//
// The rules have codes of their own, DUI8001 to DUI8010 (EDreamUIDiagnosticCode, the 8xxx layout range), and their
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
		/**
		 * A box no longer than this on either side, in pixels, is the size of an icon: the plain visuals in it (no text,
		 * nothing to press) are the parts of a drawing, and DUI8005 does not compare them.
		 */
		inline constexpr double GlyphBoxSize = 128.0;
		/** Text against what is behind it: below the first ratio a warning, below the second an info for text under LargeText px (DUI8007). */
		inline constexpr double ContrastPoor = 3.0;
		inline constexpr double ContrastLow = 4.5;
		inline constexpr double LargeText = 24.0;
		/** A control's own part (not a node the file writes) below this ratio is hidden on purpose, not hard to read. */
		inline constexpr double HiddenByControl = 1.1;
		/** A shape of at most this many vertices is kept point by point, so a turned one is measured as drawn, not by its box. */
		inline constexpr int32 ShapePoints = 256;
		/** A node reaching more than this (pixels) past the panel or card that frames it is outside it (DUI8008)... */
		inline constexpr double OutsideFrameTolerance = 4.0;
		/** ...and by at least this share of its own size on that axis, so a hairline outline is not a finding. */
		inline constexpr double OutsideFrameShare = 0.05;
		/**
		 * A plate at least this share of its frame's width and height, lying over most of it, is another layer of the same
		 * panel (a face over its shadow, a glow behind it), not content in it.
		 */
		inline constexpr double PeerPlateShare = 0.9;
		/** A visual over a text covers it when it hides at least this share of the text's glyph box (DUI8009)... */
		inline constexpr double CoversTextShare = 0.12;
		/** ...over a part at least this thick (pixels; a sliver along an edge is two things touching)... */
		inline constexpr double CoversTextThickness = 4.0;
		/** ...is at least this opaque (its colour's alpha, 0-255)... */
		inline constexpr int32 CoversTextAlpha = 40;
		/** ...or, a decoration no bigger than the text's line, lies at least this share of itself inside that line... */
		inline constexpr double CrowdsTextShare = 0.3;
		/** ...and covers less than this share of the viewport (a full-screen scrim or vignette is not about one text). */
		inline constexpr double ScrimShare = 0.6;
		/**
		 * A text drawn across the edge of a shape under it (DUI8009 too): CoversTextShare to 1 - CoversTextShare of its
		 * glyph box on the shape, where the shape is no more than this many times that box's area -- a badge, an icon, the
		 * next item's picture, not a backdrop shape or a glow the text is laid over.
		 */
		inline constexpr double UnderShapeRatio = 8.0;
		/** DUI8010 looks at panels at least this big (pixels on each side, and share of the viewport)... */
		inline constexpr double UnbalancedMinSide = 240.0;
		inline constexpr double UnbalancedMinShare = 0.02;
		/** ...and reports content that leaves an empty band this wide (share of the panel) on one side or across the middle... */
		inline constexpr double UnbalancedEmptyShare = 0.38;
		/** ...or one this wide on a side while items reach less than UnbalancedSparse of that axis... */
		inline constexpr double UnbalancedSideShare = 0.2;
		inline constexpr double UnbalancedSparse = 0.6;
		/** ...(either way the opposite side keeping less than a third of that band: room on one side only, not a centred block)... */
		inline constexpr double UnbalancedOppositeRatio = 0.33;
		/** ...or gaps between its items that take this share of its height (width): rows spread thin. */
		inline constexpr double UnbalancedVerySparse = 0.6;
		/** A panel this many times longer than it is wide is a bar or a sidebar: its middle is not looked at along its length. */
		inline constexpr double UnbalancedElongated = 2.5;
		/** An item thinner than this (pixels) is a rule, a divider or an accent bar: it frames the content, it is not content. */
		inline constexpr double UnbalancedThinItem = 6.0;
	}

	/** How `layout` words the faces turned in 3D. Facts, not rules: nothing here is a problem. */
	namespace LayoutFacing
	{
		/** A face whose plane leans at least this far (degrees) from the plane it sits in is turned, and is listed. */
		inline constexpr double MinAngle = 2.0;
		/** ...and is at least this big on the screen, both ways (pixels). */
		inline constexpr double MinSide = 24.0;
		/** A face whose centre is within this share of the viewport's width from its middle is in the middle: it faces left or right, not toward or away from the centre. */
		inline constexpr double CentreBand = 0.08;
		/** Two edges drawn within this share of each other's length are the same length: which is farther is read from depth instead. */
		inline constexpr double SameLength = 0.01;
		/** At most this many faces are listed; the rest are counted. */
		inline constexpr int32 MaxFaces = 12;
	}

	/** Where a node's outline is drawn, by the side of the screen each edge faces: what tells which edge is farther. */
	struct FLayoutFacing
	{
		enum ESide : int32 { Left = 0, Right = 1, Top = 2, Bottom = 3 };
		bool bValid = false;
		/** Each edge's drawn length, in viewport pixels: the farther of two opposite edges is drawn shorter. */
		double Length[4] = { 0.0, 0.0, 0.0, 0.0 };
		/** How far each edge's middle stands behind the screen's plane (pixels, positive away from the viewer). */
		double Depth[4] = { 0.0, 0.0, 0.0, 0.0 };
		/** The outline's centre as drawn, viewport pixels. */
		FVector2D Centre = FVector2D::ZeroVector;
	};

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

		/** Tagged Bleed (`@tag Bleed`): a decoration that draws past its frame or over text on purpose. */
		bool bBleed = false;
		/**
		 * What it draws, in viewport pixels, cut to its clipping ancestors: its shape (the glyphs of a text, the outline of
		 * a polygon, a rect block's body), and that with a shadow or glow that shows. Its rect when there is no geometry.
		 */
		FBox2D Body = FBox2D(ForceInit);
		FBox2D Drawn = FBox2D(ForceInit);
		/** Body came from the visual's geometry rather than from the rect. */
		bool bBodyMeasured = false;
		/** A rect block that draws no body (an outline) or one too faint to hide what is under it. */
		bool bHollow = false;
		/**
		 * The same in the widget's own plane, local units with Y up, and where that plane is: the rules compare two nodes
		 * in the plane of one of them, so a card turned in 3D keeps its edges straight. Invalid when not captured.
		 */
		FTransform World = FTransform::Identity;
		double PxPerUnit = 1.0;
		FBox2D LocalRect = FBox2D(ForceInit);
		FBox2D LocalBody = FBox2D(ForceInit);
		FBox2D LocalDrawn = FBox2D(ForceInit);
		/** The line a text takes: what it needs, placed by its alignment. */
		FBox2D LocalLine = FBox2D(ForceInit);
		/** The points of the shape it draws (a polygon's corners), local, when there are few enough to keep: a turned shape's box is bigger than the shape. */
		TArray<FVector2D> LocalShape;

		/** Its plane's normal in the screen's frame (X away from the viewer, Y right, Z up): (1, 0, 0) for a node flat on the screen. */
		FVector Normal = FVector::ForwardVector;
		/** Its outline as drawn, edge by edge, when it is turned in 3D. */
		FLayoutFacing Facing;

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

	/** Rules 1 to 5 and 8 to 10 on one snapshot. */
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

	/**
	 * A face turned in 3D -- a panel, a card, a plane whose own plane leans away from the one it sits in -- and which way
	 * it faces as drawn: how far it is turned and tilted, which edge is farther, whether it faces the screen's centre.
	 */
	struct FLayoutFace
	{
		int32 Node = INDEX_NONE;
		/** How it is named: its type and id, as the file writes them. */
		FString Name;
		/** Where its centre is drawn: `left`, `right` or `middle` (within LayoutFacing::CentreBand of the viewport's middle). */
		FString Side;
		/** Turned about the vertical axis, degrees from the screen's plane; 0 when it is not. */
		int32 Turn = 0;
		/** The edge drawn farther for the turn (`left`, `right`), which is the way it faces. */
		FString TurnFarther;
		/** 1 faces the screen's centre, -1 faces away from it, 0 in the middle or not turned. */
		int32 TowardCentre = 0;
		/** Tilted about the horizontal axis, degrees; 0 when it is not. */
		int32 Tilt = 0;
		/** The edge drawn farther for the tilt (`top`: tilted back, `bottom`: tilted forward). */
		FString TiltFarther;
		/** The farther edge is drawn narrower than the nearer one (foreshortened). */
		bool bNarrower = false;
		/** Turned past 90 degrees: its back is toward the viewer, drawn mirrored. */
		bool bBack = false;
		/** In words, after the name: `turned 14 degrees: faces toward the screen centre (right edge farther)`. */
		FString Words;
		/** The same, a few words, for a summary: `faces toward the centre`, `tilted back`. */
		FString Short;
	};

	/** Every face of the snapshot turned in 3D, outermost first: a node coplanar with a listed face is part of it and is not listed. */
	DREAMGUIEDITOR_API void DescribeFaces(const FLayoutSnapshot& InSnapshot, TArray<FLayoutFace>& OutFaces);

	/** One line a face (at most LayoutFacing::MaxFaces), `  Widget RosterLeft, on the left: turned 14 degrees: ...`; empty without faces. */
	DREAMGUIEDITOR_API FString FormatFaces(const FLayoutSnapshot& InSnapshot, const TArray<FLayoutFace>& InFaces);

	/** A sentence for a summary, ` In 3D: RosterLeft faces toward the centre, Board tilted back.`; empty without faces. */
	DREAMGUIEDITOR_API FString SummarizeFaces(const TArray<FLayoutFace>& InFaces);

	/** The faces as data. */
	DREAMGUIEDITOR_API TArray<TSharedPtr<FJsonValue>> FacesToJson(const FLayoutSnapshot& InSnapshot, const TArray<FLayoutFace>& InFaces);

	/** How the node's text reads in a line: `"Hello, DreamGUI"`, shortened past 40 characters. */
	DREAMGUIEDITOR_API FString QuoteText(const FString& InText);
}
