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
	if (!CacheVisualBatchMesh.IsValid())
	{
		if (auto Widget = GetWidget())
		{
			CacheVisualBatchMesh = Cast<UDreamVisualBatchMesh>(Widget->GetVisual());
		}
	}
	return CacheVisualBatchMesh.Get();
}
#if WITH_EDITOR
void UDreamMeshModifierBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (GetVisualBatchMesh())
	{
		CacheVisualBatchMesh->MarkVerticesDirty(true, true, true, true);
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
					if (GetVisualBatchMesh() != nullptr)
					{
						CacheVisualBatchMesh->MarkMeshModifierOrderChanged();
					}
				}
			});
		}
	}
	if (GetVisualBatchMesh() != nullptr)
	{
		CacheVisualBatchMesh->AddMeshModifier(this);
	}
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
	if (CacheVisualBatchMesh.IsValid())
	{
		CacheVisualBatchMesh->RemoveMeshModifier(this);
	}
}

void UDreamMeshModifierBase::SetEnable(bool Value)
{ 
	if (bEnable != Value)
	{
		bEnable = Value;
		if (GetVisualBatchMesh() != nullptr)
		{
			CacheVisualBatchMesh->MarkVerticesDirty(true, true, true, true);
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
