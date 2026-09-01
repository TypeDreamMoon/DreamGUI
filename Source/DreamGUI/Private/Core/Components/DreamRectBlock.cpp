// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamRectBlock.h"
#include "Core/DreamGUISettings.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUISpriteInfo.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Core/Components/DreamTextureBase.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUISpriteData.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"


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

void UDreamRectBlock::FillData(uint8* Data, float width, float height)
{
	int DataOffset = 0;

	uint8 BoolAsByte = PackBoolToByte(bEnableBody, bEnableOuterShadow, bEnableBodyGradient, bEnableBorder, bEnableBorderGradient, bEnableInnerShadow, bEnableRadialFill, false);
	Fill8BytesToData(Data
		, BoolAsByte
		, static_cast<uint8>(BodyTextureScaleMode)
		// The third byte of the first pixel, which has been spare since this block was designed.
		// Forced back to Image when there is no plain texture to slice -- see GetBodySampledTexture.
		, static_cast<uint8>(GetBodySampledTexture() != nullptr
			? BodyTextureDrawMode : EDreamRectBlockTextureDrawMode::Image)
		, 0
		, DataOffset);

	FillVector2ToData(Data, FVector2f(width, height), DataOffset);

	FillVector4ToData(Data, GetValueWithUnitMode(CornerRadius, CornerRadiusUnitMode, width, height, 0.5f), DataOffset);
	FillColorToData(Data, BodyColor, DataOffset);
	FillVector2ToData(Data
		, (BodyTextureMode == EDreamRectBlockTextureMode::Sprite && IsValid(BodySpriteTexture)) ? FVector2f(BodySpriteTexture->GetSpriteInfo().GetUVCenter()) : FVector2f(0.5f, 0.5f)
		, DataOffset);

	FillColorToData(Data, BodyGradientColor, DataOffset);
	FillVector2ToData(Data, GetValueWithUnitMode(BodyGradientCenter, BodyGradientCenterUnitMode, width, height), DataOffset);
	FillVector2ToData(Data, GetValueWithUnitMode(BodyGradientRadius, BodyGradientRadiusUnitMode, width, height), DataOffset);
	FillFloatToData(Data, BodyGradientRotation, DataOffset);

	FillFloatToData(Data, GetValueWithUnitMode(BorderWidth, BorderWidthUnitMode, width, height, 0.5f), DataOffset);
	FillColorToData(Data, BorderColor, DataOffset);
	FillColorToData(Data, BorderGradientColor, DataOffset);
	FillVector2ToData(Data, GetValueWithUnitMode(BorderGradientCenter, BorderGradientCenterUnitMode, width, height), DataOffset);
	FillVector2ToData(Data, GetValueWithUnitMode(BorderGradientRadius, BorderGradientRadiusUnitMode, width, height), DataOffset);
	FillFloatToData(Data, BorderGradientRotation, DataOffset);

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
		+ 4//color
		+ 8//center
		+ 8//radius
		+ 4//rotation

		//border
		+ 4//width
		+ 4//color
		//border gradient
		+ 4//color
		+ 8//center
		+ 8//radius
		+ 4//rotation

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
		;
	return result;
}

void UDreamRectBlock::FillColorToData(uint8* Data, const FColor& InValue, int& InOutDataOffset)
{
	auto ColorUint = InValue.ToPackedRGBA();
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
void UDreamRectBlock::Fill8BytesToData(uint8* Data, uint8 InValue0, uint8 InValue1, uint8 InValue2, uint8 InValue3, int& InOutDataOffset)
{
	int ByteCount = 8;//actually data only cover 4 bytes, but we need extra 4 bytes to make decode easier (because data texture is 16bytes per pixel)
	uint32 DataAsUint =
		(InValue0 << 24)
		| (InValue1 << 16)
		| (InValue2 << 8)
		| (InValue3 << 0)
		;
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
		check(RectBlockData != nullptr);
	}
	RectBlockData->Init(DataCountInBytes(), EDreamUIDataAsTexturePixelFormat::R32G32B32A32, 32);
	DataStartPosition = RectBlockData->RegisterBuffer();
	OnDataTextureChangedDelegateHandle = RectBlockData->OnDataTextureChange.AddUObject(this, &UDreamRectBlock::OnDataTextureChanged);
#if WITH_EDITOR
	if (this->GetWorld() && this->GetWorld()->WorldType == EWorldType::Editor)
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
	RectBlockData->UnregisterBuffer(DataStartPosition);
	if (OnDataTextureChangedDelegateHandle.IsValid())
	{
		RectBlockData->OnDataTextureChange.Remove(OnDataTextureChangedDelegateHandle);
		OnDataTextureChangedDelegateHandle.Reset();
	}
#if WITH_EDITOR
	if (this->GetWorld() && this->GetWorld()->WorldType == EWorldType::Editor)
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
			else SetUnitChange(BodyGradientCenter)
			else SetUnitChange(BodyGradientRadius)

			else SetUnitChange(BorderWidth)
			else SetUnitChange(BorderGradientCenter)
			else SetUnitChange(BorderGradientRadius)

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
UMaterialInterface* UDreamRectBlock::GetMaterialToCreateGeometry()
{
	if (auto Result = Super::GetMaterialToCreateGeometry())
	{
		return Result;
	}
	else
	{
		check(RectBlockData);
		return RectBlockData->GetMaterial();
	}
}
void UDreamRectBlock::OnMaterialInstanceDynamicCreated(class UMaterialInstanceDynamic* mat) 
{
	mat->SetTextureParameterValue(DataTextureParameterName, RectBlockData->GetDataTexture());
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
	if (InLocalHitPoint.X > 0 && InLocalHitPoint.Y < 0)//right bottom area of rect
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
	else if (InLocalHitPoint.X > 0 && InLocalHitPoint.Y > 0)//right top area of rect
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
	else if (InLocalHitPoint.X < 0 && InLocalHitPoint.Y > 0)//left top area of rect
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
	else if (InLocalHitPoint.X < 0 && InLocalHitPoint.Y < 0)//left bottom area of rect
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
	return true;
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
		//hit point inside rect area
		if (result.Y > Widget->GetLocalSpaceLeft() && result.Y < Widget->GetLocalSpaceRight() && result.Z > Widget->GetLocalSpaceBottom() && result.Z < Widget->GetLocalSpaceTop())
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
			if (bRaycastSupportCornerRadius)
			{
				return LineTraceUI_CheckCornerRadius(FVector2D(result.Y, result.Z));
			}

			return true;
		}
	}
	return false;
}

void UDreamRectBlock::OnDataTextureChanged(class UTexture* Texture)
{
	UIGeometry->Texture = GetTextureToCreateGeometry();
	MarkVerticesDirty(false, true, true, false);
}

void UDreamRectBlock::OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = GetWidget();
	static FDreamUISpriteInfo SimpleRectSpriteData;
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
	);

	if (InTriangleChanged || InVertexPositionChanged || InVertexUVChanged || InVertexColorChanged)
	{
		auto& vertices = InGeo.Vertices;
		for (int i = 0; i < 4; i++)
		{
			vertices[i].TextureCoordinate[3].X = DataStartPosition;
		}
		bNeedUpdateBlockData = true;
	}

	if (bNeedUpdateBlockData)
	{
		bNeedUpdateBlockData = false;

		auto BlockSize = RectBlockData->GetBlockSizeInByte();
		// The RHI upload copies whole pixels -- BlockPixelCount * 16 bytes for the R32G32B32A32
		// format registered in OnRegister -- so an exactly BlockSize-d buffer (156 bytes = 9.75
		// pixels) is overread by the tail of the last pixel. Pad to pixel granularity, zeroed.
		auto BufferSize = Align(BlockSize, 16);
		TArray<uint8> BlockBuffer;
		BlockBuffer.SetNumUninitialized(BufferSize);
		FMemory::Memzero(BlockBuffer.GetData(), BufferSize);
		FillData(BlockBuffer.GetData(), Widget->GetWidth(), Widget->GetHeight());
		RectBlockData->UpdateBlock(DataStartPosition, MoveTemp(BlockBuffer));
	}
}

void UDreamRectBlock::ApplyAtlasTextureChange_Implementation()
{
	if (BodyTextureMode != EDreamRectBlockTextureMode::Sprite)return;
	check(BodySpriteTexture);
	UIGeometry->Texture = BodySpriteTexture->GetAtlasTexture();
	GetWidget()->MarkCanvasUpdate(true);
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
	this->BodyColor = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBodyTexture(UTexture* value)
{
	this->BodyTexture = value;
	if (this->BodyTexture == nullptr)
	{
		this->BodyTexture = FDreamUIUtils::GetDefaultWhiteTexture();
	}
	MarkTextureDirty();
}
void UDreamRectBlock::SetBodySpriteTexture(UDreamUISpriteData_BaseObject* value)
{
	if (this->BodySpriteTexture != value)
	{
		this->BodySpriteTexture = value;
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
	this->BodyTextureMode = value;
	MarkTextureDirty();
	MarkVertexUVDirty();
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
	this->BodyTextureScaleMode = value;
	MarkNeedUpdateBlockData();
}

void UDreamRectBlock::SetBodyTextureDrawMode(EDreamRectBlockTextureDrawMode value)
{
	this->BodyTextureDrawMode = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBodyTextureMargin(const FMargin& value)
{
	this->BodyTextureMargin = value;
	MarkNeedUpdateBlockData();
}

void UDreamRectBlock::SetEnableBodyGradient(bool value)
{
	this->bEnableBodyGradient = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBodyGradientColor(const FColor& value)
{
	this->BodyGradientColor = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBodyGradientCenter(const FVector2D& value)
{
	this->BodyGradientCenter = FVector2f(value);
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBodyGradientRadius(const FVector2D& value)
{
	this->BodyGradientRadius = FVector2f(value);
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBodyGradientRotation(float value)
{
	this->BodyGradientRotation = value;
	MarkNeedUpdateBlockData();
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
	this->BorderGradientColor = value;
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBorderGradientCenter(const FVector2D& value)
{
	this->BorderGradientCenter = FVector2f(value);
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBorderGradientRadius(const FVector2D& value)
{
	this->BorderGradientRadius = FVector2f(value);
	MarkNeedUpdateBlockData();
}
void UDreamRectBlock::SetBorderGradientRotation(float value)
{
	this->BorderGradientRotation = value;
	MarkNeedUpdateBlockData();
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
UDreamTweener* UDreamRectBlock::BodyColorTo(FColor endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenColorGetterFunction::CreateWeakLambda(this, [this] {
		return this->BodyColor;
		}), FDreamTweenColorSetterFunction::CreateWeakLambda(this, [this](FColor value) {
			this->SetBodyColor(value);
			}), endValue, duration);
	if (Tweener)
	{
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
			PropertyValue.A = static_cast<uint8>(value * 255.0f);
			this->SetBodyColor(PropertyValue);
			}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
	}
	return Tweener;
}

#define FunctionPropertyAnimation(Property, EndValueType, GetterAndSetterType)\
UDreamTweener* UDreamRectBlock::Property##To(EndValueType endValue, float duration, float delay, EDreamTweenEase ease)\
{\
	auto Tweener =  UDreamTweenManager::To(this, FDreamTween##GetterAndSetterType##GetterFunction::CreateWeakLambda(this, [this] {\
		return (EndValueType)this->Property;\
		}), FDreamTween##GetterAndSetterType##SetterFunction::CreateWeakLambda(this, [this](EndValueType value) {\
			this->Set##Property(value);\
			}), endValue, duration);\
	if (Tweener)\
	{\
		Tweener->SetEase(ease)->SetDelay(delay);\
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);\
	}\
	return Tweener;\
}

#define FunctionAlphaAnimation(Property, Function)\
UDreamTweener* UDreamRectBlock::Function##AlphaTo(float endValue, float duration, float delay, EDreamTweenEase ease)\
{\
	auto Tweener =  UDreamTweenManager::To(this, FDreamTweenFloatGetterFunction::CreateWeakLambda(this, [this] {\
		return FDreamUIUtils::ByteToFloat01(this->BodyColor.A);\
		}), FDreamTweenFloatSetterFunction::CreateWeakLambda(this, [this](float value) {\
			auto PropertyValue = this->Property;\
			PropertyValue.A = (uint8)(value * 255.0f);\
			this->Set##Property(PropertyValue);\
			}), endValue, duration);\
	if (Tweener)\
	{\
		Tweener->SetEase(ease)->SetDelay(delay);\
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);\
	}\
	return Tweener;\
}

FunctionPropertyAnimation(CornerRadius, FVector4, Vector4);

FunctionPropertyAnimation(BodyGradientColor, FColor, Color);
FunctionAlphaAnimation(BodyGradientColor, BodyGradient);
FunctionPropertyAnimation(BodyGradientCenter, FVector2D, Vector2D);
FunctionPropertyAnimation(BodyGradientRadius, FVector2D, Vector2D);
FunctionPropertyAnimation(BodyGradientRotation, float, Float);

FunctionPropertyAnimation(BorderWidth, float, Float);
FunctionPropertyAnimation(BorderColor, FColor, Color);
FunctionAlphaAnimation(BorderColor, Border);
FunctionPropertyAnimation(BorderGradientColor, FColor, Color);
FunctionAlphaAnimation(BorderGradientColor, BorderGradient);
FunctionPropertyAnimation(BorderGradientCenter, FVector2D, Vector2D);
FunctionPropertyAnimation(BorderGradientRadius, FVector2D, Vector2D);
FunctionPropertyAnimation(BorderGradientRotation, float, Float);

FunctionPropertyAnimation(InnerShadowColor, FColor, Color);
FunctionAlphaAnimation(InnerShadowColor, InnerShadow);
FunctionPropertyAnimation(InnerShadowSize, float, Float);
FunctionPropertyAnimation(InnerShadowBlur, float, Float);
FunctionPropertyAnimation(InnerShadowAngle, float, Float);
FunctionPropertyAnimation(InnerShadowDistance, float, Float);

FunctionPropertyAnimation(RadialFillCenter, FVector2D, Vector2D);
FunctionPropertyAnimation(RadialFillRotation, float, Float);
FunctionPropertyAnimation(RadialFillAngle, float, Float);

FunctionPropertyAnimation(OuterShadowColor, FColor, Color);
FunctionAlphaAnimation(OuterShadowColor, OuterShadow);
FunctionPropertyAnimation(OuterShadowSize, float, Float);
FunctionPropertyAnimation(OuterShadowBlur, float, Float);
FunctionPropertyAnimation(OuterShadowAngle, float, Float);
FunctionPropertyAnimation(OuterShadowDistance, float, Float);

#pragma endregion

#undef LOCTEXT_NAMESPACE



// HEADLESS HAZARD, kept with the class that has it: OnRegister checks RectBlockData, which it
// loads from UDreamGUISettings. A commandlet whose project settings do not carry
// DefaultRectBlockData asserts here rather than reporting a diagnostic, with the .dui nowhere
// in the callstack. The tag stays -- it is a real visual and authors want it.
DECLARE_DREAM_GUI_VISUAL("RectBlock", UDreamRectBlock)
