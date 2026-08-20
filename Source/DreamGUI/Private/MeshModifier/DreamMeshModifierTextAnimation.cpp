// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamGUI/Public/MeshModifier/DreamMeshModifierTextAnimation.h"
#include "DreamGUI.h"
#include "Core/Components/DreamText.h"


UDreamMeshModifierTextAnimation::UDreamMeshModifierTextAnimation()
{
}
bool UDreamMeshModifierTextAnimation::CheckDreamText()
{
	if (IsValid(TextObject))return true;
	if (auto uiRenderable = GetVisualBatchMesh())
	{
		TextObject = Cast<UDreamText>(uiRenderable);
		if (IsValid(TextObject))
		{
			return true;
		}
	}
	return false;
}
void UDreamMeshModifierTextAnimation::OnRegister()
{
	Super::OnRegister();
	for (auto propertyItem : Properties)
	{
		if (IsValid(propertyItem))
		{
			propertyItem->Init();
		}
	}
}
void UDreamMeshModifierTextAnimation::OnUnregister()
{
	Super::OnUnregister();
	for (auto propertyItem : Properties)
	{
		if (IsValid(propertyItem))
		{
			propertyItem->Deinit();
		}
	}
}

#if WITH_EDITOR
void UDreamMeshModifierTextAnimation::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property != nullptr)
	{
		auto PropertyName = PropertyChangedEvent.Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamMeshModifierTextAnimation, SelectorOffset))
		{
			if (IsValid(Selector))
			{
				Selector->SetOffset(SelectorOffset);
			}
		}
	}
}
#endif

void UDreamMeshModifierTextAnimation::ModifyUIGeometry(
	FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
)
{
	if (!CheckDreamText())return;
	if (InGeometry.Vertices.Num() <= 0)return;
	if (InTriangleChanged || InUVChanged || InColorChanged || InVertexPositionChanged)
	{
		if (IsValid(Selector))
		{
			if (Selector->Select(TextObject, Selection))
			{
				if (InGeometry.Vertices.Num() <= 0)return;
				for (auto propertyItem : Properties)
				{
					if (IsValid(propertyItem))
					{
						propertyItem->ApplyProperty(TextObject, Selection, &InGeometry);
					}
				}
			}
		}
	}
}
UDreamText* UDreamMeshModifierTextAnimation::GetDreamText()
{
	CheckDreamText();
	return TextObject;
}
UDreamMeshModifierTextAnimation_Property* UDreamMeshModifierTextAnimation::GetProperty(int Index)const
{
	if (Index >= Properties.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[UUIEffectTextAnimation::GetProperty]index:%d out of range:%d"), Index, Properties.Num());
		return nullptr;
	}
	return Properties[Index];
}
void UDreamMeshModifierTextAnimation::SetSelector(UDreamMeshModifierTextAnimation_Selector* Value)
{
	if (Selector != Value)
	{
		Selector = Value;
		if (CheckDreamText())
		{
			TextObject->MarkVerticesDirty(true, true, true, true);
		}
	}
}
void UDreamMeshModifierTextAnimation::SetProperties(const TArray<UDreamMeshModifierTextAnimation_Property*>& Value)
{
	Properties = Value;
	if (CheckDreamText())
	{
		TextObject->MarkVerticesDirty(true, true, true, true);
	}
}
void UDreamMeshModifierTextAnimation::SetProperty(int Index, UDreamMeshModifierTextAnimation_Property* Value)
{
	if (Index >= Properties.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[UUIEffectTextAnimation::SetProperty]index:%d out of range:%d"), Index, Properties.Num());
		return;
	}
	if (Properties[Index] != Value)
	{
		Properties[Index] = Value;
		if (CheckDreamText())
		{
			TextObject->MarkVerticesDirty(true, true, true, true);
		}
	}
}

UDreamText* UDreamMeshModifierTextAnimation_Selector::GetDreamText()const
{
	GetUIEffectTextAnimation();
	return UIEffectTextAnimation.IsValid() ? UIEffectTextAnimation->GetDreamText() : nullptr;
}

UDreamMeshModifierTextAnimation* UDreamMeshModifierTextAnimation_Selector::GetUIEffectTextAnimation()const
{
	if (!UIEffectTextAnimation.IsValid())
	{
		if (auto outter = this->GetOuter())
		{
			UIEffectTextAnimation = Cast<UDreamMeshModifierTextAnimation>(outter);
		}
	}
	return UIEffectTextAnimation.Get();
}

#if WITH_EDITOR
void UDreamMeshModifierTextAnimation_Selector::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDreamMeshModifierTextAnimation_Selector::SetOffset(float Value)
{
	if (Offset != Value)
	{
		Offset = Value;
		if (auto DreamText = GetDreamText())
		{
			DreamText->MarkVertexPositionDirty();
		}
	}
}

float UDreamMeshModifierTextAnimation::GetSelectorOffset()const
{
	if (IsValid(Selector))
	{
		SelectorOffset = Selector->GetOffset();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[UUIEffectTextAnimation::GetSelectorOffset]selector is null!"));
	}
	return SelectorOffset;
}

void UDreamMeshModifierTextAnimation::SetSelectorOffset(float Value)
{
	if (IsValid(Selector))
	{
		Selector->SetOffset(Value);
		SelectorOffset = Value;
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[UUIEffectTextAnimation::SetSelectorOffset]selector is null!"));
	}
}

UDreamText* UDreamMeshModifierTextAnimation_Property::GetDreamText()
{
	if (auto outter = this->GetOuter())
	{
		if (auto uiTextAnimation = Cast<UDreamMeshModifierTextAnimation>(outter))
		{
			return uiTextAnimation->GetDreamText();
		}
	}
	return nullptr;
}
void UDreamMeshModifierTextAnimation_Property::MarkUITextPositionDirty()
{
	if (auto DreamText = GetDreamText())
	{
		DreamText->MarkVertexPositionDirty();
	}
}
