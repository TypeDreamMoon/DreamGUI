// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUI/Public/Core/Components/UISprite.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "LGUI/Public/Core/Components/LexCanvas.h"
#include "Core/LexUISpriteData_BaseObject.h"


UUISprite::UUISprite(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void UUISprite::BeginPlay()
{
	Super::BeginPlay();
}
#if WITH_EDITOR
void UUISprite::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto propName = Property->GetFName();
		if (propName == GET_MEMBER_NAME_CHECKED(UUISprite, fillOriginType_Radial90))
		{
			fillOrigin = (uint8)fillOriginType_Radial90;
			fillOriginType_Radial180 = (EUISpriteFillOriginType_Radial180)fillOrigin;
			fillOriginType_Radial360 = (EUISpriteFillOriginType_Radial360)fillOrigin;
		}
		else if (propName == GET_MEMBER_NAME_CHECKED(UUISprite, fillOriginType_Radial180))
		{
			fillOrigin = (uint8)fillOriginType_Radial180;
			fillOriginType_Radial90 = (EUISpriteFillOriginType_Radial90)fillOrigin;
			fillOriginType_Radial360 = (EUISpriteFillOriginType_Radial360)fillOrigin;
		}
		else if (propName == GET_MEMBER_NAME_CHECKED(UUISprite, fillOriginType_Radial360))
		{
			fillOrigin = (uint8)fillOriginType_Radial360;
			fillOriginType_Radial180 = (EUISpriteFillOriginType_Radial180)fillOrigin;
			fillOriginType_Radial90 = (EUISpriteFillOriginType_Radial90)fillOrigin;
		}
		else if (propName == GET_MEMBER_NAME_CHECKED(UUISprite, sprite))
		{
			if (IsValid(sprite))
			{
				if (sprite->GetSpriteInfo().HasBorder())
				{
					if (this->type == EUISpriteType::Normal)
					{
						this->SetSpriteType(EUISpriteType::Sliced);
					}
				}
			}
		}
		if (IsValid(sprite) && type == EUISpriteType::Tiled)
		{
			CalculateTiledWidth();
			CalculateTiledHeight();
		}
	}
}
#endif

void UUISprite::OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = GetWidget();
	auto RenderCanvas = Widget->GetRenderCanvas();
	switch (type)
	{
	case EUISpriteType::Normal:
		FLexUIGeometry::UpdateUIRectSimpleVertex(&InGeo, 
			Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), sprite->GetSpriteInfo(), RenderCanvas, this, GetFinalColor(), 
			InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
		);
		break;
	case EUISpriteType::Sliced:
	case EUISpriteType::SlicedFrame:
		if (sprite->GetSpriteInfo().HasBorder())
		{
			FLexUIGeometry::UpdateUIRectBorderVertex(&InGeo, type == EUISpriteType::Sliced, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), sprite->GetSpriteInfo(), RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		else
		{
			FLexUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
				Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), sprite->GetSpriteInfo(), RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
	break;
	case EUISpriteType::Tiled:
		if (!sprite->IsIndividual())
		{
			FLexUIGeometry::UpdateUIRectTiledVertex(&InGeo, sprite->GetSpriteInfo(), RenderCanvas, this, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), Tiled_WidthRectCount, Tiled_HeightRectCount, Tiled_WidthRemainedRectSize, Tiled_HeightRemainedRectSize, GetFinalColor(), 
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		else
		{
			FLexUISpriteInfo tempSpriteInfo;
			tempSpriteInfo.ApplyUV(0, 0, Widget->GetWidth(), Widget->GetHeight(), 1.0f / sprite->GetSpriteInfo().width, 1.0f / sprite->GetSpriteInfo().height);
			FLexUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
				Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), tempSpriteInfo, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		break;
	case EUISpriteType::Filled:
	{
		switch (fillMethod)
		{
		case EUISpriteFillMethod::Horizontal:
		case EUISpriteFillMethod::Vertical:
			FLexUIGeometry::UpdateUIRectFillHorizontalVerticalVertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), sprite->GetSpriteInfo(), fillDirectionFlip, fillAmount, fillMethod == EUISpriteFillMethod::Horizontal, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case EUISpriteFillMethod::Radial90:
			FLexUIGeometry::UpdateUIRectFillRadial90Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), sprite->GetSpriteInfo(), fillDirectionFlip, fillAmount, (EUISpriteFillOriginType_Radial90)fillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case EUISpriteFillMethod::Radial180:
			FLexUIGeometry::UpdateUIRectFillRadial180Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), sprite->GetSpriteInfo(), fillDirectionFlip, fillAmount, (EUISpriteFillOriginType_Radial180)fillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case EUISpriteFillMethod::Radial360:
			FLexUIGeometry::UpdateUIRectFillRadial360Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), sprite->GetSpriteInfo(), fillDirectionFlip, fillAmount, (EUISpriteFillOriginType_Radial360)fillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		}
	}
	break;
	}
}

void UUISprite::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	if (!IsValid(sprite))return;
	if (type == EUISpriteType::Tiled)
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

void UUISprite::CalculateTiledWidth()
{
	if (!sprite->IsIndividual())
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
		float widthCountFloat = Widget->GetWidth() / sprite->GetSpriteInfo().width;
		int widthCount = (int)widthCountFloat + 1;//rect count of width-direction, +1 means not-full-size rect
		if (widthCount != Tiled_WidthRectCount)
		{
			Tiled_WidthRectCount = widthCount;
			MarkVerticesDirty(true, true, true, false);
		}
		float remainedWidth = (widthCountFloat - (widthCount - 1)) * sprite->GetSpriteInfo().width;//not-full-size rect's width
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
void UUISprite::CalculateTiledHeight()
{
	if (!sprite->IsIndividual())
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
		float heightCountFloat = Widget->GetHeight() / sprite->GetSpriteInfo().height;
		int heightCount = (int)heightCountFloat + 1;//rect count of height-direction, +1 means not-full-size rect
		if (heightCount != Tiled_HeightRectCount)
		{
			Tiled_HeightRectCount = heightCount;
			MarkVerticesDirty(true, true, true, false);
		}
		float remainedHeight = (heightCountFloat - (heightCount - 1)) * sprite->GetSpriteInfo().height;//not-full-size rect's height
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

void UUISprite::SetSpriteType(EUISpriteType newType) {
	if (type != newType)
	{
		type = newType;
		MarkVerticesDirty(true, true, true, true);
		if (type == EUISpriteType::Tiled)
		{
			CalculateTiledWidth();
			CalculateTiledHeight();
		}
	}
}
void UUISprite::SetFillMethod(EUISpriteFillMethod newValue)
{
	if (fillMethod != newValue)
	{
		fillMethod = newValue;
		if (type == EUISpriteType::Filled)
		{
			MarkVerticesDirty(true, true, true, true);
		}
	}
}
void UUISprite::SetFillOrigin(uint8 newValue)
{
	if (fillOrigin != newValue)
	{
		fillOrigin = newValue;
		if (type == EUISpriteType::Filled)
		{
			if (fillMethod == EUISpriteFillMethod::Radial90)
			{
				MarkVerticesDirty(false, true, true, false);
			}
			else if (fillMethod == EUISpriteFillMethod::Radial180 || fillMethod == EUISpriteFillMethod::Radial360)
			{
				MarkVerticesDirty(true, true, true, true);
			}
		}
	}
}
void UUISprite::SetFillDirectionFlip(bool newValue)
{
	if (fillDirectionFlip != newValue)
	{
		fillDirectionFlip = newValue;
		if (type == EUISpriteType::Filled)
		{
			MarkVerticesDirty(false, true, true, false);
		}
	}
}
void UUISprite::SetFillAmount(float newValue)
{
	if (fillAmount != newValue)
	{
		fillAmount = newValue;
		if (type == EUISpriteType::Filled)
		{
			MarkVerticesDirty(false, true, true, false);
		}
	}
}
