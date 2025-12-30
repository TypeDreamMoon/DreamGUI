// Copyright 2019-Present LexLiu. All Rights Reserved.


#include "Extensions/LexPostProcessRenderElement.h"

#include "Core/LexUIGeometry.h"
#include "Core/Components/LexVisualPostProcess.h"
#include "Engine/TextureRenderTarget2D.h"

void ULexPostProcessRenderElement::BeginPlay()
{
	Super::BeginPlay();
	if (!bHasRegisterPostProcessChangedEvent)
	{
		RegisterPostProcessChangedEvent();
	}
}

void ULexPostProcessRenderElement::EndPlay()
{
	Super::EndPlay();
	if (bHasRegisterPostProcessChangedEvent)
	{
		UnregisterPostProcessChangedEvent();
	}
}

#if WITH_EDITOR
void ULexPostProcessRenderElement::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	auto PropName = PropertyAboutToChange->GetFName();
	if (PropName == GET_MEMBER_NAME_CHECKED(ULexPostProcessRenderElement, PostProcess))
	{
		UnregisterPostProcessChangedEvent();
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
			RegisterPostProcessChangedEvent();
			MarkTextureDirty();
			UpdateSpriteData();
		}
	}
}
#endif

void ULexPostProcessRenderElement::RegisterPostProcessChangedEvent()
{
	if (bHasRegisterPostProcessChangedEvent)return;
	if (PostProcess.IsValid())
	{
		bHasRegisterPostProcessChangedEvent = true;
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

void ULexPostProcessRenderElement::UnregisterPostProcessChangedEvent()
{
	if (!bHasRegisterPostProcessChangedEvent)return;
	bHasRegisterPostProcessChangedEvent = false;
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
		if (PostProcess->GetRenderType() == ELexBackgroundBlurRenderType::RenderTarget)
		{
			return PostProcess->GetOutputRenderTarget();
		}
	}
	return nullptr;
}

UMaterialInterface* ULexPostProcessRenderElement::GetMaterialToCreateGeometry()
{
	return Material;
}

void ULexPostProcessRenderElement::OnBeforeCreateOrUpdateGeometry()
{
	if (!bHasRegisterPostProcessChangedEvent)
	{
		RegisterPostProcessChangedEvent();
	}
}

void ULexPostProcessRenderElement::OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged,
                                                    bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	if (InGeo.Texture != nullptr)
	{
		auto Widget = GetWidget();
		auto RenderCanvas = Widget->GetRenderCanvas();
		FLexUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
				Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
		if (InVertexUVChanged)
		{
			FLexUISpriteInfo SimpleRectSpriteInfo;		
			auto& vertices = InGeo.Vertices;
			vertices[0].TextureCoordinate[2] = SimpleRectSpriteInfo.GetUV0();
			vertices[1].TextureCoordinate[2] = SimpleRectSpriteInfo.GetUV1();
			vertices[2].TextureCoordinate[2] = SimpleRectSpriteInfo.GetUV2();
			vertices[3].TextureCoordinate[2] = SimpleRectSpriteInfo.GetUV3();
		}
	}
	else
	{
		InGeo.Clear();
	}
}
