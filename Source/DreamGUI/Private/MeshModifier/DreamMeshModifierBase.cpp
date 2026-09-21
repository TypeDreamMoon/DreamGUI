// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamGUI/Public/MeshModifier/DreamMeshModifierBase.h"
#include "DreamGUI.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"

UDreamMeshModifierBase::UDreamMeshModifierBase()
{
	
}

UDreamVisualBatchMesh* UDreamMeshModifierBase::GetVisualBatchMesh()const
{
	// Resolved on every ask rather than remembered from the first one, because a widget's visual is
	// replaceable and nothing tells a modifier when it has been replaced: CreateNewVisual swaps the
	// widget's visual and re-registers only the visual itself. A weak pointer does not notice either,
	// since the old visual is not destroyed but merely orphaned, so it stays valid forever -- and a
	// modifier still holding it goes on dirtying a mesh nobody draws while the mesh that IS drawn
	// never learns the modifier exists. Both halves of that are silent.
	UDreamVisualBatchMesh* Current = nullptr;
	if (auto Widget = GetWidget())
	{
		Current = Cast<UDreamVisualBatchMesh>(Widget->GetVisual());
		if (!IsValid(Current))
		{
			Current = nullptr;
		}
	}

	UDreamVisualBatchMesh* Cached = CacheVisualBatchMesh.Get();
	if (Current != Cached)
	{
		// Written before the registration moves, so that a nested resolve reached through the calls
		// below sees a cache that already agrees and stops there. Those calls dirty the mesh, and a
		// batch mesh is entitled to be walking its own modifier list while that happens; the list
		// being walked is only ever the one this would ADD to, and AddUnique is a no-op for a
		// modifier already in it, so no iteration has its array grown underneath it.
		CacheVisualBatchMesh = Current;
		if (bRegisteredWithVisual)
		{
			// Moving the registration here instead of leaving it in OnRegister is what makes the two
			// orders a modifier and a visual can appear in equivalent. A modifier added to a widget
			// that has no visual yet used to register with nothing and then stay inert for the rest
			// of its life, which reads on screen as a component that simply does not work.
			auto* Self = const_cast<UDreamMeshModifierBase*>(this);
			if (Cached != nullptr)
			{
				Cached->RemoveMeshModifier(Self);
			}
			if (Current != nullptr)
			{
				Current->AddMeshModifier(Self);
			}
		}
	}
	return Current;
}
#if WITH_EDITOR
void UDreamMeshModifierBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Mesh = GetVisualBatchMesh())
	{
		Mesh->MarkVerticesDirty(true, true, true, true);
	}
}
#endif

void UDreamMeshModifierBase::OnRegister()
{
	Super::OnRegister();
	if (auto Widget = GetWidget())
	{
		if (!ComponentsChangedDelegateHandle.IsValid())
		{
			ComponentsChangedDelegateHandle = Widget->GetComponentsChangedEvent().AddWeakLambda(this,
				[this](EDreamWidgetComponentsChangedType ChangedType)
			{
				if (ChangedType == EDreamWidgetComponentsChangedType::Reorder)
				{
					if (auto Mesh = GetVisualBatchMesh())
					{
						Mesh->MarkMeshModifierOrderChanged();
					}
				}
			});
		}
	}
	// The flag goes up before the resolve rather than after it, because the resolve is now what
	// performs the registration.
	bRegisteredWithVisual = true;
	GetVisualBatchMesh();
}

void UDreamMeshModifierBase::OnUnregister()
{
	Super::OnUnregister();
	if (auto Widget = GetWidget())
	{
		if (ComponentsChangedDelegateHandle.IsValid())
		{
			Widget->GetComponentsChangedEvent().Remove(ComponentsChangedDelegateHandle);
			ComponentsChangedDelegateHandle.Reset();
		}
	}
	// One last resolve while the flag is still up, so that a visual swapped in behind this modifier's
	// back is the one asked to forget it. Only then is the flag dropped: every setter on this family
	// ends in a resolve, and any of them running after this point would otherwise quietly re-register
	// a component the widget has already released.
	UDreamVisualBatchMesh* Mesh = GetVisualBatchMesh();
	bRegisteredWithVisual = false;
	if (Mesh != nullptr)
	{
		Mesh->RemoveMeshModifier(this);
	}
	CacheVisualBatchMesh.Reset();
}

void UDreamMeshModifierBase::SetEnable(bool Value)
{ 
	if (bEnable != Value)
	{
		bEnable = Value;
		if (auto Mesh = GetVisualBatchMesh())
		{
			Mesh->MarkVerticesDirty(true, true, true, true);
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("UIGeometryModifierBase_Blueprint.ModifyUIGeometry"), STAT_UIGeometryModifierBase_ModifyUIGeometry, STATGROUP_DreamGUI);
void UDreamMeshModifierBase::ModifyUIGeometry(
	FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		if (GeometryModifierHelper == nullptr)
		{
			GeometryModifierHelper = NewObject<UDreamVisualBatchMeshModifierHelper>(this);
		}
		GeometryModifierHelper->UIGeo = &InGeometry;
		SCOPE_CYCLE_COUNTER(STAT_UIGeometryModifierBase_ModifyUIGeometry);
		ReceiveModifyUIGeometry(GeometryModifierHelper);
	}
}



float UDreamVisualBatchMeshModifierHelper::UITextHelperFunction_GetCharHorizontalPositionRatio01(UDreamText* InUIText, int InCharIndex)const
{
	if (InUIText == nullptr)
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_GetCharHorizontalPositionRatio01]InUIText not valid!"));
		return 0;
	}
	auto& CharPropertyArray = InUIText->GetCharPropertyArray();
#if !UE_BUILD_SHIPPING
	if (InCharIndex < 0 || InCharIndex >= CharPropertyArray.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_GetCharHorizontalPositionRatio01]InCharIndex out of range, InCharIndex: %d, ArrayNum: %d"), InCharIndex, CharPropertyArray.Num());
		return 0;
	}
#endif
	auto& originVertices = UIGeo->OriginVertices;
	auto& charPropertyItem = CharPropertyArray[InCharIndex];
	int startVertIndex = charPropertyItem.StartVertIndex;
	int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;

	auto Widget = InUIText->GetWidget();
	float leftPos = Widget->GetLocalSpaceLeft();
	float charPivotPos = 0;
	for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
	{
		charPivotPos += originVertices[vertIndex].Position.Y;
	}
	charPivotPos /= charPropertyItem.VertCount;
	return (charPivotPos - leftPos) / Widget->GetWidth();
}

void UDreamVisualBatchMeshModifierHelper::UITextHelperFunction_GetCharGeometry_AbsolutePosition(UDreamText* InUIText, int InCharIndex, FVector& OutPosition)const
{
	if (InUIText == nullptr)
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_GetCharGeometry_AbsolutePosition]InUIText not valid!"));
		return;
	}
	auto& CharPropertyArray = InUIText->GetCharPropertyArray();
#if !UE_BUILD_SHIPPING
	if (InCharIndex < 0 || InCharIndex >= CharPropertyArray.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Transform]InCharIndex out of range, InCharIndex: %d, ArrayNum: %d"), InCharIndex, CharPropertyArray.Num());
		return;
	}
#endif
	auto& originVertices = UIGeo->OriginVertices;
	auto& charPropertyItem = CharPropertyArray[InCharIndex];
	int startVertIndex = charPropertyItem.StartVertIndex;
	int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;

	float charPivotPosH = 0;
	for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
	{
		charPivotPosH += originVertices[vertIndex].Position.Y;
	}
	charPivotPosH /= charPropertyItem.VertCount;
	OutPosition = FVector(0, charPivotPosH, 0);
}

void UDreamVisualBatchMeshModifierHelper::UITextHelperFunction_ModifyCharGeometry_Transform(UDreamText* InUIText, int InCharIndex
	, EDreamUIMeshModifierHelper_TextPositionType InPositionType
	, const FVector& InPosition
	, const FRotator& InRotator
	, const FVector& InScale
)
{
	if (InUIText == nullptr)
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Transform]InUIText not valid!"));
		return;
	}
	auto& CharPropertyArray = InUIText->GetCharPropertyArray();
#if !UE_BUILD_SHIPPING
	if (InCharIndex < 0 || InCharIndex >= CharPropertyArray.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Transform]InCharIndex out of range, InCharIndex: %d, ArrayNum: %d"), InCharIndex, CharPropertyArray.Num());
		return;
	}
#endif
	auto& originVertices = UIGeo->OriginVertices;
	auto& charPropertyItem = CharPropertyArray[InCharIndex];
	int startVertIndex = charPropertyItem.StartVertIndex;
	int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;

	float charPivotPosH = 0;
	for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
	{
		charPivotPosH += originVertices[vertIndex].Position.Y;
	}
	charPivotPosH /= charPropertyItem.VertCount;
	auto charPivotPos = FVector3f(0, charPivotPosH, 0);
	switch (InPositionType)
	{
	default:
	case EDreamUIMeshModifierHelper_TextPositionType::Relative:
	{
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			pos += (FVector3f)InPosition;
		}
	}
	break;
	case EDreamUIMeshModifierHelper_TextPositionType::Absolute:
	{
		auto charPivotOffset = charPivotPos - (FVector3f)InPosition;
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			pos -= charPivotOffset;
		}
	}
	break;
	}

	if (InRotator != FRotator::ZeroRotator)
	{
		auto calcRotationMatrix = FRotationMatrix44f((FRotator3f)InRotator);
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			auto vector = pos - (FVector3f)InPosition;
			pos = (FVector3f)InPosition + calcRotationMatrix.TransformPosition(vector);
		}
	}

	if (InScale != FVector::OneVector)
	{
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			auto vector = pos - (FVector3f)InPosition;
			pos = (FVector3f)InPosition + vector * (FVector3f)InScale;
		}
	}
}
void UDreamVisualBatchMeshModifierHelper::UITextHelperFunction_ModifyCharGeometry_Position(UDreamText* InUIText, int InCharIndex, const FVector& InPosition, EDreamUIMeshModifierHelper_TextPositionType InPositionType)
{
	if (InUIText == nullptr)
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Position]InUIText not valid!"));
		return;
	}
	auto& CharPropertyArray = InUIText->GetCharPropertyArray();
#if !UE_BUILD_SHIPPING
	if (InCharIndex < 0 || InCharIndex >= CharPropertyArray.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Position]InCharIndex out of range, InCharIndex: %d, ArrayNum: %d"), InCharIndex, CharPropertyArray.Num());
		return;
	}
#endif
	auto& originVertices = UIGeo->OriginVertices;
	auto& charPropertyItem = CharPropertyArray[InCharIndex];
	int startVertIndex = charPropertyItem.StartVertIndex;
	int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;

	switch (InPositionType)
	{
	default:
	case EDreamUIMeshModifierHelper_TextPositionType::Relative:
	{
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			pos += (FVector3f)InPosition;
		}
	}
	break;
	case EDreamUIMeshModifierHelper_TextPositionType::Absolute:
	{
		auto charCenterPos = FVector3f::ZeroVector;
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			charCenterPos += originVertices[vertIndex].Position;
		}
		charCenterPos /= charPropertyItem.VertCount;
		auto centerOffset = charCenterPos - (FVector3f)InPosition;
		for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
		{
			auto& pos = originVertices[vertIndex].Position;
			pos -= centerOffset;
		}
	}
	break;
	}
}
void UDreamVisualBatchMeshModifierHelper::UITextHelperFunction_ModifyCharGeometry_Rotate(UDreamText* InUIText, int InCharIndex, const FRotator& InRotator)
{
	if (InUIText == nullptr)
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Rotate]InUIText not valid!"));
		return;
	}
	auto& CharPropertyArray = InUIText->GetCharPropertyArray();
#if !UE_BUILD_SHIPPING
	if (InCharIndex < 0 || InCharIndex >= CharPropertyArray.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Rotate]InCharIndex out of range, InCharIndex: %d, ArrayNum: %d"), InCharIndex, CharPropertyArray.Num());
		return;
	}
#endif
	auto& originVertices = UIGeo->OriginVertices;
	auto& charPropertyItem = CharPropertyArray[InCharIndex];
	int startVertIndex = charPropertyItem.StartVertIndex;
	int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;
	float charPivotPos = 0;
	for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
	{
		charPivotPos += originVertices[vertIndex].Position.Y;
	}
	charPivotPos /= charPropertyItem.VertCount;

	auto calcRotationMatrix = FRotationMatrix44f((FRotator3f)InRotator);
	for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
	{
		auto& pos = originVertices[vertIndex].Position;
		auto vector = pos - FVector3f(0, charPivotPos, 0);
		pos = FVector3f(0, charPivotPos, 0) + calcRotationMatrix.TransformPosition(vector);
	}
}
void UDreamVisualBatchMeshModifierHelper::UITextHelperFunction_ModifyCharGeometry_Scale(UDreamText* InUIText, int InCharIndex, const FVector& InScale)
{
	if (InUIText == nullptr)
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Scale]InUIText not valid!"));
		return;
	}
	auto& CharPropertyArray = InUIText->GetCharPropertyArray();
#if !UE_BUILD_SHIPPING
	if (InCharIndex < 0 || InCharIndex >= CharPropertyArray.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Scale]InCharIndex out of range, InCharIndex: %d, ArrayNum: %d"), InCharIndex, CharPropertyArray.Num());
		return;
	}
#endif
	auto& originVertices = UIGeo->OriginVertices;
	auto& charPropertyItem = CharPropertyArray[InCharIndex];
	int startVertIndex = charPropertyItem.StartVertIndex;
	int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;

	float charPivotPosH = 0;
	for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
	{
		charPivotPosH += originVertices[vertIndex].Position.Y;
	}
	charPivotPosH /= charPropertyItem.VertCount;
	auto charPivotPos = FVector3f(0, charPivotPosH, 0);

	auto calcScale = (FVector3f)InScale;
	for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
	{
		auto& pos = originVertices[vertIndex].Position;
		auto vector = pos - charPivotPos;
		pos = charPivotPos + vector * calcScale;
	}
}
void UDreamVisualBatchMeshModifierHelper::UITextHelperFunction_ModifyCharGeometry_Color(UDreamText* InUIText, int InCharIndex, const FColor& InColor)
{
	if (InUIText == nullptr)
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Color]InUIText not valid!"));
		return;
	}
	auto& CharPropertyArray = InUIText->GetCharPropertyArray();
#if !UE_BUILD_SHIPPING
	if (InCharIndex < 0 || InCharIndex >= CharPropertyArray.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Color]InCharIndex out of range, InCharIndex: %d, ArrayNum: %d"), InCharIndex, CharPropertyArray.Num());
		return;
	}
#endif
	auto& vertices = UIGeo->Vertices;
	auto& charPropertyItem = CharPropertyArray[InCharIndex];
	int startVertIndex = charPropertyItem.StartVertIndex;
	int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;

	for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
	{
		auto& color = vertices[vertIndex].Color;
		color = InColor;
	}
}
void UDreamVisualBatchMeshModifierHelper::UITextHelperFunction_ModifyCharGeometry_Alpha(UDreamText* InUIText, int InCharIndex, const float& InAlpha)
{
	if (InUIText == nullptr)
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Alpha]InUIText not valid!"));
		return;
	}
	auto& CharPropertyArray = InUIText->GetCharPropertyArray();
#if !UE_BUILD_SHIPPING
	if (InCharIndex < 0 || InCharIndex >= CharPropertyArray.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[UDreamGUIGeometryModifierHelper::UITextHelperFunction_ModifyCharGeometry_Alpha]InCharIndex out of range, InCharIndex: %d, ArrayNum: %d"), InCharIndex, CharPropertyArray.Num());
		return;
	}
#endif
	auto& vertices = UIGeo->Vertices;
	auto& charPropertyItem = CharPropertyArray[InCharIndex];
	int startVertIndex = charPropertyItem.StartVertIndex;
	int endVertIndex = charPropertyItem.StartVertIndex + charPropertyItem.VertCount;

	for (int vertIndex = startVertIndex; vertIndex < endVertIndex; vertIndex++)
	{
		auto& color = vertices[vertIndex].Color;
		color.A = InAlpha;
	}
}
