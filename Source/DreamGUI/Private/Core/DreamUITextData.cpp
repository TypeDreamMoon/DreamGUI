// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUITextData.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"

FDreamUITextGeometryCache::FDreamUITextGeometryCache()
	: Input(MakeUnique<FDreamTextLayoutInput>())
	, DisplayList(MakeUnique<FDreamTextDisplayList>())
{
}

FDreamUITextGeometryCache::~FDreamUITextGeometryCache() = default;

bool FDreamUITextGeometryCache::SetLayoutInput(const FDreamTextLayoutInput& InInput)
{
	if (*Input != InInput)
	{
		*Input = InInput;
		bIsDirty = true;
	}
	return bIsDirty;
}

const FDreamTextLayoutInput& FDreamUITextGeometryCache::GetLayoutInput() const
{
	return *Input;
}

void FDreamUITextGeometryCache::MarkDirty()
{
	bIsDirty = true;
	BestFitKey.Reset();
}

bool FDreamUITextGeometryCache::TryGetBestFit(const FDreamTextLayoutInput& InCeilingInput, float& OutSize) const
{
	if (BestFitKey.IsValid() && *BestFitKey == InCeilingInput)
	{
		OutSize = BestFitSize;
		return true;
	}
	return false;
}

void FDreamUITextGeometryCache::SetBestFit(const FDreamTextLayoutInput& InCeilingInput, float InSize)
{
	if (!BestFitKey.IsValid())
	{
		BestFitKey = MakeUnique<FDreamTextLayoutInput>();
	}
	*BestFitKey = InCeilingInput;
	BestFitSize = InSize;
}

bool FDreamUITextGeometryCache::EnsureLayout()
{
	if (!bIsDirty)return false;
	if (!Input->Font.IsValid())return false;
	bIsDirty = false;
	LayoutRunCount++;
	FDreamTextLayoutEngine::Layout(*Input, *DisplayList);
	return true;
}

void FDreamUITextGeometryCache::Paint(FDreamUIGeometry& Geometry, const FDreamTextPaintParams& Params)
{
	EnsureLayout();
	FDreamTextPainter::Paint(*DisplayList, Params, Geometry, CharPropertyArray);
}

const FDreamTextDisplayList& FDreamUITextGeometryCache::GetDisplayList() const
{
	return *DisplayList;
}

bool FDreamUITextGeometryCache::IsTextTruncated() const
{
	return DisplayList->bTruncated;
}

FVector2f FDreamUITextGeometryCache::GetPreferredSize() const
{
	return DisplayList->PreferredSize;
}

const TArray<FDreamUITextLineProperty>& FDreamUITextGeometryCache::GetLines() const
{
	return DisplayList->Lines;
}

const TArray<FDreamUIText_RichTextCustomTag>& FDreamUITextGeometryCache::GetCustomTags() const
{
	return DisplayList->CustomTags;
}

const TArray<FDreamUIText_RichTextImageTag>& FDreamUITextGeometryCache::GetImageTags() const
{
	return DisplayList->Images;
}

const TArray<FDreamUIText_Emoji>& FDreamUITextGeometryCache::GetEmojis() const
{
	return DisplayList->Emojis;
}
