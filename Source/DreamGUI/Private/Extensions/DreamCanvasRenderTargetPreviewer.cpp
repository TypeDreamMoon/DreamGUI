// Copyright 2019-Present LexLiu. All Rights Reserved.


#include "Extensions/DreamCanvasRenderTargetPreviewer.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Core/DreamUIWidgetRegistry.h"

void UDreamCanvasRenderTargetPreviewer::BeginPlay()
{
	Super::BeginPlay();
	if (!bHasRegisterRenderTargetChangedEvent)
	{
		RegisterRenderTargetChangedEvent();
	}
}

void UDreamCanvasRenderTargetPreviewer::EndPlay()
{
	Super::EndPlay();
	if (bHasRegisterRenderTargetChangedEvent)
	{
		UnregisterRenderTargetChangedEvent();
	}
}

void UDreamCanvasRenderTargetPreviewer::OnRegister()
{
	Super::OnRegister();
	if (!bHasRegisterRenderTargetChangedEvent)
	{
		RegisterRenderTargetChangedEvent();
	}
}

void UDreamCanvasRenderTargetPreviewer::OnUnregister()
{
	Super::OnUnregister();
	if (bHasRegisterRenderTargetChangedEvent)
	{
		UnregisterRenderTargetChangedEvent();
	}
}

#if WITH_EDITOR
void UDreamCanvasRenderTargetPreviewer::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	auto PropName = PropertyAboutToChange->GetFName();
	if (PropName == GET_MEMBER_NAME_CHECKED(UDreamCanvasRenderTargetPreviewer, Canvas))
	{
		UnregisterRenderTargetChangedEvent();
	}
}
void UDreamCanvasRenderTargetPreviewer::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropName = Property->GetFName();
		if (PropName == GET_MEMBER_NAME_CHECKED(UDreamCanvasRenderTargetPreviewer, Canvas))
		{
			RegisterRenderTargetChangedEvent();
			MarkTextureDirty();
			UpdateSpriteData();
		}
	}
}
#endif


/*
 * AddWeakLambda, not AddLambda, and the difference is the whole bug: a plain lambda is bound to no
 * object, so the RemoveAll(this) in the unregister half could never match it. Register and
 * unregister are called in pairs -- on every OnRegister/OnUnregister, on BeginPlay/EndPlay, and
 * around every edit of the Canvas property -- and each pair left one more orphaned lambda on the
 * canvas's event, all of them still firing.
 *
 * The captured weak pointer was the author noticing the lifetime problem and solving the wrong half
 * of it: it kept the dead lambdas from dereferencing a destroyed previewer, which is why this never
 * crashed and never got found. Binding the object is what makes the removal work, and it makes the
 * hand-rolled weak capture redundant -- AddWeakLambda already declines to fire once the object is
 * gone. UDreamPostProcessRenderElement, the sibling doing the same job, had it right all along.
 */
void UDreamCanvasRenderTargetPreviewer::RegisterRenderTargetChangedEvent()
{
	if (bHasRegisterRenderTargetChangedEvent)return;
	if (Canvas.IsValid())
	{
		bHasRegisterRenderTargetChangedEvent = true;
		Canvas->GetRenderTargetChangedEvent().AddWeakLambda(this, [this](UTextureRenderTarget2D*)
		{
			MarkTextureDirty();
			UpdateSpriteData();
		});
		MarkTextureDirty();
		UpdateSpriteData();
	}
}

void UDreamCanvasRenderTargetPreviewer::UnregisterRenderTargetChangedEvent()
{
	if (!bHasRegisterRenderTargetChangedEvent)return;
	bHasRegisterRenderTargetChangedEvent = false;
	if (Canvas.IsValid())
	{
		Canvas->GetRenderTargetChangedEvent().RemoveAll(this);
		MarkTextureDirty();
	}
}

void UDreamCanvasRenderTargetPreviewer::UpdateSpriteData()
{
	if (Canvas.IsValid())
	{
		if (auto RenderTarget = Canvas->GetRenderTarget())
		{
			SpriteInfo.Width = RenderTarget->SizeX;
			SpriteInfo.Height = RenderTarget->SizeY;
			
			SpriteInfo.MinUV.X = 0;
			SpriteInfo.MinUV.Y = 0;
			SpriteInfo.MaxUV.Y = 1;
			SpriteInfo.MaxUV.X = 1;

			MarkVertexUVDirty();
		}
	}
}

void UDreamCanvasRenderTargetPreviewer::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
	Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	UpdateSpriteData();
}

void UDreamCanvasRenderTargetPreviewer::OnTransformChanged(bool InPositionChanged, bool InScaleChanged)
{
	Super::OnTransformChanged(InPositionChanged, InScaleChanged);
	UpdateSpriteData();
}

UTexture* UDreamCanvasRenderTargetPreviewer::GetTextureToCreateGeometry()
{
	if (Canvas.IsValid())
	{
		if (Canvas->GetActualRenderMode() == EDreamRenderMode::RenderTarget)
		{
			return Canvas->GetRenderTarget();
		}
	}
	return nullptr;
}

UMaterialInterface* UDreamCanvasRenderTargetPreviewer::GetMaterialToCreateGeometry()
{
	return Material;
}

void UDreamCanvasRenderTargetPreviewer::OnBeforeCreateOrUpdateGeometry()
{
	if (!bHasRegisterRenderTargetChangedEvent)
	{
		RegisterRenderTargetChangedEvent();
	}
}

void UDreamCanvasRenderTargetPreviewer::OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged,
                                                    bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	if (InGeo.Texture != nullptr)
	{
		auto Widget = GetWidget();
		auto RenderCanvas = Widget->GetRenderCanvas();
		FDreamUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
				Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
	}
	else
	{
		InGeo.Clear();
	}
}

DECLARE_DREAM_GUI_VISUAL("CanvasRenderTargetPreviewer", UDreamCanvasRenderTargetPreviewer)
