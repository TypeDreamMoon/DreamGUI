// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/DreamSprite.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWidgetRegistry.h"


UDreamSprite::UDreamSprite(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void UDreamSprite::BeginPlay()
{
	Super::BeginPlay();
}
#if WITH_EDITOR
void UDreamSprite::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto propName = Property->GetFName();
		if (propName == GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOriginType_Radial90))
		{
			FillOrigin = (uint8)FillOriginType_Radial90;
			FillOriginType_Radial180 = (EDreamUISpriteFillOriginType_Radial180)FillOrigin;
			FillOriginType_Radial360 = (EDreamUISpriteFillOriginType_Radial360)FillOrigin;
		}
		else if (propName == GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOriginType_Radial180))
		{
			FillOrigin = (uint8)FillOriginType_Radial180;
			FillOriginType_Radial90 = (EDreamUISpriteFillOriginType_Radial90)FillOrigin;
			FillOriginType_Radial360 = (EDreamUISpriteFillOriginType_Radial360)FillOrigin;
		}
		else if (propName == GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOriginType_Radial360))
		{
			FillOrigin = (uint8)FillOriginType_Radial360;
			FillOriginType_Radial180 = (EDreamUISpriteFillOriginType_Radial180)FillOrigin;
			FillOriginType_Radial90 = (EDreamUISpriteFillOriginType_Radial90)FillOrigin;
		}
		else if (propName == GET_MEMBER_NAME_CHECKED(UDreamSprite, Sprite))
		{
			if (IsValid(Sprite))
			{
				if (Sprite->GetSpriteInfo().HasBorder())
				{
					if (this->DrawType == EDreamUISpriteDrawType::Normal)
					{
						this->SetDrawType(EDreamUISpriteDrawType::Sliced);
					}
				}
			}
		}
		if (IsValid(Sprite) && DrawType == EDreamUISpriteDrawType::Tiled)
		{
			CalculateTiledWidth();
			CalculateTiledHeight();
		}
	}
}
#endif

void UDreamSprite::OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = GetWidget();
	auto RenderCanvas = Widget->GetRenderCanvas();
	switch (DrawType)
	{
	case EDreamUISpriteDrawType::Normal:
		FDreamUIGeometry::UpdateUIRectSimpleVertex(&InGeo, 
			Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), RenderCanvas, this, GetFinalColor(), 
			InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
		);
		break;
	case EDreamUISpriteDrawType::Sliced:
	case EDreamUISpriteDrawType::SlicedFrame:
		if (Sprite->GetSpriteInfo().HasBorder())
		{
			FDreamUIGeometry::UpdateUIRectBorderVertex(&InGeo, DrawType == EDreamUISpriteDrawType::Sliced, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), RenderCanvas, this, GetFinalColor(),
				PixelsPerUnitMultiplier, 
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		else
		{
			FDreamUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
				Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
	break;
	case EDreamUISpriteDrawType::Tiled:
		if (!Sprite->IsIndividual())
		{
			FDreamUIGeometry::UpdateUIRectTiledVertex(&InGeo, Sprite->GetSpriteInfo(), RenderCanvas, this, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Tiled_WidthRectCount, Tiled_HeightRectCount, Tiled_WidthRemainedRectSize, Tiled_HeightRemainedRectSize, GetFinalColor(), 
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		else
		{
			FDreamUISpriteInfo tempSpriteInfo;
			tempSpriteInfo.ApplyUV(0, 0, Widget->GetWidth(), Widget->GetHeight(), 1.0f / Sprite->GetSpriteInfo().Width, 1.0f / Sprite->GetSpriteInfo().Height);
			FDreamUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
				Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), tempSpriteInfo, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		break;
	case EDreamUISpriteDrawType::Filled:
	{
		switch (FillMethod)
		{
		case EDreamUISpriteFillMethod::Horizontal:
		case EDreamUISpriteFillMethod::Vertical:
			FDreamUIGeometry::UpdateUIRectFillHorizontalVerticalVertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), FillDirectionFlip, FillAmount, FillMethod == EDreamUISpriteFillMethod::Horizontal, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case EDreamUISpriteFillMethod::Radial90:
			FDreamUIGeometry::UpdateUIRectFillRadial90Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), FillDirectionFlip, FillAmount, (EDreamUISpriteFillOriginType_Radial90)FillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case EDreamUISpriteFillMethod::Radial180:
			FDreamUIGeometry::UpdateUIRectFillRadial180Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), FillDirectionFlip, FillAmount, (EDreamUISpriteFillOriginType_Radial180)FillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case EDreamUISpriteFillMethod::Radial360:
			FDreamUIGeometry::UpdateUIRectFillRadial360Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Sprite->GetSpriteInfo(), FillDirectionFlip, FillAmount, (EDreamUISpriteFillOriginType_Radial360)FillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		}
	}
	break;
	}
}

void UDreamSprite::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	if (!IsValid(Sprite))return;
	if (DrawType == EDreamUISpriteDrawType::Tiled)
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

void UDreamSprite::CalculateTiledWidth()
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
void UDreamSprite::CalculateTiledHeight()
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

void UDreamSprite::SetDrawType(EDreamUISpriteDrawType Value) {
	if (DrawType != Value)
	{
		DrawType = Value;
		MarkVerticesDirty(true, true, true, true);
		if (DrawType == EDreamUISpriteDrawType::Tiled)
		{
			CalculateTiledWidth();
			CalculateTiledHeight();
		}
	}
}

void UDreamSprite::SetPixelsPerUnitMultiplier(float Value)
{
	if (PixelsPerUnitMultiplier != Value)
	{
		PixelsPerUnitMultiplier = Value;
		if (DrawType == EDreamUISpriteDrawType::Sliced || DrawType == EDreamUISpriteDrawType::SlicedFrame)
		{
			MarkVertexPositionDirty();
		}
	}
}

void UDreamSprite::SetFillMethod(EDreamUISpriteFillMethod Value)
{
	if (FillMethod != Value)
	{
		FillMethod = Value;
		if (DrawType == EDreamUISpriteDrawType::Filled)
		{
			MarkVerticesDirty(true, true, true, true);
		}
	}
}
void UDreamSprite::SetFillOrigin(uint8 Value)
{
	if (FillOrigin != Value)
	{
		FillOrigin = Value;
		if (DrawType == EDreamUISpriteDrawType::Filled)
		{
			if (FillMethod == EDreamUISpriteFillMethod::Radial90)
			{
				MarkVerticesDirty(false, true, true, false);
			}
			else if (FillMethod == EDreamUISpriteFillMethod::Radial180 || FillMethod == EDreamUISpriteFillMethod::Radial360)
			{
				MarkVerticesDirty(true, true, true, true);
			}
		}
	}
}
void UDreamSprite::SetFillDirectionFlip(bool Value)
{
	if (FillDirectionFlip != Value)
	{
		FillDirectionFlip = Value;
		if (DrawType == EDreamUISpriteDrawType::Filled)
		{
			MarkVerticesDirty(false, true, true, false);
		}
	}
}
void UDreamSprite::SetFillAmount(float Value)
{
	if (FillAmount != Value)
	{
		FillAmount = Value;
		if (DrawType == EDreamUISpriteDrawType::Filled)
		{
			MarkVerticesDirty(false, true, true, false);
		}
	}
}

DECLARE_DREAM_GUI_VISUAL("Sprite", UDreamSprite)
