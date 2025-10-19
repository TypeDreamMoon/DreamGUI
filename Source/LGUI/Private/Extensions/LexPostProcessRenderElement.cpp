// Copyright 2019-Present LexLiu. All Rights Reserved.


#include "Extensions/LexPostProcessRenderElement.h"

#include "Core/LexUIGeometry.h"
#include "Core/Components/LexVisualPostProcess.h"
#include "Engine/TextureRenderTarget2D.h"

void ULexPostProcessRenderElement::BeginPlay()
{
	Super::BeginPlay();
	if (!bHasRegisterPostProcessUpdateEvent)
	{
		RegisterPostProcessUpdateEvent();
	}
}

void ULexPostProcessRenderElement::EndPlay()
{
	Super::EndPlay();
	if (bHasRegisterPostProcessUpdateEvent)
	{
		UnregisterPostProcessUpdateEvent();
	}
}

#if WITH_EDITOR
void ULexPostProcessRenderElement::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	auto PropName = PropertyAboutToChange->GetFName();
	if (PropName == GET_MEMBER_NAME_CHECKED(ULexPostProcessRenderElement, PostProcess))
	{
		UnregisterPostProcessUpdateEvent();
	}
}
void ULexPostProcessRenderElement::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropName = Property->GetFName();
		if (PropName == GET_MEMBER_NAME_CHECKED(ULexPostProcessRenderElement, PostProcess))
		{
			RegisterPostProcessUpdateEvent();
		}
	}
}
#endif

void ULexPostProcessRenderElement::RegisterPostProcessUpdateEvent()
{
	if (PostProcess.IsValid())
	{
		bHasRegisterPostProcessUpdateEvent = true;
		PostProcess->GetWidget()->GetDimensionChangedEvent().AddWeakLambda(this, [=, this](bool, bool, bool)
		{
			UpdateSpriteData();
		});
		PostProcess->GetWidget()->GetTransformChangedEvent().AddWeakLambda(this, [=, this]()
		{
			UpdateSpriteData();
		});
		PostProcess->GetRenderTargetChangedEvent().AddWeakLambda(this, [=, this](UTextureRenderTarget2D*)
		{
			MarkTextureDirty();
			UpdateSpriteData();
		});
	}
}

void ULexPostProcessRenderElement::UnregisterPostProcessUpdateEvent()
{
	bHasRegisterPostProcessUpdateEvent = false;
	if (PostProcess.IsValid() && PostProcess->GetWidget())
	{
		PostProcess->GetWidget()->GetDimensionChangedEvent().RemoveAll(this);
		PostProcess->GetWidget()->GetTransformChangedEvent().RemoveAll(this);
		PostProcess->GetRenderTargetChangedEvent().RemoveAll(this);
	}
}

void ULexPostProcessRenderElement::UpdateSpriteData()
{
	if (PostProcess.IsValid())
	{
		if (auto RenderTarget = PostProcess->GetOutputRenderTarget())
		{
			SpriteInfo.Width = RenderTarget->SizeX;
			SpriteInfo.Height = RenderTarget->SizeY;
			
			auto ThisToPostProcessSpace = PostProcess->GetWidget()->GetComponentTransform().Inverse() * GetWidget()->GetComponentTransform();
			auto ThisLeftBottomPoint2D = this->GetWidget()->GetLocalSpaceLeftBottomPoint();
			auto ThisLeftBottomPointWorld = ThisToPostProcessSpace.TransformPosition(FVector(0, ThisLeftBottomPoint2D.X, ThisLeftBottomPoint2D.Y));
			auto LeftBottomPoint2D = FVector2D(ThisLeftBottomPointWorld.Y, ThisLeftBottomPointWorld.Z);
			auto PostProcessLeftBottomPoint2D = PostProcess->GetWidget()->GetLocalSpaceLeftBottomPoint();
			LeftBottomPoint2D -= PostProcessLeftBottomPoint2D;
			
			SpriteInfo.MinUV.X = LeftBottomPoint2D.X / PostProcess->GetWidget()->GetWidth();
			SpriteInfo.MaxUV.Y = 1 - LeftBottomPoint2D.Y / PostProcess->GetWidget()->GetHeight();
			SpriteInfo.MinUV.Y = SpriteInfo.MaxUV.Y - this->GetWidget()->GetHeight() / PostProcess->GetWidget()->GetHeight();
			SpriteInfo.MaxUV.X = this->GetWidget()->GetWidth() / PostProcess->GetWidget()->GetWidth() + SpriteInfo.MinUV.X;

			MarkUVDirty();
		}
	}
}

void ULexPostProcessRenderElement::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
	Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	UpdateSpriteData();
}

void ULexPostProcessRenderElement::OnTransformChanged()
{
	Super::OnTransformChanged();
	UpdateSpriteData();
}

UTexture* ULexPostProcessRenderElement::GetTextureToCreateGeometry()
{
	if (PostProcess.IsValid())
	{
		return PostProcess->GetOutputRenderTarget();
	}
	return nullptr;
}

UMaterialInterface* ULexPostProcessRenderElement::GetMaterialToCreateGeometry()
{
	return Material;
}

void ULexPostProcessRenderElement::OnBeforeCreateOrUpdateGeometry()
{
	if (!bHasRegisterPostProcessUpdateEvent)
	{
		RegisterPostProcessUpdateEvent();
	}
}

void ULexPostProcessRenderElement::OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged,
                                                    bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = GetWidget();
	auto RenderCanvas = Widget->GetRenderCanvas();
	FLexUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
			Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, RenderCanvas, this, GetFinalColor(),
			InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
		);
}
