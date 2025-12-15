// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexVisual.h"
#include "LGUI.h"
#include "Core/Components/LexCanvas.h"
#include "Utils/LexUIUtils.h"
#include "LGUI/Public/MeshModifier/LexMeshModifierBase.h"
#include "Core/Components/LexVisualBatchMesh.h"
#include "TextureResource.h"
#include "Core/LexUIClipData.h"
#include "Core/LexUIDataAsTexture.h"
#include "Engine/Texture2D.h"



bool ULexVisualCustomRaycast::Raycast(const ULexVisual* InVisual, const FVector& InLocalSpaceRayStart, const FVector& InLocalSpaceRayEnd, FVector& OutHitPoint, FVector& OutHitNormal)const
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveRaycast(InVisual, InLocalSpaceRayStart, InLocalSpaceRayEnd, OutHitPoint, OutHitNormal);
	}
	return false;
}

bool ULexVisualCustomRaycast::GetRaycastPixelFromUIBatchMeshVisual(const ULexVisualBatchMesh* InVisual, const FVector& InLocalSpaceRayStart, const FVector& InLocalSpaceRayEnd, FVector2D& OutUV, FColor& OutPixel, FVector& OutHitPoint, FVector& OutHitNormal)
{
	if (auto UIGeo = InVisual->GetGeometry())
	{
		//triangle hit test
		auto& originVertices = UIGeo->OriginVertices;
		auto& vertices = UIGeo->Vertices;
		auto& triangleIndices = UIGeo->Triangles;
		const int triangleCount = triangleIndices.Num() / 3;
		int index = 0;
		for (int i = 0; i < triangleCount; i++)
		{
			auto vertIndex0 = triangleIndices[index++];
			auto vertIndex1 = triangleIndices[index++];
			auto vertIndex2 = triangleIndices[index++];
			auto point0 = (FVector)(originVertices[vertIndex0].Position);
			auto point1 = (FVector)(originVertices[vertIndex1].Position);
			auto point2 = (FVector)(originVertices[vertIndex2].Position);
			if (FMath::SegmentTriangleIntersection(InLocalSpaceRayStart, InLocalSpaceRayEnd, point0, point1, point2, OutHitPoint, OutHitNormal))
			{
				auto baryCentric = FMath::ComputeBaryCentric2D(OutHitPoint, point0, point1, point2);
				auto& uv0 = vertices[vertIndex0].TextureCoordinate[0];
				auto& uv1 = vertices[vertIndex1].TextureCoordinate[0];
				auto& uv2 = vertices[vertIndex2].TextureCoordinate[0];
				OutUV = FVector2D(baryCentric.X * uv0 + baryCentric.Y * uv1 + baryCentric.Z * uv2);
				//get pixel
				if (auto Texture2D = Cast<UTexture2D>(UIGeo->Texture.Get()))
				{
					auto PlatformData = Texture2D->GetPlatformData();
					if (PlatformData && PlatformData->Mips.Num() > 0)
					{
						auto TexPosX = (int)(OutUV.X * PlatformData->SizeX);
						auto TexPosY = (int)(OutUV.Y * PlatformData->SizeY);
						auto TexPos = TexPosX + TexPosY * PlatformData->SizeX;

						if (auto Pixels = (FColor*)(PlatformData->Mips[0].BulkData.Lock(LOCK_READ_ONLY)))
						{
							OutPixel = Pixels[TexPos];
						}
						PlatformData->Mips[0].BulkData.Unlock();

						return true;
					}
				}
			}
		}
	}
	return false;
}


ULexVisual::ULexVisual(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	VisualType = ELexVisualType::None;

	bColorChanged = true;
	bTransformChanged = true;
}

void ULexVisual::BeginPlay()
{
	bColorChanged = true;
	bTransformChanged = true;
}

void ULexVisual::BeginDestroy()
{
	Super::BeginDestroy();
	if (auto Widget = GetWidget())
	{
		Widget->RemoveVisual();
	}
}

#if WITH_EDITOR
void ULexVisual::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (this != GetDefault<ULexVisual>())
	{
		MarkAllDirty();
	}
}

bool ULexVisual::CanEditChange(const FProperty* InProperty) const
{
	auto PropertyName = InProperty->GetFName();
	static auto RayCastType_Name = GET_MEMBER_NAME_CHECKED(ULexVisual, RaycastType);
	static auto CustomRaycastObject_Name = GET_MEMBER_NAME_CHECKED(ULexVisual, CustomRaycastObject);
	static auto VisiblePixelThreshold_Name = GET_MEMBER_NAME_CHECKED(ULexVisual, VisiblePixelThreshold);
	if (PropertyName == RayCastType_Name)
	{
		if (this != GetDefault<ULexVisual>())
		{
			if (!GetWidget()->GetRaycastableInHierarchy())
			{
				return false;
			}
		}
	}
	else if (PropertyName == CustomRaycastObject_Name)
	{
		if (this != GetDefault<ULexVisual>())
		{
			if (!GetWidget()->GetRaycastableInHierarchy() || RaycastType!=ELexVisualRaycastType::Custom)
			{
				return false;
			}
		}
	}
	else if (PropertyName == VisiblePixelThreshold_Name)
	{
		if (this != GetDefault<ULexVisual>())
		{
			if (!GetWidget()->GetRaycastableInHierarchy() || RaycastType!=ELexVisualRaycastType::VisiblePixel)
			{
				return false;
			}
		}
	}
	return UObject::CanEditChange(InProperty);
}
#endif

int ULexVisual::GetClipDataStartPosition() const
{
	if (auto ClipData = GetWidget()->GetClipData().Pin())
	{
		return ClipData->GetBufferStartPos();
	}
	return 0;
}

UTexture* ULexVisual::GetClipDataTexture() const
{
	if (auto RenderCanvas = GetWidget()->GetRenderCanvas())
	{
		return RenderCanvas->GetClipDataTexture();
	}
	return nullptr;
}

void ULexVisual::OnTransformChanged()
{
	bTransformChanged = true;
	GetWidget()->MarkCanvasUpdate(false, true, false);
}

void ULexVisual::MarkColorDirty()
{
	bColorChanged = true;
	GetWidget()->MarkCanvasUpdate(false, false, false);
}

void ULexVisual::CheckClipDataStartPosition()
{
	auto NowClipDataStartPosition = GetClipDataStartPosition();
	if (ClipDataStartPosition != NowClipDataStartPosition)
	{
		ClipDataStartPosition = NowClipDataStartPosition;
		bClipDataPositionChanged = true;
	}
}

void ULexVisual::UpdateGeometryWidgetPropertyData(FLexUIGeometry& InMesh, int InDataStartPosition)
{
	auto& vertices = InMesh.Vertices;
	for (int i = 0; i < vertices.Num(); i++)
	{
		vertices[i].TextureCoordinate[1].X = InDataStartPosition;
	}
}

void ULexVisual::MarkAllDirty()
{
	bColorChanged = true;
	bTransformChanged = true;
	bClipDataPositionChanged = true;
	GetWidget()->MarkCanvasUpdate(false, true, false);
}

void ULexVisual::GetGeometryBoundsInLocalSpace(FVector2D& OutMinPoint, FVector2D& OutMaxPoint)const
{
	auto Widget = GetWidget();
	OutMinPoint = Widget->GetLocalSpaceLeftBottomPoint();
	OutMaxPoint = Widget->GetLocalSpaceRightTopPoint();
}

void ULexVisual::GetGeometryBounds3DInLocalSpace(FVector& OutMinPoint, FVector& OutMaxPoint)const
{
	auto Widget = GetWidget();
	const auto MinPoint2D = Widget->GetLocalSpaceLeftBottomPoint();
	const auto MaxPoint2D = Widget->GetLocalSpaceRightTopPoint();
	OutMinPoint = FVector(0.1f, MinPoint2D.X, MinPoint2D.Y);
	OutMaxPoint = FVector(0.1f, MaxPoint2D.X, MaxPoint2D.Y);
}

bool ULexVisual::LineTraceUIRect(FHitResult& OutHit, const FVector& Start, const FVector& End)const
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
			return true;
		}
	}
	return false;
}
bool ULexVisual::LineTraceUIGeometry(FLexUIGeometry* InGeo, FHitResult& OutHit, const FVector& Start, const FVector& End)const
{
	auto Widget = GetWidget();
	const auto InverseTf = Widget->GetComponentTransform().Inverse();
	const auto LocalSpaceRayOrigin = InverseTf.TransformPosition(Start);
	const auto LocalSpaceRayEnd = InverseTf.TransformPosition(End);

	//DrawDebugLine(this->GetWorld(), Start, End, FColor::Red, false, 5.0f);//just for test
	//check Line-Plane intersection first, then check Line-Triangle
	//start and end point must be different side of X plane
	if (FMath::Sign(LocalSpaceRayOrigin.X) != FMath::Sign(LocalSpaceRayEnd.X))
	{
		//triangle hit test
		auto& vertices = InGeo->OriginVertices;
		auto& triangleIndices = InGeo->Triangles;
		const int triangleCount = triangleIndices.Num() / 3;
		int index = 0;
		for (int i = 0; i < triangleCount; i++)
		{
			auto point0 = (FVector)(vertices[triangleIndices[index++]].Position);
			auto point1 = (FVector)(vertices[triangleIndices[index++]].Position);
			auto point2 = (FVector)(vertices[triangleIndices[index++]].Position);
			FVector HitPoint, HitNormal;
			if (FMath::SegmentTriangleIntersection(LocalSpaceRayOrigin, LocalSpaceRayEnd, point0, point1, point2, HitPoint, HitNormal))
			{
				OutHit.TraceStart = Start;
				OutHit.TraceEnd = End;
				OutHit.Component = (UPrimitiveComponent*)Widget;//actually this convert is incorrect, but I need this pointer
				OutHit.Location = Widget->GetComponentTransform().TransformPosition(HitPoint);
				OutHit.Normal = Widget->GetComponentTransform().TransformVector(HitNormal);
				OutHit.Normal.Normalize();
				OutHit.Distance = FVector::Distance(Start, OutHit.Location);
				OutHit.ImpactPoint = OutHit.Location;
				OutHit.ImpactNormal = OutHit.Normal;
				OutHit.FaceIndex = i;
				return true;
			}
		}
	}
	return false;
}

bool ULexVisual::LineTraceUICustom(FHitResult& OutHit, const FVector& Start, const FVector& End)const
{
	if (!IsValid(CustomRaycastObject))
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d EUIRenderableRaycastType::Custom need a UUIRenderableCustomRaycast component on this actor!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return false;
	}
	auto Widget = GetWidget();
	const auto InverseTf = Widget->GetComponentTransform().Inverse();
	const auto LocalSpaceRayOrigin = InverseTf.TransformPosition(Start);
	const auto LocalSpaceRayEnd = InverseTf.TransformPosition(End);

	if (FMath::Sign(LocalSpaceRayOrigin.X) != FMath::Sign(LocalSpaceRayEnd.X))
	{
		FVector HitPoint, HitNormal;
		if (CustomRaycastObject->Raycast(this, LocalSpaceRayOrigin, LocalSpaceRayEnd, HitPoint, HitNormal))
		{
			OutHit.TraceStart = Start;
			OutHit.TraceEnd = End;
			OutHit.Component = (UPrimitiveComponent*)Widget;//actually this convert is incorrect, but I need this pointer
			OutHit.Location = Widget->GetComponentTransform().TransformPosition(HitPoint);
			OutHit.Normal = Widget->GetComponentTransform().TransformVector(HitNormal);
			OutHit.Normal.Normalize();
			OutHit.Distance = FVector::Distance(Start, OutHit.Location);
			OutHit.ImpactPoint = OutHit.Location;
			OutHit.ImpactNormal = OutHit.Normal;
			return true;
		}
	}
	return false;
}

void ULexVisual::SetColor(FColor Value)
{
	if (Color != Value)
	{
		Color = Value;
		MarkColorDirty();
	}
}
void ULexVisual::SetAlpha(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	auto uintAlpha = (uint8)(Value * 255);
	if (Color.A != uintAlpha)
	{
		MarkColorDirty();
		Color.A = uintAlpha;
	}
}

void ULexVisual::SetRaycastTarget(bool Value)
{
	bRaycastTarget = Value;
}

void ULexVisual::SetCustomRaycastObject(ULexVisualCustomRaycast* Value)
{
	CustomRaycastObject = Value;
}

FColor ULexVisual::GetFinalColor()const
{
	FColor Result = this->Color;
	Result.A = Result.A * GetWidget()->GetFinalRenderOpacity();
	return Result;
}

uint8 ULexVisual::GetFinalAlpha()const
{
	return Color.A * GetWidget()->GetFinalRenderOpacity();
}

float ULexVisual::GetFinalAlpha01()const
{
	return FLexUIUtils::Color255To1_Table[GetFinalAlpha()];
}

bool ULexVisual::LineTraceUI(FHitResult& OutHit, const FVector& Start, const FVector& End)const
{
	return LineTraceUIRect(OutHit, Start, End);
}

int ULexVisual::WidgetPropertyDataLength =
	sizeof(FVector2f)//1st pixel, x: byte1- font mark, byte2- extra marks; y: clip data coordinate
	+ sizeof(FVector2f)//1st pixel, zw: widget width & height
	+ sizeof(FVector2f)//2nd pixel, xy: widget rect center position
;
void ULexVisual::FillWidgetPropertyDataForMaterial(ULexVisual* Visual, uint8 FontMark)
{
	auto StartPosition = Visual->WidgetPropertyDataStartPosition;
	if (StartPosition <= INDEX_NONE)return;
	auto Widget = Visual->GetWidget();
	if (!Widget)return;
	auto Canvas = Widget->GetRenderCanvas();
	if (!Canvas)return;
	auto Data = Canvas->GetWidgetPropertyDataAsTexture();
	if (!Data)return;
	uint8* BlockBuffer = new uint8[WidgetPropertyDataLength];
	FMemory::Memzero(BlockBuffer, WidgetPropertyDataLength);
	int BlockBufferOffset = 0;
	
	{
		uint8 ExtraMark =
			(Canvas->IsRenderByLexUIRendererOrUERenderer() ? 1 : 0) << 7
		;
		uint32 Marks =
			FontMark << 24
			| ExtraMark << 16
		;
		FMemory::Memcpy(BlockBuffer + BlockBufferOffset, &Marks, 4);
		BlockBufferOffset += 4;
		uint32 ClipDataStartPosition = Visual->ClipDataStartPosition;
		FMemory::Memcpy(BlockBuffer + BlockBufferOffset, &ClipDataStartPosition, 4);
		BlockBufferOffset += 4;
	}
	
	//width & height
	auto Size = FVector2f(Widget->GetWidth(), Widget->GetHeight());
	FMemory::Memcpy(BlockBuffer + BlockBufferOffset, &Size, sizeof(FVector2f));
	BlockBufferOffset += sizeof(FVector2f);
	
	//widget rect center position in canvas space
	auto WidgetToWorldMatrix = Widget->GetComponentTransform().ToMatrixWithScale();
	auto WidgetLocalSpaceCenter = Widget->GetLocalSpaceCenter();
	auto CenterPositionInWorldSpace = WidgetToWorldMatrix.TransformPosition(FVector(0, WidgetLocalSpaceCenter.X, WidgetLocalSpaceCenter.Y));
	auto CenterPositionInCanvasSpace = Canvas->GetLexWidget()->GetComponentTransform().InverseTransformPosition(CenterPositionInWorldSpace);
	FMemory::Memcpy(BlockBuffer + BlockBufferOffset, &CenterPositionInCanvasSpace, sizeof(FVector2f));
	BlockBufferOffset += sizeof(FVector2f);
	
	Data->UpdateBlock(StartPosition, BlockBuffer);
}

void ULexVisual::FillWidgetPropertyDataForMaterial_FirstPixel(ULexVisual* Visual, uint8 FontMark)
{
	auto StartPosition = Visual->WidgetPropertyDataStartPosition;
	if (StartPosition <= INDEX_NONE)return;
	auto Widget = Visual->GetWidget();
	if (!Widget)return;
	auto Canvas = Widget->GetRenderCanvas();
	if (!Canvas)return;
	auto Data = Canvas->GetWidgetPropertyDataAsTexture();
	if (!Data)return;
	uint8* BlockBuffer = new uint8[WidgetPropertyDataLength];
	FMemory::Memzero(BlockBuffer, WidgetPropertyDataLength);
	int BlockBufferOffset = 0;
	
	{
		uint8 ExtraMark =
			(Canvas->IsRenderByLexUIRendererOrUERenderer() ? 1 : 0) << 7
		;
		uint32 Marks =
			FontMark << 24
			| ExtraMark << 16
		;
		FMemory::Memcpy(BlockBuffer + BlockBufferOffset, &Marks, 4);
		BlockBufferOffset += 4;
		uint32 ClipDataStartPosition = Visual->ClipDataStartPosition;
		FMemory::Memcpy(BlockBuffer + BlockBufferOffset, &ClipDataStartPosition, 4);
		BlockBufferOffset += 4;
	}
	
	//width & height
	auto Size = FVector2f(Widget->GetWidth(), Widget->GetHeight());
	FMemory::Memcpy(BlockBuffer + BlockBufferOffset, &Size, sizeof(FVector2f));
	BlockBufferOffset += sizeof(FVector2f);
	Data->UpdateBlock(0, StartPosition, BlockBuffer, 1);
}

#pragma region TweenAnimation
#include "LTweenManager.h"
#include "Core/LexUISettings.h"
ULTweener* ULexVisual::ColorTo(FColor endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenColorGetterFunction::CreateUObject(this, &ULexVisual::GetColor), FLTweenColorSetterFunction::CreateUObject(this, &ULexVisual::SetColor), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
ULTweener* ULexVisual::ColorFrom(FColor startValue, float duration, float delay, ELTweenEase ease)
{
	auto endValue = this->GetColor();
	this->SetColor(startValue);
	auto Tweener = ULTweenManager::To(this, FLTweenColorGetterFunction::CreateUObject(this, &ULexVisual::GetColor), FLTweenColorSetterFunction::CreateUObject(this, &ULexVisual::SetColor), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}

ULTweener* ULexVisual::AlphaTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexVisual::GetAlpha), FLTweenFloatSetterFunction::CreateUObject(this, &ULexVisual::SetAlpha), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
ULTweener* ULexVisual::AlphaFrom(float startValue, float duration, float delay, ELTweenEase ease)
{
	auto endValue = this->GetAlpha();
	this->SetAlpha(startValue);
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexVisual::GetAlpha), FLTweenFloatSetterFunction::CreateUObject(this, &ULexVisual::SetAlpha), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
#pragma endregion


