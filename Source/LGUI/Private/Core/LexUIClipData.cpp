// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIClipData.h"

#include "Core/Components/LexWidget.h"
#include "Core/LexUIDataAsTexture.h"
#include "Core/Components/LexCanvas.h"

int FLexUIClipData::InheritClipDepth = 16;
int FLexUIClipData::SingleBlockSizeInBytes =
	sizeof(FMatrix44f)//canvas to clip object's space, last column of matrix: (half-width, half-height, isValid, 1)
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
		auto Column3 = CanvasToWidgetMatrix.GetColumn(3);
		auto RenderSize = FVector2f(TargetClip->Widget->GetWidth(), TargetClip->Widget->GetHeight());
		Column3.X = RenderSize.X * 0.5f;
		Column3.Y = RenderSize.Y * 0.5f;
		Column3.Z = 1;
		CanvasToWidgetMatrix.SetColumn(3, Column3);
		CanvasToWidgetMatrix = CanvasToWidgetMatrix.GetTransposed();//matrix in memory is aligned as row-primary, so transpose it then in hlsl we can read as column-primary
		FMemory::Memcpy(BlockBuffer + BlockDataOffset, &CanvasToWidgetMatrix, SingleBlockSizeInBytes);
		BlockDataOffset += SingleBlockSizeInBytes;
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
		if (!TargetClip->Parent.IsValid())
		{
			break;
		}
		
		TargetClip = TargetClip->Parent.Pin().Get();
	}
	return true;
}

