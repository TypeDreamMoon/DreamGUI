// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamVisualBatchMesh.h"
#include "Core/IDreamUISpriteRenderInterface.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Layout/Margin.h"
#include "Core/DreamLayerStyleTypes.h"
#include "Core/DreamUIShapeBlend.h"
#include "Core/Text/DreamTextPaint.h"
#include "DreamRectBlock.generated.h"


UCLASS(ClassGroup = (DreamGUI), BlueprintType)
class DREAMGUI_API UDreamRectBlockData :public UDreamUIDataAsTexture
{
	GENERATED_BODY()
private:

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TObjectPtr<UMaterialInterface> DefaultMaterial;
protected:
	virtual void PostInitProperties()override;
public:
	UMaterialInterface* GetMaterial();
};

UENUM(BlueprintType)
enum class EDreamRectBlockTextureScaleMode: uint8
{
	Stretch,
	FitIn,
	Envelop,
};

/**
 * How a body texture's own edges survive being drawn at a size that is not its own -- Slate's
 * ESlateBrushDrawType, and the one thing FDreamUIFaceBrush could not say.
 *
 * Nine-slice is done in the PIXEL SHADER, as a remap of the sampled UV, not as nine quads. A rect
 * block is one quad whose whole silhouette -- rounded corners, border, inner and outer shadow -- is
 * signed-distance work against that quad; cutting it into nine would mean nine SDFs that each know
 * only their own ninth, and the corner radius alone would be unrecoverable. Remapping the sample
 * instead leaves every one of those untouched and changes only which texel a body pixel reads.
 */
UENUM(BlueprintType)
enum class EDreamRectBlockTextureDrawMode : uint8
{
	/** The whole texture across the whole rect, scaled by the scale mode. */
	Image,
	/** Nine-slice: the margins keep their authored pixel size and only the middle stretches. */
	Box,
	/** Nine-slice with the middle left out -- a frame drawn around whatever is behind it. */
	Border,
};
UENUM(BlueprintType)
enum class EDreamRectBlockUnitMode : uint8
{
	/** Direct value */
	Value			UMETA(DisplayName="V"),
	/** Percent with rect size from 0 to 100 */
	Percentage		UMETA(DisplayName="%"),
};
UENUM(BlueprintType)
enum class EDreamRectBlockTextureMode : uint8
{
	Texture,
	Sprite,
};

class UDreamUISpriteData_BaseObject;
class UDreamShapeBlendGroup;

/**
 * UV channel-
 *		UV0: full 0~1 UV coordinate for calculate sdf
 *		UV1: Default DreamCanvas use, check DreamCanvas
 *		UV2: Body texture's coordinate
 *		UV3: X- for RectBlock data coordinate
 */
UCLASS(ClassGroup = (DreamGUI), BlueprintType, Blueprintable)
class DREAMGUI_API UDreamRectBlock : public UDreamVisualBatchMesh
	, public IDreamUISpriteRenderInterface
{
	GENERATED_BODY()

public:
	UDreamRectBlock(const FObjectInitializer& ObjectInitializer);

	/**
	 * The mark a rect block's widget record carries, past the font marks (EDreamUIFontTextureMark): what tells the
	 * built-in UI shader to draw a pixel as a rect block (DreamUIShade.ush).
	 */
	static constexpr uint8 BuiltInShaderMark = 4;
	/**
	 * Drawn by the built-in UI shader rather than through its material: no material of its own, its shape kept in its
	 * world's rows of the default rect block data -- the rows every built-in draw binds -- and a canvas that draws with
	 * the built-in shader. Its canvas then batches it with what it draws there -- the label on a button, say -- where
	 * its material drew it in a draw call of its own: a world of panels each with a button on it drew twice the panels.
	 */
	bool IsDrawnByBuiltInShader() const;

	/** The texels of a rect block's row before its layer style's record (DreamUIRectBlock.ush's DREAMUI_RECT_BLOCK_STYLE_FIRST). */
	static constexpr int32 OwnDataTexels = 12;
	/**
	 * Its widget's layer style (UDreamLayerStyle) changed, or came or went: the style drawn in its pixels is written into its row
	 * again and its quad grown by what the style reaches.
	 */
	void OnLayerStyleChanged();

	/**
	 * Its own shadows, border and gradient written as a layer style: the outer shadow as a drop shadow, the inner shadow as an inner
	 * shadow, the border as an inside stroke (its gradient as the stroke's), the body gradient as a radial gradient overlay. The
	 * same four effects, a second way to write them; the rect block draws them itself, and a layer style on its widget adds to them.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ProceduralRect")
	FDreamLayerEffects GetEffectsAsLayerStyle() const;
	/**
	 * The reverse: InStyle's first drop shadow, inner shadow, stroke and gradient overlay written into this rect block's own
	 * shadows, border and gradient, each turned off where InStyle has none. What those cannot say -- a blend mode, a contour,
	 * noise, a stroke outside the edge -- is dropped; the effects of other kinds are not read.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ProceduralRect")
	void SetEffectsFromLayerStyle(const FDreamLayerEffects& InStyle);

	/** The body's gradient as written: with no stops, the two-colour one (GetEffectiveBodyGradient) is drawn. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ProceduralRect")
	const FDreamGradient& GetBodyGradient() const { return BodyGradient; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ProceduralRect")
	void SetBodyGradient(const FDreamGradient& InGradient);
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ProceduralRect")
	const FDreamGradient& GetBorderGradient() const { return BorderGradient; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ProceduralRect")
	void SetBorderGradient(const FDreamGradient& InGradient);
	/**
	 * The gradient the body is drawn with while bEnableBodyGradient is on: BodyGradient, or with no stops the two-colour
	 * gradient its old properties say, as two stops at its widget's size (MakeTwoColourGradient).
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ProceduralRect")
	FDreamGradient GetEffectiveBodyGradient() const;
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ProceduralRect")
	FDreamGradient GetEffectiveBorderGradient() const;
	/**
	 * A two-colour gradient as stops: InInnerColor at InCenter fading to InOuterColor at InRadius (both in units, the centre
	 * from the rect's bottom left), turned by InRotation degrees, on a rect InWidth by InHeight -- a radial ellipse of an
	 * explicit radius mixed in linear light, the inner colour laid over the outer one as the rect block always laid it.
	 */
	static FDreamGradient MakeTwoColourGradient(const FColor& InInnerColor, const FColor& InOuterColor, const FVector2f& InCenter, const FVector2f& InRadius,
		float InRotation, float InWidth, float InHeight);
	/** The rows of its world's paint rows its gradients are drawn from; INDEX_NONE for one it does not draw. For tests and tools. */
	int32 GetBodyGradientRow() const { return BodyGradientRow; }
	int32 GetBorderGradientRow() const { return BorderGradientRow; }

	/** Its corners' radii in units, as its unit mode resolves them at its widget's size. */
	FVector4f GetResolvedCornerRadius() const;
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ProceduralRect")
	EDreamShapeBlendOperation GetShapeBlendOperation() const { return ShapeBlendOperation; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ProceduralRect")
	void SetShapeBlendOperation(EDreamShapeBlendOperation Value);
	/**
	 * What its parent's shape blend group (UDreamShapeBlendGroup) says of it: a member melted into the group's shape, drawn by
	 * the group's first member, the leader, which draws InShape (in its own frame) over InBounds. A null group: on its own again.
	 */
	void SetShapeBlend(UDreamShapeBlendGroup* InGroup, bool bInLeader, const FDreamUIShapeBlendShape* InShape, const FBox2f& InBounds);
	/** Melted into a shape blend group's shape, drawn by its leader or by nothing of its own. */
	bool IsMeltedIntoShapeBlend() const { return ShapeBlendGroup.IsValid(); }
	bool IsShapeBlendLeader() const { return bShapeBlendLeader && ShapeBlendGroup.IsValid(); }

	/**
	 * A reveal highlight on its border (a UDreamPointerLight over it): the border's gradient centred on InCenter (units from its
	 * rect's bottom left, as BorderGradientCenter in units), InRadius across, in InColor, in place of its own border gradient
	 * while on. Not saved.
	 */
	void SetRevealHighlight(bool bInOn, const FVector2f& InCenter, float InRadius, const FColor& InColor);
	bool HasRevealHighlight() const { return bRevealHighlight; }
	/** Drawn by that material rather than the built-in shader while it is set. */
	virtual void SetMaterialOverride(UMaterialInterface* InMaterial) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	using Super::PreEditChange;
	virtual void PreEditChange(FEditPropertyChain& PropertyAboutToChange) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
protected:
	virtual void OnPreChangeSpriteProperty();
	virtual void OnPostChangeSpriteProperty();
#endif
protected:
	virtual void BeginPlay()override;
	virtual void EndPlay()override;

	virtual void OnRegister()override;
	virtual void OnUnregister()override;
protected:
	friend class FDreamRectBlockCustomization;

#pragma region BlockData
	// DreamGUI_Hidden on every *UnitMode below, the body's texture mode and texture, and bUniformSetCornerRadius: the
	// details panel edits each on another property's row -- the unit button beside a value, the Texture/Sprite switch and
	// picker heading the body's texture group, the lock on the corner radius (FDreamRectBlockCustomization).
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FVector4f CornerRadius = FVector4f(0.1f, 0.1f, 0.1f, 0.1f);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DreamGUI_Hidden))
		EDreamRectBlockUnitMode CornerRadiusUnitMode = EDreamRectBlockUnitMode::Percentage;
	/** Prevent edge aliasing, useful when in 3d. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", AdvancedDisplay)
		bool bSoftEdge = true;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableBody = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FColor BodyColor = FColor::White;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DreamGUI_Hidden))
		EDreamRectBlockTextureMode BodyTextureMode = EDreamRectBlockTextureMode::Sprite;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DisplayThumbnail = "false", DreamGUI_Hidden))
		TObjectPtr<class UTexture> BodyTexture = nullptr;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DisplayThumbnail = "false"))
		TObjectPtr<UDreamUISpriteData_BaseObject> BodySpriteTexture = nullptr;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (EditCondition = "BodyTexture"))
		EDreamRectBlockTextureScaleMode BodyTextureScaleMode = EDreamRectBlockTextureScaleMode::Stretch;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (EditCondition = "BodyTexture"))
		EDreamRectBlockTextureDrawMode BodyTextureDrawMode = EDreamRectBlockTextureDrawMode::Image;
	/**
	 * The nine-slice margins, in TEXTURE pixels: how much of each edge keeps its own size instead of
	 * stretching. Ignored in Image mode.
	 *
	 * Texture pixels rather than a fraction, because that is the number an artist actually has -- a
	 * button skin's border is eight pixels wide in the file, and it stays eight whatever the texture
	 * is later resized to. The shader is handed both this and the fraction it works out to, because
	 * it cannot ask a texture how big it is without a fetch it does not otherwise need.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (EditCondition = "BodyTexture", ClampMin = "0.0"))
		FMargin BodyTextureMargin = FMargin(0.0f);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableBodyGradient = false;
	/**
	 * What the body is filled with in place of BodyColor while bEnableBodyGradient is on, times its texture: a gradient of up
	 * to FDreamGradient::MaxStops stops, linear, radial, angle (Conic), diamond, reflected or four corners -- the gradient a
	 * text paints with, drawn from the same rows. With no stops it is the two-colour gradient rect blocks had before
	 * (BodyGradientColor at BodyGradientCenter to BodyColor at BodyGradientRadius, turned by BodyGradientRotation), as the two
	 * stops GetEffectiveBodyGradient makes of it; the first edit in the editor writes those stops here, and the .dui line it
	 * writes takes the place of the two-colour lines.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (EditCondition = "bEnableBodyGradient",
		DreamGUI_Handle = "Gradient", DreamGUI_GradientFallback = "GetEffectiveBodyGradient",
		DreamGUI_Replaces = "BodyGradientColor,BodyGradientCenter,BodyGradientCenterUnitMode,BodyGradientRadius,BodyGradientRadiusUnitMode,BodyGradientRotation"))
		FDreamGradient BodyGradient;
	/**
	 * The two-colour gradient's colour, centre (from the rect's bottom left), radius and turn, and the units of the two:
	 * read from what was written before BodyGradient had stops, and drawn while it has none. Not shown in the editor; their
	 * setters are deprecated.
	 */
	UPROPERTY()
		FColor BodyGradientColor = FColor::Black;
	UPROPERTY()
		FVector2f BodyGradientCenter = FVector2f(0.5f, 0.5f);
	UPROPERTY()
		EDreamRectBlockUnitMode BodyGradientCenterUnitMode = EDreamRectBlockUnitMode::Percentage;
	UPROPERTY()
		FVector2f BodyGradientRadius = FVector2f(0.5f, 0.5f);
	UPROPERTY()
		EDreamRectBlockUnitMode BodyGradientRadiusUnitMode = EDreamRectBlockUnitMode::Percentage;
	UPROPERTY()
		float BodyGradientRotation = 0;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableBorder = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float BorderWidth = 2;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DreamGUI_Hidden))
		EDreamRectBlockUnitMode BorderWidthUnitMode = EDreamRectBlockUnitMode::Value;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FColor BorderColor = FColor::Black;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableBorderGradient = false;
	/** What the border is drawn in place of BorderColor while bEnableBorderGradient is on: as BodyGradient is for the body. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (EditCondition = "bEnableBorderGradient",
		DreamGUI_Handle = "Gradient", DreamGUI_GradientFallback = "GetEffectiveBorderGradient",
		DreamGUI_Replaces = "BorderGradientColor,BorderGradientCenter,BorderGradientCenterUnitMode,BorderGradientRadius,BorderGradientRadiusUnitMode,BorderGradientRotation"))
		FDreamGradient BorderGradient;
	/** The border's two-colour gradient, as BodyGradientColor and the rest are the body's. */
	UPROPERTY()
		FColor BorderGradientColor = FColor::Black;
	UPROPERTY()
		FVector2f BorderGradientCenter = FVector2f(0.5f, 0.5f);
	UPROPERTY()
		EDreamRectBlockUnitMode BorderGradientCenterUnitMode = EDreamRectBlockUnitMode::Percentage;
	UPROPERTY()
		FVector2f BorderGradientRadius = FVector2f(0.5f, 0.5f);
	UPROPERTY()
		EDreamRectBlockUnitMode BorderGradientRadiusUnitMode = EDreamRectBlockUnitMode::Percentage;
	UPROPERTY()
		float BorderGradientRotation = 0;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableInnerShadow = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FColor InnerShadowColor = FColor::Black;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float InnerShadowSize = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DreamGUI_Hidden))
		EDreamRectBlockUnitMode InnerShadowSizeUnitMode = EDreamRectBlockUnitMode::Value;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float InnerShadowBlur = 4;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DreamGUI_Hidden))
		EDreamRectBlockUnitMode InnerShadowBlurUnitMode = EDreamRectBlockUnitMode::Value;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (ClampMin = "0.0", ClampMax = "360.0"))
		float InnerShadowAngle = 45;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float InnerShadowDistance = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DreamGUI_Hidden))
		EDreamRectBlockUnitMode InnerShadowDistanceUnitMode = EDreamRectBlockUnitMode::Value;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableRadialFill = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FVector2f RadialFillCenter = FVector2f(0.5f, 0.5f);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DreamGUI_Hidden))
		EDreamRectBlockUnitMode RadialFillCenterUnitMode = EDreamRectBlockUnitMode::Percentage;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float RadialFillRotation = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (ClampMin = "0.0", ClampMax = "360.0"))
		float RadialFillAngle = 270;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		bool bEnableOuterShadow = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		FColor OuterShadowColor = FColor(0, 0, 0, 128);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float OuterShadowSize = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DreamGUI_Hidden))
		EDreamRectBlockUnitMode OuterShadowSizeUnitMode = EDreamRectBlockUnitMode::Value;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (ClampMin = "0.0"))
		float OuterShadowBlur = 4;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DreamGUI_Hidden))
		EDreamRectBlockUnitMode OuterShadowBlurUnitMode = EDreamRectBlockUnitMode::Value;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (ClampMin = "0.0", ClampMax = "360.0"))
		float OuterShadowAngle = 45;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		float OuterShadowDistance = 4;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect", meta = (DreamGUI_Hidden))
		EDreamRectBlockUnitMode OuterShadowDistanceUnitMode = EDreamRectBlockUnitMode::Value;

	void FillData(uint8* Data, float width, float height, const struct FDreamUILayerStyleRecord* InStyle = nullptr);
	/** The style drawn in this rect block's pixels: its widget's layer style's record, when that is drawn here; else null. */
	const struct FDreamUILayerStyleRecord* FindLayerStyleRecord() const;
	float GetValueWithUnitMode(float SourceValue, EDreamRectBlockUnitMode UnitMode, float RectWidth, float RectHeight, float AdditionalScale)const;
	FVector4f GetValueWithUnitMode(const FVector4f& SourceValue, EDreamRectBlockUnitMode UnitMode, float RectWidth, float RectHeight, float AdditionalScale)const;
	FVector2f GetValueWithUnitMode(const FVector2f& SourceValue, EDreamRectBlockUnitMode UnitMode, float RectWidth, float RectHeight)const;
	FVector2f GetInnerShadowOffset(float RectWidth, float RectHeight);
	FVector2f GetOuterShadowOffset(float RectWidth, float RectHeight);
static constexpr int DataCountInBytes();

	void FillColorToData(uint8* Data, const FColor& InValue, int& InOutDataOffset);
	uint8 PackBoolToByte(
		bool v0
		, bool v1
		, bool v2
		, bool v3
		, bool v4
		, bool v5
		, bool v6
		, bool v7
	);
	/**
	 * Write three bytes into one pixel of the float data texture, under a constant top byte that
	 * keeps the word a normal float (the shader reads it back with asuint(), and mobile GPUs flush a
	 * denormal to zero on load). The fourth byte this used to take was never written.
	 */
	void Fill8BytesToData(uint8* Data, uint8 InValue0, uint8 InValue1, uint8 InValue2, int& InOutDataOffset);
public:
	/**
	 * The two bit-field packings this element puts in its float data texture, as pure functions so
	 * that what the shader has to mirror can be stated once and tested.
	 *
	 * Both keep the float EXPONENT (bits 30..23) non-zero, because the data texture is R32_FLOAT and
	 * every reader gets the word through a float load: a GPU with flush-to-zero -- most mobile ones,
	 * and not optional there -- turns a denormal into 0 before asuint() ever runs. With the old
	 * layouts an opaque black (0x000000ff as R,G,B,A) and "body enabled, nothing else, any non-default
	 * texture mode" were both denormals, so the first drew as fully transparent and the second drew
	 * nothing at all -- on devices only, with everything correct on the desktop the author tested on.
	 * DreamUI_UnpackUintColor and the flag decode in DreamUIRectBlock.ush are the mirror of these.
	 */
	static uint32 PackColorForDataTexture(const FColor& InValue);
	static uint32 PackFlagsForDataTexture(uint8 InBoolValues, uint8 InTextureScaleMode, uint8 InTextureDrawMode);
	/** Top byte of the flags pixel: present only so the word is a normal float. */
	static constexpr uint32 FlagsNormalFloatMarker = 0x3f000000;
private:
	void FillFloatToData(uint8* Data, const float& InValue, int& InOutDataOffset);
	void FillVector2ToData(uint8* Data, const FVector2f& InValue, int& InOutDataOffset);
	void FillVector4ToData(uint8* Data, const FVector4f& InValue, int& InOutDataOffset);


#define OnFloatUnitModeChanged(Property, AdditionalScale)\
	void On##Property##UnitModeChanged(float width, float height)\
	{\
		if (Property##UnitMode == EDreamRectBlockUnitMode::Value)\
		{\
			Property = Property * (width < height ? width : height) * AdditionalScale;\
		}\
		else\
		{\
			Property = Property / (width < height ? width : height) / AdditionalScale;\
		}\
	}

#define OnVector2UnitModeChanged(Property)\
	void On##Property##UnitModeChanged(float width, float height)\
	{\
		if (Property##UnitMode == EDreamRectBlockUnitMode::Value)\
		{\
			Property.X = Property.X * width;\
			Property.Y = Property.Y * height;\
		}\
		else\
		{\
			Property.X = Property.X / width;\
			Property.Y = Property.Y / height;\
		}\
	}

	void OnCornerRadiusUnitModeChanged(float width, float height);

	OnFloatUnitModeChanged(BorderWidth, 0.5f);

	OnFloatUnitModeChanged(InnerShadowSize, 0.5f);
	OnFloatUnitModeChanged(InnerShadowBlur, 1.0f);
	OnFloatUnitModeChanged(InnerShadowDistance, 0.5f);

	OnVector2UnitModeChanged(RadialFillCenter);

	OnFloatUnitModeChanged(OuterShadowSize, 0.5f);
	OnFloatUnitModeChanged(OuterShadowBlur, 1.0f);
	OnFloatUnitModeChanged(OuterShadowDistance, 0.5f);

#pragma endregion

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (DreamGUI_Hidden))
		bool bUniformSetCornerRadius = true;
#endif

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
		TObjectPtr<class UDreamRectBlockData> RectBlockData = nullptr;
	/**
	 * The rows this rect block's shape is uploaded into: its world's, for RectBlockData (see
	 * UDreamUIManagerWorldSubsystem::GetRectBlockDataRows), taken when it registers. Null while unregistered.
	 */
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
		TObjectPtr<class UDreamUIDataAsTexture> DataRows = nullptr;
	/** Its world's rows, or the asset's own in a world with no UI manager to keep any. */
	UDreamUIDataAsTexture* FindDataRows() const;
	/** How this rect block joins the shape its parent's shape blend group makes, when there is one (UDreamShapeBlendGroup). */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ProceduralRect")
		EDreamShapeBlendOperation ShapeBlendOperation = EDreamShapeBlendOperation::Union;

	/** When do raycast interaction, will the CornerRadius be considered? Only support RaycastType.Rect. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Raycast")
		bool bRaycastSupportCornerRadius = true;

	int DataStartPosition = 0;
	static FName DataTextureParameterName;

	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;

	virtual void OnBeforeCreateOrUpdateGeometry()override;
	virtual UTexture* GetTextureToCreateGeometry()override;
	virtual UMaterialInterface* GetMaterialToCreateGeometry()override;
	virtual void AddMaterialParameters(FDreamUIMaterialParameters& InOutParameters) const override;
	virtual uint8 GetFontMark_WidgetPropertyDataForMaterial() override { return BuiltInShaderMark; }

	//virtual void OnAnchorChange(bool InPivotChange, bool InWidthChange, bool InHeightChange, bool InDiscardCache = true)override;
	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)override;
	virtual void MarkAllDirty()override;
	virtual bool GetAnythingDirty() const override;

	void MarkNeedUpdateBlockData();
	uint8 bNeedUpdateBlockData : 1;
	/** How far its layer style reached when its quad was last made. */
	float LastStyleReach = 0.0f;
	/** The shape blend group it is melted into, the shape it draws as the group's leader and the row that holds it. */
	TWeakObjectPtr<UDreamShapeBlendGroup> ShapeBlendGroup;
	FDreamUIShapeBlendShape ShapeBlendShape;
	FBox2f ShapeBlendBounds = FBox2f(ForceInit);
	int32 ShapeBlendRow = INDEX_NONE;
	bool bShapeBlendLeader = false;
	/** See SetRevealHighlight. */
	bool bRevealHighlight = false;
	FVector2f RevealCenter = FVector2f::ZeroVector;
	float RevealRadius = 0.0f;
	FColor RevealColor = FColor::Transparent;
	void ReleaseShapeBlendRow();
	/** The gradients drawn, from their rows of the paint rows of the world they were taken in, and what those rows hold. */
	int32 BodyGradientRow = INDEX_NONE;
	int32 BorderGradientRow = INDEX_NONE;
	FDreamGradient BodyGradientHeld;
	FDreamGradient BorderGradientHeld;
	TWeakObjectPtr<class UDreamUIManagerWorldSubsystem> GradientRowsOwner;
	/** The gradients to draw at a rect InWidth by InHeight: the written ones, or the two-colour ones as stops. */
	FDreamGradient ResolveBodyGradient(float InWidth, float InHeight) const;
	FDreamGradient ResolveBorderGradient(float InWidth, float InHeight) const;
	/** Its widget's size, or 100 by 100 without one: what the two-colour gradient's units are measured against. */
	FVector2f GetSizeForGradients() const;
	/** Its gradient rows brought to what it draws now -- the reveal highlight in the border's place while it is on. Before FillData. */
	void UpdateGradientRows(float InWidth, float InHeight);
	void ReleaseGradientRows();
#if WITH_EDITORONLY_DATA
	/** A field of a gradient that had no stops is being edited, and what that gradient drew before (PreEditChange). */
	bool bBodyGradientEditFromTwoColour = false;
	bool bBorderGradientEditFromTwoColour = false;
	FDreamGradient GradientBeforeEdit;
#endif
	uint8 bHasAddToSprite : 1;
public:
	/** Its body's rounded rect: what a widget clipping to it as the layer below is clipped to. */
	virtual bool GetClipUnderlay(struct FDreamClipUnderlay& OutUnderlay)const override;
protected:
	bool LineTraceUI_CheckCornerRadius(const FVector2D& InLocalHitPoint)const;
	virtual bool LineTraceUIRect(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const override;
public:
#pragma region IDreamUISpriteRenderInterface
	virtual UDreamUISpriteData_BaseObject* SpriteRenderGetSprite_Implementation()const override { return BodySpriteTexture; }
	virtual void ApplyAtlasTextureChange_Implementation()override;
#pragma endregion

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FVector4f& GetCornerRadius()const { return CornerRadius; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetCornerRadiusUnitMode()const { return CornerRadiusUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableBody()const { return bEnableBody; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FColor& GetBodyColor()const { return BodyColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UTexture* GetBodyTexture()const { return BodyTexture; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UDreamUISpriteData_BaseObject* GetBodySpriteTexture()const { return BodySpriteTexture; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockTextureMode GetBodyTextureMode()const { return BodyTextureMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockTextureScaleMode GetBodyTextureScaleMode()const { return BodyTextureScaleMode; }
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ProceduralRect")
		EDreamRectBlockTextureDrawMode GetBodyTextureDrawMode()const { return BodyTextureDrawMode; }
	UFUNCTION(BlueprintPure, Category = "DreamGUI-ProceduralRect")
		FMargin GetBodyTextureMargin()const { return BodyTextureMargin; }

	/**
	 * The nine-slice margins a rect this size can actually honour, in quad pixels, LTRB.
	 *
	 * A skin drawn for a big button put on a small one asks for two caps that together are wider
	 * than the rect: the middle span goes negative and the two caps read past each other. Both are
	 * scaled down TOGETHER, which is what Slate does and what keeps the RATIO between them -- an
	 * asymmetric frame stays asymmetric as it shrinks instead of going lopsided.
	 *
	 * Public and static because it is the only part of the packing with an opinion in it; the rest
	 * is memcpy into a byte layout the shader reads back.
	 */
	static FVector4f ResolveSliceMarginPixels(const FMargin& InMargin, float InWidth, float InHeight);

	/**
	 * The texture a nine-slice margin is measured against, or null when there is not one.
	 *
	 * Null for an ATLAS SPRITE, deliberately, and that is what gates nine-slice off for one: a
	 * sprite's UV span is a sub-rect of its atlas, so remapping the sample within 0..1 would walk
	 * straight into the neighbouring sprite. Supporting it means handing the shader that sub-rect
	 * too; until then a sprite draws as Image, which is the look it has always had rather than a
	 * frame that is subtly reading somebody else's art.
	 */
	const UTexture* GetBodySampledTexture() const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetSoftEdge()const { return bSoftEdge; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableBodyGradient()const { return bEnableBodyGradient; }
	/**
	 * The two-colour gradient's colour, centre (from the rect's bottom left), radius and turn -- or, once BodyGradient has
	 * stops, its first stop's colour, its centre, its radius and its angle, in the two-colour units.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FColor GetBodyGradientColor()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector2f GetBodyGradientCenter()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetBodyGradientCenterUnitMode()const { return BodyGradientCenterUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector2f GetBodyGradientRadius()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetBodyGradientRadiusUnitMode()const { return BodyGradientRadiusUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetBodyGradientRotation()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableBorder()const { return bEnableBorder; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetBorderWidth()const { return BorderWidth; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetBorderWidthUnitMode()const { return BorderWidthUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FColor& GetBorderColor()const { return BorderColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableBorderGradient()const { return bEnableBorderGradient; }
	/** The border's, as GetBodyGradientColor and the rest are the body's. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FColor GetBorderGradientColor()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector2f GetBorderGradientCenter()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetBorderGradientCenterUnitMode()const { return BorderGradientCenterUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector2f GetBorderGradientRadius()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetBorderGradientRadiusUnitMode()const { return BorderGradientRadiusUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetBorderGradientRotation()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableInnerShadow()const { return bEnableInnerShadow; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FColor& GetInnerShadowColor()const { return InnerShadowColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetInnerShadowSize()const { return InnerShadowSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetInnerShadowSizeUnitMode()const { return InnerShadowSizeUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetInnerShadowBlur()const { return InnerShadowBlur; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetInnerShadowBlurUnitMode()const { return InnerShadowBlurUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetInnerShadowAngle()const { return InnerShadowAngle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetInnerShadowDistance()const { return InnerShadowDistance; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetInnerShadowDistanceUnitMode()const { return InnerShadowDistanceUnitMode; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableRadialFill()const { return bEnableRadialFill; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FVector2f& GetRadialFillCenter()const { return RadialFillCenter; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetRadialFillCenterUnitMode()const { return RadialFillCenterUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetRadialFillRotation()const { return RadialFillRotation; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetRadialFillAngle()const { return RadialFillAngle; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetEnableOuterShadow()const { return bEnableOuterShadow; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const FColor& GetOuterShadowColor()const { return OuterShadowColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetOuterShadowSize()const { return OuterShadowSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetOuterShadowSizeUnitMode()const { return OuterShadowSizeUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetOuterShadowBlur()const { return OuterShadowBlur; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetOuterShadowBlurUnitMode()const { return OuterShadowBlurUnitMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetOuterShadowAngle()const { return OuterShadowAngle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetOuterShadowDistance()const { return OuterShadowDistance; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamRectBlockUnitMode GetOuterShadowDistanceUnitMode()const { return OuterShadowDistanceUnitMode; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetRaycastSupportCornerRadius()const { return bRaycastSupportCornerRadius; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetCornerRadius(const FVector4& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetCornerRadiusUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableBody(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyColor(const FColor& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyTexture(UTexture* value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodySpriteTexture(UDreamUISpriteData_BaseObject* value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyTextureMode(EDreamRectBlockTextureMode value);
	/** Set size from current body texture or Sprite */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSizeFromBodyTexture();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBodyTextureScaleMode(EDreamRectBlockTextureScaleMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ProceduralRect")
		void SetBodyTextureDrawMode(EDreamRectBlockTextureDrawMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ProceduralRect")
		void SetBodyTextureMargin(const FMargin& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSoftEdge(bool value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableBodyGradient(bool value);
	/**
	 * The two-colour gradient's setters. On a gradient that has stops they set its first stop's colour, its centre, its
	 * radius (as an explicit one) and its angle.
	 */
	UE_DEPRECATED(5.8, "Set BodyGradient (SetBodyGradient) instead: a gradient of any number of stops.")
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeprecatedFunction, DeprecationMessage = "Set Body Gradient instead: a gradient of any number of stops."))
		void SetBodyGradientColor(const FColor& value);
	UE_DEPRECATED(5.8, "Set BodyGradient (SetBodyGradient) instead: a gradient of any number of stops.")
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeprecatedFunction, DeprecationMessage = "Set Body Gradient instead: a gradient of any number of stops."))
		void SetBodyGradientCenter(const FVector2D& value);
	UE_DEPRECATED(5.8, "Set BodyGradient (SetBodyGradient) instead: its centre is a fraction of the rect.")
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeprecatedFunction, DeprecationMessage = "Set Body Gradient instead: its centre is a fraction of the rect."))
		void SetBodyGradientCenterUnitMode(EDreamRectBlockUnitMode value);
	UE_DEPRECATED(5.8, "Set BodyGradient (SetBodyGradient) instead: a gradient of any number of stops.")
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeprecatedFunction, DeprecationMessage = "Set Body Gradient instead: a gradient of any number of stops."))
		void SetBodyGradientRadius(const FVector2D& value);
	UE_DEPRECATED(5.8, "Set BodyGradient (SetBodyGradient) instead: its radius is a fraction of the rect.")
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeprecatedFunction, DeprecationMessage = "Set Body Gradient instead: its radius is a fraction of the rect."))
		void SetBodyGradientRadiusUnitMode(EDreamRectBlockUnitMode value);
	UE_DEPRECATED(5.8, "Set BodyGradient (SetBodyGradient) instead: a gradient of any number of stops.")
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeprecatedFunction, DeprecationMessage = "Set Body Gradient instead: a gradient of any number of stops."))
		void SetBodyGradientRotation(float value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableBorder(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBorderWidth(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBorderWidthUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetBorderColor(const FColor& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableBorderGradient(bool value);
	/** The border's two-colour setters, as the body's: on a gradient that has stops, its first stop, centre, radius and angle. */
	UE_DEPRECATED(5.8, "Set BorderGradient (SetBorderGradient) instead: a gradient of any number of stops.")
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeprecatedFunction, DeprecationMessage = "Set Border Gradient instead: a gradient of any number of stops."))
		void SetBorderGradientColor(const FColor& value);
	UE_DEPRECATED(5.8, "Set BorderGradient (SetBorderGradient) instead: a gradient of any number of stops.")
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeprecatedFunction, DeprecationMessage = "Set Border Gradient instead: a gradient of any number of stops."))
		void SetBorderGradientCenter(const FVector2D& value);
	UE_DEPRECATED(5.8, "Set BorderGradient (SetBorderGradient) instead: its centre is a fraction of the rect.")
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeprecatedFunction, DeprecationMessage = "Set Border Gradient instead: its centre is a fraction of the rect."))
		void SetBorderGradientCenterUnitMode(EDreamRectBlockUnitMode value);
	UE_DEPRECATED(5.8, "Set BorderGradient (SetBorderGradient) instead: a gradient of any number of stops.")
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeprecatedFunction, DeprecationMessage = "Set Border Gradient instead: a gradient of any number of stops."))
		void SetBorderGradientRadius(const FVector2D& value);
	UE_DEPRECATED(5.8, "Set BorderGradient (SetBorderGradient) instead: its radius is a fraction of the rect.")
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeprecatedFunction, DeprecationMessage = "Set Border Gradient instead: its radius is a fraction of the rect."))
		void SetBorderGradientRadiusUnitMode(EDreamRectBlockUnitMode value);
	UE_DEPRECATED(5.8, "Set BorderGradient (SetBorderGradient) instead: a gradient of any number of stops.")
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeprecatedFunction, DeprecationMessage = "Set Border Gradient instead: a gradient of any number of stops."))
		void SetBorderGradientRotation(float value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableInnerShadow(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowColor(const FColor& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowSize(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowSizeUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowBlur(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowBlurUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowAngle(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowDistance(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetInnerShadowDistanceUnitMode(EDreamRectBlockUnitMode value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableRadialFill(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRadialFillCenter(const FVector2D& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRadialFillCenterUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRadialFillRotation(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRadialFillAngle(float value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEnableOuterShadow(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowColor(const FColor& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowSize(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowSizeUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowBlur(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowBlurUnitMode(EDreamRectBlockUnitMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowAngle(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowDistance(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOuterShadowDistanceUnitMode(EDreamRectBlockUnitMode value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRaycastSupportCornerRadius(bool value);

#pragma region TweenAnimation
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* CornerRadiusTo(FVector4 endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyAlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyGradientColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyGradientAlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyGradientCenterTo(FVector2D endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyGradientRadiusTo(FVector2D endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BodyGradientRotationTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderWidthTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderAlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderGradientColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderGradientAlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderGradientCenterTo(FVector2D endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderGradientRadiusTo(FVector2D endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* BorderGradientRotationTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* InnerShadowColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* InnerShadowAlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* InnerShadowSizeTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* InnerShadowBlurTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* InnerShadowAngleTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* InnerShadowDistanceTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* RadialFillCenterTo(FVector2D endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* RadialFillRotationTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* RadialFillAngleTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* OuterShadowColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* OuterShadowAlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* OuterShadowSizeTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* OuterShadowBlurTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* OuterShadowAngleTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* OuterShadowDistanceTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
#pragma endregion
};
