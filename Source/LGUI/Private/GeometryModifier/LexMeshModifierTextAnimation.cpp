// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "GeometryModifier/LexMeshModifierTextAnimation.h"
#include "LGUI.h"
#include "Core/Components/LexText.h"


ULexMeshModifierTextAnimation::ULexMeshModifierTextAnimation()
{
}
bool ULexMeshModifierTextAnimation::CheckUIText()
{
	if (IsValid(TextObject))return true;
	if (auto uiRenderable = GetLexVisual())
	{
		TextObject = Cast<ULexText>(uiRenderable);
		if (IsValid(TextObject))
		{
			return true;
		}
	}
	return false;
}
void ULexMeshModifierTextAnimation::BeginPlay()
{
	Super::BeginPlay();
	for (auto propertyItem : Properties)
	{
		if (IsValid(propertyItem))
		{
			propertyItem->Init();
		}
	}
}
void ULexMeshModifierTextAnimation::EndPlay()
{
	Super::EndPlay();
	for (auto propertyItem : Properties)
	{
		if (IsValid(propertyItem))
		{
			propertyItem->Deinit();
		}
	}
}

#if WITH_EDITOR
void ULexMeshModifierTextAnimation::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property != nullptr)
	{
		auto PropertyName = PropertyChangedEvent.Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexMeshModifierTextAnimation, SelectorOffset))
		{
			if (IsValid(Selector))
			{
				Selector->SetOffset(SelectorOffset);
			}
		}
	}
}
#endif

void ULexMeshModifierTextAnimation::ModifyUIGeometry(
	FLexUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
)
{
	if (!CheckUIText())return;
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
ULexText* ULexMeshModifierTextAnimation::GetUIText()
{
	CheckUIText();
	return TextObject;
}
ULexMeshModifierTextAnimation_Property* ULexMeshModifierTextAnimation::GetProperty(int Index)const
{
	if (Index >= Properties.Num())
	{
		UE_LOG(LGUI, Error, TEXT("[UUIEffectTextAnimation::GetProperty]index:%d out of range:%d"), Index, Properties.Num());
		return nullptr;
	}
	return Properties[Index];
}
void ULexMeshModifierTextAnimation::SetSelector(ULexMeshModifierTextAnimation_Selector* Value)
{
	if (Selector != Value)
	{
		Selector = Value;
		if (CheckUIText())
		{
			TextObject->MarkVerticesDirty(true, true, true, true);
		}
	}
}
void ULexMeshModifierTextAnimation::SetProperties(const TArray<ULexMeshModifierTextAnimation_Property*>& Value)
{
	Properties = Value;
	if (CheckUIText())
	{
		TextObject->MarkVerticesDirty(true, true, true, true);
	}
}
void ULexMeshModifierTextAnimation::SetProperty(int Index, ULexMeshModifierTextAnimation_Property* Value)
{
	if (Index >= Properties.Num())
	{
		UE_LOG(LGUI, Error, TEXT("[UUIEffectTextAnimation::SetProperty]index:%d out of range:%d"), Index, Properties.Num());
		return;
	}
	if (Properties[Index] != Value)
	{
		Properties[Index] = Value;
		if (CheckUIText())
		{
			TextObject->MarkVerticesDirty(true, true, true, true);
		}
	}
}

ULexText* ULexMeshModifierTextAnimation_Selector::GetUIText()const
{
	GetUIEffectTextAnimation();
	return UIEffectTextAnimation.IsValid() ? UIEffectTextAnimation->GetUIText() : nullptr;
}

ULexMeshModifierTextAnimation* ULexMeshModifierTextAnimation_Selector::GetUIEffectTextAnimation()const
{
	if (!UIEffectTextAnimation.IsValid())
	{
		if (auto outter = this->GetOuter())
		{
			UIEffectTextAnimation = Cast<ULexMeshModifierTextAnimation>(outter);
		}
	}
	return UIEffectTextAnimation.Get();
}

void ULexMeshModifierTextAnimation_Selector::SetOffset(float Value)
{
	if (Offset != Value)
	{
		Offset = Value;
		if (auto uiText = GetUIText())
		{
			uiText->MarkVertexPositionDirty();
		}
	}
}

float ULexMeshModifierTextAnimation::GetSelectorOffset()const
{
	if (IsValid(Selector))
	{
		SelectorOffset = Selector->GetOffset();
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[UUIEffectTextAnimation::GetSelectorOffset]selector is null!"));
	}
	return SelectorOffset;
}

void ULexMeshModifierTextAnimation::SetSelectorOffset(float Value)
{
	if (IsValid(Selector))
	{
		Selector->SetOffset(Value);
		SelectorOffset = Value;
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[UUIEffectTextAnimation::SetSelectorOffset]selector is null!"));
	}
}

ULexText* ULexMeshModifierTextAnimation_Property::GetUIText()
{
	if (auto outter = this->GetOuter())
	{
		if (auto uiTextAnimation = Cast<ULexMeshModifierTextAnimation>(outter))
		{
			return uiTextAnimation->GetUIText();
		}
	}
	return nullptr;
}
void ULexMeshModifierTextAnimation_Property::MarkUITextPositionDirty()
{
	if (auto uiText = GetUIText())
	{
		uiText->MarkVertexPositionDirty();
	}
}
