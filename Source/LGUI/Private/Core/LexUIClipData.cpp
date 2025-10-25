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

void FLexUIClipData::Add2DTranslationToMatrix(FMatrix44d& Matrix, const FVector2d& Translation)
{
	auto M = &Matrix.M[0][0];
	M[13] += Translation.X;
	M[14] += Translation.Y;
}
void FLexUIClipData::UpdateData()
{
	if (!bNeedUpdateData)return;
	bNeedUpdateData = false;
	
	uint8* BlockBuffer = new uint8[BlockSizeInBytes];
	FMemory::Memzero(BlockBuffer, BlockSizeInBytes);
	int BlockDataOffset = 0;
	auto RenderCanvasWidget = this->GetWidget()->GetRenderCanvas()->GetLexWidget();
	auto CanvasToWorldMatrix = RenderCanvasWidget->GetComponentTransform().ToMatrixWithScale();
	//auto CanvasLocalSpaceCenter = RenderCanvasWidget->GetLocalSpaceCenter();
	//Add2DTranslationToMatrix(CanvasToWorldMatrix, CanvasLocalSpaceCenter);
	FLexUIClipData* TargetClip = this;
	for (int i = 0; i < InheritClipDepth; i++)
	{
		auto WidgetToWorldMatrix = TargetClip->Widget->GetComponentTransform().ToMatrixWithScale();
		auto WidgetLocalSpaceCenter = TargetClip->Widget->GetLocalSpaceCenter();
		Add2DTranslationToMatrix(WidgetToWorldMatrix, WidgetLocalSpaceCenter);
		auto WorldToWidgetMatrix = WidgetToWorldMatrix.Inverse();
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
		if (LocalPoint.Y < Widget->GetLocalSpaceLeft())return false;
		if (LocalPoint.Y > Widget->GetLocalSpaceRight())return false;
		if (LocalPoint.Z < Widget->GetLocalSpaceBottom())return false;
		if (LocalPoint.Z > Widget->GetLocalSpaceTop())return false;
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
	auto Radius = CornerRadius.X;
	auto CenterPos = FVector2D(InWidget->GetLocalSpaceRight() - Radius, InWidget->GetLocalSpaceBottom() + Radius);
	if (InLocalHitPoint.X > CenterPos.X && InLocalHitPoint.Y < CenterPos.Y)//right bottom area of rect
	{
		if (FVector2D::DistSquared(InLocalHitPoint, CenterPos) > Radius * Radius)
		{
			return false;
		}
		return true;
	}
	Radius = CornerRadius.Y;
	CenterPos = FVector2D(InWidget->GetLocalSpaceRight() - Radius, InWidget->GetLocalSpaceTop() - Radius);
	if (InLocalHitPoint.X > CenterPos.X && InLocalHitPoint.Y > CenterPos.Y)//right top area of rect
	{
		if (FVector2D::DistSquared(InLocalHitPoint, CenterPos) > Radius * Radius)
		{
			return false;
		}
		return true;
	}
	Radius = CornerRadius.Z;
	CenterPos = FVector2D(InWidget->GetLocalSpaceLeft() + Radius, InWidget->GetLocalSpaceTop() - Radius);
	if (InLocalHitPoint.X < CenterPos.X && InLocalHitPoint.Y > CenterPos.Y)//left top area of rect
	{
		if (FVector2D::DistSquared(InLocalHitPoint, CenterPos) > Radius * Radius)
		{
			return false;
		}
		return true;
	}
	Radius = CornerRadius.W;
	CenterPos = FVector2D(InWidget->GetLocalSpaceLeft() + Radius, InWidget->GetLocalSpaceBottom() + Radius);
	if (InLocalHitPoint.X < CenterPos.X && InLocalHitPoint.Y < CenterPos.Y)//left bottom area of rect
	{
		if (FVector2D::DistSquared(InLocalHitPoint, CenterPos) > Radius * Radius)
		{
			return false;
		}
		return true;
	}
	return true;
}


