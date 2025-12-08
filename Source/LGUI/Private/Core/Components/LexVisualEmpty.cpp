// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexVisualEmpty.h"
#include "Core/LexUIGeometry.h"
#include "Utils/LexUIUtils.h"

#if WITH_EDITOR
void ULexVisualEmpty::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
}
void ULexVisualEmpty::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

ULexVisualEmpty::ULexVisualEmpty(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

UTexture* ULexVisualEmpty::GetTextureToCreateGeometry()
{
	return FLexUIUtils::GetDefaultWhiteTexture();
}
UMaterialInterface* ULexVisualEmpty::GetMaterialToCreateGeometry()
{
	return nullptr;
}

void ULexVisualEmpty::OnUpdateGeometry(FLexUIGeometry& InMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	
}

void ULexVisualEmpty::PostInitProperties()
{
	Super::PostInitProperties();
}

void ULexVisualEmpty::BeginDestroy()
{
	Super::BeginDestroy();
}

void ULexVisualEmpty::OnRegister()
{
	Super::OnRegister();
}

void ULexVisualEmpty::OnUnregister()
{
	Super::OnUnregister();
}
