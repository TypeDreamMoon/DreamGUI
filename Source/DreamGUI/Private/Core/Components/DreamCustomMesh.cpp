// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/DreamCustomMesh.h"
#include "DreamGUI.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUICustomMeshSource.h"
#include "Core/Components/DreamTextureBase.h"

#define LOCTEXT_NAMESPACE "UICustomMesh"

UDreamCustomMesh::UDreamCustomMesh(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
}

bool UDreamCustomMesh::SupportDrawCallBatching()const
{
	if (IsValid(CustomMesh))
	{
		return CustomMesh->SupportDrawcallBatching();
	}
	return true;
}
void UDreamCustomMesh::OnBeforeCreateOrUpdateGeometry()
{

}
UTexture* UDreamCustomMesh::GetTextureToCreateGeometry()
{
	return FDreamUIUtils::GetDefaultWhiteTexture();
}
void UDreamCustomMesh::OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	if (IsValid(CustomMesh))
	{
		CustomMesh->UIGeo = &InGeo;
		CustomMesh->OnFillMesh(this, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
	}
}

#if WITH_EDITOR
bool UDreamCustomMesh::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

	}

	return Super::CanEditChange(InProperty);
}
void UDreamCustomMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (auto Property = PropertyChangedEvent.MemberProperty)
	{
		auto PropertyName = Property->GetFName();
	}
}
#endif

void UDreamCustomMesh::SetCustomMesh(UDreamUICustomMeshSource* Value)
{
	if (CustomMesh != Value)
	{
		CustomMesh = Value;
		MarkAllDirty();
	}
}

#undef LOCTEXT_NAMESPACE
