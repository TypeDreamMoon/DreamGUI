// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/DreamTexture.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWidgetRegistry.h"


UDreamTexture::UDreamTexture(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void UDreamTexture::BeginPlay()
{
	Super::BeginPlay();
}
#if WITH_EDITOR
void UDreamTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	CheckSpriteData();
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropName = Property->GetFName();
		if (PropName == GET_MEMBER_NAME_CHECKED(UDreamTexture, fillOriginType_Radial90))
		{
			FillOrigin = (uint8)fillOriginType_Radial90;
			fillOriginType_Radial180 = (EDreamUISpriteFillOriginType_Radial180)FillOrigin;
			fillOriginType_Radial360 = (EDreamUISpriteFillOriginType_Radial360)FillOrigin;
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDreamTexture, fillOriginType_Radial180))
		{
			FillOrigin = (uint8)fillOriginType_Radial180;
			fillOriginType_Radial90 = (EDreamUISpriteFillOriginType_Radial90)FillOrigin;
			fillOriginType_Radial360 = (EDreamUISpriteFillOriginType_Radial360)FillOrigin;
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDreamTexture, fillOriginType_Radial360))
		{
			FillOrigin = (uint8)fillOriginType_Radial360;
			fillOriginType_Radial180 = (EDreamUISpriteFillOriginType_Radial180)FillOrigin;
			fillOriginType_Radial90 = (EDreamUISpriteFillOriginType_Radial90)FillOrigin;
		}
	}
}
#endif

void UDreamTexture::CheckSpriteData()
{
	if (IsValid(Texture))
	{
		SpriteInfo.Width = Texture->GetSurfaceWidth();
		SpriteInfo.Height = Texture->GetSurfaceHeight();
		if (DrawType != EDreamUISpriteDrawType::Tiled)
		{
			SpriteInfo.ApplyUV(0, 0, SpriteInfo.Width, SpriteInfo.Height, 1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height, UVRect);
			SpriteInfo.ApplyBorderUV(1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height);
		}
	}
}

void UDreamTexture::OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = GetWidget();
	auto RenderCanvas = Widget->GetRenderCanvas();
	switch (DrawType)
	{
	case EDreamUISpriteDrawType::Normal:
		FDreamUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
			Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, RenderCanvas, this, GetFinalColor(),
			InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
		);
		break;
	case EDreamUISpriteDrawType::Sliced:
	case EDreamUISpriteDrawType::SlicedFrame:
		if (SpriteInfo.HasBorder())
		{
			FDreamUIGeometry::UpdateUIRectBorderVertex(&InGeo, DrawType == EDreamUISpriteDrawType::Sliced, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, RenderCanvas, this, GetFinalColor(),
				PixelsPerUnitMultiplier, 
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		else
		{
			FDreamUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
				Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		break;
	case EDreamUISpriteDrawType::Tiled:
		FDreamUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
			Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, RenderCanvas, this, GetFinalColor(),
			InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
		);
		break;
	case EDreamUISpriteDrawType::Filled:
	{
		switch (FillMethod)
		{
		case EDreamUISpriteFillMethod::Horizontal:
		case EDreamUISpriteFillMethod::Vertical:
			FDreamUIGeometry::UpdateUIRectFillHorizontalVerticalVertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, FillDirectionFlip, FillAmount, FillMethod == EDreamUISpriteFillMethod::Horizontal, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case EDreamUISpriteFillMethod::Radial90:
			FDreamUIGeometry::UpdateUIRectFillRadial90Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, FillDirectionFlip, FillAmount, (EDreamUISpriteFillOriginType_Radial90)FillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case EDreamUISpriteFillMethod::Radial180:
			FDreamUIGeometry::UpdateUIRectFillRadial180Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, FillDirectionFlip, FillAmount, (EDreamUISpriteFillOriginType_Radial180)FillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case EDreamUISpriteFillMethod::Radial360:
			FDreamUIGeometry::UpdateUIRectFillRadial360Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, FillDirectionFlip, FillAmount, (EDreamUISpriteFillOriginType_Radial360)FillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		}
	}
	break;
	}
}

void UDreamTexture::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	if (!IsValid(Texture))return;
	if (DrawType == EDreamUISpriteDrawType::Tiled)
	{
        if (InWidthChange || InHeightChange)
        {
        	auto Widget = GetWidget();
            SpriteInfo.ApplyUV(0, 0, Widget->GetWidth(), Widget->GetHeight(), 1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height);
            MarkVertexUVDirty();
        }
	}
    if (InPivotChange || InWidthChange || InHeightChange)
    {
        MarkVertexPositionDirty();
    }
}


void UDreamTexture::SetDrawType(EDreamUISpriteDrawType Value)
{
	if (DrawType != Value)
	{
		DrawType = Value;
		MarkVerticesDirty(true, true, true, true);
	}
}
void UDreamTexture::SetSpriteInfo(FDreamUISpriteInfo Value) 
{
	if (SpriteInfo != Value)
	{
		SpriteInfo = Value;
		MarkVertexUVDirty();
		CheckSpriteData();
	}
}

void UDreamTexture::SetUVRect(FVector4f Value)
{
	if (UVRect != Value)
	{
		UVRect = Value;
		MarkVertexUVDirty();
		CheckSpriteData();
	}
}

void UDreamTexture::SetPixelsPerUnitMultiplier(float Value)
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

void UDreamTexture::SetTexture(UTexture* Value)
{
	if (Texture != Value)
	{
		Super::SetTexture(Value);
		CheckSpriteData();
	}
}

void UDreamTexture::SetFillMethod(EDreamUISpriteFillMethod Value)
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
void UDreamTexture::SetFillOrigin(uint8 Value)
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
void UDreamTexture::SetFillDirectionFlip(bool Value)
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
void UDreamTexture::SetFillAmount(float Value)
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


DECLARE_DREAM_GUI_VISUAL("Texture", UDreamTexture)
