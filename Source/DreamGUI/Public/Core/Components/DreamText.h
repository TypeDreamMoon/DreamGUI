// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "DreamVisualBatchMesh.h"
#include "Core/IDreamUICultureChangedInterface.h"
#include "Core/DreamUITextData.h"
//for FDreamTextCoverageReport, which a text keeps for the painter to write
#include "Core/Text/DreamTextPainter.h"
#include "UObject/ObjectKey.h"
#include "DreamText.generated.h"


class UDreamUIFontData_BaseObject;
class UDreamUIRichTextImageData_BaseObject;
class UDreamUIRichTextCustomStyleData;
class UDreamUIManagerWorldSubsystem;
class UDreamWidget;
class UDreamGradientAsset;
struct FDreamTextLayoutInput;
struct FDreamTextPaintParams;

/**
 * The vertex channels of a text's quads (FDreamUIMeshVertex; DreamTextQuadCode in DreamTextPainter.h says what each kind
 * of quad writes):
 *		UV0: its glyph's texels in the font's atlas
 *		UV1: X its widget record in its canvas's widget property data, Y the atlas slice
 *		UV2: X its quad code -- the kind of glyph quad, its field layer and dilation or its coverage phase -- plus 128 times
 *		     its paint slot (DreamTextQuadCode::SlotStride), Y where it sits along its lyric fill run
 *		UV3: X the lyric fill progress, Y the glow boost (a coverage glyph: its contrast; a colour glyph: texels per em)
 *		UV4: where it sits in the boxes its paint slot is measured across, (0, 0) for a quad that is not painted
 */
/** A `<a=Id>` range was clicked; Id is what the markup named it. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamTextHyperlinkEvent, FName, Id);
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamTextHyperlinkCppEvent, FName);

/** Whether a text may draw its small sizes from hinted coverage glyphs rather than from its font's distance field. */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamTextSmallTextRaster : uint8
{
	/** Coverage glyphs wherever the project, the font and the text's situation allow (UDreamGUISettings::bSmallTextCoverage). */
	Auto,
	/** Always the distance field. */
	Off,
};

/** What decided how a text drew its small sizes at its last paint; see UDreamText::GetSmallTextState. */
enum class EDreamTextSmallTextGate : uint8
{
	/** Not painted since it was made or moved to another canvas. */
	NotPainted,
	/** Its small sizes came from coverage glyphs. */
	Coverage,
	/** SmallTextRaster is Off. */
	Off,
	/** The font draws no coverage glyphs: not an outline (multi-channel) distance field, or coverage is off for it or the project. */
	Font,
	/**
	 * An override material that does not shade through MF_DreamUI_Shade draws it (it lacks the shading's marker,
	 * DreamUIShadeMaterial::ShadeMarkerParameter), and only DreamGUI's shading knows coverage glyphs.
	 */
	OverrideMaterial,
	/**
	 * A material that does not shade through MF_DreamUI_Shade draws it -- its font's, or, with the built-in UI shader off,
	 * the canvas's default material -- and only DreamGUI's shading (DreamUIShade.ush) knows coverage glyphs.
	 */
	Material,
	/** Its text style has effects (an outline, a glow, an underlay) and UDreamGUISettings::SmallTextEffectFace is Field. */
	Style,
	/** An enabled mesh modifier moves or re-maps its vertices. */
	Modifier,
	/** Pixel snapping resolves to Disabled on it or above it. */
	Snapping,
	/** It has no render canvas. */
	NoCanvas,
	/** Its root canvas renders in world space. In an editor world a screen-space canvas does too: it is drawn in the level. */
	WorldSpace,
	/**
	 * Its screen-space canvas renders at a fraction of the screen's resolution (UDreamCanvas::ScreenSpaceRenderScale): into
	 * a smaller target scaled up afterwards, or not, as the renderer decides on its own thread -- no pixel grid to place on.
	 */
	RenderScale,
	/**
	 * It is in a render layer, which places it on the GPU. No longer given: a text in a layer is placed on the device grid
	 * as any other once the layer has held still, and is RenderLayerMoving until then.
	 */
	RenderLayer,
	/**
	 * It is not flat, or rolled, mirrored or unevenly scaled relative to its root canvas, or sheared, or under a perspective;
	 * or its canvas draws its units unevenly on the target (wider than tall, or the other way), by more than an eighth of a
	 * pixel across the widget.
	 */
	Transform,
	/** None of its glyphs is small enough at its device scale (UDreamUIFontData_BaseObject::GetCoverageMaxPixelSize). */
	Large,
	/**
	 * Its device scale has not settled yet -- or, with DreamGUI.Text.SmallTextOnMove 2, it is moving off its device grid:
	 * drawn from the field meanwhile, and in its world's sharpen set.
	 */
	Settling,
	/**
	 * Its text style softens or dilates the face (FaceSoftness, FaceDilate), which reshapes the face a coverage glyph cannot:
	 * effects alone no longer rule coverage out (UDreamGUISettings::SmallTextEffectFace), and Style keeps meaning that the
	 * setting is Field.
	 */
	StyleFace,
	/** Its render layer moved within the last few frames: drawn from the field until the layer holds still, then repainted. */
	RenderLayerMoving,
};

/**
 * What the small-text gate decided at a text's last paint, and what its debounce and raster-scale hysteresis keep from one
 * paint to the next. Device pixels are those of what the text's root canvas renders into -- its render target, or the
 * screen -- measured through the matrices the canvas is drawn with: S of them to a unit of the text's local space.
 */
struct FDreamTextSmallTextState
{
	/** Why the last paint drew what it drew: Coverage when its small sizes came from coverage glyphs. */
	EDreamTextSmallTextGate Gate = EDreamTextSmallTextGate::NotPainted;
	/** S, at the last paint that could place the text on the device pixel grid. */
	float DeviceScale = 0.0f;
	/** The scale coverage glyphs are rasterized at, kept while S stays within 1% of it. */
	float RasterScale = 0.0f;
	/** Where the device pixel grid lies in the text's local space (FDreamTextCoverageParams::SnapOrigin), at that paint. */
	FVector2f SnapOrigin = FVector2f::ZeroVector;
	/** The root canvas renders to a render target, which blends in linear space. */
	bool bLinearTarget = false;
	/** The smallest glyph that paint could draw from coverage, in the text's units; MAX_flt when it had none. */
	float MinGlyphSize = 0.0f;
	/**
	 * S as a paint last saw it, and how many sweeps in a row have found the text holding still since: its scale unchanged,
	 * and, while its world's sweep watches it, its transform not changed at all (a move only starts the count again).
	 */
	float SettlingScale = 0.0f;
	int32 SettledSweeps = 0;
	/** A paint has seen S. The first one counts as settled: a text that appears draws crisp at once. */
	bool bScaleSeen = false;
	/**
	 * In its world's sharpen set: on the field only because S has not settled, or watched since a move it did not measure
	 * (a rolled, too large, settling or moving text, or one in a render layer), and looked at by the sweep once it has held
	 * still -- repainted from coverage when it can draw from it there.
	 */
	bool bWaitingToSharpen = false;
	/**
	 * It moved off the device grid of its coverage quads with DreamGUI.Text.SmallTextOnMove 1 or 2 and has not held still
	 * since: those quads are kept as they are (1), or it is on the field (2), until the sweep finds it still.
	 */
	bool bMoving = false;
};

/** One layer of a painted text's slot, as its paint table holds it: the gradient it paints with, and the row holding that. */
struct FDreamTextPaintLayerState
{
	/** What the layer paints with, as last taken up; the painter is handed this copy (FDreamTextPaintSlot). */
	FDreamGradient Gradient;
	/** The row of the world's paint rows holding Gradient; INDEX_NONE while the layer holds none. */
	int32 Row = INDEX_NONE;
	/** The layer paints. */
	bool bPainting = false;
};

/**
 * What a painted text holds of its world's paint rows (DreamPaintRows has their layout): made when the text first paints
 * anything -- its own paints, or a rich-text tag paint whose name resolved -- and let go of when it paints nothing any
 * more, leaves its tree or its world, or goes. Game thread.
 */
struct FDreamTextPaintState
{
	FDreamTextPaintState()
	{
		for (FVector4f& Pixel : TablePixels)
		{
			Pixel = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		}
	}

	/** The manager the rows were taken from, and are given back to. */
	TWeakObjectPtr<UDreamUIManagerWorldSubsystem> Manager;
	/** The text's table, which its widget record links to (row + 1); INDEX_NONE while it has none. */
	int32 TextRow = INDEX_NONE;
	/** Slot 1 (DreamTextQuadCode::TextSlot), the style's own paints: [0] the face, [1] the outline, [2] the overlay. */
	FDreamTextPaintLayerState Own[3];
	/** Slots FirstTagSlot to MaxSlot, the rich-text tag paints, in order: their faces. */
	FDreamTextPaintLayerState Tags[DreamTextQuadCode::MaxSlot - DreamTextQuadCode::FirstTagSlot + 1];
	/** The display list's PaintNames as last resolved, at which of its generations, and each name's slot (FDreamTextPaints::NameSlots). */
	TArray<FName> ResolvedNames;
	uint64 ResolvedGeneration = 0;
	TArray<uint8> NameSlots;
	/**
	 * Something a name resolved through changed (a custom style, a gradient asset, the project's gradient presets): resolved
	 * again by the next update.
	 */
	bool bTagPaintsStale = true;
	/** The text's material does not shade through MF_DreamUI_Shade: painted in its vertex colours, holding no rows at all. */
	bool bVertexColorFallback = false;
	/** Slot 1's box aspect (FDreamTextPaintSlot::BoxAspect), from the boxes of the layout last painted. */
	float OwnBoxAspect = 1.0f;
	/** The table as last written, so that a write sends only what changed; nothing is assumed of a table not written yet. */
	FVector4f TablePixels[DreamPaintRows::RowWidth];
	bool bTableWritten = false;
	/** The gradient assets the paints come from (a paint's Preset, a custom style entry's), each with its change binding. */
	TArray<TPair<TWeakObjectPtr<UDreamGradientAsset>, FDelegateHandle>> PresetBindings;
};

UCLASS(ClassGroup = (DreamGUI), Blueprintable)
class DREAMGUI_API UDreamText : public UDreamVisualBatchMesh, public IDreamUICultureChangedInterface
{
	GENERATED_BODY()

public:	
	UDreamText(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay()override;
	virtual void OnRegister()override;
	virtual void OnUnregister()override;
	virtual void BeginDestroy() override;
	virtual void OnRenderCanvasChanged(UDreamCanvas* InOldCanvas, UDreamCanvas* InNewCanvas)override;
public:
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	/**
	 * The details panel edits a field inside TextStyle -- a paint's gradient, one of its stops -- through a chain whose active
	 * node is that field, and only the chain names the member it is in: TextStyle's state before the edit is kept from here.
	 */
	virtual void PreEditChange(class FEditPropertyChain& PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
	/** An undo that put another font in Font lets go of the one the text was added to, and of the coverage it held with it. */
	virtual void PostEditUndo() override;
protected:
	/** No, for an edit of the paints alone or of their animation: PostEditChangeProperty gave it what a setter would. */
	virtual bool MarksAllDirtyOnPropertyEdit(const FPropertyChangedEvent& InEvent) const override;
#endif
	void RegisterOnRichTextImageDataChange();
	void UnregisterOnRichTextImageDataChange();
	FDelegateHandle RichTextImageDataChangedDelegateHandle;
	void RegisterOnRichTextCustomStyleDataChange();
	void UnregisterOnRichTextCustomStyleDataChange();
	FDelegateHandle RichTextCustomStyleDataChangedDelegateHandle;
public:
	static FName GetPropertyName_Text()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamText, Text);
	}
	static FName GetPropertyName_Font()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamText, Font);
	}
	static FName GetPropertyName_OverrideMaterial()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamText, OverrideMaterial);
	}

protected:
	friend class FDreamTextCustomization;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (DisplayThumbnail = "false"))
		TObjectPtr<UDreamUIFontData_BaseObject> Font;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (MultiLine="true"))
		FText Text = FText::FromString(TEXT("New Text"));
	/**
	 * The font has the last word: a rasterizing font (bitmap, and the FreeType base) caps the size it
	 * will render at GetFontSizeLimit(), 200 by default, and lays out at the cap. A distance-field font
	 * has no cap, which is why this metadata's ceiling is the higher of the two.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = "2", ClampMax = "500"))
		float FontSize = 16;
	/** use font kerning for better text layout. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bUseKerning = true;
	/**
	 * Let the font join letters into its standard and contextual ligatures ("fi", "ffl"), as browsers do by default
	 * (Slate's default shaping of left-to-right text has none). The layout leaves them out on its own while FontSpace.X
	 * is not zero, which is the CSS rule, and so does a text whose characters something animates one by one (see
	 * RegisterPerCharacterAnimation): a ligature draws several characters as one glyph. Off, it also turns off the
	 * font's contextual alternates in Latin, Greek and Cyrillic; scripts that need theirs to join keep them.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter = "GetLigatures", Setter = "SetLigatures", meta = (AllowPrivateAccess = true))
	bool bLigatures = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FVector2D FontSpace = FVector2D(0, 0);
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUITextParagraphHorizontalAlign HAlign = EDreamUITextParagraphHorizontalAlign::Center;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUITextParagraphVerticalAlign VAlign = EDreamUITextParagraphVerticalAlign::Middle;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUITextOverflowType OverflowType = EDreamUITextOverflowType::VerticalOverflow;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	ETextWrappingPolicy WrappingPolicy = ETextWrappingPolicy::AllowPerCharacterWrapping;
	/**
	 * Keep CJK words together when wrapping, using ICU's dictionary -- CSS's `word-break: auto-phrase`.
	 * Only matters with VerticalOverflow. Needs the packaged ICU data to include the CJK dictionary
	 * (Project Settings > Packaging > Internationalization Support: CJK, EFIGSCJK or All); without it
	 * this quietly falls back to per-character breaks.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EDreamTextPhraseWrap PhraseWrap = EDreamTextPhraseWrap::Off;
	/**
	 * Outline, underlay, glow and fill look, drawn by the built-in shader. Needs a distance-field font
	 * (OutlineMultiChannel source) and "Use Built-in UI Shader" on; has no effect with a custom material.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	FDreamTextStyle TextStyle;
	/** Fill progress of the whole text (per line), 0..1, for lyric-style reveals. Segments override it. */
	UPROPERTY(Transient)
	float FillProgress = 1.0f;
	/** Glow boost of the whole text; segments override it. */
	UPROPERTY(Transient)
	float GlowBoost = 0.0f;
	/** Character runs with their own fill progress; see FDreamTextFillSegment. */
	UPROPERTY(Transient)
	TArray<FDreamTextFillSegment> FillSegments;
	/**
	 * Where the face paint's gradient stands along its line, in fractions of its length (FDreamPaintAnimation::Phase): 1
	 * moves it one whole gradient on. Animated -- keyed in Sequencer, tweened by UDreamTextPaintLibrary::PaintPhaseTo -- it
	 * writes one pixel of the text's paint table: no repaint, no layout. Applies to the text's tag paints too.
	 */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFacePaintPhase, Category = "DreamGUI|Paint", meta = (UIMin = "-2", UIMax = "2"))
	float FacePaintPhase = 0.0f;
	/** The same for the outline paint. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOutlinePaintPhase, Category = "DreamGUI|Paint", meta = (UIMin = "-2", UIMax = "2"))
	float OutlinePaintPhase = 0.0f;
	/** The same for the overlay paint: what moves a shimmer band across (UDreamTextPaintLibrary::PlayShimmer runs it from -1 to 1). */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOverlayPaintPhase, Category = "DreamGUI|Paint", meta = (UIMin = "-2", UIMax = "2"))
	float OverlayPaintPhase = 0.0f;
	/** Degrees added to every paint's angle (a Linear's direction, a Conic's start, a Diamond's turn). One pixel of the paint table a change, as the phases. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPaintAngleOffset, Category = "DreamGUI|Paint", meta = (UIMin = "-360", UIMax = "360"))
	float PaintAngleOffset = 0.0f;
	/**
	 * Padding between the widget's rect and the text laid out inside it, the way UMG's text Margin
	 * works. The text is wrapped, aligned and overflow-tested against the rect MINUS this, so a
	 * margin narrows the wrap width rather than just shifting the result.
	 *
	 * It also grows what this text reports as its preferred size, so a parent that sizes itself to
	 * its content leaves room for the padding instead of squeezing it back out.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	FMargin Margin = FMargin(0.0f);
	/**
	 * Scales the distance from one line to the next, 1 being the font's own line height -- UMG's
	 * LineHeightPercentage. Only the gap between lines moves; the glyphs keep their size, so this
	 * tightens or opens up a paragraph without changing how big the letters are.
	 *
	 * FontSpace.Y is added on top and is a flat distance, which is the difference between the two:
	 * one scales with the font, the other does not.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true, ClampMin = "0.0", UIMin = "0.5", UIMax = "3.0"))
	float LineHeightPercentage = 1.0f;
	/**
	 * Wrap the text at this width instead of at the content box's, UMG's WrapTextAt. Zero or less
	 * keeps the content box, which is the usual case.
	 *
	 * Worth having separately because the box is also what the text is ALIGNED in: a narrower wrap
	 * width gives a narrow column of text that still centres over the full widget, which the box
	 * alone cannot express. It does not truncate -- Truncate and Ellipsis still measure against the
	 * box, since what they are about is what fits on screen.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true, ClampMin = "0.0"))
	float WrapTextAt = 0.0f;
	/**
	 * Floor under the width this text reports to a content-sized parent, UMG's MinDesiredWidth. Keeps a
	 * label that is momentarily short (a number counting down, a name that has not arrived) from
	 * collapsing the row around it. Zero means no floor. It is a layout request only: nothing here
	 * clips or stretches the glyphs, and the text still draws inside whatever box the parent gives it.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true, ClampMin = "0.0"))
	float MinDesiredWidth = 0.0f;
	/**
	 * Wrap the text even when the overflow policy is not VerticalOverflow -- UMG's AutoWrapText, which
	 * it keeps separate from its overflow policy for exactly this reason. With Ellipsis it is what
	 * turns a single elided line into a wrapped paragraph whose LAST visible line is elided; with
	 * Truncate, a paragraph cut off at the bottom of the box. VerticalOverflow always wraps, so this
	 * changes nothing there.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter = "GetAutoWrapText", Setter = "SetAutoWrapText", meta = (AllowPrivateAccess = true))
	bool bAutoWrapText = false;
	/** Case the text is drawn in. The Text property keeps what was authored; this is presentation only. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EDreamUITextTransformPolicy TextTransform = EDreamUITextTransformPolicy::None;
	/** Which way the paragraph reads. Auto asks the bidi algorithm, which is what it always did. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EDreamTextFlowDirection FlowDirection = EDreamTextFlowDirection::Auto;
	/**
	 * The language the text is written in, as a culture name ("ja", "zh-Hans", "en-US"): the font's fallbacks meant for it
	 * are preferred (FDreamUIFontFallback::Cultures), and HarfBuzz picks the forms a script draws differently per language
	 * (locl). Empty, the default, is the game's current language. A rich text's <lang=xx>...</lang> overrides it for what
	 * the tag encloses. Line breaking follows the game's culture whatever this says.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	FString Language;
	/**
	 * Tab stops, in spaces of the font at the text's size (plus letter spacing), from the line's start edge: CSS tab-size,
	 * 8 as browsers have it. A tab narrower than half a space jumps to the next stop; 0 makes a tab take no room.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true, ClampMin = "0.0", UIMax = "16.0"))
	float TabSize = 8.0f;
	/** Where a justified line (HAlign Justify) gets its extra room: after word separators, between CJK characters, or everywhere. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EDreamTextJustify TextJustify = EDreamTextJustify::Auto;
	/** How a justified paragraph's last line, and a line a newline ends, align (HAlign Justify only). */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EDreamTextLastLineAlign LastLineAlign = EDreamTextLastLineAlign::Auto;
	/**
	 * Off keeps this text on its font's distance field at every size. Auto lets small sizes draw from hinted coverage
	 * glyphs where that can be exact (UDreamGUISettings::bSmallTextCoverage says where): crisper at 10-20 px, the same
	 * layout, carets and selection.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", AdvancedDisplay, Getter, Setter, meta = (AllowPrivateAccess = true))
	EDreamTextSmallTextRaster SmallTextRaster = EDreamTextSmallTextRaster::Auto;
	/**
	 * Underline the whole text, the way UMG puts it on a text's style rather than only in markup.
	 * Rich text's `<u>` still works and nests on top of it: a style is where the run starts, a tag is
	 * what it does from there.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter = "GetUnderline", Setter = "SetUnderline", meta = (AllowPrivateAccess = true))
	bool bUnderline = false;
	/** Strike the whole text through; `<s>` nests on top of it, as `<u>` does over bUnderline. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter = "GetStrikethrough", Setter = "SetStrikethrough", meta = (AllowPrivateAccess = true))
	bool bStrikethrough = false;
	/**
	 * Shrink the font until the text fits the content box, uGUI's Best Fit. FontSize becomes the
	 * size the text is allowed to reach rather than the size it is drawn at, and BestFitMinSize is
	 * how small it may go before the text is simply allowed to overflow.
	 *
	 * Neither UMG nor Slate has an equivalent: their text is content-sized, so the box grows to the
	 * text instead. Here the box is authored, which is exactly the case that needs this.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter = "GetBestFit", Setter = "SetBestFit", meta = (AllowPrivateAccess = true))
	bool bBestFit = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true, EditCondition = "bBestFit", ClampMin = "1.0"))
	float BestFitMinSize = 8.0f;
	/** What Best Fit settled on last layout. Not authored, so not a UPROPERTY the user can edit. */
	mutable float RenderedFontSize = 0.0f;
	/** Use a custom material to render this text */
    UPROPERTY(EditAnywhere, Category = "DreamUI")
    TObjectPtr<UMaterialInterface> OverrideMaterial = nullptr;
	/**
	 * Expand character's rect area to generate bigger mesh, useful for effects of OverrideMaterial.
	 * Only valid for SDF font. In pixels at the font's SampleFontSize, and at most the font's SDFRadius
	 * less 0.02 em: that is all the field a glyph's atlas cell holds, so a larger value draws the same.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamUI", meta = (ClampMin = "0.0"))
	float ExpandMeshSize = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	EDreamUITextFontStyle FontStyle = EDreamUITextFontStyle::None;
	/**
	 * rich text support, eg:
	 * <b>Bold</b>
	 * <i>Italic</i>
	 * <u>Underline</u>
	 * <s>Strikethrough</s>
	 * <size=48>Point size 48</size>
	 * <size=+18>Point size increased by 18</size>
	 * <size=-18>Point size decreased by 18</size>
	 * <color=yellow>Yellow text</color> names: black white gray/grey silver red green/lime blue orange
	 *     purple yellow cyan/aqua magenta/fuchsia maroon navy olive teal pink brown gold transparent
	 * <color=#0f0>, <color=#0f08>, <color=#00ff00>, <color=#00ff0080> hex, short or long, with or without alpha
	 * <color=rgb(0,255,0)>, <color=rgba(0,255,0,0.5)> the CSS functional forms; the alpha is 0..1 when
	 *     it is written with a decimal point, 0..255 otherwise
	 * <sup>Superscript</sup>
	 * <sub>Subscript</sub>
	 * <MyTag>Custom tag</MyTag> use any string as custom tag. custom tag can use for char selection (check TextAnimation usage), and for custom style (check RichTextCustomStyleData)
	 * <a=Buy>Clickable</a> a custom tag that can also be clicked: put a UUITextHyperlink on the widget
	 *     and it broadcasts UDreamText::OnHyperlinkClicked with the id
	 * <img=smile/> display a image with key "smile" which defined in RichTextImageData property, can be used for emoji
	 * <img=smile,24/> the same image 24 tall, as wide as its aspect ratio makes it; <img=smile,24,24/> sets both
	 * <img=smile,24,baseline/> where the image sits on its line: middle (the default, centred), baseline, top or bottom; an image taller than its line grows the line
	 * &lt; &gt; &amp; &quot; &apos; &nbsp; and &#1234; / &#x1F600; write a character the markup would
	 *     otherwise eat -- the only way to show a literal '<'. Plain text does NOT unescape, as in UMG.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bRichText = false;
	/** Flags to enable/disable rich text tag. */
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (Bitmask, BitmaskEnum = "/Script/DreamGUI.EDreamUIText_RichTextTagFilterFlags", EditCondition = "bRichText"))
		int32 RichTextTagFilterFlags = 0xffffffff;
	/** rich text custom style data for custom tag and rendering custom style */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (EditCondition = "bRichText"))
		TObjectPtr<UDreamUIRichTextCustomStyleData> RichTextCustomStyleData = nullptr;
	/** rich text image data for rendering image inside UIText */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (EditCondition = "bRichText"))
		TObjectPtr<UDreamUIRichTextImageData_BaseObject> RichTextImageData = nullptr;
	/**
	 * The amount of pixels per unit to use for dynamically created bitmap texture, such as BitmapFont. 
	 * But!!! Do not set this value too large if you already have large font size of DreamText, because that will result in extremely large texture! 
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", AdvancedDisplay)
	float DynamicPixelsPerUnit = 1.0f;
	/** created object for rich text image */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", Transient, AdvancedDisplay)
	TArray<TObjectPtr<UDreamWidget>> CreatedRichTextImageObjectArray;
	/** created object for emoji */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", Transient, AdvancedDisplay)
	TArray<TObjectPtr<UDreamWidget>> CreatedEmojiObjectArray;
private:
	bool bHasAddToFont = false;
	/**
	 * The font RegisterFont added the text to, while bHasAddToFont: what UnregisterFont takes it off. An undo puts another
	 * font in Font with nothing told first, so Font is not always that font.
	 */
	TWeakObjectPtr<UDreamUIFontData_BaseObject> FontAddedTo;

	mutable FDreamUITextGeometryCache CacheTextGeometryData;
	/**
	 * Where GetPreferredSizeWithin lays the text out at a width it is about to be given, apart from the layout it is drawn
	 * from: made the first time a panel offers it a width other than its own, and marked dirty with the other.
	 */
	mutable TUniquePtr<FDreamUITextGeometryCache> MeasureAtWidthCache;
	void UpdateCacheTextGeometry()const;
	void ConditionalUpdateCacheTextGeometry()const;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const TArray<FDreamUITextCharProperty>& GetCharPropertyArray()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		int32 GetVisibleCharCount()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const TArray<FDreamUIText_RichTextCustomTag>& GetRichTextCustomTagArray()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const TArray<FDreamUIText_RichTextImageTag>& GetRichTextImageTagArray()const;
public:
	virtual void MarkAllDirty()override;

	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry()override;

	virtual void OnBeforeCreateOrUpdateGeometry()override;
	virtual bool GetShouldAffectByPixelSnapping()const override;
	/**
	 * Yes while it draws small-text coverage glyphs, unless the move kept it on the device pixel grid of its last paint (a
	 * whole-pixel translation at the same device scale) -- or DreamGUI.Text.SmallTextOnMove 1 keeps its quads while it
	 * moves. A text on the field only because of where it is or how it moves (rolled, too large, in a moving render layer,
	 * settling or moving) is never measured for a move: the move is noted, and its world's sharpen sweep places it once it
	 * has held still, repainting it then if it can draw from coverage there.
	 */
	virtual bool GetRepaintsOnTransformChange()const override;
	/** Pixel snapping is read by the layout of a pixel-perfect font and by the small-text gate: a change repaints. */
	virtual void OnPixelSnappingChanged()override;
	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
	virtual uint8 GetFontMark_WidgetPropertyDataForMaterial() override;
	virtual void FillWidgetPropertyDataForMaterial_Extra(class UDreamUIDataAsTexture* DataAsTexture) override;
	/**
	 * The font mark (an EDreamUIFontTextureMark) this text's record in its canvas's widget property data was last
	 * written with, which is what the shader decodes the atlas by: one channel for a bitmap or a single-channel field,
	 * the median of three for MTSDF. 0 until the record is first written.
	 */
	uint8 GetWidgetPropertyFontMark()const { return WrittenFontMark; }
	/**
	 * What the small-text gate decided at this text's last paint -- whether its small sizes came from coverage glyphs and
	 * at what device scale, or what kept them on the field -- and what its debounce keeps between paints.
	 */
	const FDreamTextSmallTextState& GetSmallTextState()const { return SmallTextState; }
	/** What the last paint the gate let draw from coverage found: how many items did, and how many waited for their glyph. */
	const FDreamTextCoverageReport& GetSmallTextReport()const { return SmallTextReport; }
	virtual void OnCultureChanged_Implementation()override;

public:
	void ApplyFontTextureChange();
	void ApplyFontMaterialChange();
	void ApplyRecreateText();
	void ApplyFontEmojiChange();

	virtual void MarkTextureDirty()override;

	FORCEINLINE static bool IsVisibleChar(uint32 Codepoint)
	{
		if (Codepoint < 0x20) return false;    // C0
		if (Codepoint == 0x7F) return false;   // DEL

		// zero width
		if (Codepoint == 0x200B || Codepoint == 0x200C || Codepoint == 0x200D)
			return false;

		// 
		if (Codepoint == '\n' || Codepoint == '\r' || Codepoint == '\t' || Codepoint == ' ')
			return false;

		return true;
	}
	/** count visible char count of the string */
	static int VisibleCharCountInString(const FString& srcStr);

	void GenerateRichTextImageObject();
	void GenerateEmojiObject();

	virtual float GetPreferredWidth() const override;
	virtual float GetPreferredHeight() const override;
	/**
	 * A text that wraps at its box (VerticalOverflow, or AutoWrapText) with no WrapTextAt of its own, offered a bounded width:
	 * its height at that width, laid out apart from the layout it is drawn from, and the narrower of its one-line width and
	 * the offer. A panel measuring a paragraph in a Fill slot gets the paragraph's height at the slot's width, not at the
	 * width the widget happened to have -- one character per line before the first arrangement. Anything else: as before.
	 */
	virtual FVector2f GetPreferredSizeWithin(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const override;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UDreamUIFontData_BaseObject* GetFont()const { return Font; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")	const FText& GetText()const { return Text; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetFontSize()const { return FontSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetUseKerning()const { return bUseKerning; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetLigatures()const { return bLigatures; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") FVector2D GetFontSpace()const { return FontSpace; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamUITextOverflowType GetOverflowType()const { return OverflowType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") const FMargin& GetMargin()const { return Margin; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetLineHeightPercentage()const { return LineHeightPercentage; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetWrapTextAt()const { return WrapTextAt; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetMinDesiredWidth()const { return MinDesiredWidth; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetAutoWrapText()const { return bAutoWrapText; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamUITextTransformPolicy GetTextTransform()const { return TextTransform; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamTextFlowDirection GetFlowDirection()const { return FlowDirection; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") const FString& GetLanguage()const { return Language; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetTabSize()const { return TabSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamTextJustify GetTextJustify()const { return TextJustify; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamTextLastLineAlign GetLastLineAlign()const { return LastLineAlign; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamTextSmallTextRaster GetSmallTextRaster()const { return SmallTextRaster; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetUnderline()const { return bUnderline; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetStrikethrough()const { return bStrikethrough; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetBestFit()const { return bBestFit; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetBestFitMinSize()const { return BestFitMinSize; }
	/** The size Best Fit actually drew at, which is GetFontSize() when Best Fit is off. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetRenderedFontSize()const { return RenderedFontSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") ETextWrappingPolicy GetWrappingPolicy()const{return WrappingPolicy;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamTextPhraseWrap GetPhraseWrap()const{return PhraseWrap;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") const FDreamTextStyle& GetTextStyle()const{return TextStyle;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetFillProgress()const{return FillProgress;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetGlowBoost()const{return GlowBoost;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") const TArray<FDreamTextFillSegment>& GetFillSegments()const{return FillSegments;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamUITextFontStyle GetFontStyle()const { return FontStyle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetRichText()const { return bRichText; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") int32 GetRichTextTagFilterFlags()const { return RichTextTagFilterFlags; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UDreamUIRichTextCustomStyleData* GetRichTextCustomStyleData()const { return RichTextCustomStyleData; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UDreamUIRichTextImageData_BaseObject* GetRichTextImageData()const { return RichTextImageData; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamUITextParagraphHorizontalAlign GetParagraphHorizontalAlignment()const { return HAlign; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamUITextParagraphVerticalAlign GetParagraphVerticalAlignment()const { return VAlign; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UMaterialInterface* GetOverrideMaterial()const{return OverrideMaterial;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetExpandMeshSize()const{return ExpandMeshSize;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetDynamicPixelsPerUnit()const { return DynamicPixelsPerUnit; }

	/** indicating whether the text is Truncated or using Ellipsis */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool IsTextTruncated()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetFont(UDreamUIFontData_BaseObject* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", Meta = (AutoCreateRefTerm = "Value"))
		void SetText(const FText& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetFontSize(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetUseKerning(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetLigatures(bool Value);
	/**
	 * Something that animates this text's characters one by one -- TextAnimation, which moves each glyph by its index in
	 * GetCharPropertyArray -- registers here for as long as it does so. A ligature draws several characters as one glyph,
	 * which would leave such an animator glyphs short, so the text lays out without ligatures while anything is
	 * registered; and each character's underline and strikethrough are painted as pieces of its own, which the animator
	 * moves with it, instead of one strip across a run. Registering the same animator twice counts once, and an animator
	 * that is destroyed stops counting.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void RegisterPerCharacterAnimation(const UObject* InAnimator);
	/** The animator is done with this text's characters; see RegisterPerCharacterAnimation. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void UnregisterPerCharacterAnimation(const UObject* InAnimator);
	/** Whether anything registered through RegisterPerCharacterAnimation still animates this text's characters. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool HasPerCharacterAnimation()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetFontSpace(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOverflowType(EDreamUITextOverflowType Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetWrappingPolicy(ETextWrappingPolicy Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetPhraseWrap(EDreamTextPhraseWrap Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetTextStyle(const FDreamTextStyle& Value);
	/** Fill progress of every line, 0..1; glyphs inside a fill segment keep the segment's own value. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetFillProgress(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetGlowBoost(float Value);
	/** Character runs that fill independently (a lyric line's words or syllables). Replaces the previous set. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetFillSegments(const TArray<FDreamTextFillSegment>& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void ClearFillSegments();

	/*
	 * PAINTS (FDreamTextPaint, the paint fields of FDreamTextStyle). Each setter does what SetTextStyle does with the one
	 * field changed, at the least cost: a layer turned on or off, or the boxes, repaint the quads (their slots, UV4 and
	 * colours); another gradient or preset for a layer still painting, and the overlay blend, write rows and the table
	 * only -- another gradient row taken and the text's table written, with no repaint; nothing here lays the text out
	 * again.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Paint")
	void SetFacePaint(const FDreamTextPaint& Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Paint")
	const FDreamTextPaint& GetFacePaint()const { return TextStyle.FacePaint; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Paint")
	void SetOutlinePaint(const FDreamTextPaint& Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Paint")
	const FDreamTextPaint& GetOutlinePaint()const { return TextStyle.OutlinePaint; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Paint")
	void SetOverlayPaint(const FDreamTextPaint& Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Paint")
	const FDreamTextPaint& GetOverlayPaint()const { return TextStyle.OverlayPaint; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Paint")
	void SetOverlayBlend(EDreamTextOverlayBlend Value);
	/** What the text's own paints are measured across, across and down (FDreamTextStyle::PaintBoxHorizontal, PaintBoxVertical). */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Paint")
	void SetPaintBoxes(EDreamTextPaintBox InHorizontal, EDreamTextPaintBox InVertical);

	/*
	 * PAINT ANIMATION: what the text moves its paints by, one pixel of its paint table a change -- no repaint, no layout
	 * (a text whose material does not shade through MF_DreamUI_Shade, painted in its vertex colours, repaints instead).
	 */
	UFUNCTION(BlueprintSetter)
	void SetFacePaintPhase(float Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Paint")
	float GetFacePaintPhase()const { return FacePaintPhase; }
	UFUNCTION(BlueprintSetter)
	void SetOutlinePaintPhase(float Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Paint")
	float GetOutlinePaintPhase()const { return OutlinePaintPhase; }
	UFUNCTION(BlueprintSetter)
	void SetOverlayPaintPhase(float Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Paint")
	float GetOverlayPaintPhase()const { return OverlayPaintPhase; }
	UFUNCTION(BlueprintSetter)
	void SetPaintAngleOffset(float Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Paint")
	float GetPaintAngleOffset()const { return PaintAngleOffset; }
	/** Fractions of the box added to every paint's centre (and to a Linear's mid-point). Not saved. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Paint")
	void SetPaintCenterOffset(FVector2D Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Paint")
	FVector2D GetPaintCenterOffset()const;
	/** Times every paint's Scale: above 1 stretches the gradients, below 1 squeezes them. Not saved. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Paint")
	void SetPaintScale(float Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Paint")
	float GetPaintScale()const;
	/** The row of the world's paint rows this text's table is, while it paints; INDEX_NONE otherwise. For tests and the memory report. */
	int32 GetPaintTextRow()const;
	/**
	 * What this text last wrote into pixel InPixel of its paint table (DreamPaintRows: the slots' gradient rows, phases,
	 * box aspects, angle offset, centre offset and scale); false while it has no table. For tests.
	 */
	bool GetPaintTablePixel(int32 InPixel, FVector4f& OutPixel)const;

	/**
	 * InLayer, a render layer whose canvas found UDreamWidget::GetLayerHoldsCoverageText set, moved: called by that canvas
	 * from UDreamCanvas::MarkRenderLayerMoved, at every move announced while the layer's bit is set, with the layer's
	 * moved frame already stamped. The texts drawing coverage glyphs inside it are measured against the layer's new place once: a whole-pixel
	 * move at the same scale keeps their quads; anything else marks them for a repaint from the field until the layer has
	 * held still for a few frames, and clears the layer's bit. Game thread; costs nothing for a layer no such text holds.
	 */
	static void OnRenderLayerMoved(UDreamWidget* InLayer);
	/**
	 * Told by its canvas when its widget is looked at while it is not drawn -- hidden, collapsed, inactive, or under a canvas
	 * that is: the text lets go of the coverage epoch its quads drew from (UDreamUIFontData_BaseObject::MoveCoverageHold), so
	 * that the font can give those cells back. The geometry update that comes before its quads are drawn again holds that
	 * epoch again while it is still the font's own, and repaints first when it is not. Game thread.
	 */
	void OnNotDrawn();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetMargin(const FMargin& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetLineHeightPercentage(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetWrapTextAt(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetMinDesiredWidth(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetAutoWrapText(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetTextTransform(EDreamUITextTransformPolicy Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetFlowDirection(EDreamTextFlowDirection Value);
	/** A culture name, or empty for the game's current language; see Language. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetLanguage(const FString& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetTabSize(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetTextJustify(EDreamTextJustify Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetLastLineAlign(EDreamTextLastLineAlign Value);
	/** A repaint, never a layout: coverage glyphs only replace quads. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetSmallTextRaster(EDreamTextSmallTextRaster Value);
	/**
	 * Keep this text's layout between layouts and lay out again only what an edit touched, while something types into it:
	 * UITextInput turns it on for its visual. Not saved. See FDreamUITextGeometryCache::SetIncrementalLayout.
	 */
	void SetIncrementalLayout(bool bInEnabled);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetUnderline(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetStrikethrough(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetBestFit(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetBestFitMinSize(float Value);
	/**
	 * The largest whole size in [InMinSize, InMaxSize] whose measurement fits InBox, or InMinSize
	 * when even that does not. InMeasure returns the text size a given font size produces.
	 * Bisects, so it costs about eight measurements rather than one per size.
	 */
	static float FindBestFitFontSize(const FVector2f& InBox, float InMinSize, float InMaxSize,
		TFunctionRef<FVector2f(float)> InMeasure);
	/**
	 * The rect the text is actually laid out in: the widget's own, inset by Margin. Never negative
	 * on either axis -- a margin wider than the widget leaves no room rather than an inside-out box.
	 * Static so the arithmetic can be tested without a widget.
	 */
	static void GetContentBox(const FVector2f& InWidgetSize, const FVector2f& InPivot, const FMargin& InMargin,
		FVector2f& OutSize, FVector2f& OutPivot);
	/**
	 * The layout input this text would lay out with at the given font size: its own properties plus
	 * what the widget and canvas contribute. Public so the pipeline can be driven from outside the
	 * component -- tests, tools -- against the same numbers the component uses.
	 */
	static FDreamTextLayoutInput MakeLayoutInput(const UDreamText* Text, float InFontSize);
	/** The paint parameters this text would paint with: the font's quad style, the canvas's needs, the final colour. */
	static FDreamTextPaintParams MakePaintParams(const UDreamText* Text);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetFontStyle(EDreamUITextFontStyle Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRichText(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRichTextTagFilterFlags(int32 Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRichTextImageData(UDreamUIRichTextImageData_BaseObject* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRichTextCustomStyleData(UDreamUIRichTextCustomStyleData* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
    	void SetOverrideMaterial(UMaterialInterface* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetExpandMeshSize(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetDynamicPixelsPerUnit(float Value);
private:
	void ClearCreatedRichTextImageObject();
	void ClearEmojiObject();
	void RegisterFont();
	void UnregisterFont();
	FDelegateHandle EmojiDataChangedDelegateHandle;
	FDelegateHandle GlyphsReadyDelegateHandle;
	FDelegateHandle CoverageGlyphsChangedDelegateHandle;
	/** The last layout had glyphs still on the font's worker; relayout when the font says they landed. */
	mutable bool bWaitingForGlyphs = false;
	/** See GetSmallTextState. */
	FDreamTextSmallTextState SmallTextState;
	/** What the painter reports through FDreamTextCoverageParams::Report; zeroed before every paint. See GetSmallTextReport. */
	mutable FDreamTextCoverageReport SmallTextReport;
	/**
	 * How many vertices the last paint wrote. A paint not asked to change the triangles can still change their count: a
	 * coverage glyph draws a character whose field glyph is still being made, or stops drawing it when the gate says no.
	 */
	int32 PaintedVertexCount = 0;
	/** The world whose sharpen set holds this text, while one does. */
	TWeakObjectPtr<UDreamUIManagerWorldSubsystem> SmallTextSharpenManager;
	/**
	 * The small-text gate, run right before every paint against the layout it paints: decides whether small sizes draw
	 * from coverage glyphs this time and where the device pixel grid lies (MakePaintParams reads the answer from
	 * SmallTextState), and keeps the debounce and the raster-scale hysteresis.
	 */
	void ResolveSmallTextRaster();
	/** The gate's conditions on the text itself rather than on where it is drawn: Coverage when none of them rules it out. */
	EDreamTextSmallTextGate GetSmallTextConditionsGate();
	/** Into the sharpen set of the world this text is registered in, whose sweep binds while the set holds anything. */
	void JoinSmallTextSharpenSet();
	void LeaveSmallTextSharpenSet();
	/**
	 * The sharpen sweep, right before a world's root canvases update. Every text in its set that has held still for long
	 * enough is placed on the device grid once, repainted from coverage glyphs -- at the scale it settled at -- when it can
	 * draw from them there, and leaves the set; and the texts waiting on a render layer that has held still are repainted.
	 * Within the world's repaint budget for the frame (UDreamGUISettings::SmallTextRepaintBudgetPerFrame): the rest wait for
	 * the next frame.
	 */
	static void SweepSmallTextSharpenSet(UDreamUIManagerWorldSubsystem* InManager);
	/** What RegisterPerCharacterAnimation was told is animating the characters, each once; gone objects count for nothing. */
	TArray<TWeakObjectPtr<const UObject>> PerCharacterAnimators;
	/** The tag colours SetTagColorOverride put in force, as (tag index, colour): handed to the painter as they are. */
	TArray<TPair<int32, FColor>> TagColorOverrides;
	/**
	 * The tag each of TagColorOverrides was put on, in the same order, as the layout described it then. An override names
	 * its tag by index, and a layout can put another tag at that index -- the text edited, a tag added in front -- so a
	 * layout takes off every override whose tag is no longer where it was (DropMovedTagColorOverrides) rather than colour
	 * a tag nobody chose. One put on before the text had a tag at its index has CharIndexStart INDEX_NONE, and takes the
	 * tag the next layout puts there.
	 */
	TArray<FDreamUIText_RichTextCustomTag> TagColorOverrideTags;
	/** The font mark the widget property record was last written with; see GetWidgetPropertyFontMark. */
	uint8 WrittenFontMark = 0;
	/**
	 * Throw away the layout, and the one measured at an offered width, so the next query or paint lays the text out
	 * again. Marking the vertices dirty does not: whether a layout is stale is decided when one is asked for, by
	 * comparing the layout input (SetLayoutInput), which holds everything the layout reads from this text, its widget
	 * and its canvas -- so a repaint, a TextAnimation frame or a fill sweeping a lyric costs no layout. This is for what
	 * that comparison cannot see: a data asset or a font changing underneath the same pointer, glyphs landing, the
	 * culture, the Best Fit settings.
	 */
	void MarkLayoutDirty();
	/** Repaint for a changed tag colour override, when there is a widget to repaint in. */
	void MarkTagColorsDirty();
	/** After a layout: take off every tag colour override whose tag is no longer at its index (see TagColorOverrideTags). */
	void DropMovedTagColorOverrides();
	/** Lay out again for a changed ligature switch, when there is a widget to repaint in. */
	void MarkLigaturesDirty();

	/*
	 * SMALL TEXT ON THE MOVE, IN RENDER LAYERS, AND THE CELLS IT DRAWS FROM.
	 */
	/** Its place in its world's sharpen set (SmallTextSharpenManager's), for leaving the set without a search. */
	int32 SmallTextSharpenIndex = INDEX_NONE;
	/**
	 * The render layer whose lists this text is on in its world's small-text set -- drawing coverage glyphs in the layer, or
	 * waiting for it to hold still (bSmallTextLayerWaiting) -- by key, which finds the lists after the layer has gone; the
	 * world's manager; and the text's place on its list.
	 */
	TObjectKey<UDreamWidget> SmallTextLayerKey;
	TWeakObjectPtr<UDreamUIManagerWorldSubsystem> SmallTextLayerManager;
	int32 SmallTextLayerIndex = INDEX_NONE;
	bool bSmallTextLayerWaiting = false;
	/**
	 * The coverage epoch this text holds (UDreamUIFontData_BaseObject::MoveCoverageHold), and the font it holds it with.
	 * Plain members, never properties: a transaction, a duplicate for play or a save must never copy a hold.
	 */
	TWeakObjectPtr<UDreamUIFontData_BaseObject> CoverageHoldFont;
	uint32 CoverageHoldEpoch = 0;
	/**
	 * The epoch, and its font, that the quads drew from when they stopped being drawn and the text let go of its hold
	 * (OnNotDrawn): held again by the next geometry update while it is still the font's own, else the text repaints first.
	 * 0 when nothing was let go of that way.
	 */
	TWeakObjectPtr<UDreamUIFontData_BaseObject> CoverageUndrawnFont;
	uint32 CoverageUndrawnEpoch = 0;
	/**
	 * A transform change the gate did not measure (DreamGUI.Text.SmallTextOnMove, and a text kept on the field by where it
	 * is): the text joins its world's sharpen set, whose sweep looks at it once it has held still. bInMoving: its coverage
	 * quads left their grid.
	 */
	void WatchSmallTextMove(bool bInMoving);
	/**
	 * The sweep's look at a watched text that has held still: one placement on the device grid. True: repaint it.
	 * bOutListedWithLayer: left on the field by where it is, it is to go on its render layer's lists, if it is in one -- to
	 * wait for a moving layer to hold still, or to hear of a turned or scaled one's next move (SetSmallTextLayer).
	 */
	bool RecheckSmallTextPlacement(bool& bOutListedWithLayer);
	/** Bind the sweep of InManager's world to its manager's tick (GetOnBeforeRootCanvasesUpdate), once, while its set holds texts to sweep. */
	static void BindSmallTextSweep(UDreamUIManagerWorldSubsystem* InManager);
	/**
	 * Onto InLayer's list of the texts that hear of its moves -- drawing coverage glyphs in it, or kept on the field by its
	 * turn or its scale (bInWaiting false) -- or of those waiting for it to hold still (true), and off any list it was on;
	 * null takes it off every list.
	 */
	void SetSmallTextLayer(UDreamWidget* InLayer, bool bInWaiting);
	void LeaveSmallTextLayer();
	/** Forget the layer list this text was on: the list has let go of it already. */
	void ForgetSmallTextLayer();
	/**
	 * Bound once, by the first text registered: every painted text repaints when a small-text console variable changes
	 * (UDreamGUISettings::GetOnSmallTextCoverageChanged), and every text whose tag paints were resolved resolves them again
	 * when the project's gradient presets are edited (DreamGradientPresets::OnPresetsChanged).
	 */
	static void BindProjectSettingsChanges();
	static void OnSmallTextSettingsChanged();
	static void OnGradientPresetsChanged();
	/** Hold coverage epoch InEpoch of InFont, letting go of the one held before; null or 0 holds none. */
	void HoldCoverageEpoch(UDreamUIFontData_BaseObject* InFont, uint32 InEpoch);
	/**
	 * What draws this text: its override material, else its font's, else its canvas's default material where the canvas does
	 * not draw with the built-in UI shader (UDreamCanvas::UpdateDrawCallMaterial); null for the built-in UI shader.
	 */
	UMaterialInterface* GetDrawingMaterial()const;
	/**
	 * What draws it shades through MF_DreamUI_Shade -- the built-in UI shader does -- and so knows coverage glyphs, colour
	 * glyphs and paints: DreamUIShadeMaterial::ShadeMarkerParameter, read as a cooked game reads it.
	 */
	bool IsShadingThroughDreamGUI()const;

	/*
	 * PAINTS: what the text holds of its world's paint rows (FDreamTextPaintState).
	 */
	/** See FDreamTextPaintState: null while the text paints nothing. */
	TUniquePtr<FDreamTextPaintState> PaintState;
	/** See SetPaintCenterOffset and SetPaintScale. Not saved. */
	FVector2f PaintCenterOffset = FVector2f::ZeroVector;
	float PaintScaleMultiplier = 1.0f;
	/**
	 * The text style as it was when the details panel began an edit of it (PreEditChange), to tell an edit of the paints
	 * alone -- which costs what their setters cost -- from any other; and whether the edit being finished was one of those.
	 */
	TUniquePtr<FDreamTextStyle> StyleBeforeEdit;
	bool bPaintEditHandled = false;
	/**
	 * Bring PaintState up to what the text's style and its display list's tag paints ask for: the tag names resolved when
	 * they changed, gradient rows taken and given back, the table written, the record's link. True when the quads paint
	 * with something else than before -- a layer on or off, a name's slot, the table or the fallback came or went -- which
	 * only a repaint shows.
	 */
	bool UpdatePaintState();
	/** Give the rows back (the table and every gradient row), keeping what the layers paint with. */
	void ReleasePaintRows();
	/** Give the rows back and forget the paints: the text paints nothing. */
	void ReleasePaintState();
	/** Write what changed of the table: the slots in use, their rows, phases, aspects, angle offset, centre offset and scale. */
	void WritePaintTable();
	/** A paint setter's change: a layer turned on or off repaints; another gradient takes other rows, without a repaint. */
	void OnPaintsChanged(bool bInLayerTurnedOnOrOff);
	/** The rows and the table brought up to date outside a paint; a repaint when only that can show the change. */
	void RefreshPaintRowsWithoutRepaint();
	/** A phase, the angle offset, the centre offset or the scale changed: the table written, or (vertex colours) a repaint. */
	void OnPaintAnimationChanged();
	/** A gradient asset a paint comes from changed. */
	void OnPaintPresetChanged();
	/** What the face paint is animated by (FDreamPaintAnimation): what the table holds, for the vertex-colour fallback. */
	FDreamPaintAnimation GetFacePaintAnimation()const;
protected:
	virtual uint32 GetWidgetMarksRecordLink()const override;
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;
public:
#pragma region UITextInputComponent
	/**
	 * .
	 * @param moveType 0-left, 1-right, 2-up, 3-down, 4-start, 5-end
	 * @return true- data changed
	 */
	bool MoveCaret(int32 moveType, int32& inOutCaretPositionIndex, int32& inOutCaretPositionLineIndex, FVector2f& inOutCaretPosition);
	/**
	 * The offset in the source string, in UTF-16 code units, that caret inCaretPositionIndex stands at. The caret
	 * that ends a soft-wrapped line stands where the next line starts: the wrap is not in the text.
	 */
	int GetCharIndexByCaretIndex(int32 inCaretPositionIndex);
	int GetLastCaret();
	/** get caret position and line index */
	void FindCaretByIndex(int32& inOutCaretPositionIndex, FVector2f& outCaretPosition, int32& outCaretPositionLineIndex, int32& outVisibleCaretStartIndex);
	/** find current caret position */
	void FindCaret(FVector2f& inOutCaretPosition, int32 inCaretPositionLineIndex, int32& outCaretPositionIndex);
	/** find caret index by position */
	void FindCaretByWorldPosition(FVector inWorldPosition, FVector2f& outCaretPosition, int32& outCaretPositionLineIndex, int32& outCaretPositionIndex);
	/** The caret standing at source offset inCharIndex; for an offset inside a cluster, the caret just past the cluster. */
	int GetCaretIndexByCharIndex(int32 inCharIndex);
	bool GetVisibleCharRangeForMultiLine(int32& inOutCaretPositionIndex, int32& inOutCaretPositionLineIndex, int32& inOutVisibleCaretStartLineIndex, int32& inOutVisibleCaretStartIndex, int inMaxLineCount, int32& outVisibleCharStartIndex, int32& outVisibleCharCount);

	/** range selection */
	void GetSelectionProperty(int32 InSelectionStartCaretIndex, int32 InSelectionEndCaretIndex, TArray<FDreamUITextSelectionProperty>& OutSelectionProeprtyArray);
#pragma endregion UITextInputComponent

#pragma region Hyperlink
	/**
	 * The `<a=Id>...</a>` ranges in this text, as character ranges with their ids. A hyperlink is a
	 * custom tag that can also be clicked, so these are a filtered view of GetRichTextCustomTagArray.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	TArray<FDreamUIText_RichTextCustomTag> GetHyperlinks()const;
	/**
	 * The id of the hyperlink under a world-space point, if any. The clickable area is each character's
	 * painted quad opened up by a quarter of the font size, so the gaps between letters and the room
	 * above the x-height belong to the link rather than falling through it.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool FindHyperlinkByWorldPosition(FVector InWorldPosition, FName& OutId)const;
	/**
	 * FindHyperlinkByWorldPosition's hit test, answering with the link's index in GetRichTextCustomTagArray rather
	 * than its id -- two links may share an id -- or INDEX_NONE when no link is under the point.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	int32 FindHyperlinkIndexByWorldPosition(FVector InWorldPosition)const;
	/** Hit-tests the point and, on a hit, broadcasts OnHyperlinkClicked. True when a link was hit. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool TryClickHyperlinkAtWorldPosition(FVector InWorldPosition);
	/** Broadcast by TryClickHyperlinkAtWorldPosition; UUITextHyperlink is what routes pointer clicks into it. */
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI", DisplayName = "OnHyperlinkClicked")
	FDreamTextHyperlinkEvent OnHyperlinkClickedBP;
	/** The C++ side of the same event, for a native control that cannot bind a dynamic delegate. */
	FDreamTextHyperlinkCppEvent OnHyperlinkClickedCPP;
#pragma endregion Hyperlink

#pragma region TagColorOverride
	/**
	 * Draw the glyphs of one rich-text tag in InColor instead of the colour the markup gives them, until cleared --
	 * how UUITextHyperlink shows a link hovered or pressed. InTagIndex indexes GetRichTextCustomTagArray (a link is a
	 * tag too). The colour is applied when the text is painted, so this repaints the text and never lays it out again.
	 * It belongs to the tag at InTagIndex now: a layout that puts another tag at that index -- the text edited, a tag
	 * added in front -- takes it off, rather than colour a tag nobody chose.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetTagColorOverride(int32 InTagIndex, FColor InColor);
	/** Give one tag back the colour its markup gives it. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void ClearTagColorOverride(int32 InTagIndex);
	/** Give every tag back the colour its markup gives it. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void ClearTagColorOverrides();
	/** The colour a tag is drawn in instead of its own, when SetTagColorOverride put one in force. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetTagColorOverride(int32 InTagIndex, FColor& OutColor)const;
	/** Every override in force, as (tag index, colour) pairs: what the painter is handed. */
	const TArray<TPair<int32, FColor>>& GetTagColorOverrides()const { return TagColorOverrides; }
#pragma endregion TagColorOverride

	const FDreamUITextGeometryCache& GetCacheTextGeometryData()const { UpdateCacheTextGeometry(); return CacheTextGeometryData; }
};
