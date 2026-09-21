// Copyright 2019-Present LexLiu. All Rights Reserved.


#include "Extensions/DreamPostProcessRenderElement.h"

#include "Core/DreamUIGeometry.h"
#include "Core/DreamUISpriteInfo.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Core/DreamUIWidgetRegistry.h"

FName UDreamPostProcessRenderElement::DreamUI_World2PostProcess_Row1 = FName(TEXT("DreamUI_World2PostProcess_Row1"));
FName UDreamPostProcessRenderElement::DreamUI_World2PostProcess_Row2 = FName(TEXT("DreamUI_World2PostProcess_Row2"));
FName UDreamPostProcessRenderElement::DreamUI_World2PostProcess_Row3 = FName(TEXT("DreamUI_World2PostProcess_Row3"));
FName UDreamPostProcessRenderElement::DreamUI_World2PostProcess_Row4 = FName(TEXT("DreamUI_World2PostProcess_Row4"));

void UDreamPostProcessRenderElement::BeginPlay()
{
	Super::BeginPlay();
	RegisterPostProcessChangedEvent();
}

void UDreamPostProcessRenderElement::EndPlay()
{
	Super::EndPlay();
	UnregisterPostProcessChangedEvent();
}

#if WITH_EDITOR
void UDreamPostProcessRenderElement::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	// Null means "an undo is about to restore everything", which no per-property branch below can
	// answer. See the note on UDreamWidget::PreEditChange.
	if (PropertyAboutToChange == nullptr)
	{
		return;
	}
	auto PropName = PropertyAboutToChange->GetFName();
	if (PropName == GET_MEMBER_NAME_CHECKED(UDreamPostProcessRenderElement, PostProcess))
	{
		UnregisterPostProcessChangedEvent();
	}
}
void UDreamPostProcessRenderElement::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropName = Property->GetFName();
		if (PropName == GET_MEMBER_NAME_CHECKED(UDreamPostProcessRenderElement, PostProcess))
		{
			RegisterPostProcessChangedEvent();
			MarkTextureDirty();
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDreamPostProcessRenderElement, Material))
		{
			MaterialInstanceDynamic = nullptr;
			MarkMaterialDirty();
		}
	}
}
#endif

void UDreamPostProcessRenderElement::RegisterPostProcessChangedEvent()
{
	if (bHasRegisterPostProcessChangedEvent)return;
	// The widget is asked for here exactly as Unregister asks for it nineteen lines below, which has
	// checked it since someone crashed on it: a post-process that is valid as a component can still
	// have no owning widget. Registering was the half that never caught up -- and it is the half that
	// runs first, so it is the one that decides whether Unregister ever has anything to remove.
	UDreamWidget* PostProcessWidget = PostProcess.IsValid() ? PostProcess->GetWidget() : nullptr;
	if (PostProcessWidget != nullptr)
	{
		bHasRegisterPostProcessChangedEvent = true;
		PostProcessWidget->GetDimensionChangedEvent().AddWeakLambda(this, [=, this](bool, bool, bool)
		{
			MarkCanvasUpdate();
		});
		PostProcessWidget->GetTransformChangedEvent().AddWeakLambda(this, [=, this]()
		{
			MarkCanvasUpdate();
		});
		PostProcess->GetRenderTargetChangedEvent().AddWeakLambda(this, [=, this](UTextureRenderTarget2D*)
		{
			MarkTextureDirty();
		});
	}
}

void UDreamPostProcessRenderElement::UnregisterPostProcessChangedEvent()
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

void UDreamPostProcessRenderElement::SetMaterialParameter()
{
	if (PostProcess.IsValid())
	{
		CheckMaterialInstanceDynamic();
		if (IsValid(MaterialInstanceDynamic))
		{
			if (PostProcess.IsValid())
			{
				if (PostProcess->GetRenderType() == EDreamBackgroundBlurRenderType::RenderTarget)
				{
					SetMaterialMatrixProperty(PostProcess.Get(), MaterialInstanceDynamic);
				}
			}
		}
	}
}

void UDreamPostProcessRenderElement::CheckMaterialInstanceDynamic()
{
	if (!IsValid(MaterialInstanceDynamic))
	{
		if (IsValid(Material))
		{
			MaterialInstanceDynamic = UMaterialInstanceDynamic::Create(Material, this);
			MaterialInstanceDynamic->SetFlags(RF_Transient);
		}
	}
}

void UDreamPostProcessRenderElement::SetMaterialMatrixProperty(UDreamVisualPostProcess* PostProcess, UMaterialInstanceDynamic* MID)
{
	// Same widget three times, and it is the same widget that RegisterPostProcessChangedEvent can
	// find absent; without it there is no transform to build the matrix from, and leaving the
	// parameters at their previous values is what a missing widget should cost.
	UDreamWidget* PostProcessWidget = PostProcess != nullptr ? PostProcess->GetWidget() : nullptr;
	if (PostProcessWidget == nullptr)
	{
		return;
	}
	auto WorldToPPTransform = PostProcessWidget->GetWorldTransform().Inverse();
	auto WorldToPPMatrix = FMatrix44f(WorldToPPTransform.ToMatrixWithScale());
	auto Size = PostProcessWidget->GetSize();
	auto Min = PostProcessWidget->GetLocalSpaceLeftBottomPoint();
	auto& M = WorldToPPMatrix.M;
	M[0][3] = Size.X;
	M[1][3] = Size.Y;
	M[2][3] = Min.X;
	M[3][3] = Min.Y;
	MID->SetVectorParameterValue(DreamUI_World2PostProcess_Row1, FVector4f(M[0][0], M[0][1], M[0][2], M[0][3]));
	MID->SetVectorParameterValue(DreamUI_World2PostProcess_Row2, FVector4f(M[1][0], M[1][1], M[1][2], M[1][3]));
	MID->SetVectorParameterValue(DreamUI_World2PostProcess_Row3, FVector4f(M[2][0], M[2][1], M[2][2], M[2][3]));
	MID->SetVectorParameterValue(DreamUI_World2PostProcess_Row4, FVector4f(M[3][0], M[3][1], M[3][2], M[3][3]));
}

void UDreamPostProcessRenderElement::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
	Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
}

void UDreamPostProcessRenderElement::OnTransformChanged(bool InPositionChanged, bool InScaleChanged)
{
	Super::OnTransformChanged(InPositionChanged, InScaleChanged);
}

UTexture* UDreamPostProcessRenderElement::GetTextureToCreateGeometry()
{
	if (PostProcess.IsValid())
	{
		if (PostProcess->GetRenderType() == EDreamBackgroundBlurRenderType::RenderTarget)
		{
			return PostProcess->GetOutputRenderTarget();
		}
	}
	return nullptr;
}

UMaterialInterface* UDreamPostProcessRenderElement::GetMaterialToCreateGeometry()
{
	CheckMaterialInstanceDynamic();
	return MaterialInstanceDynamic;
}

void UDreamPostProcessRenderElement::OnBeforeCreateOrUpdateGeometry()
{
	RegisterPostProcessChangedEvent();
	SetMaterialParameter();//this will set parameter no mater geometry changes
}

void UDreamPostProcessRenderElement::OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged,
                                                    bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	if (InGeo.Texture != nullptr)
	{
		FDreamUISpriteInfo SimpleRectSpriteInfo;		
		auto Widget = GetWidget();
		auto RenderCanvas = Widget->GetRenderCanvas();
		FDreamUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
				Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SimpleRectSpriteInfo, RenderCanvas, this, GetFinalColor(),
				InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
			);
	}
	else
	{
		InGeo.Clear();
	}
}

DECLARE_DREAM_GUI_VISUAL("PostProcessRenderElement", UDreamPostProcessRenderElement)
