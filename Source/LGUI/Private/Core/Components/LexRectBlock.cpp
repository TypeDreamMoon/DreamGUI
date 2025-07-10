// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUI/Public/Core/Components/LexRectBlock.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "LGUI/Public/Core/Components/LexCanvas.h"
#include "Core/LexUISpriteInfo.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Core/LexUIDrawCall.h"
#include "LGUI/Public/Core/Components/UITextureBase.h"
#include "Utils/LexUIUtils.h"
#include "Core/LexUISpriteData.h"
#include "Core/LexUISpriteData_BaseObject.h"

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_DISABLE_OPTIMIZATION_SHIP
#endif

#define LOCTEXT_NAMESPACE "UIProceduralRect"


void ULGUIProceduralRectData::PostInitProperties()
{
	Super::PostInitProperties();
}
UMaterialInterface* ULGUIProceduralRectData::GetMaterial()
{
	if (!DefaultMaterial)
	{
		DefaultMaterial = LoadObject<UMaterialInterface>(NULL, TEXT("/LGUI/Materials/LexUI_RectBlock"));;
	}
	return DefaultMaterial;
}


void ULexRectBlock::FillData(uint8* Data, float width, float height)
{
	int DataOffset = 0;

	uint8 BoolAsByte = PackBoolToByte(bEnableBody, bSoftEdge, bEnableBodyGradient, bEnableBorder, bEnableBorderGradient, bEnableInnerShadow, bEnableRadialFill, false);
	Fill4BytesToData(Data
		, BoolAsByte
		, (uint8)BodyTextureScaleMode
		, 0, 0
		, DataOffset);

	FillVector2ToData(Data, FVector2f(width, height), DataOffset);

	FillVector4ToData(Data, GetValueWithUnitMode(CornerRadius, CornerRadiusUnitMode, width, height, 0.5f), DataOffset);
	FillColorToData(Data, BodyColor, DataOffset);
	FillVector2ToData(Data
		, (BodyTextureMode == EUIProceduralBodyTextureMode::Sprite && IsValid(BodySpriteTexture)) ? FVector2f(BodySpriteTexture->GetSpriteInfo().GetUVCenter()) : FVector2f(0.5f, 0.5f)
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
}

float ULexRectBlock::GetValueWithUnitMode(float SourceValue, EUIProceduralRectUnitMode UnitMode, float RectWidth, float RectHeight, float AdditionalScale)const
{
	return UnitMode == EUIProceduralRectUnitMode::Value ? SourceValue : (SourceValue * 0.01f * (RectWidth < RectHeight ? RectWidth : RectHeight) * AdditionalScale);
}
FVector4f ULexRectBlock::GetValueWithUnitMode(const FVector4f& SourceValue, EUIProceduralRectUnitMode UnitMode, float RectWidth, float RectHeight, float AdditionalScale)const
{
	return UnitMode == EUIProceduralRectUnitMode::Value ? SourceValue : (SourceValue * 0.01f * (RectWidth < RectHeight ? RectWidth : RectHeight) * AdditionalScale);
}
FVector2f ULexRectBlock::GetValueWithUnitMode(const FVector2f& SourceValue, EUIProceduralRectUnitMode UnitMode, float RectWidth, float RectHeight)const
{
	return UnitMode == EUIProceduralRectUnitMode::Value ? SourceValue : (SourceValue * 0.01f * FVector2f(RectWidth, RectHeight));
}

FVector2f ULexRectBlock::GetInnerShadowOffset(float RectWidth, float RectHeight)
{
	float AngleRadian = FMath::DegreesToRadians(InnerShadowAngle + 90);
	float Sin = FMath::Sin(AngleRadian);
	float Cos = FMath::Cos(AngleRadian);
	float Distance = GetValueWithUnitMode(InnerShadowDistance, InnerShadowDistanceUnitMode, RectWidth, RectHeight, 0.5f);
	return FVector2f(-Sin, Cos) * Distance;
}
FVector2f ULexRectBlock::GetOuterShadowOffset(float RectWidth, float RectHeight)
{
	float AngleRadian = FMath::DegreesToRadians(OuterShadowAngle + 90);
	float Sin = FMath::Sin(AngleRadian);
	float Cos = FMath::Cos(AngleRadian);
	float Distance = GetValueWithUnitMode(OuterShadowDistance, OuterShadowDistanceUnitMode, RectWidth, RectHeight, 0.5f);
	return FVector2f(-Sin, Cos) * Distance;
}

constexpr int ULexRectBlock::DataCountInBytes()
{
	const int result =
		4//bool and enum

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
		;
	return result;
}

void ULexRectBlock::FillColorToData(uint8* Data, const FColor& InValue, int& InOutDataOffset)
{
	auto ColorUint = InValue.ToPackedRGBA();
	int ByteCount = 4;
	FMemory::Memcpy(Data + InOutDataOffset, &ColorUint, ByteCount);
	InOutDataOffset += ByteCount;
}
uint8 ULexRectBlock::PackBoolToByte(
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
	uint8 Result;
	Result =
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
void ULexRectBlock::Fill4BytesToData(uint8* Data, uint8 InValue0, uint8 InValue1, uint8 InValue2, uint8 InValue3, int& InOutDataOffset)
{
	int ByteCount = 4;
	uint32 DataAsUint =
		(InValue0 << 24)
		| (InValue1 << 16)
		| (InValue2 << 8)
		| (InValue3 << 0)
		;
	FMemory::Memcpy(Data + InOutDataOffset, &DataAsUint, ByteCount);
	InOutDataOffset += ByteCount;
}
void ULexRectBlock::FillFloatToData(uint8* Data, const float& InValue, int& InOutDataOffset)
{
	int ByteCount = 4;
	FMemory::Memcpy(Data + InOutDataOffset, &InValue, ByteCount);
	InOutDataOffset += ByteCount;
}
void ULexRectBlock::FillVector2ToData(uint8* Data, const FVector2f& InValue, int& InOutDataOffset)
{
	int ByteCount = 8;
	FMemory::Memcpy(Data + InOutDataOffset, &InValue, ByteCount);
	InOutDataOffset += ByteCount;
}
void ULexRectBlock::FillVector4ToData(uint8* Data, const FVector4f& InValue, int& InOutDataOffset)
{
	int ByteCount = 16;
	FMemory::Memcpy(Data + InOutDataOffset, &InValue, ByteCount);
	InOutDataOffset += ByteCount;
}
void ULexRectBlock::OnCornerRadiusUnitModeChanged(float width, float height)
{
	if (CornerRadiusUnitMode == EUIProceduralRectUnitMode::Value)//from percentage to value
	{
		CornerRadius = CornerRadius * 0.01f * (width < height ? width : height) * 0.5f;
	}
	else//from value to percentage
	{
		CornerRadius = CornerRadius * 100.0f / (width < height ? width : height) * 2.0f;
	}
}

FName ULexRectBlock::DataTextureParameterName = TEXT("LexUI_RectBlockDataTexture");

ULexRectBlock::ULexRectBlock(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	bNeedUpdateBlockData = true;
}

void ULexRectBlock::BeginPlay()
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
void ULexRectBlock::EndPlay()
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

void ULexRectBlock::OnRegister()
{
	Super::OnRegister();
	if (ProceduralRectData == nullptr)
	{
		ProceduralRectData = LoadObject<ULGUIProceduralRectData>(NULL, TEXT("/LGUI/DefaultProceduralRectData"));
		check(ProceduralRectData != nullptr);
	}
	ProceduralRectData->Init(DataCountInBytes());
	DataStartPosition = ProceduralRectData->RegisterBuffer();
	OnDataTextureChangedDelegateHandle = ProceduralRectData->OnDataTextureChange.AddUObject(this, &ULexRectBlock::OnDataTextureChanged);
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
void ULexRectBlock::OnUnregister()
{
	Super::OnUnregister();
	ProceduralRectData->UnregisterBuffer(DataStartPosition);
	if (OnDataTextureChangedDelegateHandle.IsValid())
	{
		ProceduralRectData->OnDataTextureChange.Remove(OnDataTextureChangedDelegateHandle);
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
void ULexRectBlock::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{

#define SetUnitChange(Property)\
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexRectBlock, Property##UnitMode))\
	{\
		this->On##Property##UnitModeChanged(GetWidget()->GetRenderWidth(), GetWidget()->GetRenderHeight());\
	}


	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
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

		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexRectBlock, BodyTextureMode))
		{
			MarkTextureDirty();
			MarkUVDirty();
		}
		
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexRectBlock, bUniformSetCornerRadius))
		{
			if (bUniformSetCornerRadius)
			{
				CornerRadius.Y = CornerRadius.Z = CornerRadius.W = CornerRadius.X;
			}
		}
	}
}

bool ULexRectBlock::CanEditChange(const FProperty* InProperty) const
{
	auto PropertyName = InProperty->GetFName();
	static auto RaycastSupportCornerRadius_Name = GET_MEMBER_NAME_CHECKED(ULexRectBlock, bRaycastSupportCornerRadius);
	if (PropertyName == RaycastSupportCornerRadius_Name)
	{
		if (!GetWidget()->IsVisibleForHitTest() || RaycastType != ELexVisualHitTestType::Rect)
		{
			return false;
		}
	}
	return Super::CanEditChange(InProperty);
}

void ULexRectBlock::OnPreChangeSpriteProperty()
{
	if (IsValid(BodySpriteTexture))
	{
		BodySpriteTexture->RemoveUISprite(this);
		bHasAddToSprite = false;
	}
}
void ULexRectBlock::OnPostChangeSpriteProperty()
{
	if (IsValid(BodySpriteTexture))
	{
		BodySpriteTexture->AddUISprite(this);
		bHasAddToSprite = true;
	}
}
#endif

void ULexRectBlock::OnBeforeCreateOrUpdateGeometry()
{
	
}

UTexture* ULexRectBlock::GetTextureToCreateGeometry()
{
	if (BodyTextureMode == EUIProceduralBodyTextureMode::Texture)
	{
		if (!IsValid(this->BodyTexture))
		{
			this->BodyTexture = UUITextureBase::GetDefaultWhiteTexture();
		}
		return this->BodyTexture;
	}
	else
	{
		if (!IsValid(BodySpriteTexture))
		{
			BodySpriteTexture = ULexUISpriteData::GetDefaultWhiteSolid();
		}
		if (IsValid(BodySpriteTexture) && IsValid(BodySpriteTexture->GetAtlasTexture()))
		{
			return BodySpriteTexture->GetAtlasTexture();
		}
	}
	return nullptr;
}
UMaterialInterface* ULexRectBlock::GetMaterialToCreateGeometry()
{
	if (auto Result = Super::GetMaterialToCreateGeometry())
	{
		return Result;
	}
	else
	{
		check(ProceduralRectData);
		return ProceduralRectData->GetMaterial();
	}
}
void ULexRectBlock::UpdateMaterialClipType()
{
	geometry->Material = GetMaterialToCreateGeometry();
	if (DrawCall.IsValid())
	{
		DrawCall->bMaterialChanged = true;
		DrawCall->bMaterialNeedToReassign = true;
		DrawCall->bNeedToUpdateVertex = true;
	}
}
void ULexRectBlock::OnMaterialInstanceDynamicCreated(class UMaterialInstanceDynamic* mat) 
{
	mat->SetTextureParameterValue(DataTextureParameterName, ProceduralRectData->GetDataTexture());
}

void ULexRectBlock::MarkAllDirty()
{
	Super::MarkAllDirty();
	bNeedUpdateBlockData = true;
}

bool ULexRectBlock::LineTraceUI_CheckCornerRadius(const FVector2D& InLocalHitPoint)const
{
	auto Widget = GetWidget();
	auto TempCornerRadius = GetValueWithUnitMode(CornerRadius, CornerRadiusUnitMode, Widget->GetRenderWidth(), Widget->GetRenderHeight(), 0.5f);
	auto HalfWidth = Widget->GetRenderWidth() * 0.5f;
	auto HalfHeight = Widget->GetRenderHeight() * 0.5f;
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
bool ULexRectBlock::LineTraceUIRect(FHitResult& OutHit, const FVector& Start, const FVector& End)const
{
	auto Widget = GetWidget();
	auto InverseTf = Widget->GetComponentTransform().Inverse();
	auto LocalSpaceRayOrigin = InverseTf.TransformPosition(Start);
	auto LocalSpaceRayEnd = InverseTf.TransformPosition(End);

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
			OutHit.Component = (UPrimitiveComponent*)Widget;//acturally this convert is incorrect, but I need this pointer
			OutHit.Location = Widget->GetComponentTransform().TransformPosition(result);
			OutHit.Normal = Widget->GetComponentTransform().TransformVector(FVector(1, 0, 0));
			OutHit.Normal.Normalize();
			OutHit.Distance = FVector::Distance(Start, OutHit.Location);
			OutHit.ImpactPoint = OutHit.Location;
			OutHit.ImpactNormal = OutHit.Normal;

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

void ULexRectBlock::OnDataTextureChanged(class UTexture* Texture)
{
	geometry->Texture = GetTextureToCreateGeometry();
	if (DrawCall.IsValid())
	{
		DrawCall->Texture = geometry->Texture;
		DrawCall->bTextureChanged = true;
		DrawCall->bNeedToUpdateVertex = true;
	}
	MarkVerticesDirty(false, true, true, false);
	GetWidget()->MarkCanvasUpdate(true, true, false);
}

void ULexRectBlock::OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = GetWidget();
	static FLexUISpriteInfo SimpleRectSpriteData;
	FLexUIGeometry::UpdateUIProceduralRectSimpleVertex(&InGeo
		, this->bEnableBody || this->bEnableBorder || this->bEnableInnerShadow
		, this->bEnableOuterShadow
		, this->GetOuterShadowOffset(Widget->GetRenderWidth(), Widget->GetRenderHeight())
		, this->GetValueWithUnitMode(OuterShadowSize, OuterShadowSizeUnitMode, Widget->GetRenderWidth(), Widget->GetRenderHeight(), 0.5f)
		, this->GetValueWithUnitMode(OuterShadowBlur, OuterShadowBlurUnitMode, Widget->GetRenderWidth(), Widget->GetRenderHeight(), 1)
		, this->bSoftEdge,
		Widget->GetRenderWidth(), Widget->GetRenderHeight(), FVector2f(Widget->GetPivot())
		, SimpleRectSpriteData, IsValid(BodySpriteTexture) ? BodySpriteTexture->GetSpriteInfo() : SimpleRectSpriteData
		, Widget->GetRenderCanvas(), this, GetFinalColor(),
		InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
	);

	if (InTriangleChanged || InVertexPositionChanged || InVertexUVChanged || InVertexColorChanged)
	{
		auto& vertices = InGeo.Vertices;
		if (this->bEnableOuterShadow)
		{
			for (int i = 0; i < 8; i++)
			{
				vertices[i].TextureCoordinate[1].Y = DataStartPosition;
			}
			for (int i = 0; i < 4; i++)
			{
				vertices[i].TextureCoordinate[2] = FVector2f(1, 0);
			}
		}
		else
		{
			for (int i = 0; i < 4; i++)
			{
				vertices[i].TextureCoordinate[1].Y = DataStartPosition;
				vertices[i].TextureCoordinate[2] = FVector2f(0, 0);
			}
		}
		bNeedUpdateBlockData = true;
	}

	if (bNeedUpdateBlockData)
	{
		bNeedUpdateBlockData = false;

		auto BlockSize = ProceduralRectData->GetBlockSizeInByte();
		uint8* BlockBuffer = new uint8[BlockSize];
		FMemory::Memzero(BlockBuffer, BlockSize);
		FillData(BlockBuffer, Widget->GetRenderWidth(), Widget->GetRenderHeight());
		ProceduralRectData->UpdateBlock(DataStartPosition, BlockBuffer);
	}
}

void ULexRectBlock::ApplyAtlasTextureChange_Implementation()
{
	if (BodyTextureMode != EUIProceduralBodyTextureMode::Sprite)return;
	check(BodySpriteTexture);
	geometry->Texture = BodySpriteTexture->GetAtlasTexture();
	if (DrawCall.IsValid())
	{
		DrawCall->Texture = geometry->Texture;
		DrawCall->bTextureChanged = true;
	}
	GetWidget()->MarkCanvasUpdate(true, true, false);
}
void ULexRectBlock::ApplyAtlasTextureScaleUp_Implementation()
{
	if (BodyTextureMode != EUIProceduralBodyTextureMode::Sprite)return;
	check(BodySpriteTexture);
	auto& vertices = geometry->Vertices;
	if (vertices.Num() != 0)
	{
		for (int i = 0; i < vertices.Num(); i++)
		{
			auto& uv = vertices[i];
			uv.TextureCoordinate[0].X *= 0.5f;
			uv.TextureCoordinate[0].Y *= 0.5f;
		}
	}
	geometry->Texture = BodySpriteTexture->GetAtlasTexture();
	if (DrawCall.IsValid())
	{
		DrawCall->Texture = geometry->Texture;
		DrawCall->bTextureChanged = true;
		DrawCall->bNeedToUpdateVertex = true;
	}
	MarkVerticesDirty(false, true, true, false);
	GetWidget()->MarkCanvasUpdate(true, true, false);
}

void ULexRectBlock::SetCornerRadius(const FVector4& value)
{
	this->CornerRadius = (FVector4f)value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetEnableBody(bool value)
{
	this->bEnableBody = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void ULexRectBlock::SetBodyColor(const FColor& value)
{
	this->BodyColor = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetBodyTexture(UTexture* value)
{
	this->BodyTexture = value;
	if (this->BodyTexture == nullptr)
	{
		this->BodyTexture = UUITextureBase::GetDefaultWhiteTexture();
	}
	MarkTextureDirty();
}
void ULexRectBlock::SetBodySpriteTexture(ULexUISpriteData_BaseObject* value)
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
		MarkUVDirty();
	}
}
void ULexRectBlock::SetBodyTextureMode(EUIProceduralBodyTextureMode value)
{
	this->BodyTextureMode = value;
	MarkTextureDirty();
	MarkUVDirty();
}
void ULexRectBlock::SetSizeFromBodyTexture()
{
	if (BodyTextureMode == EUIProceduralBodyTextureMode::Sprite)
	{
		if (IsValid(this->BodySpriteTexture))
		{
			GetWidget()->SetSize(FLexWidgetSize2::MakeFixed(FVector2f(this->BodySpriteTexture->GetSpriteInfo().GetSourceWidth(), this->BodySpriteTexture->GetSpriteInfo().GetSourceHeight())));
		}
		else
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d Sprite is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		}
	}
	else
	{
		if (IsValid(this->BodyTexture))
		{
			GetWidget()->SetSize(FLexWidgetSize2::MakeFixed(FVector2f(this->BodyTexture->GetSurfaceWidth(), this->BodyTexture->GetSurfaceHeight())));
		}
		else
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d Texture is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		}
	}
}
void ULexRectBlock::SetSoftEdge(bool value)
{
	this->bSoftEdge = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void ULexRectBlock::SetBodyTextureScaleMode(EUIProceduralRectTextureScaleMode value)
{
	this->BodyTextureScaleMode = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}

void ULexRectBlock::SetEnableBodyGradient(bool value)
{
	this->bEnableBodyGradient = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetBodyGradientColor(const FColor& value)
{
	this->BodyGradientColor = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetBodyGradientCenter(const FVector2D& value)
{
	this->BodyGradientCenter = (FVector2f)value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetBodyGradientRadius(const FVector2D& value)
{
	this->BodyGradientRadius = (FVector2f)value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetBodyGradientRotation(float value)
{
	this->BodyGradientRotation = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}

void ULexRectBlock::SetEnableBorder(bool value)
{
	this->bEnableBorder = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void ULexRectBlock::SetBorderWidth(float value)
{
	this->BorderWidth = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetBorderColor(const FColor& value)
{
	this->BorderColor = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetEnableBorderGradient(bool value)
{
	this->bEnableBorderGradient = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetBorderGradientColor(const FColor& value)
{
	this->BorderGradientColor = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetBorderGradientCenter(const FVector2D& value)
{
	this->BorderGradientCenter = (FVector2f)value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetBorderGradientRadius(const FVector2D& value)
{
	this->BorderGradientRadius = (FVector2f)value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetBorderGradientRotation(float value)
{
	this->BorderGradientRotation = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}

void ULexRectBlock::SetEnableInnerShadow(bool value)
{
	this->bEnableInnerShadow = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void ULexRectBlock::SetInnerShadowColor(const FColor& value)
{
	this->InnerShadowColor = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetInnerShadowSize(float value)
{
	this->InnerShadowSize = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetInnerShadowBlur(float value)
{
	this->InnerShadowBlur = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetInnerShadowAngle(float value)
{
	this->InnerShadowAngle = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void ULexRectBlock::SetInnerShadowDistance(float value)
{
	this->InnerShadowDistance = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}

void ULexRectBlock::SetEnableRadialFill(bool value)
{
	this->bEnableRadialFill = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetRadialFillCenter(const FVector2D& value)
{
	this->RadialFillCenter = (FVector2f)value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetRadialFillRotation(float value)
{
	this->RadialFillRotation = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void ULexRectBlock::SetRadialFillAngle(float value)
{
	this->RadialFillAngle = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}

void ULexRectBlock::SetEnableOuterShadow(bool value)
{
	this->bEnableOuterShadow = value;
	MarkAllDirty();
}
void ULexRectBlock::SetOuterShadowColor(const FColor& value)
{
	this->OuterShadowColor = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetOuterShadowSize(float value)
{
	this->OuterShadowSize = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetOuterShadowBlur(float value)
{
	this->OuterShadowBlur = value;
	bNeedUpdateBlockData = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, false);
}
void ULexRectBlock::SetOuterShadowAngle(float value)
{
	this->OuterShadowAngle = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}
void ULexRectBlock::SetOuterShadowDistance(float value)
{
	this->OuterShadowDistance = value;
	bNeedUpdateBlockData = true;
	MarkVertexPositionDirty();
}

#define FunctionSetPropertyUnitMode(Property)\
void ULexRectBlock::Set##Property##UnitMode(EUIProceduralRectUnitMode value)\
{\
	this->Property##UnitMode = value;\
	bNeedUpdateBlockData = true;\
	GetWidget()->MarkCanvasUpdate(false, false, false, false);\
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

void ULexRectBlock::SetRaycastSupportCornerRadius(bool value)
{
	bRaycastSupportCornerRadius = value;
}

#pragma region TweenAnimation
#include "LTweenManager.h"
#include "Core/LGUISettings.h"
ULTweener* ULexRectBlock::BodyColorTo(FColor endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenColorGetterFunction::CreateWeakLambda(this, [this] {
		return this->BodyColor;
		}), FLTweenColorSetterFunction::CreateWeakLambda(this, [this](FColor value) {
			this->SetBodyColor(value);
			}), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
ULTweener* ULexRectBlock::BodyAlphaTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateWeakLambda(this, [this] {
		return FLexUIUtils::Color255To1_Table[this->BodyColor.A];
		}), FLTweenFloatSetterFunction::CreateWeakLambda(this, [this](float value) {
			auto PropertyValue = this->BodyColor;
			PropertyValue.A = (uint8)(value * 255.0f);
			this->SetBodyColor(PropertyValue);
			}), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}

#define FunctionPropertyAnimation(Property, EndValueType, GetterAndSetterType)\
ULTweener* ULexRectBlock::Property##To(EndValueType endValue, float duration, float delay, ELTweenEase ease)\
{\
	auto Tweener =  ULTweenManager::To(this, FLTween##GetterAndSetterType##GetterFunction::CreateWeakLambda(this, [this] {\
		return (EndValueType)this->Property;\
		}), FLTween##GetterAndSetterType##SetterFunction::CreateWeakLambda(this, [this](EndValueType value) {\
			this->Set##Property(value);\
			}), endValue, duration);\
	if (Tweener)\
	{\
		bool bAffectByGamePause;\
		bool bAffectByTimeDilation;\
		if (GetWidget()->IsScreenSpaceOverlayUI())\
		{\
			bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;\
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;\
		}\
		else\
		{\
			bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;\
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;\
		}\
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);\
	}\
	return Tweener;\
}

#define FunctionAlphaAnimation(Property, Function)\
ULTweener* ULexRectBlock::Function##AlphaTo(float endValue, float duration, float delay, ELTweenEase ease)\
{\
	auto Tweener =  ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateWeakLambda(this, [this] {\
		return FLexUIUtils::Color255To1_Table[this->BodyColor.A];\
		}), FLTweenFloatSetterFunction::CreateWeakLambda(this, [this](float value) {\
			auto PropertyValue = this->Property;\
			PropertyValue.A = (uint8)(value * 255.0f);\
			this->Set##Property(PropertyValue);\
			}), endValue, duration);\
	if (Tweener)\
	{\
		bool bAffectByGamePause;\
		bool bAffectByTimeDilation;\
		if (GetWidget()->IsScreenSpaceOverlayUI())\
		{\
			bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;\
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;\
		}\
		else\
		{\
			bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;\
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;\
		}\
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);\
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

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif
