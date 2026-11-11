// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamRectBlock.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIClipMask.h"
#include "Core/DreamUISpriteInfo.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DreamUIRender/DreamUIMaterialProxy.h"
#include "Core/Components/DreamTextureBase.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUISpriteData.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/DreamUILayerStyleMath.h"
#include "Core/Components/DreamShapeBlendGroup.h"


#define LOCTEXT_NAMESPACE "DreamRectBlock"


void UDreamRectBlockData::PostInitProperties()
{
	Super::PostInitProperties();
}
UMaterialInterface* UDreamRectBlockData::GetMaterial()
{
	if (!DefaultMaterial)
	{
		DefaultMaterial = UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultRectBlockMaterial, TEXT("DefaultRectBlockMaterial"));
	}
	return DefaultMaterial;
}

void UDreamRectBlock::FillData(uint8* Data, float width, float height, const FDreamUILayerStyleRecord* InStyle)
{
	int DataOffset = 0;

	// The last flag, spare until a layer style came: its record follows this block's own texels. A gradient's flag says
	// its row is held (UpdateGradientRows); a reveal highlight is the border's gradient, centred on the light that makes it.
	uint8 BoolAsByte = PackBoolToByte(bEnableBody, bEnableOuterShadow, BodyGradientRow != INDEX_NONE, bEnableBorder, BorderGradientRow != INDEX_NONE, bEnableInnerShadow, bEnableRadialFill, InStyle != nullptr);
	Fill8BytesToData(Data
		, BoolAsByte
		, static_cast<uint8>(BodyTextureScaleMode)
		// The third byte of the first pixel, which has been spare since this block was designed.
		// Forced back to Image when there is no plain texture to slice -- see GetBodySampledTexture.
		, static_cast<uint8>(GetBodySampledTexture() != nullptr
			? BodyTextureDrawMode : EDreamRectBlockTextureDrawMode::Image)
		, DataOffset);
	// The first texel's second word, spare since this block was designed: the row of the shape a shape blend group's leader
	// draws, plus one (0 for none).
	const float ShapeRowValue = IsShapeBlendLeader() && ShapeBlendRow != INDEX_NONE ? static_cast<float>(ShapeBlendRow + 1) : 0.0f;
	FMemory::Memcpy(Data + 4, &ShapeRowValue, sizeof(float));

	FillVector2ToData(Data, FVector2f(width, height), DataOffset);

	FillVector4ToData(Data, GetValueWithUnitMode(CornerRadius, CornerRadiusUnitMode, width, height, 0.5f), DataOffset);
	FillColorToData(Data, BodyColor, DataOffset);
	FillVector2ToData(Data
		, (BodyTextureMode == EDreamRectBlockTextureMode::Sprite && IsValid(BodySpriteTexture)) ? FVector2f(BodySpriteTexture->GetSpriteInfo().GetUVCenter()) : FVector2f(0.5f, 0.5f)
		, DataOffset);

	// The gradients are rows of the world's paint rows (FDreamGradient, as a text paints with): the body's row here, the
	// border's after its colour, -1 for none. The twenty bytes after each are spare.
	FillFloatToData(Data, static_cast<float>(BodyGradientRow), DataOffset);
	DataOffset += 20;

	FillFloatToData(Data, GetValueWithUnitMode(BorderWidth, BorderWidthUnitMode, width, height, 0.5f), DataOffset);
	FillColorToData(Data, BorderColor, DataOffset);
	FillFloatToData(Data, static_cast<float>(BorderGradientRow), DataOffset);
	DataOffset += 20;

	FillColorToData(Data, InnerShadowColor, DataOffset);
	FillFloatToData(Data, GetValueWithUnitMode(InnerShadowSize, InnerShadowSizeUnitMode, width, height, 0.5f), DataOffset);
	FillFloatToData(Data, GetValueWithUnitMode(InnerShadowBlur, InnerShadowBlurUnitMode, width, height, 1.0f), DataOffset);
	FillVector2ToData(Data, GetInnerShadowOffset(width, height), DataOffset);

	FillVector2ToData(Data, GetValueWithUnitMode(RadialFillCenter, RadialFillCenterUnitMode, width, height), DataOffset);
	FillFloatToData(Data, RadialFillRotation, DataOffset);
	FillFloatToData(Data, RadialFillAngle, DataOffset);

	FillColorToData(Data, OuterShadowColor, DataOffset);
	FillFloatToData(Data, GetValueWithUnitMode(OuterShadowSize, OuterShadowSizeUnitMode, width, height, 0.5f), DataOffset);
	FillFloatToData(Data, GetValueWithUnitMode(OuterShadowBlur, OuterShadowBlurUnitMode, width, height, 1.0f), DataOffset);
	FillVector2ToData(Data, GetOuterShadowOffset(width, height), DataOffset);

	// The nine-slice margins, in the two spaces the shader needs -- see ResolveSliceMarginPixels for
	// what happens when a rect is too small to hold the ones it was given.
	FVector2f TextureSize(1.0f, 1.0f);
	if (const UTexture* SampledTexture = GetBodySampledTexture())
	{
		TextureSize = FVector2f(
			FMath::Max(static_cast<float>(SampledTexture->GetSurfaceWidth()), 1.0f),
			FMath::Max(static_cast<float>(SampledTexture->GetSurfaceHeight()), 1.0f));
	}
	const FVector4f MarginPixels = ResolveSliceMarginPixels(BodyTextureMargin, width, height);
	// The UV pair comes from the AUTHORED margin, not the clamped one: which texels the cap reads is
	// a fact about the texture, and squeezing the cap on screen must not also start reading a
	// different part of the image.
	FillVector4ToData(Data, FVector4f(
		FMath::Max(BodyTextureMargin.Left, 0.0f) / TextureSize.X,
		FMath::Max(BodyTextureMargin.Top, 0.0f) / TextureSize.Y,
		FMath::Max(BodyTextureMargin.Right, 0.0f) / TextureSize.X,
		FMath::Max(BodyTextureMargin.Bottom, 0.0f) / TextureSize.Y), DataOffset);
	FillVector4ToData(Data, MarginPixels, DataOffset);
	// The block's own 188 bytes end part way through its twelfth texel; a style's record starts on the next texel.
	check(DataOffset <= OwnDataTexels * 16);
	if (InStyle != nullptr && InStyle->Texels.Num() > 0)
	{
		FMemory::Memcpy(Data + OwnDataTexels * 16, InStyle->Texels.GetData(), InStyle->Texels.Num() * sizeof(FVector4f));
	}
}

float UDreamRectBlock::GetValueWithUnitMode(float SourceValue, EDreamRectBlockUnitMode UnitMode, float RectWidth, float RectHeight, float AdditionalScale)const
{
	return UnitMode == EDreamRectBlockUnitMode::Value ? SourceValue : (SourceValue * (RectWidth < RectHeight ? RectWidth : RectHeight) * AdditionalScale);
}
FVector4f UDreamRectBlock::GetValueWithUnitMode(const FVector4f& SourceValue, EDreamRectBlockUnitMode UnitMode, float RectWidth, float RectHeight, float AdditionalScale)const
{
	return UnitMode == EDreamRectBlockUnitMode::Value ? SourceValue : (SourceValue * (RectWidth < RectHeight ? RectWidth : RectHeight) * AdditionalScale);
}
FVector2f UDreamRectBlock::GetValueWithUnitMode(const FVector2f& SourceValue, EDreamRectBlockUnitMode UnitMode, float RectWidth, float RectHeight)const
{
	return UnitMode == EDreamRectBlockUnitMode::Value ? SourceValue : (SourceValue * FVector2f(RectWidth, RectHeight));
}

FVector2f UDreamRectBlock::GetInnerShadowOffset(float RectWidth, float RectHeight)
{
	float AngleRadian = FMath::DegreesToRadians(InnerShadowAngle + 90);
	float Sin = FMath::Sin(AngleRadian);
	float Cos = FMath::Cos(AngleRadian);
	float Distance = GetValueWithUnitMode(InnerShadowDistance, InnerShadowDistanceUnitMode, RectWidth, RectHeight, 0.5f);
	return FVector2f(-Sin, Cos) * Distance;
}
FVector2f UDreamRectBlock::GetOuterShadowOffset(float RectWidth, float RectHeight)
{
	float AngleRadian = FMath::DegreesToRadians(OuterShadowAngle + 90);
	float Sin = FMath::Sin(AngleRadian);
	float Cos = FMath::Cos(AngleRadian);
	float Distance = GetValueWithUnitMode(OuterShadowDistance, OuterShadowDistanceUnitMode, RectWidth, RectHeight, 0.5f);
	return FVector2f(-Sin, Cos) * Distance;
}

FVector4f UDreamRectBlock::ResolveSliceMarginPixels(const FMargin& InMargin, float InWidth, float InHeight)
{
	FVector4f Result(
		FMath::Max(InMargin.Left, 0.0f), FMath::Max(InMargin.Top, 0.0f),
		FMath::Max(InMargin.Right, 0.0f), FMath::Max(InMargin.Bottom, 0.0f));
	const float HorizontalCaps = Result.X + Result.Z;
	if (HorizontalCaps > InWidth && HorizontalCaps > KINDA_SMALL_NUMBER)
	{
		// Together, so the ratio survives: the shader's middle span is quad minus both caps, and
		// clamping them one at a time would leave the first at full size and take the whole squeeze
		// out of the second.
		const float Scale = FMath::Max(InWidth, 0.0f) / HorizontalCaps;
		Result.X *= Scale;
		Result.Z *= Scale;
	}
	const float VerticalCaps = Result.Y + Result.W;
	if (VerticalCaps > InHeight && VerticalCaps > KINDA_SMALL_NUMBER)
	{
		const float Scale = FMath::Max(InHeight, 0.0f) / VerticalCaps;
		Result.Y *= Scale;
		Result.W *= Scale;
	}
	return Result;
}

const UTexture* UDreamRectBlock::GetBodySampledTexture() const
{
	return BodyTextureMode == EDreamRectBlockTextureMode::Texture ? BodyTexture.Get() : nullptr;
}

constexpr int UDreamRectBlock::DataCountInBytes()
{
	const int result =
		4//bool and enum
		+ 4//for extra bool and enum

		+ 8//quad size
		+ 16//corner radius
		+ 4//body color
		+ 8//body texture's uv's center point

		//gradient
		+ 4//its row in the world's paint rows
		+ 20//spare

		//border
		+ 4//width
		+ 4//color
		//border gradient
		+ 4//its row in the world's paint rows
		+ 20//spare

		//inner shadow
		+ 4//color
		+ 4//size
		+ 4//blur
		+ 8//offset, this is not angle & distance, we calculate offset result here

		//radial fill
		+ 8//center
		+ 4//rotation
		+ 4//angle

		//outer shadow
		+ 4//color
		+ 4//size
		+ 4//blur
		+ 8//offset, this is not angle & distance, we calculate offset result here

		//nine-slice. Both, because the shader needs the margins in TWO spaces and can get neither
		//from the other without asking the texture how big it is -- a fetch it does not otherwise
		//need. The UV pair says which texels the caps are; the pixel pair says how much of the quad
		//they occupy.
		+ 16//margin as a fraction of the texture, LTRB
		+ 16//the same margin in quad pixels, LTRB

		//a layer style drawn in this block's pixels (UDreamLayerStyle), read only when the last flag says so and uploaded
		//only then: a rect block with no style uploads its own texels alone, as it always did
		+ DreamUILayerStyleLayout::MaxTexels * 16
		;
	return result;
}

uint32 UDreamRectBlock::PackColorForDataTexture(const FColor& InValue)
{
	// ARGB, not RGBA, and the order matters for a reason that has nothing to do with colour:
	// this word is stored in a float data texture and read back with asuint(), so its BIT PATTERN has
	// to be a normal float. The exponent lives in the top byte, and with red there an opaque black
	// (0x000000ff) is a denormal -- which mobile GPUs flush to zero on load, turning every dark
	// opaque colour into a fully transparent one. With alpha on top, the only colours that can still
	// flush are those with alpha 0, which read back as transparent either way.
	// DreamUI_UnpackUintColor in DreamUIRectBlock.ush unpacks in this order; the two must ship together.
	return
		((uint32)InValue.A << 24)
		| ((uint32)InValue.R << 16)
		| ((uint32)InValue.G << 8)
		| ((uint32)InValue.B << 0)
		;
}

uint32 UDreamRectBlock::PackFlagsForDataTexture(uint8 InBoolValues, uint8 InTextureScaleMode, uint8 InTextureDrawMode)
{
	// The three payload bytes sit BELOW a constant top byte, which exists only to make the word a
	// normal float: the flags used to occupy the float's exponent, so "body enabled, everything else
	// off, any non-default texture mode" packed to a denormal and every flag read back as 0 -- an
	// element that draws nothing at all. 0x3f as the exponent byte is ~0.5, which no payload can push
	// back into denormal range.
	return FlagsNormalFloatMarker
		| ((uint32)InBoolValues << 16)
		| ((uint32)InTextureScaleMode << 8)
		| ((uint32)InTextureDrawMode << 0)
		;
}

void UDreamRectBlock::FillColorToData(uint8* Data, const FColor& InValue, int& InOutDataOffset)
{
	const uint32 ColorUint = PackColorForDataTexture(InValue);
	int ByteCount = 4;
	FMemory::Memcpy(Data + InOutDataOffset, &ColorUint, ByteCount);
	InOutDataOffset += ByteCount;
}
uint8 UDreamRectBlock::PackBoolToByte(
	bool v0
	, bool v1
	, bool v2
	, bool v3
	, bool v4
	, bool v5
	, bool v6
	, bool v7
)
{
	uint8 Result =
		((v0 ? 1 : 0) << 7)
		| ((v1 ? 1 : 0) << 6)
		| ((v2 ? 1 : 0) << 5)
		| ((v3 ? 1 : 0) << 4)
		| ((v4 ? 1 : 0) << 3)
		| ((v5 ? 1 : 0) << 2)
		| ((v6 ? 1 : 0) << 1)
		| ((v7 ? 1 : 0) << 0)
		;
	return Result;
}
void UDreamRectBlock::Fill8BytesToData(uint8* Data, uint8 InValue0, uint8 InValue1, uint8 InValue2, int& InOutDataOffset)
{
	int ByteCount = 8;//actually data only cover 4 bytes, but we need extra 4 bytes to make decode easier (because data texture is 16bytes per pixel)
	// Three payload bytes under a constant top byte -- see PackFlagsForDataTexture for why. The
	// fourth byte this used to take is the one that no longer fits; it never carried anything.
	// The decode in DreamUIRectBlock.ush shifts by the same amounts; the two must ship together.
	uint32 DataAsUint = PackFlagsForDataTexture(InValue0, InValue1, InValue2);
	FMemory::Memcpy(Data + InOutDataOffset, &DataAsUint, 4);
	InOutDataOffset += ByteCount;
}
void UDreamRectBlock::FillFloatToData(uint8* Data, const float& InValue, int& InOutDataOffset)
{
	int ByteCount = 4;
	FMemory::Memcpy(Data + InOutDataOffset, &InValue, ByteCount);
	InOutDataOffset += ByteCount;
}
void UDreamRectBlock::FillVector2ToData(uint8* Data, const FVector2f& InValue, int& InOutDataOffset)
{
	int ByteCount = 8;
	FMemory::Memcpy(Data + InOutDataOffset, &InValue, ByteCount);
	InOutDataOffset += ByteCount;
}
void UDreamRectBlock::FillVector4ToData(uint8* Data, const FVector4f& InValue, int& InOutDataOffset)
{
	int ByteCount = 16;
	FMemory::Memcpy(Data + InOutDataOffset, &InValue, ByteCount);
	InOutDataOffset += ByteCount;
}
void UDreamRectBlock::OnCornerRadiusUnitModeChanged(float width, float height)
{
	if (CornerRadiusUnitMode == EDreamRectBlockUnitMode::Value)//from percentage to value
	{
		CornerRadius = CornerRadius * (width < height ? width : height) * 0.5f;
	}
	else//from value to percentage
	{
		CornerRadius = CornerRadius / (width < height ? width : height) * 2.0f;
	}
}

FName UDreamRectBlock::DataTextureParameterName = TEXT("DreamUI_RectBlockDataTexture");

UDreamRectBlock::UDreamRectBlock(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	bNeedUpdateBlockData = true;
	BodyTexture = FDreamUIUtils::GetDefaultWhiteTexture();
	BodySpriteTexture = UDreamUISpriteData::GetDefaultWhiteSolid();
}

void UDreamRectBlock::BeginPlay()
{
	Super::BeginPlay();
	if (!bHasAddToSprite)
	{
		if (IsValid(BodySpriteTexture))
		{
			BodySpriteTexture->AddUISprite(this);
			bHasAddToSprite = true;
		}
	}
}
void UDreamRectBlock::EndPlay()
{
	Super::EndPlay();
	if (bHasAddToSprite)
	{
		if (IsValid(BodySpriteTexture))
		{
			BodySpriteTexture->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
}

void UDreamRectBlock::OnRegister()
{
	Super::OnRegister();
	if (RectBlockData == nullptr)
	{
		RectBlockData = UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultRectBlockData, TEXT("DefaultRectBlockData"));
	}
	// A settings default that does not resolve is a content problem, not a programmer error: it is a
	// soft reference off a native CDO, so a cook driven by an explicit package list need never have
	// pulled it in, and a commandlet whose project settings do not carry it lands here too (see the
	// HEADLESS HAZARD note at the bottom of this file). check() made that a hard crash in Development
	// and Test packages and a null dereference on the very next line in Shipping, where DO_CHECK is 0.
	// LoadSetting has already logged which path failed; what is lost is the data block, so the rest of
	// this class treats a null RectBlockData as "no data block" and leaves DataStartPosition at
	// INDEX_NONE, which is what OnUnregister and the geometry update already key off.
	if (RectBlockData != nullptr)
	{
		DataRows = FindDataRows();
		DataStartPosition = DataRows->RegisterBuffer();
	}
	else
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s has no RectBlockData, so it cannot upload its block and will draw with the canvas default material."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
#if WITH_EDITOR
	if (DreamUI::IsEditingWorld(this))
	{
		if (!bHasAddToSprite)
		{
			if (IsValid(BodySpriteTexture))
			{
				BodySpriteTexture->AddUISprite(this);
				bHasAddToSprite = true;
			}
		}
	}
#endif
}
void UDreamRectBlock::OnUnregister()
{
	Super::OnUnregister();
	//hand the row back exactly once: leaving DataStartPosition pointing at it meant a second
	//unregister (re-register/unregister sequences do happen) put the same row on the free list twice,
	//and it would then be handed to two different rect blocks
	// RectBlockData is null when OnRegister could not load the settings default, and DataStartPosition
	// is then still its as-declared 0 rather than INDEX_NONE -- so the row test alone would call
	// UnregisterBuffer on nothing.
	ReleaseShapeBlendRow();
	ReleaseGradientRows();
	if (DataRows != nullptr && DataStartPosition > INDEX_NONE)
	{
		DataRows->UnregisterBuffer(DataStartPosition);
		DataStartPosition = INDEX_NONE;
	}
	DataRows = nullptr;
#if WITH_EDITOR
	if (DreamUI::IsEditingWorld(this))
	{
		if (bHasAddToSprite)
		{
			if (IsValid(BodySpriteTexture))
			{
				BodySpriteTexture->RemoveUISprite(this);
				bHasAddToSprite = false;
			}
		}
	}
#endif
}

#if WITH_EDITOR
void UDreamRectBlock::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{

#define SetUnitChange(Property)\
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamRectBlock, Property##UnitMode))\
	{\
		this->On##Property##UnitModeChanged(GetWidget()->GetWidth(), GetWidget()->GetHeight());\
	}


	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
		if (!this->GetName().StartsWith("Default__"))
		{
			SetUnitChange(CornerRadius)

			else SetUnitChange(BorderWidth)

			else SetUnitChange(InnerShadowSize)
			else SetUnitChange(InnerShadowBlur)
			else SetUnitChange(InnerShadowDistance)

			else SetUnitChange(RadialFillCenter)

			else SetUnitChange(OuterShadowSize)
			else SetUnitChange(OuterShadowBlur)
			else SetUnitChange(OuterShadowDistance)
		}

		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamRectBlock, BodyTextureMode))
		{
			if (!this->GetName().StartsWith("Default__"))
			{
				MarkTextureDirty();
				MarkVertexUVDirty();
			}
		}
		
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bUniformSetCornerRadius))
		{
			if (bUniformSetCornerRadius)
			{
				CornerRadius.Y = CornerRadius.Z = CornerRadius.W = CornerRadius.X;
			}
		}
	}
}

void UDreamRectBlock::PreEditChange(FEditPropertyChain& PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	// A field of a gradient that is still the two-colour one is about to be edited: what it draws now is kept, so that the
	// edit lands on its two stops rather than on an empty gradient (PostEditChangeChainProperty).
	const FEditPropertyChain::TDoubleLinkedListNode* MemberNode = PropertyAboutToChange.GetActiveMemberNode();
	const FName Member = MemberNode != nullptr && MemberNode->GetValue() != nullptr ? MemberNode->GetValue()->GetFName() : NAME_None;
	bBodyGradientEditFromTwoColour = Member == GET_MEMBER_NAME_CHECKED(UDreamRectBlock, BodyGradient) && BodyGradient.Stops.IsEmpty();
	bBorderGradientEditFromTwoColour = Member == GET_MEMBER_NAME_CHECKED(UDreamRectBlock, BorderGradient) && BorderGradient.Stops.IsEmpty();
	if (bBodyGradientEditFromTwoColour)
	{
		GradientBeforeEdit = GetEffectiveBodyGradient();
	}
	else if (bBorderGradientEditFromTwoColour)
	{
		GradientBeforeEdit = GetEffectiveBorderGradient();
	}
}

void UDreamRectBlock::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// The first edit of a two-colour gradient's field: its two stops, with the field as it was set. An edit that wrote the
	// whole gradient, or gave it stops, needs nothing.
	auto Materialize = [this, &PropertyChangedEvent](FDreamGradient& InOutGradient, bool& bInOutFromTwoColour)
	{
		if (!bInOutFromTwoColour)
		{
			return;
		}
		bInOutFromTwoColour = false;
		const FEditPropertyChain::TDoubleLinkedListNode* MemberNode = PropertyChangedEvent.PropertyChain.GetActiveMemberNode();
		const FEditPropertyChain::TDoubleLinkedListNode* FieldNode = MemberNode != nullptr ? MemberNode->GetNextNode() : nullptr;
		const FProperty* Field = FieldNode != nullptr ? FieldNode->GetValue() : nullptr;
		if (!InOutGradient.Stops.IsEmpty() || Field == nullptr || Field->GetOwnerStruct() != FDreamGradient::StaticStruct())
		{
			return;
		}
		FDreamGradient Edited = GradientBeforeEdit;
		Field->CopyCompleteValue_InContainer(&Edited, &InOutGradient);
		InOutGradient = Edited;
	};
	Materialize(BodyGradient, bBodyGradientEditFromTwoColour);
	Materialize(BorderGradient, bBorderGradientEditFromTwoColour);
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

bool UDreamRectBlock::CanEditChange(const FProperty* InProperty) const
{
	auto PropertyName = InProperty->GetFName();
	static auto RaycastSupportCornerRadius_Name = GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bRaycastSupportCornerRadius);
	if (PropertyName == RaycastSupportCornerRadius_Name)
	{
		if (!this->GetName().StartsWith("Default__"))
		{
			if (!GetWidget()->GetRaycastableInHierarchy() || RaycastType != EDreamVisualRaycastType::Rect)
			{
				return false;
			}
		}
	}
	return Super::CanEditChange(InProperty);
}

void UDreamRectBlock::OnPreChangeSpriteProperty()
{
	if (IsValid(BodySpriteTexture))
	{
		BodySpriteTexture->RemoveUISprite(this);
		bHasAddToSprite = false;
	}
}
void UDreamRectBlock::OnPostChangeSpriteProperty()
{
	if (IsValid(BodySpriteTexture))
	{
		BodySpriteTexture->AddUISprite(this);
		bHasAddToSprite = true;
	}
}
#endif

void UDreamRectBlock::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
	Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
}

void UDreamRectBlock::OnBeforeCreateOrUpdateGeometry()
{
	
}

UTexture* UDreamRectBlock::GetTextureToCreateGeometry()
{
	if (BodyTextureMode == EDreamRectBlockTextureMode::Texture)
	{
		if (!IsValid(this->BodyTexture))
		{
			this->BodyTexture = FDreamUIUtils::GetDefaultWhiteTexture();
		}
		return this->BodyTexture;
	}
	else
	{
		if (!IsValid(BodySpriteTexture))
		{
			BodySpriteTexture = UDreamUISpriteData::GetDefaultWhiteSolid();
		}
		if (IsValid(BodySpriteTexture) && IsValid(BodySpriteTexture->GetAtlasTexture()))
		{
			return BodySpriteTexture->GetAtlasTexture();
		}
	}
	return nullptr;
}
static_assert(UDreamRectBlock::BuiltInShaderMark > static_cast<uint8>(EDreamUIFontTextureMark::Mtsdf),
	"a rect block's mark is past every font mark: the shader tells the two apart by it");

bool UDreamRectBlock::IsDrawnByBuiltInShader() const
{
	if (MaterialOverride != nullptr)
	{
		return false;
	}
	// Its shape in its world's rows -- not the asset's own, which a world with no UI manager keeps -- of the default data,
	// which is what a built-in draw binds (UDreamCanvas::UpdateDrawCallMaterial).
	if (RectBlockData == nullptr || DataRows == nullptr || DataRows.Get() == RectBlockData.Get()
		|| RectBlockData != UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultRectBlockData, TEXT("DefaultRectBlockData")))
	{
		return false;
	}
	const UDreamWidget* Widget = GetWidget();
	const UDreamCanvas* Canvas = Widget != nullptr ? Widget->GetRenderCanvas() : nullptr;
	// As the canvas decides a section with no material (UpdateDrawCallMaterial). A change of the canvas, or of how it
	// renders, marks everything of this visual dirty, its material with it, and asks this again.
	return Canvas != nullptr && UDreamUISettings::GetUseBuiltInUIShader() && Canvas->IsRenderByDreamUIRendererOrUERenderer();
}

const FDreamUILayerStyleRecord* UDreamRectBlock::FindLayerStyleRecord() const
{
	const UDreamWidget* Widget = GetWidget();
	if (IsMeltedIntoShapeBlend())
	{
		// Melted: the group's widget's style, round the melted shape, drawn by the leader alone.
		const UDreamLayerStyle* GroupStyle = IsShapeBlendLeader() && Widget != nullptr ? UDreamLayerStyle::Find(Widget->GetParent()) : nullptr;
		return GroupStyle != nullptr && GroupStyle->GetPath() == EDreamLayerStylePath::Element ? &GroupStyle->GetRecord() : nullptr;
	}
	const UDreamLayerStyle* Style = UDreamLayerStyle::Find(Widget);
	return Style != nullptr && Style->GetPath() == EDreamLayerStylePath::Element ? &Style->GetRecord() : nullptr;
}

FVector4f UDreamRectBlock::GetResolvedCornerRadius() const
{
	const UDreamWidget* Widget = GetWidget();
	return Widget != nullptr ? GetValueWithUnitMode(CornerRadius, CornerRadiusUnitMode, Widget->GetWidth(), Widget->GetHeight(), 0.5f) : CornerRadius;
}

void UDreamRectBlock::SetShapeBlendOperation(EDreamShapeBlendOperation Value)
{
	if (ShapeBlendOperation != Value)
	{
		ShapeBlendOperation = Value;
		if (UDreamShapeBlendGroup* Group = ShapeBlendGroup.Get())
		{
			Group->Refresh();
		}
	}
}

void UDreamRectBlock::SetShapeBlend(UDreamShapeBlendGroup* InGroup, bool bInLeader, const FDreamUIShapeBlendShape* InShape, const FBox2f& InBounds)
{
	ShapeBlendGroup = InGroup;
	bShapeBlendLeader = InGroup != nullptr && bInLeader && InShape != nullptr;
	if (bShapeBlendLeader)
	{
		ShapeBlendShape = *InShape;
		ShapeBlendBounds = InBounds;
		if (ShapeBlendRow == INDEX_NONE && DataRows != nullptr)
		{
			ShapeBlendRow = DataRows->RegisterBuffer();
		}
	}
	else
	{
		ShapeBlendShape.Texels.Reset();
		ShapeBlendBounds = FBox2f(ForceInit);
		ReleaseShapeBlendRow();
	}
	LastStyleReach = -1.0f;
	MarkAllDirty();
}

void UDreamRectBlock::ReleaseShapeBlendRow()
{
	if (ShapeBlendRow != INDEX_NONE && DataRows != nullptr)
	{
		DataRows->UnregisterBuffer(ShapeBlendRow);
	}
	ShapeBlendRow = INDEX_NONE;
}

void UDreamRectBlock::SetRevealHighlight(bool bInOn, const FVector2f& InCenter, float InRadius, const FColor& InColor)
{
	if (bRevealHighlight == bInOn && (!bInOn || (RevealCenter == InCenter && RevealRadius == InRadius && RevealColor == InColor)))
	{
		return;
	}
	bRevealHighlight = bInOn;
	RevealCenter = InCenter;
	RevealRadius = FMath::Max(InRadius, 1e-3f);
	RevealColor = InColor;
	MarkNeedUpdateBlockData();
}

namespace DreamRectBlockGradientLocal
{
	/** InInner laid over InOuter, as the two-colour gradient laid its colour over the body's: in linear light, premultiplied. */
	FColor Over(const FColor& InInner, const FColor& InOuter)
	{
		if (InInner.A == 255)
		{
			return InInner;
		}
		if (InInner.A == 0)
		{
			return InOuter;
		}
		const FLinearColor Inner = FLinearColor::FromSRGBColor(InInner);
		const FLinearColor Outer = FLinearColor::FromSRGBColor(InOuter);
		const float Under = Outer.A * (1.0f - Inner.A);
		const float Alpha = Inner.A + Under;
		return FLinearColor(
			(Inner.R * Inner.A + Outer.R * Under) / Alpha,
			(Inner.G * Inner.A + Outer.G * Under) / Alpha,
			(Inner.B * Inner.A + Outer.B * Under) / Alpha,
			Alpha).ToFColor(/*bSRGB*/ true);
	}

	/** A two-colour centre or radius (from the rect's bottom left, in units or fractions by its unit mode) as fractions, and back. */
	FVector2f ToFractions(const FVector2f& InValue, EDreamRectBlockUnitMode InMode, float InWidth, float InHeight)
	{
		return InMode == EDreamRectBlockUnitMode::Value ? FVector2f(InValue.X / FMath::Max(InWidth, 1e-3f), InValue.Y / FMath::Max(InHeight, 1e-3f)) : InValue;
	}

	FVector2f FromFractions(const FVector2f& InFractions, EDreamRectBlockUnitMode InMode, float InWidth, float InHeight)
	{
		return InMode == EDreamRectBlockUnitMode::Value ? FVector2f(InFractions.X * InWidth, InFractions.Y * InHeight) : InFractions;
	}

	/**
	 * InOutRow made to hold InWanted (null: no row) in InManager's paint rows, InOutHeld the gradient it holds. A different
	 * gradient takes a row before the old one is given back, so a row the same pixels already fill is shared, not written.
	 */
	void HoldRow(UDreamUIManagerWorldSubsystem* InManager, const FDreamGradient* InWanted, int32& InOutRow, FDreamGradient& InOutHeld)
	{
		if (InWanted == nullptr || InManager == nullptr)
		{
			if (InOutRow != INDEX_NONE && InManager != nullptr)
			{
				InManager->ReleasePaintGradientRow(InOutRow);
			}
			InOutRow = INDEX_NONE;
			return;
		}
		if (InOutRow != INDEX_NONE && InOutHeld == *InWanted)
		{
			return;
		}
		const int32 OldRow = InOutRow;
		InOutRow = InManager->AcquirePaintGradientRow(*InWanted);
		InOutHeld = *InWanted;
		if (OldRow != INDEX_NONE)
		{
			InManager->ReleasePaintGradientRow(OldRow);
		}
	}
}

FDreamGradient UDreamRectBlock::MakeTwoColourGradient(const FColor& InInnerColor, const FColor& InOuterColor, const FVector2f& InCenter, const FVector2f& InRadius,
	float InRotation, float InWidth, float InHeight)
{
	const float Width = FMath::Max(InWidth, 1e-3f);
	const float Height = FMath::Max(InHeight, 1e-3f);
	FDreamGradient Gradient;
	Gradient.Type = EDreamPaintType::Radial;
	Gradient.Shape = EDreamPaintRadialShape::Ellipse;
	Gradient.Size = EDreamPaintRadialSize::Explicit;
	Gradient.Interpolation = EDreamPaintInterpolation::Linear;
	Gradient.Spread = EDreamPaintSpread::Pad;
	// From the rect's bottom left, as the two-colour gradient measured it, to fractions from its top left.
	Gradient.Center = FVector2f(InCenter.X / Width, 1.0f - InCenter.Y / Height);
	Gradient.Radius = FVector2f(InRadius.X / Width, InRadius.Y / Height);
	Gradient.Angle = InRotation;
	Gradient.Stops = { FDreamGradientStop(0.0f, DreamRectBlockGradientLocal::Over(InInnerColor, InOuterColor)), FDreamGradientStop(1.0f, InOuterColor) };
	return Gradient;
}

FDreamGradient UDreamRectBlock::ResolveBodyGradient(float InWidth, float InHeight) const
{
	if (!BodyGradient.Stops.IsEmpty())
	{
		return BodyGradient;
	}
	return MakeTwoColourGradient(BodyGradientColor, BodyColor, GetValueWithUnitMode(BodyGradientCenter, BodyGradientCenterUnitMode, InWidth, InHeight),
		GetValueWithUnitMode(BodyGradientRadius, BodyGradientRadiusUnitMode, InWidth, InHeight), BodyGradientRotation, InWidth, InHeight);
}

FDreamGradient UDreamRectBlock::ResolveBorderGradient(float InWidth, float InHeight) const
{
	if (!BorderGradient.Stops.IsEmpty())
	{
		return BorderGradient;
	}
	return MakeTwoColourGradient(BorderGradientColor, BorderColor, GetValueWithUnitMode(BorderGradientCenter, BorderGradientCenterUnitMode, InWidth, InHeight),
		GetValueWithUnitMode(BorderGradientRadius, BorderGradientRadiusUnitMode, InWidth, InHeight), BorderGradientRotation, InWidth, InHeight);
}

FVector2f UDreamRectBlock::GetSizeForGradients() const
{
	const UDreamWidget* Widget = GetWidget();
	return Widget != nullptr ? FVector2f(Widget->GetWidth(), Widget->GetHeight()) : FVector2f(100.0f, 100.0f);
}

FDreamGradient UDreamRectBlock::GetEffectiveBodyGradient() const
{
	const FVector2f Size = GetSizeForGradients();
	return ResolveBodyGradient(Size.X, Size.Y);
}

FDreamGradient UDreamRectBlock::GetEffectiveBorderGradient() const
{
	const FVector2f Size = GetSizeForGradients();
	return ResolveBorderGradient(Size.X, Size.Y);
}

void UDreamRectBlock::SetBodyGradient(const FDreamGradient& InGradient)
{
	if (BodyGradient != InGradient)
	{
		BodyGradient = InGradient;
		MarkNeedUpdateBlockData();
	}
}

void UDreamRectBlock::SetBorderGradient(const FDreamGradient& InGradient)
{
	if (BorderGradient != InGradient)
	{
		BorderGradient = InGradient;
		MarkNeedUpdateBlockData();
	}
}

void UDreamRectBlock::UpdateGradientRows(float InWidth, float InHeight)
{
	using namespace DreamRectBlockGradientLocal;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld());
	if (GradientRowsOwner.IsValid() && GradientRowsOwner.Get() != Manager)
	{
		ReleaseGradientRows();
	}
	GradientRowsOwner = Manager;
	// A member of a shape blend draws none of its own effects, and its leader draws the melted shape in its members' colours.
	TOptional<FDreamGradient> Body;
	TOptional<FDreamGradient> Border;
	if (!IsMeltedIntoShapeBlend())
	{
		if (bEnableBody && bEnableBodyGradient)
		{
			Body = ResolveBodyGradient(InWidth, InHeight);
		}
		if (bEnableBorder && bRevealHighlight)
		{
			// The light's highlight on the border, in place of its own gradient: its colour at the light, the border's at its reach.
			const FVector2f Reach(RevealRadius, RevealRadius);
			Border = MakeTwoColourGradient(RevealColor, BorderColor, RevealCenter, Reach, 0.0f, InWidth, InHeight);
		}
		else if (bEnableBorder && bEnableBorderGradient)
		{
			Border = ResolveBorderGradient(InWidth, InHeight);
		}
	}
	const FDreamGradient* WantedBody = Body.IsSet() && Body->IsPainting() ? &Body.GetValue() : nullptr;
	const FDreamGradient* WantedBorder = Border.IsSet() && Border->IsPainting() ? &Border.GetValue() : nullptr;
	HoldRow(Manager, WantedBody, BodyGradientRow, BodyGradientHeld);
	HoldRow(Manager, WantedBorder, BorderGradientRow, BorderGradientHeld);
}

void UDreamRectBlock::ReleaseGradientRows()
{
	UDreamUIManagerWorldSubsystem* Manager = GradientRowsOwner.Get();
	DreamRectBlockGradientLocal::HoldRow(Manager, nullptr, BodyGradientRow, BodyGradientHeld);
	DreamRectBlockGradientLocal::HoldRow(Manager, nullptr, BorderGradientRow, BorderGradientHeld);
	BodyGradientRow = INDEX_NONE;
	BorderGradientRow = INDEX_NONE;
	GradientRowsOwner.Reset();
}

void UDreamRectBlock::SetMaterialOverride(UMaterialInterface* InMaterial)
{
	if (MaterialOverride != InMaterial)
	{
		Super::SetMaterialOverride(InMaterial);
		// Its data rows are bound one way for the built-in shader and another for a material.
		MarkAllDirty();
	}
}
void UDreamRectBlock::OnLayerStyleChanged()
{
	// The quad only when the style reaches out further or less far: a light moving over a bevel changes its record alone.
	const FDreamUILayerStyleRecord* Style = FindLayerStyleRecord();
	const float Reach = Style != nullptr ? Style->OuterReach : 0.0f;
	if (Reach != LastStyleReach)
	{
		LastStyleReach = Reach;
		MarkAllDirty();
	}
	else
	{
		MarkNeedUpdateBlockData();
	}
}

namespace DreamRectBlockStyleLocal
{
	FColor Opaque(FColor InColor)
	{
		InColor.A = 255;
		return InColor;
	}

	uint8 Alpha(float InOpacity)
	{
		return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(InOpacity, 0.0f, 1.0f) * 255.0f));
	}

	/** An angle in (-180, 180]. */
	float Signed(float InDegrees)
	{
		float Wrapped = FMath::Fmod(InDegrees, 360.0f);
		Wrapped = Wrapped > 180.0f ? Wrapped - 360.0f : (Wrapped <= -180.0f ? Wrapped + 360.0f : Wrapped);
		return Wrapped;
	}

	/**
	 * A rect block's gradient as a style's fill: its type, its stops as colour and opacity stops, its centre as the offset,
	 * its angle turned from CSS's (0 up, clockwise) to an image editor's (0 to the right, counter-clockwise). A style has no
	 * four corners (they go as a linear) and measures its length its own way: the rest is near, not the same.
	 */
	FDreamStyleGradientFill ToStyleFill(const FDreamGradient& InGradient, float InAspect)
	{
		FDreamStyleGradientFill Fill;
		bool bCentred = true;
		switch (InGradient.Type)
		{
		case EDreamPaintType::Radial: Fill.Type = EDreamStyleGradientType::Radial; break;
		case EDreamPaintType::Conic: Fill.Type = EDreamStyleGradientType::Angle; break;
		case EDreamPaintType::Diamond: Fill.Type = EDreamStyleGradientType::Diamond; break;
		case EDreamPaintType::Reflected: Fill.Type = EDreamStyleGradientType::Reflected; break;
		default: Fill.Type = EDreamStyleGradientType::Linear; bCentred = false; break;
		}
		Fill.Angle = Signed(90.0f - InGradient.Angle);
		// The style's offset is a fraction of the size from the middle, +Y down, as the gradient's centre is from the top left.
		Fill.Offset = bCentred ? FVector2f(InGradient.Center.X - 0.5f, InGradient.Center.Y - 0.5f) : FVector2f::ZeroVector;
		if (InGradient.Type == EDreamPaintType::Radial || InGradient.Type == EDreamPaintType::Diamond)
		{
			// The style's radius is its half size along the angle: the gradient's first radius as a share of it.
			const FVector2f Radii = InGradient.GetEndingRadii(InAspect);
			Fill.Scale = FMath::Clamp(Radii.X * InGradient.Scale / FMath::Max(InAspect * 0.5f, 1e-3f), 0.1f, 1.5f);
		}
		else
		{
			Fill.Scale = FMath::Clamp(InGradient.Scale, 0.1f, 1.5f);
		}
		// A style's gradient starts with stops of its own.
		Fill.Gradient.ColorStops.Reset();
		Fill.Gradient.OpacityStops.Reset();
		for (const FDreamGradientStop& Stop : InGradient.Stops)
		{
			const float Location = FMath::Clamp(Stop.Position, 0.0f, 1.0f);
			Fill.Gradient.ColorStops.Add(FDreamStyleGradientColorStop(Location, Opaque(Stop.Color)));
			Fill.Gradient.OpacityStops.Add(FDreamStyleGradientOpacityStop(Location, Stop.Color.A / 255.0f));
		}
		return Fill;
	}

	/**
	 * The reverse: a style's fill as a rect block's gradient, mixed in sRGB as an image editor mixes, a stop at every place
	 * its colour or its opacity has one (as many as fit). Midpoints and the style's own length are not kept.
	 */
	FDreamGradient FromStyleFill(const FDreamStyleGradientFill& InFill, float InOpacity)
	{
		FDreamGradient Gradient;
		switch (InFill.Type)
		{
		case EDreamStyleGradientType::Radial: Gradient.Type = EDreamPaintType::Radial; break;
		case EDreamStyleGradientType::Angle: Gradient.Type = EDreamPaintType::Conic; break;
		case EDreamStyleGradientType::Diamond: Gradient.Type = EDreamPaintType::Diamond; break;
		case EDreamStyleGradientType::Reflected: Gradient.Type = EDreamPaintType::Reflected; break;
		default: Gradient.Type = EDreamPaintType::Linear; break;
		}
		Gradient.Angle = FMath::Fmod(90.0f - InFill.Angle + 720.0f, 360.0f);
		Gradient.Center = FVector2f(0.5f + InFill.Offset.X, 0.5f + InFill.Offset.Y);
		Gradient.Interpolation = EDreamPaintInterpolation::SRGB;
		TArray<float> Places;
		for (const FDreamStyleGradientColorStop& Stop : InFill.Gradient.ColorStops)
		{
			Places.AddUnique(FMath::Clamp(Stop.Location, 0.0f, 1.0f));
		}
		for (const FDreamStyleGradientOpacityStop& Stop : InFill.Gradient.OpacityStops)
		{
			Places.AddUnique(FMath::Clamp(Stop.Location, 0.0f, 1.0f));
		}
		if (Places.IsEmpty())
		{
			Places = { 0.0f, 1.0f };
		}
		Places.Sort();
		Places.SetNum(FMath::Min(Places.Num(), FDreamGradient::MaxStops));
		for (const float Place : Places)
		{
			const float T = InFill.bReverse ? 1.0f - Place : Place;
			const FVector4f Colour = InFill.Gradient.Evaluate(T);
			Gradient.Stops.Add(FDreamGradientStop(Place, FLinearColor(Colour.X, Colour.Y, Colour.Z, Colour.W * InOpacity).ToFColor(false)));
		}
		return Gradient;
	}
}

FDreamLayerEffects UDreamRectBlock::GetEffectsAsLayerStyle() const
{
	using namespace DreamRectBlockStyleLocal;
	FDreamLayerEffects Out;
	const UDreamWidget* Widget = GetWidget();
	const float Width = Widget != nullptr ? Widget->GetWidth() : 100.0f;
	const float Height = Widget != nullptr ? Widget->GetHeight() : 100.0f;
	if (bEnableOuterShadow)
	{
		// Solid Size out from the edge, then fading over half Blur: a soft shadow Size + Blur / 2 long, solid for its first part.
		const float Solid = GetValueWithUnitMode(OuterShadowSize, OuterShadowSizeUnitMode, Width, Height, 0.5f);
		const float Fade = GetValueWithUnitMode(OuterShadowBlur, OuterShadowBlurUnitMode, Width, Height, 1.0f) * 0.5f;
		FDreamStyleDropShadow& Shadow = Out.DropShadows.AddDefaulted_GetRef();
		Shadow.BlendMode = EDreamUIBlendMode::Alpha;
		Shadow.Color = Opaque(OuterShadowColor);
		Shadow.Opacity = OuterShadowColor.A / 255.0f;
		Shadow.bUseGlobalLight = false;
		Shadow.Angle = OuterShadowAngle;
		Shadow.Distance = GetValueWithUnitMode(OuterShadowDistance, OuterShadowDistanceUnitMode, Width, Height, 0.5f);
		Shadow.Size = Solid + Fade;
		Shadow.Spread = Shadow.Size > 0.0f ? FMath::Clamp((Solid / Shadow.Size + 1.0f) * 0.5f, 0.0f, 1.0f) : 1.0f;
	}
	if (bEnableInnerShadow)
	{
		const float Solid = GetValueWithUnitMode(InnerShadowSize, InnerShadowSizeUnitMode, Width, Height, 0.5f);
		const float Fade = GetValueWithUnitMode(InnerShadowBlur, InnerShadowBlurUnitMode, Width, Height, 1.0f);
		FDreamStyleInnerShadow& Shadow = Out.InnerShadows.AddDefaulted_GetRef();
		Shadow.BlendMode = EDreamUIBlendMode::Alpha;
		Shadow.Color = Opaque(InnerShadowColor);
		Shadow.Opacity = InnerShadowColor.A / 255.0f;
		Shadow.bUseGlobalLight = false;
		Shadow.Angle = InnerShadowAngle;
		Shadow.Distance = GetValueWithUnitMode(InnerShadowDistance, InnerShadowDistanceUnitMode, Width, Height, 0.5f);
		Shadow.Size = Solid + Fade;
		Shadow.Choke = Shadow.Size > 0.0f ? FMath::Clamp(Solid / Shadow.Size, 0.0f, 1.0f) : 0.0f;
	}
	if (bEnableBorder && BorderWidth != 0.0f)
	{
		FDreamStyleStroke& Stroke = Out.Strokes.AddDefaulted_GetRef();
		Stroke.Position = EDreamStyleStrokePosition::Inside;
		Stroke.Size = FMath::Abs(GetValueWithUnitMode(BorderWidth, BorderWidthUnitMode, Width, Height, 0.5f));
		Stroke.Color = Opaque(BorderColor);
		Stroke.Opacity = BorderColor.A / 255.0f;
		const FDreamGradient BorderPaint = ResolveBorderGradient(Width, Height);
		if (bEnableBorderGradient && BorderPaint.IsPainting())
		{
			Stroke.Fill = EDreamStyleStrokeFill::Gradient;
			Stroke.Opacity = 1.0f;
			Stroke.Gradient = ToStyleFill(BorderPaint, Width / FMath::Max(Height, 1e-3f));
		}
	}
	const FDreamGradient BodyPaint = ResolveBodyGradient(Width, Height);
	if (bEnableBody && bEnableBodyGradient && BodyPaint.IsPainting())
	{
		// Over the body's colour, all of it: the gradient takes the colour's place where it is opaque.
		FDreamStyleGradientOverlay& Overlay = Out.GradientOverlays.AddDefaulted_GetRef();
		Overlay.Fill = ToStyleFill(BodyPaint, Width / FMath::Max(Height, 1e-3f));
	}
	return Out;
}

void UDreamRectBlock::SetEffectsFromLayerStyle(const FDreamLayerEffects& InStyle)
{
	using namespace DreamRectBlockStyleLocal;
	auto LightAngle = [&InStyle](bool bGlobal, float InAngle) { return DreamLayerStyleLight::GetAngle(InStyle, bGlobal, InAngle); };
	const float Scale = FMath::Max(InStyle.Scale, 0.01f);
	const FDreamStyleDropShadow* Drop = InStyle.DropShadows.FindByPredicate([](const FDreamStyleDropShadow& S) { return S.bEnabled; });
	bEnableOuterShadow = InStyle.bEnabled && Drop != nullptr;
	if (bEnableOuterShadow)
	{
		const float Size = Drop->Size * Scale;
		const float Solid = FMath::Max((2.0f * Drop->Spread - 1.0f) * Size, 0.0f);
		OuterShadowColor = Drop->Color;
		OuterShadowColor.A = Alpha(Drop->Opacity);
		OuterShadowAngle = FMath::Fmod(LightAngle(Drop->bUseGlobalLight, Drop->Angle) + 360.0f, 360.0f);
		OuterShadowDistance = Drop->Distance * Scale;
		OuterShadowSize = Solid;
		OuterShadowBlur = 2.0f * (Size - Solid);
		OuterShadowDistanceUnitMode = OuterShadowSizeUnitMode = OuterShadowBlurUnitMode = EDreamRectBlockUnitMode::Value;
	}
	const FDreamStyleInnerShadow* Inner = InStyle.InnerShadows.FindByPredicate([](const FDreamStyleInnerShadow& S) { return S.bEnabled; });
	bEnableInnerShadow = InStyle.bEnabled && Inner != nullptr;
	if (bEnableInnerShadow)
	{
		const float Size = Inner->Size * Scale;
		InnerShadowColor = Inner->Color;
		InnerShadowColor.A = Alpha(Inner->Opacity);
		InnerShadowAngle = FMath::Fmod(LightAngle(Inner->bUseGlobalLight, Inner->Angle) + 360.0f, 360.0f);
		InnerShadowDistance = Inner->Distance * Scale;
		InnerShadowSize = Inner->Choke * Size;
		InnerShadowBlur = (1.0f - Inner->Choke) * Size;
		InnerShadowDistanceUnitMode = InnerShadowSizeUnitMode = InnerShadowBlurUnitMode = EDreamRectBlockUnitMode::Value;
	}
	const FDreamStyleStroke* Stroke = InStyle.Strokes.FindByPredicate([](const FDreamStyleStroke& S) { return S.bEnabled && S.Size > 0.0f; });
	bEnableBorder = InStyle.bEnabled && Stroke != nullptr;
	if (bEnableBorder)
	{
		BorderWidth = Stroke->Size * Scale;
		BorderWidthUnitMode = EDreamRectBlockUnitMode::Value;
		bEnableBorderGradient = Stroke->Fill == EDreamStyleStrokeFill::Gradient;
		if (bEnableBorderGradient)
		{
			BorderGradient = FromStyleFill(Stroke->Gradient, Stroke->Opacity);
		}
		else
		{
			BorderColor = Stroke->Color;
			BorderColor.A = Alpha(Stroke->Opacity);
		}
	}
	const FDreamStyleGradientOverlay* Overlay = InStyle.GradientOverlays.FindByPredicate([](const FDreamStyleGradientOverlay& S) { return S.bEnabled; });
	bEnableBodyGradient = InStyle.bEnabled && Overlay != nullptr;
	if (bEnableBodyGradient)
	{
		BodyGradient = FromStyleFill(Overlay->Fill, Overlay->Opacity);
	}
	MarkAllDirty();
}

UMaterialInterface* UDreamRectBlock::GetMaterialToCreateGeometry()
{
	if (auto Result = Super::GetMaterialToCreateGeometry())
	{
		return Result;
	}
	else if (IsDrawnByBuiltInShader())
	{
		// No material: the canvas draws it with the built-in shader, batched with what it draws there.
		return nullptr;
	}
	else
	{
		// Null when the settings default did not load (see OnRegister). A null material here is the
		// same answer UDreamImage gives for a brush that holds no material, and the canvas already
		// falls back for it -- unlike check(), which crashed Development and Test packages over it.
		return RectBlockData != nullptr ? RectBlockData->GetMaterial() : nullptr;
	}
}
void UDreamRectBlock::AddMaterialParameters(FDreamUIMaterialParameters& InOutParameters) const
{
	if (DataRows != nullptr)
	{
		InOutParameters.SetTexture(DataTextureParameterName, DataRows->GetDataTexture());
	}
}

UDreamUIDataAsTexture* UDreamRectBlock::FindDataRows() const
{
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		return Manager->GetRectBlockDataRows(RectBlockData, DataCountInBytes(), EDreamUIDataAsTexturePixelFormat::R32G32B32A32);
	}
	// A world no UI manager runs in draws no rect block, but one can register there all the same; it keeps to the
	// asset's own rows, as every world used to.
	RectBlockData->Init(DataCountInBytes(), EDreamUIDataAsTexturePixelFormat::R32G32B32A32, 32);
	return RectBlockData;
}

void UDreamRectBlock::MarkAllDirty()
{
	Super::MarkAllDirty();
	bNeedUpdateBlockData = true;
}

bool UDreamRectBlock::GetAnythingDirty() const
{
	return Super::GetAnythingDirty() || bNeedUpdateBlockData;
}

void UDreamRectBlock::MarkNeedUpdateBlockData()
{
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false);
}

bool UDreamRectBlock::GetClipUnderlay(FDreamClipUnderlay& OutUnderlay) const
{
	OutUnderlay = FDreamClipUnderlay();
	const UDreamWidget* Widget = GetWidget();
	OutUnderlay.Kind = FDreamClipUnderlay::EKind::Shape;
	OutUnderlay.Center = FVector2f(Widget->GetLocalSpaceCenter());
	OutUnderlay.HalfSize = FVector2f(Widget->GetWidth(), Widget->GetHeight()) * 0.5f;
	OutUnderlay.CornerRadius = GetValueWithUnitMode(CornerRadius, CornerRadiusUnitMode, Widget->GetWidth(), Widget->GetHeight(), 0.5f);
	return true;
}

bool UDreamRectBlock::LineTraceUI_CheckCornerRadius(const FVector2D& InLocalHitPoint)const
{
	auto Widget = GetWidget();
	auto TempCornerRadius = GetValueWithUnitMode(CornerRadius, CornerRadiusUnitMode, Widget->GetWidth(), Widget->GetHeight(), 0.5f);
	auto HalfWidth = Widget->GetWidth() * 0.5f;
	auto HalfHeight = Widget->GetHeight() * 0.5f;
	auto MinSize = FMath::Min(HalfWidth, HalfHeight);
	TempCornerRadius.X = FMath::Min(TempCornerRadius.X, MinSize);
	TempCornerRadius.Y = FMath::Min(TempCornerRadius.Y, MinSize);
	TempCornerRadius.Z = FMath::Min(TempCornerRadius.Z, MinSize);
	TempCornerRadius.W = FMath::Min(TempCornerRadius.W, MinSize);
	//the four tests below must cover the whole plane between them: with strict inequalities on both
	//axes a point sitting exactly on X == 0 or Y == 0 matched no quadrant and fell through to the
	//unconditional `return true` at the end, skipping the corner test entirely
	if (InLocalHitPoint.X >= 0 && InLocalHitPoint.Y < 0)//right bottom area of rect
	{
		auto Radius = TempCornerRadius.X;
		auto CenterPos = FVector2D(Widget->GetLocalSpaceRight() - Radius, Widget->GetLocalSpaceBottom() + Radius);
		if (InLocalHitPoint.X > CenterPos.X && InLocalHitPoint.Y < CenterPos.Y)
		{
			if (FVector2D::DistSquared(InLocalHitPoint, CenterPos) > Radius * Radius)
			{
				return false;
			}
		}
		return true;
	}
	else if (InLocalHitPoint.X >= 0 && InLocalHitPoint.Y >= 0)//right top area of rect
	{
		auto Radius = TempCornerRadius.Y;
		auto CenterPos = FVector2D(Widget->GetLocalSpaceRight() - Radius, Widget->GetLocalSpaceTop() - Radius);
		if (InLocalHitPoint.X > CenterPos.X && InLocalHitPoint.Y > CenterPos.Y)
		{
			if (FVector2D::DistSquared(InLocalHitPoint, CenterPos) > Radius * Radius)
			{
				return false;
			}
		}
		return true;
	}
	else if (InLocalHitPoint.X < 0 && InLocalHitPoint.Y >= 0)//left top area of rect
	{
		auto Radius = TempCornerRadius.Z;
		auto CenterPos = FVector2D(Widget->GetLocalSpaceLeft() + Radius, Widget->GetLocalSpaceTop() - Radius);
		if (InLocalHitPoint.X < CenterPos.X && InLocalHitPoint.Y > CenterPos.Y)
		{
			if (FVector2D::DistSquared(InLocalHitPoint, CenterPos) > Radius * Radius)
			{
				return false;
			}
		}
		return true;
	}
	else//left bottom area of rect (X < 0 && Y < 0)
	{
		auto Radius = TempCornerRadius.W;
		auto CenterPos = FVector2D(Widget->GetLocalSpaceLeft() + Radius, Widget->GetLocalSpaceBottom() + Radius);
		if (InLocalHitPoint.X < CenterPos.X && InLocalHitPoint.Y < CenterPos.Y)
		{
			if (FVector2D::DistSquared(InLocalHitPoint, CenterPos) > Radius * Radius)
			{
				return false;
			}
		}
		return true;
	}
}
bool UDreamRectBlock::LineTraceUIRect(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const
{
	auto Widget = GetWidget();
	// This function OVERRIDES UDreamVisual::LineTraceUIRect, so the perspective handling added there
	// never reaches a RectBlock unless it is repeated here. Kept as a copy rather than a call to
	// the base because the two differ further down, at the corner-radius test.
	const bool bPerspective = Widget->HasPerspectiveApplied();
	const FMatrix WidgetToWorldMatrix = bPerspective ? Widget->GetWorldMatrix() : FMatrix::Identity;
	const FMatrix WorldToWidgetMatrix = bPerspective ? WidgetToWorldMatrix.Inverse() : FMatrix::Identity;
	auto InverseTf = Widget->GetWorldTransform().Inverse();
	auto LocalSpaceRayOrigin = bPerspective ? FVector(WorldToWidgetMatrix.TransformPosition(Start)) : InverseTf.TransformPosition(Start);
	auto LocalSpaceRayEnd = bPerspective ? FVector(WorldToWidgetMatrix.TransformPosition(End)) : InverseTf.TransformPosition(End);

	//DrawDebugLine(this->GetWorld(), Start, End, FColor::Red, false);//just for test
	//start and end point must be different side of X plane
	if (FMath::Sign(LocalSpaceRayOrigin.X) != FMath::Sign(LocalSpaceRayEnd.X))
	{
		auto result = FMath::LinePlaneIntersection(LocalSpaceRayOrigin, LocalSpaceRayEnd, FVector::ZeroVector, FVector(1, 0, 0));
		// Melted into a shape blend: where the melted shape is, and this the member nearest it -- the shader's formula.
		const UDreamShapeBlendGroup* Group = IsMeltedIntoShapeBlend() ? ShapeBlendGroup.Get() : nullptr;
		const FVector WorldResult = bPerspective ? FVector(WidgetToWorldMatrix.TransformPosition(result)) : Widget->GetWorldTransform().TransformPosition(result);
		const bool bOnShape = Group != nullptr
			? Group->HitTest(this, WorldResult)
			: (result.Y > Widget->GetLocalSpaceLeft() && result.Y < Widget->GetLocalSpaceRight() && result.Z > Widget->GetLocalSpaceBottom() && result.Z < Widget->GetLocalSpaceTop());
		//hit point inside rect area
		if (bOnShape)
		{
			OutHit.TraceStart = Start;
			OutHit.TraceEnd = End;
			OutHit.Widget = Widget;
			OutHit.Location = bPerspective
				? FVector(WidgetToWorldMatrix.TransformPosition(result))
				: Widget->GetWorldTransform().TransformPosition(result);
			OutHit.Normal = bPerspective
				? FVector(WidgetToWorldMatrix.TransformVector(FVector(1, 0, 0)))
				: Widget->GetWorldTransform().TransformVector(FVector(1, 0, 0));
			OutHit.Normal.Normalize();
			OutHit.Distance = FVector::Distance(Start, OutHit.Location);
			OutHit.ImpactPoint = OutHit.Location;

			//check corner radius
			if (bRaycastSupportCornerRadius && Group == nullptr)
			{
				return LineTraceUI_CheckCornerRadius(FVector2D(result.Y, result.Z));
			}

			return true;
		}
	}
	return false;
}

void UDreamRectBlock::OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = GetWidget();
	static FDreamUISpriteInfo SimpleRectSpriteData;
	const FDreamUILayerStyleRecord* Style = FindLayerStyleRecord();
	// The leader's quad covers its group's shape: the shape's bounds, from its rect's centre +Y down, in its own space.
	FBox2f ShapeBlendQuad(ForceInit);
	if (IsShapeBlendLeader() && ShapeBlendBounds.bIsValid)
	{
		const FVector2f Centre(Widget->GetLocalSpaceCenter());
		ShapeBlendQuad = FBox2f(FVector2f(Centre.X + ShapeBlendBounds.Min.X, Centre.Y - ShapeBlendBounds.Max.Y), FVector2f(Centre.X + ShapeBlendBounds.Max.X, Centre.Y - ShapeBlendBounds.Min.Y));
	}
	FDreamUIGeometry::UpdateRectBlockVertex(&InGeo
		, this->bEnableOuterShadow
		, this->GetOuterShadowOffset(Widget->GetWidth(), Widget->GetHeight())
		, this->GetValueWithUnitMode(OuterShadowSize, OuterShadowSizeUnitMode, Widget->GetWidth(), Widget->GetHeight(), 0.5f)
		, this->GetValueWithUnitMode(OuterShadowBlur, OuterShadowBlurUnitMode, Widget->GetWidth(), Widget->GetHeight(), 1)
		, this->bSoftEdge,
		Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot())
		, SimpleRectSpriteData, BodyTextureMode == EDreamRectBlockTextureMode::Sprite ? (IsValid(BodySpriteTexture) ? BodySpriteTexture->GetSpriteInfo() : SimpleRectSpriteData) : SimpleRectSpriteData
		, Widget->GetRenderCanvas(), this, GetFinalColor(),
		InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
		, Style != nullptr ? Style->OuterReach : 0.0f
		, IsShapeBlendLeader() ? &ShapeBlendQuad : nullptr
	);
	if (IsMeltedIntoShapeBlend() && !IsShapeBlendLeader() && (InVertexPositionChanged || InTriangleChanged))
	{
		// Melted into a shape its leader draws: nothing of its own, every corner on one point.
		for (FDreamUIOriginVertexData& Vertex : InGeo.OriginVertices)
		{
			Vertex.Position = FVector3f::ZeroVector;
		}
	}

	if (InTriangleChanged || InVertexPositionChanged || InVertexUVChanged || InVertexColorChanged)
	{
		auto& vertices = InGeo.Vertices;
		for (int i = 0; i < 4; i++)
		{
			vertices[i].TextureCoordinate[3].X = DataStartPosition;
		}
		bNeedUpdateBlockData = true;
	}

	// No data block means there is nothing to upload into -- see OnRegister.
	if (bNeedUpdateBlockData && DataRows != nullptr)
	{
		bNeedUpdateBlockData = false;

		// Its own texels, and its style's record after them when it has one: no more of the row than it uses, so a rect block
		// with no style uploads what it always did.
		const int32 PixelCount = OwnDataTexels + (Style != nullptr ? Style->Texels.Num() : 0);
		check(PixelCount * 16 <= Align(DataRows->GetBlockSizeInByte(), 16));
		TArray<uint8> BlockBuffer;
		BlockBuffer.SetNumZeroed(PixelCount * 16);
		UpdateGradientRows(Widget->GetWidth(), Widget->GetHeight());
		FillData(BlockBuffer.GetData(), Widget->GetWidth(), Widget->GetHeight(), Style);
		DataRows->UpdateBlock(0, DataStartPosition, MoveTemp(BlockBuffer), PixelCount);
		if (IsShapeBlendLeader() && ShapeBlendRow != INDEX_NONE && ShapeBlendShape.Texels.Num() > 0)
		{
			// The group's shape, in a row of its own.
			TArray<uint8> ShapeBuffer;
			ShapeBuffer.SetNumZeroed(ShapeBlendShape.Texels.Num() * sizeof(FVector4f));
			FMemory::Memcpy(ShapeBuffer.GetData(), ShapeBlendShape.Texels.GetData(), ShapeBuffer.Num());
			DataRows->UpdateBlock(0, ShapeBlendRow, MoveTemp(ShapeBuffer), ShapeBlendShape.Texels.Num());
		}
	}
}

void UDreamRectBlock::ApplyAtlasTextureChange_Implementation()
{
	if (BodyTextureMode != EDreamRectBlockTextureMode::Sprite)return;
	// The sprite is an authored property and the mode is another: an asset can be in Sprite mode
	// with no sprite set, and this is reached from the atlas repack broadcast rather than from a
	// setter that could have refused it. check() made that content state a crash in Development
	// and Test packages and a null dereference on the next line in Shipping, where DO_CHECK is 0.
	if (!IsValid(BodySpriteTexture))
	{
		return;
	}
	UIGeometry->Texture = BodySpriteTexture->GetAtlasTexture();
	if (UDreamWidget* OwningWidget = GetWidget())
	{
		OwningWidget->MarkCanvasUpdate(true);
	}
}

void UDreamRectBlock::SetCornerRadius(const FVector4& value)
{
	this->CornerRadius = FVector4f(value);
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetEnableBody(bool value)
{
	this->bEnableBody = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void UDreamRectBlock::SetBodyColor(const FColor& value)
{
	if (this->BodyColor != value)
	{
		this->BodyColor = value;
		MarkNeedUpdateBlockData();
	}
}
void UDreamRectBlock::SetBodyTexture(UTexture* value)
{
	if (value == nullptr)
	{
		value = FDreamUIUtils::GetDefaultWhiteTexture();
	}
	// A control skins its face on every state change with the brush it has: the same texture again changes nothing, and a
	// texture marked dirty rebuilds the canvas's draw calls.
	if (this->BodyTexture != value)
	{
		this->BodyTexture = value;
		MarkTextureDirty();
	}
}
void UDreamRectBlock::SetBodySpriteTexture(UDreamUISpriteData_BaseObject* value)
{
	if (this->BodySpriteTexture != value)
	{
		// The atlas comparison and the RemoveUISprite below both need the *old* sprite, so the
		// assignment happens once, after the registration hand-over.
		if ((!IsValid(BodySpriteTexture) || !IsValid(value))
			|| (BodySpriteTexture->GetAtlasTexture() != value->GetAtlasTexture()))
		{
			//remove from old
			if (IsValid(BodySpriteTexture))
			{
				BodySpriteTexture->RemoveUISprite(this);
				bHasAddToSprite = false;
			}
			//add to new
			if (IsValid(value))
			{
				value->AddUISprite(this);
				bHasAddToSprite = true;
			}
			MarkTextureDirty();
		}
		BodySpriteTexture = value;
		MarkVertexUVDirty();
	}
}
void UDreamRectBlock::SetBodyTextureMode(EDreamRectBlockTextureMode value)
{
	if (this->BodyTextureMode != value)
	{
		this->BodyTextureMode = value;
		MarkTextureDirty();
		MarkVertexUVDirty();
	}
}
void UDreamRectBlock::SetSizeFromBodyTexture()
{
	if (BodyTextureMode == EDreamRectBlockTextureMode::Sprite)
	{
		if (IsValid(this->BodySpriteTexture))
		{
			GetWidget()->SetWidth(this->BodySpriteTexture->GetSpriteInfo().GetSourceWidth());
			GetWidget()->SetHeight(this->BodySpriteTexture->GetSpriteInfo().GetSourceHeight());
		}
		else
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Sprite is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		}
	}
	else
	{
		if (IsValid(this->BodyTexture))
		{
			GetWidget()->SetWidth(this->BodyTexture->GetSurfaceWidth());
			GetWidget()->SetHeight(this->BodyTexture->GetSurfaceHeight());
		}
		else
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Texture is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		}
	}
}
void UDreamRectBlock::SetSoftEdge(bool value)
{
	this->bSoftEdge = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void UDreamRectBlock::SetBodyTextureScaleMode(EDreamRectBlockTextureScaleMode value)
{
	if (this->BodyTextureScaleMode != value)
	{
		this->BodyTextureScaleMode = value;
		MarkNeedUpdateBlockData();
	}
}

void UDreamRectBlock::SetBodyTextureDrawMode(EDreamRectBlockTextureDrawMode value)
{
	if (this->BodyTextureDrawMode != value)
	{
		this->BodyTextureDrawMode = value;
		MarkNeedUpdateBlockData();
	}
}
void UDreamRectBlock::SetBodyTextureMargin(const FMargin& value)
{
	if (this->BodyTextureMargin != value)
	{
		this->BodyTextureMargin = value;
		MarkNeedUpdateBlockData();
	}
}

void UDreamRectBlock::SetEnableBodyGradient(bool value)
{
	this->bEnableBodyGradient = value;
	MarkNeedUpdateBlockData();
}
/*
 * The two-colour gradient's setters and getters: its own properties while the gradient has no stops, else the gradient's
 * -- the first stop's colour, its centre, its radius and its angle, in the unit modes the two-colour properties say.
 */
namespace DreamRectBlockTwoColourLocal
{
	using namespace DreamRectBlockGradientLocal;

	void SetColor(FDreamGradient& InOutGradient, FColor& InOutTwoColour, const FColor& InValue)
	{
		(InOutGradient.Stops.IsEmpty() ? InOutTwoColour : InOutGradient.Stops[0].Color) = InValue;
	}

	FColor GetColor(const FDreamGradient& InGradient, const FColor& InTwoColour)
	{
		return InGradient.Stops.IsEmpty() ? InTwoColour : InGradient.Stops[0].Color;
	}

	void SetCenter(FDreamGradient& InOutGradient, FVector2f& InOutTwoColour, EDreamRectBlockUnitMode InMode, const FVector2f& InValue, const FVector2f& InSize)
	{
		if (InOutGradient.Stops.IsEmpty())
		{
			InOutTwoColour = InValue;
			return;
		}
		// From the bottom left there, from the top left here.
		const FVector2f Fractions = ToFractions(InValue, InMode, InSize.X, InSize.Y);
		InOutGradient.Center = FVector2f(Fractions.X, 1.0f - Fractions.Y);
	}

	FVector2f GetCenter(const FDreamGradient& InGradient, const FVector2f& InTwoColour, EDreamRectBlockUnitMode InMode, const FVector2f& InSize)
	{
		return InGradient.Stops.IsEmpty() ? InTwoColour
			: FromFractions(FVector2f(InGradient.Center.X, 1.0f - InGradient.Center.Y), InMode, InSize.X, InSize.Y);
	}

	void SetRadius(FDreamGradient& InOutGradient, FVector2f& InOutTwoColour, EDreamRectBlockUnitMode InMode, const FVector2f& InValue, const FVector2f& InSize)
	{
		if (InOutGradient.Stops.IsEmpty())
		{
			InOutTwoColour = InValue;
			return;
		}
		InOutGradient.Size = EDreamPaintRadialSize::Explicit;
		InOutGradient.Radius = ToFractions(InValue, InMode, InSize.X, InSize.Y);
	}

	FVector2f GetRadius(const FDreamGradient& InGradient, const FVector2f& InTwoColour, EDreamRectBlockUnitMode InMode, const FVector2f& InSize)
	{
		if (InGradient.Stops.IsEmpty())
		{
			return InTwoColour;
		}
		// The radii it draws with, in box heights, as fractions of the width and the height.
		const float Height = FMath::Max(InSize.Y, 1e-3f);
		const FVector2f Radii = InGradient.GetEndingRadii(InSize.X / Height);
		return FromFractions(FVector2f(Radii.X * Height / FMath::Max(InSize.X, 1e-3f), Radii.Y), InMode, InSize.X, InSize.Y);
	}

	void SetRotation(FDreamGradient& InOutGradient, float& InOutTwoColour, float InValue)
	{
		(InOutGradient.Stops.IsEmpty() ? InOutTwoColour : InOutGradient.Angle) = InValue;
	}

	float GetRotation(const FDreamGradient& InGradient, float InTwoColour)
	{
		return InGradient.Stops.IsEmpty() ? InTwoColour : InGradient.Angle;
	}
}

void UDreamRectBlock::SetBodyGradientColor(const FColor& value)
{
	DreamRectBlockTwoColourLocal::SetColor(BodyGradient, BodyGradientColor, value);
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBodyGradientCenter(const FVector2D& value)
{
	DreamRectBlockTwoColourLocal::SetCenter(BodyGradient, BodyGradientCenter, BodyGradientCenterUnitMode, FVector2f(value), GetSizeForGradients());
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBodyGradientRadius(const FVector2D& value)
{
	DreamRectBlockTwoColourLocal::SetRadius(BodyGradient, BodyGradientRadius, BodyGradientRadiusUnitMode, FVector2f(value), GetSizeForGradients());
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBodyGradientRotation(float value)
{
	DreamRectBlockTwoColourLocal::SetRotation(BodyGradient, BodyGradientRotation, value);
	MarkNeedUpdateBlockData();
}
FColor UDreamRectBlock::GetBodyGradientColor() const
{
	return DreamRectBlockTwoColourLocal::GetColor(BodyGradient, BodyGradientColor);
}
FVector2f UDreamRectBlock::GetBodyGradientCenter() const
{
	return DreamRectBlockTwoColourLocal::GetCenter(BodyGradient, BodyGradientCenter, BodyGradientCenterUnitMode, GetSizeForGradients());
}
FVector2f UDreamRectBlock::GetBodyGradientRadius() const
{
	return DreamRectBlockTwoColourLocal::GetRadius(BodyGradient, BodyGradientRadius, BodyGradientRadiusUnitMode, GetSizeForGradients());
}
float UDreamRectBlock::GetBodyGradientRotation() const
{
	return DreamRectBlockTwoColourLocal::GetRotation(BodyGradient, BodyGradientRotation);
}

void UDreamRectBlock::SetEnableBorder(bool value)
{
	this->bEnableBorder = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void UDreamRectBlock::SetBorderWidth(float value)
{
	this->BorderWidth = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBorderColor(const FColor& value)
{
	this->BorderColor = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetEnableBorderGradient(bool value)
{
	this->bEnableBorderGradient = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBorderGradientColor(const FColor& value)
{
	DreamRectBlockTwoColourLocal::SetColor(BorderGradient, BorderGradientColor, value);
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBorderGradientCenter(const FVector2D& value)
{
	DreamRectBlockTwoColourLocal::SetCenter(BorderGradient, BorderGradientCenter, BorderGradientCenterUnitMode, FVector2f(value), GetSizeForGradients());
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBorderGradientRadius(const FVector2D& value)
{
	DreamRectBlockTwoColourLocal::SetRadius(BorderGradient, BorderGradientRadius, BorderGradientRadiusUnitMode, FVector2f(value), GetSizeForGradients());
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBorderGradientRotation(float value)
{
	DreamRectBlockTwoColourLocal::SetRotation(BorderGradient, BorderGradientRotation, value);
	MarkNeedUpdateBlockData();
}
FColor UDreamRectBlock::GetBorderGradientColor() const
{
	return DreamRectBlockTwoColourLocal::GetColor(BorderGradient, BorderGradientColor);
}
FVector2f UDreamRectBlock::GetBorderGradientCenter() const
{
	return DreamRectBlockTwoColourLocal::GetCenter(BorderGradient, BorderGradientCenter, BorderGradientCenterUnitMode, GetSizeForGradients());
}
FVector2f UDreamRectBlock::GetBorderGradientRadius() const
{
	return DreamRectBlockTwoColourLocal::GetRadius(BorderGradient, BorderGradientRadius, BorderGradientRadiusUnitMode, GetSizeForGradients());
}
float UDreamRectBlock::GetBorderGradientRotation() const
{
	return DreamRectBlockTwoColourLocal::GetRotation(BorderGradient, BorderGradientRotation);
}

void UDreamRectBlock::SetEnableInnerShadow(bool value)
{
	this->bEnableInnerShadow = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetInnerShadowColor(const FColor& value)
{
	this->InnerShadowColor = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetInnerShadowSize(float value)
{
	this->InnerShadowSize = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetInnerShadowBlur(float value)
{
	this->InnerShadowBlur = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetInnerShadowAngle(float value)
{
	this->InnerShadowAngle = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void UDreamRectBlock::SetInnerShadowDistance(float value)
{
	this->InnerShadowDistance = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}

void UDreamRectBlock::SetEnableRadialFill(bool value)
{
	this->bEnableRadialFill = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetRadialFillCenter(const FVector2D& value)
{
	this->RadialFillCenter = FVector2f(value);
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetRadialFillRotation(float value)
{
	this->RadialFillRotation = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void UDreamRectBlock::SetRadialFillAngle(float value)
{
	this->RadialFillAngle = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}

void UDreamRectBlock::SetEnableOuterShadow(bool value)
{
	this->bEnableOuterShadow = value;
	MarkAllDirty();
}
void UDreamRectBlock::SetOuterShadowColor(const FColor& value)
{
	this->OuterShadowColor = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetOuterShadowSize(float value)
{
	this->OuterShadowSize = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetOuterShadowBlur(float value)
{
	this->OuterShadowBlur = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetOuterShadowAngle(float value)
{
	this->OuterShadowAngle = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void UDreamRectBlock::SetOuterShadowDistance(float value)
{
	this->OuterShadowDistance = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}

#define FunctionSetPropertyUnitMode(Property)\
void UDreamRectBlock::Set##Property##UnitMode(EDreamRectBlockUnitMode value)\
{\
	this->Property##UnitMode = value;\
	MarkNeedUpdateBlockData();\
}

FunctionSetPropertyUnitMode(CornerRadius);
FunctionSetPropertyUnitMode(BodyGradientCenter);
FunctionSetPropertyUnitMode(BodyGradientRadius);
FunctionSetPropertyUnitMode(BorderWidth);
FunctionSetPropertyUnitMode(BorderGradientCenter);
FunctionSetPropertyUnitMode(BorderGradientRadius);
FunctionSetPropertyUnitMode(InnerShadowSize);
FunctionSetPropertyUnitMode(InnerShadowBlur);
FunctionSetPropertyUnitMode(InnerShadowDistance);
FunctionSetPropertyUnitMode(RadialFillCenter);
FunctionSetPropertyUnitMode(OuterShadowSize);
FunctionSetPropertyUnitMode(OuterShadowBlur);
FunctionSetPropertyUnitMode(OuterShadowDistance);

void UDreamRectBlock::SetRaycastSupportCornerRadius(bool value)
{
	bRaycastSupportCornerRadius = value;
}

#pragma region TweenAnimation
#include "DreamTweenManager.h"
#include "Core/DreamUIWidgetRegistry.h"
namespace DreamRectBlockAlphaTweenLocal
{
	/**
	 * An alpha tween's value as the byte it is stored in. The eases that overshoot carry it past 0..1 (OutBack
	 * reaches 1.1), and the plain cast of 280 into a byte wrapped it to 24: a fade-in that flashed nearly
	 * transparent just before it settled.
	 */
	/**
	 * One bit for each property the rect block's tween verbs write, for UDreamTweener::SetExclusive. A colour's alpha
	 * is a bit of its own, so BorderAlphaTo and BorderColorTo replace each other and BorderWidthTo neither. The top two
	 * bits are UDreamVisual's own colour and alpha.
	 */
	namespace Channel
	{
		constexpr uint64 BodyColorRGB = 1ull << 0;
		constexpr uint64 BodyColorA = 1ull << 1;
		constexpr uint64 BodyColor = BodyColorRGB | BodyColorA;
		constexpr uint64 CornerRadius = 1ull << 2;
		constexpr uint64 BodyGradientColorRGB = 1ull << 3;
		constexpr uint64 BodyGradientColorA = 1ull << 4;
		constexpr uint64 BodyGradientColor = BodyGradientColorRGB | BodyGradientColorA;
		constexpr uint64 BodyGradientCenter = 1ull << 5;
		constexpr uint64 BodyGradientRadius = 1ull << 6;
		constexpr uint64 BodyGradientRotation = 1ull << 7;
		constexpr uint64 BorderWidth = 1ull << 8;
		constexpr uint64 BorderColorRGB = 1ull << 9;
		constexpr uint64 BorderColorA = 1ull << 10;
		constexpr uint64 BorderColor = BorderColorRGB | BorderColorA;
		constexpr uint64 BorderGradientColorRGB = 1ull << 11;
		constexpr uint64 BorderGradientColorA = 1ull << 12;
		constexpr uint64 BorderGradientColor = BorderGradientColorRGB | BorderGradientColorA;
		constexpr uint64 BorderGradientCenter = 1ull << 13;
		constexpr uint64 BorderGradientRadius = 1ull << 14;
		constexpr uint64 BorderGradientRotation = 1ull << 15;
		constexpr uint64 InnerShadowColorRGB = 1ull << 16;
		constexpr uint64 InnerShadowColorA = 1ull << 17;
		constexpr uint64 InnerShadowColor = InnerShadowColorRGB | InnerShadowColorA;
		constexpr uint64 InnerShadowSize = 1ull << 18;
		constexpr uint64 InnerShadowBlur = 1ull << 19;
		constexpr uint64 InnerShadowAngle = 1ull << 20;
		constexpr uint64 InnerShadowDistance = 1ull << 21;
		constexpr uint64 RadialFillCenter = 1ull << 22;
		constexpr uint64 RadialFillRotation = 1ull << 23;
		constexpr uint64 RadialFillAngle = 1ull << 24;
		constexpr uint64 OuterShadowColorRGB = 1ull << 25;
		constexpr uint64 OuterShadowColorA = 1ull << 26;
		constexpr uint64 OuterShadowColor = OuterShadowColorRGB | OuterShadowColorA;
		constexpr uint64 OuterShadowSize = 1ull << 27;
		constexpr uint64 OuterShadowBlur = 1ull << 28;
		constexpr uint64 OuterShadowAngle = 1ull << 29;
		constexpr uint64 OuterShadowDistance = 1ull << 30;
	}
	uint8 AlphaToByte(float InAlpha)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(InAlpha * 255.0f), 0, 255));
	}
}
UDreamTweener* UDreamRectBlock::BodyColorTo(FColor endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenColorGetterFunction::CreateWeakLambda(this, [this] {
		return this->BodyColor;
		}), FDreamTweenColorSetterFunction::CreateWeakLambda(this, [this](FColor value) {
			this->SetBodyColor(value);
			}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetExclusive(this, DreamRectBlockAlphaTweenLocal::Channel::BodyColor);
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamRectBlock::BodyAlphaTo(float endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenFloatGetterFunction::CreateWeakLambda(this, [this] {
		return FDreamUIUtils::ByteToFloat01(this->BodyColor.A);
		}), FDreamTweenFloatSetterFunction::CreateWeakLambda(this, [this](float value) {
			auto PropertyValue = this->BodyColor;
			PropertyValue.A = DreamRectBlockAlphaTweenLocal::AlphaToByte(value);
			this->SetBodyColor(PropertyValue);
			}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetExclusive(this, DreamRectBlockAlphaTweenLocal::Channel::BodyColorA);
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
	}
	return Tweener;
}

#define FunctionPropertyAnimation(Property, EndValueType, GetterAndSetterType, Channels)\
UDreamTweener* UDreamRectBlock::Property##To(EndValueType endValue, float duration, float delay, EDreamTweenEase ease)\
{\
	auto Tweener =  UDreamTweenManager::To(this, FDreamTween##GetterAndSetterType##GetterFunction::CreateWeakLambda(this, [this] {\
		return (EndValueType)this->Property;\
		}), FDreamTween##GetterAndSetterType##SetterFunction::CreateWeakLambda(this, [this](EndValueType value) {\
			this->Set##Property(value);\
			}), endValue, duration);\
	if (Tweener)\
	{\
		Tweener->SetExclusive(this, Channels);\
		Tweener->SetEase(ease)->SetDelay(delay);\
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);\
	}\
	return Tweener;\
}

// The getter reads the alpha of the colour the tween WRITES. It used to read BodyColor's for all five,
// a copy of BodyAlphaTo's line, so every one of these faded its own colour starting from the body's
// alpha: a transparent border told to fade in over an opaque body jumped to opaque on the first step
// and stayed there. The tween takes its start from this getter on that first step.
#define FunctionAlphaAnimation(Property, Function, Channels)\
UDreamTweener* UDreamRectBlock::Function##AlphaTo(float endValue, float duration, float delay, EDreamTweenEase ease)\
{\
	auto Tweener =  UDreamTweenManager::To(this, FDreamTweenFloatGetterFunction::CreateWeakLambda(this, [this] {\
		return FDreamUIUtils::ByteToFloat01(this->Property.A);\
		}), FDreamTweenFloatSetterFunction::CreateWeakLambda(this, [this](float value) {\
			auto PropertyValue = this->Property;\
			PropertyValue.A = DreamRectBlockAlphaTweenLocal::AlphaToByte(value);\
			this->Set##Property(PropertyValue);\
			}), endValue, duration);\
	if (Tweener)\
	{\
		Tweener->SetExclusive(this, Channels);\
		Tweener->SetEase(ease)->SetDelay(delay);\
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);\
	}\
	return Tweener;\
}

// The gradients' tweens read and write through the two-colour getters and setters: the first stop's colour, the centre,
// the radius and the angle of a gradient that has stops.
#define FunctionGradientAnimation(Property, EndValueType, GetterAndSetterType, Channels)\
UDreamTweener* UDreamRectBlock::Property##To(EndValueType endValue, float duration, float delay, EDreamTweenEase ease)\
{\
	auto Tweener =  UDreamTweenManager::To(this, FDreamTween##GetterAndSetterType##GetterFunction::CreateWeakLambda(this, [this] {\
		return (EndValueType)this->Get##Property();\
		}), FDreamTween##GetterAndSetterType##SetterFunction::CreateWeakLambda(this, [this](EndValueType value) {\
			this->Set##Property(value);\
			}), endValue, duration);\
	if (Tweener)\
	{\
		Tweener->SetExclusive(this, Channels);\
		Tweener->SetEase(ease)->SetDelay(delay);\
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);\
	}\
	return Tweener;\
}

#define FunctionGradientAlphaAnimation(Property, Function, Channels)\
UDreamTweener* UDreamRectBlock::Function##AlphaTo(float endValue, float duration, float delay, EDreamTweenEase ease)\
{\
	auto Tweener =  UDreamTweenManager::To(this, FDreamTweenFloatGetterFunction::CreateWeakLambda(this, [this] {\
		return FDreamUIUtils::ByteToFloat01(this->Get##Property().A);\
		}), FDreamTweenFloatSetterFunction::CreateWeakLambda(this, [this](float value) {\
			FColor PropertyValue = this->Get##Property();\
			PropertyValue.A = DreamRectBlockAlphaTweenLocal::AlphaToByte(value);\
			this->Set##Property(PropertyValue);\
			}), endValue, duration);\
	if (Tweener)\
	{\
		Tweener->SetExclusive(this, Channels);\
		Tweener->SetEase(ease)->SetDelay(delay);\
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);\
	}\
	return Tweener;\
}

FunctionPropertyAnimation(CornerRadius, FVector4, Vector4, DreamRectBlockAlphaTweenLocal::Channel::CornerRadius);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FunctionGradientAnimation(BodyGradientColor, FColor, Color, DreamRectBlockAlphaTweenLocal::Channel::BodyGradientColor);
FunctionGradientAlphaAnimation(BodyGradientColor, BodyGradient, DreamRectBlockAlphaTweenLocal::Channel::BodyGradientColorA);
FunctionGradientAnimation(BodyGradientCenter, FVector2D, Vector2D, DreamRectBlockAlphaTweenLocal::Channel::BodyGradientCenter);
FunctionGradientAnimation(BodyGradientRadius, FVector2D, Vector2D, DreamRectBlockAlphaTweenLocal::Channel::BodyGradientRadius);
FunctionGradientAnimation(BodyGradientRotation, float, Float, DreamRectBlockAlphaTweenLocal::Channel::BodyGradientRotation);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

FunctionPropertyAnimation(BorderWidth, float, Float, DreamRectBlockAlphaTweenLocal::Channel::BorderWidth);
FunctionPropertyAnimation(BorderColor, FColor, Color, DreamRectBlockAlphaTweenLocal::Channel::BorderColor);
FunctionAlphaAnimation(BorderColor, Border, DreamRectBlockAlphaTweenLocal::Channel::BorderColorA);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
FunctionGradientAnimation(BorderGradientColor, FColor, Color, DreamRectBlockAlphaTweenLocal::Channel::BorderGradientColor);
FunctionGradientAlphaAnimation(BorderGradientColor, BorderGradient, DreamRectBlockAlphaTweenLocal::Channel::BorderGradientColorA);
FunctionGradientAnimation(BorderGradientCenter, FVector2D, Vector2D, DreamRectBlockAlphaTweenLocal::Channel::BorderGradientCenter);
FunctionGradientAnimation(BorderGradientRadius, FVector2D, Vector2D, DreamRectBlockAlphaTweenLocal::Channel::BorderGradientRadius);
FunctionGradientAnimation(BorderGradientRotation, float, Float, DreamRectBlockAlphaTweenLocal::Channel::BorderGradientRotation);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

FunctionPropertyAnimation(InnerShadowColor, FColor, Color, DreamRectBlockAlphaTweenLocal::Channel::InnerShadowColor);
FunctionAlphaAnimation(InnerShadowColor, InnerShadow, DreamRectBlockAlphaTweenLocal::Channel::InnerShadowColorA);
FunctionPropertyAnimation(InnerShadowSize, float, Float, DreamRectBlockAlphaTweenLocal::Channel::InnerShadowSize);
FunctionPropertyAnimation(InnerShadowBlur, float, Float, DreamRectBlockAlphaTweenLocal::Channel::InnerShadowBlur);
FunctionPropertyAnimation(InnerShadowAngle, float, Float, DreamRectBlockAlphaTweenLocal::Channel::InnerShadowAngle);
FunctionPropertyAnimation(InnerShadowDistance, float, Float, DreamRectBlockAlphaTweenLocal::Channel::InnerShadowDistance);

FunctionPropertyAnimation(RadialFillCenter, FVector2D, Vector2D, DreamRectBlockAlphaTweenLocal::Channel::RadialFillCenter);
FunctionPropertyAnimation(RadialFillRotation, float, Float, DreamRectBlockAlphaTweenLocal::Channel::RadialFillRotation);
FunctionPropertyAnimation(RadialFillAngle, float, Float, DreamRectBlockAlphaTweenLocal::Channel::RadialFillAngle);

FunctionPropertyAnimation(OuterShadowColor, FColor, Color, DreamRectBlockAlphaTweenLocal::Channel::OuterShadowColor);
FunctionAlphaAnimation(OuterShadowColor, OuterShadow, DreamRectBlockAlphaTweenLocal::Channel::OuterShadowColorA);
FunctionPropertyAnimation(OuterShadowSize, float, Float, DreamRectBlockAlphaTweenLocal::Channel::OuterShadowSize);
FunctionPropertyAnimation(OuterShadowBlur, float, Float, DreamRectBlockAlphaTweenLocal::Channel::OuterShadowBlur);
FunctionPropertyAnimation(OuterShadowAngle, float, Float, DreamRectBlockAlphaTweenLocal::Channel::OuterShadowAngle);
FunctionPropertyAnimation(OuterShadowDistance, float, Float, DreamRectBlockAlphaTweenLocal::Channel::OuterShadowDistance);

#pragma endregion

#undef LOCTEXT_NAMESPACE



// HEADLESS NOTE, kept with the class that has it: OnRegister loads RectBlockData from
// UDreamGUISettings. A commandlet whose project settings do not carry DefaultRectBlockData used to
// assert here rather than report a diagnostic, with the .dui nowhere in the callstack; it now logs
// the failure and the rect block draws with the canvas default material instead.
DECLARE_DREAM_GUI_VISUAL("RectBlock", UDreamRectBlock)
