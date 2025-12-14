// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/UIRenderTarget.h"
#include "Core/Components/LexCanvas.h"
#include "LGUI.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Utils/LexUIUtils.h"
#include "PrefabSystem/LGUIPrefabManager.h"
#include "Core/LexUIGeometry.h"
#include "Core/LexUISpriteInfo.h"
#include "Core/LexUICustomMeshSource.h"
#include "Core/LexUIManager.h"

#define LOCTEXT_NAMESPACE "UIRenderTarget"

UUIRenderTarget::UUIRenderTarget(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	TargetCanvas = FLexUIComponentReference(ULexCanvas::StaticClass());
}

bool UUIRenderTarget::SupportDrawCallBatching()const
{
	if (IsValid(CustomMesh))
	{
		return CustomMesh->SupportDrawcallBatching();
	}
	else
	{
		return true;
	}
}
void UUIRenderTarget::OnBeforeCreateOrUpdateGeometry()
{

}
UTexture* UUIRenderTarget::GetTextureToCreateGeometry()
{
	UTexture* Result = nullptr;
	if (auto Canvas = GetCanvas())
	{
		Result = Canvas->GetRenderTarget();
	}
#if WITH_EDITOR
	if (!Result && !GetWorld()->IsGameWorld())//if not find valid texture (because canvas not create rendertarget yet, and edit mode not register the callback event), then get it next frame
	{
		ULexUIEditorManagerObject::AddOneShotTickFunction([this]() {
			MarkTextureDirty();
			});
	}
#endif
	return Result;
}
void UUIRenderTarget::OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	if (IsValid(CustomMesh))
	{
		CustomMesh->UIGeo = &InGeo;
		CustomMesh->OnFillMesh(this, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
	}
	else
	{
		auto Widget = GetWidget();
		static FLexUISpriteInfo SpriteInfo;
		FLexUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
			Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, Widget->GetRenderCanvas(), this, GetFinalColor(),
			InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
		);
	}
}

void UUIRenderTarget::BeginPlay()
{
	Super::BeginPlay();
	if (auto Canvas = GetCanvas())
	{
		Canvas->GetRenderTargetChangedEvent().AddWeakLambda(this, [this](UTextureRenderTarget2D* RenderTarget) {
			GetWidget()->SetWidgetActive(true);
			this->MarkTextureDirty();
			});
	}
}

ULexCanvas* UUIRenderTarget::GetTargetCanvas_Implementation()const
{
	return GetCanvas();
}
bool UUIRenderTarget::PerformLineTrace_Implementation(const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV)
{
	if (IsValid(CustomMesh))
	{
		bool Result = CustomMesh->GetHitUV(this, InHitFaceIndex, InHitPoint, InLineStart, InLineEnd, OutHitUV);
		OutHitUV.Y = 1.0f - OutHitUV.Y;
		return Result;
	}
	else
	{
		auto Widget = GetWidget();
		// Find the hit location on the component
		FVector ComponentHitLocation = Widget->GetComponentTransform().InverseTransformPosition(InHitPoint);

		// Convert the 3D position of component space, into the 2D equivalent
		auto LocationRelativeToLeftBottom = FVector2D(ComponentHitLocation.Y, ComponentHitLocation.Z) - Widget->GetLocalSpaceLeftBottomPoint();
		auto Location01 = LocationRelativeToLeftBottom / FVector2D(Widget->GetWidth(), Widget->GetHeight());

		OutHitUV = Location01;
		return true;
	}
}

#if WITH_EDITOR
bool UUIRenderTarget::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

	}

	return Super::CanEditChange(InProperty);
}
void UUIRenderTarget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (auto Property = PropertyChangedEvent.MemberProperty)
	{
		auto PropertyName = Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UUIRenderTarget, TargetCanvas))
		{
			if (!TargetCanvas.IsValidComponentReference())
			{
				TargetCanvasObject = nullptr;
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIRenderTarget, CustomMesh))
		{
			if (IsValid(CustomMesh))//custom mesh use geometry raycast to get precise uv
			{
				this->SetRaycastType(ELexVisualRaycastType::Mesh);
			}
		}
	}
}
#endif

ULexCanvas* UUIRenderTarget::GetCanvas()const
{
	if (TargetCanvasObject.IsValid())
	{
		return TargetCanvasObject.Get();
	}
	if (!TargetCanvas.IsValidComponentReference())
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d TargetCanvas not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	auto Canvas = TargetCanvas.GetComponent<ULexCanvas>();
	if (Canvas == nullptr)
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d TargetCanvas not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	if (!Canvas->IsRootCanvas())
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d TargetCanvas must be a root canvas!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	if (Canvas->GetRenderMode() != ELexRenderMode::RenderTarget)
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d TargetCanvas's render mode must be RenderTarget!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	TargetCanvasObject = Canvas;
	return Canvas;
}

void UUIRenderTarget::SetCanvas(ULexCanvas* Value)
{
	if (TargetCanvasObject.Get() != Value)
	{
		TargetCanvasObject = Value;
	}
}

#undef LOCTEXT_NAMESPACE
