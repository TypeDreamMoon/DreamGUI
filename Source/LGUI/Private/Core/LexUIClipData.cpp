// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIClipData.h"

#include "Core/Components/LexWidget.h"
#include "Core/LexUIDataAsTexture.h"
#include "Core/Components/LexCanvas.h"

int FLexUIClipData::InheritClipDepth = 16;
int FLexUIClipData::SingleBlockSizeInBytes =
	sizeof(FMatrix44f)//canvas to clip object's space, last column of matrix: (half-width, half-height, isValid, softness)
	+ sizeof(FVector4f)//CornerRadius
	;
int FLexUIClipData::BlockSizeInBytes = SingleBlockSizeInBytes * FLexUIClipData::InheritClipDepth;

FLexUIClipData::FLexUIClipData(const TSharedPtr<FLexUIClipData>& InParent, ULexUIDataAsTexture* InDataTexture, ULexWidget* InWidget)
{
	this->Parent = InParent;
	this->DataTexture = InDataTexture;
	this->Widget = InWidget;
	this->BufferStartPos = this->DataTexture->RegisterBuffer();
}

FLexUIClipData::~FLexUIClipData()
{
	this->DataTexture->UnregisterBuffer(this->BufferStartPos);
}

void FLexUIClipData::UpdateData()
{
	if (!bNeedUpdateData)return;
	bNeedUpdateData = false;
	
	uint8* BlockBuffer = new uint8[BlockSizeInBytes];
	FMemory::Memzero(BlockBuffer, BlockSizeInBytes);
	int BlockDataOffset = 0;
	auto CanvasToWorldMatrix = this->GetWidget()->GetRenderCanvas()->GetLexWidget()->GetComponentTransform().ToMatrixWithScale();
	FLexUIClipData* TargetClip = this;
	for (int i = 0; i < InheritClipDepth; i++)
	{
		auto WorldToWidgetMatrix = TargetClip->Widget->GetComponentTransform().ToInverseMatrixWithScale();
		auto CanvasToWidgetMatrix = FMatrix44f(CanvasToWorldMatrix * WorldToWidgetMatrix);
		auto& M = CanvasToWidgetMatrix.M;
		auto RenderSize = FVector2f(TargetClip->Widget->GetWidth(), TargetClip->Widget->GetHeight());
		M[0][3] = RenderSize.X * 0.5f;//half width
		M[1][3] = RenderSize.Y * 0.5f;//half height
		M[2][3] = 0;//softness
		M[3][3] = 1;//isValid
		CanvasToWidgetMatrix = CanvasToWidgetMatrix.GetTransposed();//matrix in memory is aligned as row-primary, so transpose it then in hlsl we can read as column-primary
		FMemory::Memcpy(BlockBuffer + BlockDataOffset, &CanvasToWidgetMatrix, sizeof(FMatrix44f));
		BlockDataOffset += sizeof(FMatrix44f);
		auto CornerRadius = TargetClip->GetWidget()->GetClippingCornerRadius();
		CornerRadius = FVector4f(CornerRadius.Y, CornerRadius.X, CornerRadius.W, CornerRadius.Z);//flip vertical
		FMemory::Memcpy(BlockBuffer + BlockDataOffset, &CornerRadius, sizeof(FVector4f));
		BlockDataOffset += sizeof(FVector4f);
		if (!TargetClip->Parent.IsValid())
		{
			break;
		}
		TargetClip = TargetClip->Parent.Pin().Get();
	}
	DataTexture->UpdateBlock(BufferStartPos, BlockBuffer);
}

bool FLexUIClipData::IsPointVisible(const FVector& Point) const
{
	auto TargetClip = this;
	for (int i = 0; i < InheritClipDepth; i++)
	{
		auto TargetWidget = TargetClip->Widget;
		auto LocalPoint = TargetWidget->GetComponentTransform().InverseTransformPosition(Point);
		auto LocalSize = FVector2f(TargetWidget->GetWidth(), TargetWidget->GetHeight());
		auto HalfLocalSize = LocalSize * 0.5f;
		FVector2D clipRectMin, clipRectMax;
		clipRectMin.X = -HalfLocalSize.X;
		clipRectMin.Y = -HalfLocalSize.Y;
		clipRectMax.X = HalfLocalSize.X;
		clipRectMax.Y = HalfLocalSize.Y;
		//out of range
		if (LocalPoint.Y < clipRectMin.X) return false;
		if (LocalPoint.Z < clipRectMin.Y) return false;
		if (LocalPoint.Y > clipRectMax.X) return false;
		if (LocalPoint.Z > clipRectMax.Y) return false;
		if (!IsPointVisible_CheckCornerRadius(FVector2D(LocalPoint.Y, LocalPoint.Z), TargetWidget.Get()))
			return false;
		if (!TargetClip->Parent.IsValid())
		{
			break;
		}
		
		TargetClip = TargetClip->Parent.Pin().Get();
	}
	return true;
}

bool FLexUIClipData::IsPointVisible_CheckCornerRadius(const FVector2D& InLocalHitPoint, ULexWidget* InWidget) const
{
	auto CornerRadius = InWidget->GetClippingCornerRadius();
	if (CornerRadius.X <= 0 && CornerRadius.Y <= 0 && CornerRadius.Z <= 0 && CornerRadius.W <= 0)
		return true;
	auto HalfWidth = InWidget->GetWidth() * 0.5f;
	auto HalfHeight = InWidget->GetHeight() * 0.5f;
	auto MinSize = FMath::Min(HalfWidth, HalfHeight);
	CornerRadius.X = FMath::Min(CornerRadius.X, MinSize);
	CornerRadius.Y = FMath::Min(CornerRadius.Y, MinSize);
	CornerRadius.Z = FMath::Min(CornerRadius.Z, MinSize);
	CornerRadius.W = FMath::Min(CornerRadius.W, MinSize);
	if (InLocalHitPoint.X > 0 && InLocalHitPoint.Y < 0)//right bottom area of rect
	{
		auto Radius = CornerRadius.X;
		auto CenterPos = FVector2D(InWidget->GetLocalSpaceRight() - Radius, InWidget->GetLocalSpaceBottom() + Radius);
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
		auto Radius = CornerRadius.Y;
		auto CenterPos = FVector2D(InWidget->GetLocalSpaceRight() - Radius, InWidget->GetLocalSpaceTop() - Radius);
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
		auto Radius = CornerRadius.Z;
		auto CenterPos = FVector2D(InWidget->GetLocalSpaceLeft() + Radius, InWidget->GetLocalSpaceTop() - Radius);
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
		auto Radius = CornerRadius.W;
		auto CenterPos = FVector2D(InWidget->GetLocalSpaceLeft() + Radius, InWidget->GetLocalSpaceBottom() + Radius);
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
