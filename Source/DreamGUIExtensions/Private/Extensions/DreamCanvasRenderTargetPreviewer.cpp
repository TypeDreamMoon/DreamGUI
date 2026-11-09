// Copyright 2019-Present LexLiu. All Rights Reserved.


#include "Extensions/DreamCanvasRenderTargetPreviewer.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Materials/MaterialInterface.h"
#include "Core/DreamUIClipData.h"
#include "DreamUIRender/DreamUICompositor.h"

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
	// Null means "an undo is about to restore everything", which no per-property branch below can
	// answer. See the note on UDreamWidget::PreEditChange.
	if (PropertyAboutToChange == nullptr)
	{
		return;
	}
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

void UDreamCanvasRenderTargetPreviewer::PreEditUndo()
{
	// Undo replaces Canvas without the per-property edit notifications. Unsubscribe before the
	// pointer changes so both event ownership and the lazy registration flag start over together.
	UnregisterRenderTargetChangedEvent();
	Super::PreEditUndo();
}

void UDreamCanvasRenderTargetPreviewer::PostEditUndo()
{
	Super::PostEditUndo();
	if (IsValid(this))
	{
		RegisterRenderTargetChangedEvent();
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

void UDreamCanvasRenderTargetPreviewer::SetPreviewCanvas(UDreamCanvas* Value)
{
	if (Canvas.Get() == Value)
	{
		return;
	}
	//the change event is registered against the OLD canvas's render target, so it has to come off
	//before the pointer moves and go back on after -- the same order PostEditChangeProperty uses
	UnregisterRenderTargetChangedEvent();
	Canvas = Value;
	RegisterRenderTargetChangedEvent();
	MarkTextureDirty();
	UpdateSpriteData();
}

void UDreamCanvasRenderTargetPreviewer::SetPreviewMaterial(UMaterialInterface* Value)
{
	if (Material != Value)
	{
		Material = Value;
		MarkMaterialDirty();
		UpdateDrawsThroughComposite();
	}
}

void UDreamCanvasRenderTargetPreviewer::SetComposite(bool bInComposite, EDreamUIBlendMode InBlendMode, float InOpacity)
{
	InOpacity = FMath::Clamp(InOpacity, 0.0f, 1.0f);
	const bool bBlendChanged = CompositeBlendMode != InBlendMode || CompositeOpacity != InOpacity;
	bComposite = bInComposite;
	CompositeBlendMode = InBlendMode;
	CompositeOpacity = InOpacity;
	const bool bWas = bDrawsThroughComposite;
	UpdateDrawsThroughComposite();
	if (bWas == bDrawsThroughComposite && bBlendChanged)
	{
		// Read again by the canvas every frame: only drawn again.
		if (UDreamWidget* Widget = GetWidget())
		{
			if (UDreamCanvas* RenderCanvas = Widget->GetRenderCanvas())
			{
				RenderCanvas->MarkCanvasUpdate(false);
			}
		}
	}
}

void UDreamCanvasRenderTargetPreviewer::UpdateDrawsThroughComposite()
{
	const bool bNow = bComposite && Material == nullptr;
	if (bDrawsThroughComposite != bNow)
	{
		bDrawsThroughComposite = bNow;
		// The canvas's draw calls are made again: this element's entry gives way to a group's markers, or takes its place back.
		MarkMaterialDirty();
	}
}

bool UDreamCanvasRenderTargetPreviewer::GetCompositeSource(FDreamUICompositeDesc& OutDesc) const
{
	const UDreamWidget* Widget = GetWidget();
	const UDreamCanvas* RenderCanvas = Widget != nullptr ? Widget->GetRenderCanvas() : nullptr;
	UTexture* Target = Canvas.IsValid() && Canvas->GetActualRenderMode() == EDreamRenderMode::RenderTarget ? Canvas->GetRenderTarget() : nullptr;
	if (!bDrawsThroughComposite || Target == nullptr || RenderCanvas == nullptr || RenderCanvas->GetWidget() == nullptr
		|| Widget->GetWidth() <= 0.0f || Widget->GetHeight() <= 0.0f)
	{
		return false;
	}
	OutDesc.bExternal = true;
	OutDesc.BlendMode = CompositeBlendMode;
	OutDesc.Opacity = CompositeOpacity * GetFinalAlpha01();
	OutDesc.Textures = FDreamUIBuiltInDrawParams();
	OutDesc.Textures.MainTexture = Target;
	// The widget's rect, its first texel at its top left: from the canvas onto the widget, then across its rect.
	const FMatrix WidgetToWorld = Widget->GetWorldTransform().ToMatrixWithScale();
	const FMatrix CanvasToWorld = RenderCanvas->GetWidget()->GetWorldTransform().ToMatrixWithScale();
	const double Left = Widget->GetLocalSpaceLeft();
	const double Right = Widget->GetLocalSpaceRight();
	const double Bottom = Widget->GetLocalSpaceBottom();
	const double Top = Widget->GetLocalSpaceTop();
	const double Width = Right - Left;
	const double Height = Top - Bottom;
	// u = (y - Left) / Width and v = (Top - z) / Height, for a point (x, y, z) of the widget.
	const FMatrix LocalToUV(
		FPlane(0.0, 0.0, 0.0, 0.0),
		FPlane(1.0 / Width, 0.0, 0.0, 0.0),
		FPlane(0.0, -1.0 / Height, 0.0, 0.0),
		FPlane(-Left / Width, Top / Height, 0.0, 1.0));
	OutDesc.CanvasToUV = FMatrix44f(CanvasToWorld * WidgetToWorld.Inverse() * LocalToUV);
	OutDesc.CanvasToWorld = CanvasToWorld;
	FBox Rect(ForceInit);
	Rect += WidgetToWorld.TransformPosition(FVector(0.0, Left, Bottom));
	Rect += WidgetToWorld.TransformPosition(FVector(0.0, Right, Bottom));
	Rect += WidgetToWorld.TransformPosition(FVector(0.0, Left, Top));
	Rect += WidgetToWorld.TransformPosition(FVector(0.0, Right, Top));
	OutDesc.WorldBounds = Rect;
	// The clip this widget is drawn with, as its element would have been.
	OutDesc.MaskClipCoordinate = 0.0f;
	if (const TSharedPtr<FDreamUIClipData> Clip = Widget->GetClipData().Pin())
	{
		OutDesc.MaskClipCoordinate = static_cast<float>(Clip->GetBufferStartPos());
		OutDesc.Textures.ClipDataTexture = RenderCanvas->GetClipDataTexture();
		FDreamUIMaskTextures Textures;
		Clip->GetMaskTextures(Textures);
		OutDesc.Textures.MaskTexture0 = Textures.Slots[0].Get();
		OutDesc.Textures.MaskTexture1 = Textures.Slots[1].Get();
	}
	return true;
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
