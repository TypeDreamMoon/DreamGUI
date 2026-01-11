// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexSprite.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Core/Components/LexCanvas.h"
#include "Core/LexUISpriteData_BaseObject.h"


ULexSprite::ULexSprite(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void ULexSprite::BeginPlay()
{
	Super::BeginPlay();
}
#if WITH_EDITOR
void ULexSprite::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto propName = Property->GetFName();
		if (propName == GET_MEMBER_NAME_CHECKED(ULexSprite, FillOriginType_Radial90))
		{
			FillOrigin = (uint8)FillOriginType_Radial90;
			FillOriginType_Radial180 = (ELexUISpriteFillOriginType_Radial180)FillOrigin;
			FillOriginType_Radial360 = (ELexUISpriteFillOriginType_Radial360)FillOrigin;
		}
		else if (propName == GET_MEMBER_NAME_CHECKED(ULexSprite, FillOriginType_Radial180))
		{
			FillOrigin = (uint8)FillOriginType_Radial180;
			FillOriginType_Radial90 = (ELexUISpriteFillOriginType_Radial90)FillOrigin;
			FillOriginType_Radial360 = (ELexUISpriteFillOriginType_Radial360)FillOrigin;
		}
		else if (propName == GET_MEMBER_NAME_CHECKED(ULexSprite, FillOriginType_Radial360))
		{
			FillOrigin = (uint8)FillOriginType_Radial360;
			FillOriginType_Radial180 = (ELexUISpriteFillOriginType_Radial180)FillOrigin;
			FillOriginType_Radial90 = (ELexUISpriteFillOriginType_Radial90)FillOrigin;
		}
		else if (propName == GET_MEMBER_NAME_CHECKED(ULexSprite, Sprite))
		{
			if (IsValid(Sprite))
			{
				if (Sprite->GetSpriteInfo().HasBorder())
				{
					if (this->DrawType == ELexUISpriteDrawType::Normal)
					{
						this->SetDrawType(ELexUISpriteDrawType::Sliced);
					}
				}
			}
		}
		if (IsValid(Sprite) && DrawType == ELexUISpriteDrawType::Tiled)
		{
			CalculateTiledWidth();
			CalculateTiledHeight();
		}
	}
}
#endif

void ULexSprite::OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = GetWidget();
	auto RenderCanvas = Widget->GetRenderCanvas();
	switch (DrawType)
	{
	case ELexUISpriteDrawType::Normal:
		FLexUIGeometry::UpdateUIRectSimpleVertex(&InGeo, 
			Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), RenderCanvas, this, GetFinalColor(), 
			InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
		);
		break;
	case ELexUISpriteDrawType::Sliced:
	case ELexUISpriteDrawType::SlicedFrame:
		if (Sprite->GetSpriteInfo().HasBorder())
		{
			FLexUIGeometry::UpdateUIRectBorderVertex(&InGeo, DrawType == ELexUISpriteDrawType::Sliced, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), RenderCanvas, this, GetFinalColor(),
				PixelsPerUnitMultiplier, 
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		else
		{
			FLexUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
				Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
	break;
	case ELexUISpriteDrawType::Tiled:
		if (!Sprite->IsIndividual())
		{
			FLexUIGeometry::UpdateUIRectTiledVertex(&InGeo, Sprite->GetSpriteInfo(), RenderCanvas, this, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Tiled_WidthRectCount, Tiled_HeightRectCount, Tiled_WidthRemainedRectSize, Tiled_HeightRemainedRectSize, GetFinalColor(), 
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		else
		{
			FLexUISpriteInfo tempSpriteInfo;
			tempSpriteInfo.ApplyUV(0, 0, Widget->GetWidth(), Widget->GetHeight(), 1.0f / Sprite->GetSpriteInfo().Width, 1.0f / Sprite->GetSpriteInfo().Height);
			FLexUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
				Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), tempSpriteInfo, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		break;
	case ELexUISpriteDrawType::Filled:
	{
		switch (FillMethod)
		{
		case ELexUISpriteFillMethod::Horizontal:
		case ELexUISpriteFillMethod::Vertical:
			FLexUIGeometry::UpdateUIRectFillHorizontalVerticalVertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), FillDirectionFlip, FillAmount, FillMethod == ELexUISpriteFillMethod::Horizontal, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case ELexUISpriteFillMethod::Radial90:
			FLexUIGeometry::UpdateUIRectFillRadial90Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), FillDirectionFlip, FillAmount, (ELexUISpriteFillOriginType_Radial90)FillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case ELexUISpriteFillMethod::Radial180:
			FLexUIGeometry::UpdateUIRectFillRadial180Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), FillDirectionFlip, FillAmount, (ELexUISpriteFillOriginType_Radial180)FillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case ELexUISpriteFillMethod::Radial360:
			FLexUIGeometry::UpdateUIRectFillRadial360Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), FillDirectionFlip, FillAmount, (ELexUISpriteFillOriginType_Radial360)FillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		}
	}
	break;
	}
}

void ULexSprite::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	if (!IsValid(Sprite))return;
	if (DrawType == ELexUISpriteDrawType::Tiled)
	{
        if (InWidthChange)
        {
			CalculateTiledWidth();
        }
		if (InHeightChange)
		{
			CalculateTiledHeight();
		}
	}
    else
    {
        if (InPivotChange || InWidthChange || InHeightChange)
        {
			MarkVertexPositionDirty();
		}
    }
}

void ULexSprite::CalculateTiledWidth()
{
	if (!Sprite->IsIndividual())
	{
		auto Widget = GetWidget();
		if (Widget->GetWidth() <= 0)
		{
			if (Tiled_WidthRectCount != 0)
			{
				Tiled_WidthRectCount = 0;
				Tiled_WidthRemainedRectSize = 0;
				MarkVerticesDirty(true, true, true, false);
			}
			return;
		}
		float widthCountFloat = Widget->GetWidth() / Sprite->GetSpriteInfo().Width;
		int widthCount = (int)widthCountFloat + 1;//rect count of width-direction, +1 means not-full-size rect
		if (widthCount != Tiled_WidthRectCount)
		{
			Tiled_WidthRectCount = widthCount;
			MarkVerticesDirty(true, true, true, false);
		}
		float remainedWidth = (widthCountFloat - (widthCount - 1)) * Sprite->GetSpriteInfo().Width;//not-full-size rect's width
		if (remainedWidth != Tiled_WidthRemainedRectSize)
		{
			Tiled_WidthRemainedRectSize = remainedWidth;
			MarkVerticesDirty(false, true, true, false);
		}
	}
	else
	{
		MarkVerticesDirty(false, true, true, false);
	}
}
void ULexSprite::CalculateTiledHeight()
{
	if (!Sprite->IsIndividual())
	{
		auto Widget = GetWidget();
		if (Widget->GetHeight() <= 0)
		{
			if (Tiled_HeightRectCount != 0)
			{
				Tiled_HeightRectCount = 0;
				Tiled_HeightRemainedRectSize = 0;
				MarkVerticesDirty(true, true, true, false);
			}
			return;
		}
		float heightCountFloat = Widget->GetHeight() / Sprite->GetSpriteInfo().Height;
		int heightCount = (int)heightCountFloat + 1;//rect count of height-direction, +1 means not-full-size rect
		if (heightCount != Tiled_HeightRectCount)
		{
			Tiled_HeightRectCount = heightCount;
			MarkVerticesDirty(true, true, true, false);
		}
		float remainedHeight = (heightCountFloat - (heightCount - 1)) * Sprite->GetSpriteInfo().Height;//not-full-size rect's height
		if (remainedHeight != Tiled_HeightRemainedRectSize)
		{
			Tiled_HeightRemainedRectSize = remainedHeight;
			MarkVerticesDirty(false, true, true, false);
		}
	}
	else
	{
		MarkVerticesDirty(false, true, true, false);
	}
}

void ULexSprite::SetDrawType(ELexUISpriteDrawType Value) {
	if (DrawType != Value)
	{
		DrawType = Value;
		MarkVerticesDirty(true, true, true, true);
		if (DrawType == ELexUISpriteDrawType::Tiled)
		{
			CalculateTiledWidth();
			CalculateTiledHeight();
		}
	}
}

void ULexSprite::SetPixelsPerUnitMultiplier(float Value)
{
	if (PixelsPerUnitMultiplier != Value)
	{
		PixelsPerUnitMultiplier = Value;
		if (DrawType == ELexUISpriteDrawType::Sliced || DrawType == ELexUISpriteDrawType::SlicedFrame)
		{
			MarkVertexPositionDirty();
		}
	}
}

void ULexSprite::SetFillMethod(ELexUISpriteFillMethod Value)
{
	if (FillMethod != Value)
	{
		FillMethod = Value;
		if (DrawType == ELexUISpriteDrawType::Filled)
		{
			MarkVerticesDirty(true, true, true, true);
		}
	}
}
void ULexSprite::SetFillOrigin(uint8 Value)
{
	if (FillOrigin != Value)
	{
		FillOrigin = Value;
		if (DrawType == ELexUISpriteDrawType::Filled)
		{
			if (FillMethod == ELexUISpriteFillMethod::Radial90)
			{
				MarkVerticesDirty(false, true, true, false);
			}
			else if (FillMethod == ELexUISpriteFillMethod::Radial180 || FillMethod == ELexUISpriteFillMethod::Radial360)
			{
				MarkVerticesDirty(true, true, true, true);
			}
		}
	}
}
void ULexSprite::SetFillDirectionFlip(bool Value)
{
	if (FillDirectionFlip != Value)
	{
		FillDirectionFlip = Value;
		if (DrawType == ELexUISpriteDrawType::Filled)
		{
			MarkVerticesDirty(false, true, true, false);
		}
	}
}
void ULexSprite::SetFillAmount(float Value)
{
	if (FillAmount != Value)
	{
		FillAmount = Value;
		if (DrawType == ELexUISpriteDrawType::Filled)
		{
			MarkVerticesDirty(false, true, true, false);
		}
	}
}
