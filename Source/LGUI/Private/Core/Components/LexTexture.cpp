// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexTexture.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Core/Components/LexCanvas.h"



ULexTexture::ULexTexture(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void ULexTexture::BeginPlay()
{
	Super::BeginPlay();
}
#if WITH_EDITOR
void ULexTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	CheckSpriteData();
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropName = Property->GetFName();
		if (PropName == GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial90))
		{
			FillOrigin = (uint8)fillOriginType_Radial90;
			fillOriginType_Radial180 = (ELexUISpriteFillOriginType_Radial180)FillOrigin;
			fillOriginType_Radial360 = (ELexUISpriteFillOriginType_Radial360)FillOrigin;
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial180))
		{
			FillOrigin = (uint8)fillOriginType_Radial180;
			fillOriginType_Radial90 = (ELexUISpriteFillOriginType_Radial90)FillOrigin;
			fillOriginType_Radial360 = (ELexUISpriteFillOriginType_Radial360)FillOrigin;
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial360))
		{
			FillOrigin = (uint8)fillOriginType_Radial360;
			fillOriginType_Radial180 = (ELexUISpriteFillOriginType_Radial180)FillOrigin;
			fillOriginType_Radial90 = (ELexUISpriteFillOriginType_Radial90)FillOrigin;
		}
	}
}
#endif

void ULexTexture::CheckSpriteData()
{
	if (IsValid(Texture))
	{
		SpriteInfo.Width = Texture->GetSurfaceWidth();
		SpriteInfo.Height = Texture->GetSurfaceHeight();
		if (DrawType != ELexUISpriteDrawType::Tiled)
		{
			ApplyUVRect();
			SpriteInfo.ApplyUV(0, 0, SpriteInfo.Width, SpriteInfo.Height, 1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height, UVRect);
			SpriteInfo.ApplyBorderUV(1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height);
		}
	}
}

void ULexTexture::ApplyUVRect()
{
	switch (UVRectControlMode)
	{
	default:
	case ELexUITextureUVRectControlMode::None:
		break;
	case ELexUITextureUVRectControlMode::KeepAspectRatio_FitIn:
		{
			auto Widget = GetWidget();
			auto TextureWidth = Texture->GetSurfaceWidth();
			auto TextureHeight = Texture->GetSurfaceHeight();
			auto TextureAspect = TextureWidth / TextureHeight;
			auto ThisWidth = Widget->GetWidth();
			auto ThisHeight = Widget->GetHeight();
			auto ThisAspect = ThisWidth / ThisHeight;
			if (TextureAspect > ThisAspect)
			{
				auto VerticalScale = TextureAspect / ThisAspect;
				auto VerticalOffset = (1.0f - VerticalScale) * 0.5f;
				UVRect = FVector4(0, VerticalOffset, 1, VerticalScale);
			}
			else
			{
				auto HorizontalScale = ThisAspect / TextureAspect;
				auto HorizontalOffset = (1.0f - HorizontalScale) * 0.5f;
				UVRect = FVector4(HorizontalOffset, 0, HorizontalScale, 1);
			}
		}
		break;
	case ELexUITextureUVRectControlMode::KeepAspectRatio_Envelope:
		{
			auto Widget = GetWidget();
			auto TextureWidth = Texture->GetSurfaceWidth();
			auto TextureHeight = Texture->GetSurfaceHeight();
			auto TextureAspect = TextureWidth / TextureHeight;
			auto ThisWidth = Widget->GetWidth();
			auto ThisHeight = Widget->GetHeight();
			auto ThisAspect = ThisWidth / ThisHeight;
			if (TextureAspect > ThisAspect)
			{
				auto HorizontalScale = ThisAspect / TextureAspect;
				auto HorizontalOffset = (1.0f - HorizontalScale) * 0.5f;
				UVRect = FVector4(HorizontalOffset, 0, HorizontalScale, 1);
			}
			else
			{
				auto VerticalScale = TextureAspect / ThisAspect;
				auto VerticalOffset = (1.0f - VerticalScale) * 0.5f;
				UVRect = FVector4(0, VerticalOffset, 1, VerticalScale);
			}
		}
		break;
	}
}

void ULexTexture::OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = GetWidget();
	auto RenderCanvas = Widget->GetRenderCanvas();
	switch (DrawType)
	{
	case ELexUISpriteDrawType::Normal:
		FLexUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
			Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, RenderCanvas, this, GetFinalColor(),
			InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
		);
		break;
	case ELexUISpriteDrawType::Sliced:
	case ELexUISpriteDrawType::SlicedFrame:
		if (SpriteInfo.HasBorder())
		{
			FLexUIGeometry::UpdateUIRectBorderVertex(&InGeo, DrawType == ELexUISpriteDrawType::Sliced, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		else
		{
			FLexUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
				Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		}
		break;
	case ELexUISpriteDrawType::Tiled:
		FLexUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
			Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, RenderCanvas, this, GetFinalColor(),
			InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
		);
		break;
	case ELexUISpriteDrawType::Filled:
	{
		switch (FillMethod)
		{
		case ELexUISpriteFillMethod::Horizontal:
		case ELexUISpriteFillMethod::Vertical:
			FLexUIGeometry::UpdateUIRectFillHorizontalVerticalVertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, FillDirectionFlip, FillAmount, FillMethod == ELexUISpriteFillMethod::Horizontal, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case ELexUISpriteFillMethod::Radial90:
			FLexUIGeometry::UpdateUIRectFillRadial90Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, FillDirectionFlip, FillAmount, (ELexUISpriteFillOriginType_Radial90)FillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case ELexUISpriteFillMethod::Radial180:
			FLexUIGeometry::UpdateUIRectFillRadial180Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, FillDirectionFlip, FillAmount, (ELexUISpriteFillOriginType_Radial180)FillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		case ELexUISpriteFillMethod::Radial360:
			FLexUIGeometry::UpdateUIRectFillRadial360Vertex(&InGeo, Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, FillDirectionFlip, FillAmount, (ELexUISpriteFillOriginType_Radial360)FillOrigin, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
			break;
		}
	}
	break;
	}
}

void ULexTexture::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	if (!IsValid(Texture))return;
	if (DrawType == ELexUISpriteDrawType::Tiled)
	{
        if (InWidthChange || InHeightChange)
        {
        	auto Widget = GetWidget();
            SpriteInfo.ApplyUV(0, 0, Widget->GetWidth(), Widget->GetHeight(), 1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height);
            MarkUVDirty();
        }
	}
	if (UVRectControlMode != ELexUITextureUVRectControlMode::None)
	{
		if (InWidthChange || InHeightChange)
		{
			CheckSpriteData();
			MarkUVDirty();
		}
	}
    if (InPivotChange || InWidthChange || InHeightChange)
    {
        MarkVertexPositionDirty();
    }
}


void ULexTexture::SetDrawType(ELexUISpriteDrawType Value)
{
	if (DrawType != Value)
	{
		DrawType = Value;
		MarkVerticesDirty(true, true, true, true);
	}
}
void ULexTexture::SetSpriteInfo(FLexUISpriteInfo Value) 
{
	if (SpriteInfo != Value)
	{
		SpriteInfo = Value;
		MarkUVDirty();
		CheckSpriteData();
	}
}

void ULexTexture::SetUVRect(FVector4 Value)
{
	if (UVRect != Value)
	{
		UVRect = Value;
		MarkUVDirty();
		CheckSpriteData();
	}
}

void ULexTexture::SetTexture(UTexture* Value)
{
	if (Texture != Value)
	{
		Super::SetTexture(Value);
		if (UVRectControlMode == ELexUITextureUVRectControlMode::KeepAspectRatio_FitIn
			|| UVRectControlMode == ELexUITextureUVRectControlMode::KeepAspectRatio_Envelope
			)
		{
			MarkUVDirty();
		}
		CheckSpriteData();
	}
}

void ULexTexture::SetFillMethod(ELexUISpriteFillMethod Value)
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
void ULexTexture::SetFillOrigin(uint8 Value)
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
void ULexTexture::SetFillDirectionFlip(bool Value)
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
void ULexTexture::SetFillAmount(float Value)
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
void ULexTexture::SetUVRectControlMode(ELexUITextureUVRectControlMode Value)
{
	if (UVRectControlMode != Value)
	{
		UVRectControlMode = Value;
		MarkUVDirty();
		CheckSpriteData();
	}
}


