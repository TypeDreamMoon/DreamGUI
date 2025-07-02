// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUI/Public/Core/Components/UICustomMesh.h"
#include "LGUI.h"
#include "Utils/LexUIUtils.h"
#include "Core/LexUIGeometry.h"
#include "Core/LGUICustomMesh.h"
#include "LGUI/Public/Core/Components/UITextureBase.h"

#define LOCTEXT_NAMESPACE "UICustomMesh"

UUICustomMesh::UUICustomMesh(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
}

bool UUICustomMesh::SupportDrawCallBatching()const
{
	if (IsValid(CustomMesh))
	{
		return CustomMesh->SupportDrawcallBatching();
	}
	return true;
}
void UUICustomMesh::OnBeforeCreateOrUpdateGeometry()
{

}
UTexture* UUICustomMesh::GetTextureToCreateGeometry()
{
	return UUITextureBase::GetDefaultWhiteTexture();
}
void UUICustomMesh::OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	if (IsValid(CustomMesh))
	{
		CustomMesh->UIGeo = &InGeo;
		CustomMesh->OnFillMesh(this, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
	}
}

#if WITH_EDITOR
bool UUICustomMesh::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

	}

	return Super::CanEditChange(InProperty);
}
void UUICustomMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (auto Property = PropertyChangedEvent.MemberProperty)
	{
		auto PropertyName = Property->GetFName();
	}
}
#endif

void UUICustomMesh::SetCustomMesh(ULGUICustomMesh* Value)
{
	if (CustomMesh != Value)
	{
		CustomMesh = Value;
		MarkAllDirty();
	}
}

#undef LOCTEXT_NAMESPACE
