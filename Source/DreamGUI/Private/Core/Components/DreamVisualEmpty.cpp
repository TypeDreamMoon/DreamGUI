// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/DreamVisualEmpty.h"
#include "Core/DreamUIGeometry.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUIWidgetRegistry.h"

#if WITH_EDITOR
void UDreamVisualEmpty::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
}
void UDreamVisualEmpty::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

UDreamVisualEmpty::UDreamVisualEmpty(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

UTexture* UDreamVisualEmpty::GetTextureToCreateGeometry()
{
	return FDreamUIUtils::GetDefaultWhiteTexture();
}
UMaterialInterface* UDreamVisualEmpty::GetMaterialToCreateGeometry()
{
	return nullptr;
}

void UDreamVisualEmpty::OnUpdateGeometry(FDreamUIGeometry& InMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	
}

void UDreamVisualEmpty::PostInitProperties()
{
	Super::PostInitProperties();
}

void UDreamVisualEmpty::BeginDestroy()
{
	Super::BeginDestroy();
}

void UDreamVisualEmpty::OnRegister()
{
	Super::OnRegister();
}

void UDreamVisualEmpty::OnUnregister()
{
	Super::OnUnregister();
}

// Draws nothing and still takes raycasts -- the invisible hit area every UI needs, and the one
// thing a plain `Widget` cannot be, having no visual to hit-test against at all.
DECLARE_DREAM_GUI_VISUAL("Empty", UDreamVisualEmpty)
