// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexCustomMesh.h"
#include "LGUI.h"
#include "Utils/LexUIUtils.h"
#include "Core/LexUIGeometry.h"
#include "Core/LexUICustomMeshSource.h"
#include "Core/Components/LexTextureBase.h"

#define LOCTEXT_NAMESPACE "UICustomMesh"

ULexCustomMesh::ULexCustomMesh(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
}

bool ULexCustomMesh::SupportDrawCallBatching()const
{
	if (IsValid(CustomMesh))
	{
		return CustomMesh->SupportDrawcallBatching();
	}
	return true;
}
void ULexCustomMesh::OnBeforeCreateOrUpdateGeometry()
{

}
UTexture* ULexCustomMesh::GetTextureToCreateGeometry()
{
	return FLexUIUtils::GetDefaultWhiteTexture();
}
void ULexCustomMesh::OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	if (IsValid(CustomMesh))
	{
		CustomMesh->UIGeo = &InGeo;
		CustomMesh->OnFillMesh(this, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
	}
}

#if WITH_EDITOR
bool ULexCustomMesh::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

	}

	return Super::CanEditChange(InProperty);
}
void ULexCustomMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (auto Property = PropertyChangedEvent.MemberProperty)
	{
		auto PropertyName = Property->GetFName();
	}
}
#endif

void ULexCustomMesh::SetCustomMesh(ULexUICustomMeshSource* Value)
{
	if (CustomMesh != Value)
	{
		CustomMesh = Value;
		MarkAllDirty();
	}
}

#undef LOCTEXT_NAMESPACE
