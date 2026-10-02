// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "MeshModifier/DreamMeshModifierTextAnimation.h"
#include "DreamGUI.h"
#include "Core/Components/DreamText.h"


UDreamMeshModifierTextAnimation::UDreamMeshModifierTextAnimation()
{
	// A modifier works when the geometry is built, never each frame or when its widget moves.
	DeclareTickUnused(StaticClass());
	DeclareTransformChangedUnused(StaticClass());
}
bool UDreamMeshModifierTextAnimation::CheckDreamText()
{
	// Re-derived every time instead of trusting the cache. The mesh accessor now follows a visual
	// that was swapped under the widget, and a text left behind by such a swap is still a perfectly
	// valid object -- so a cache-first check would go on animating the label nobody draws.
	TextObject = Cast<UDreamText>(GetVisualBatchMesh());
	return IsValid(TextObject);
}
void UDreamMeshModifierTextAnimation::SyncRegisteredText()
{
	UDreamText* Current = CheckDreamText() ? TextObject.Get() : nullptr;
	UDreamText* Previous = RegisteredText.Get();
	if (Current == Previous)
	{
		return;
	}
	if (Previous != nullptr)
	{
		Previous->UnregisterPerCharacterAnimation(this);
	}
	RegisteredText = Current;
	if (Current != nullptr)
	{
		Current->RegisterPerCharacterAnimation(this);
	}
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
	// Every property addresses glyphs by character index, which a ligature would merge.
	SyncRegisteredText();
}
void UDreamMeshModifierTextAnimation::OnUnregister()
{
	if (UDreamText* Registered = RegisteredText.Get())
	{
		Registered->UnregisterPerCharacterAnimation(this);
	}
	RegisteredText.Reset();
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

void UDreamMeshModifierTextAnimation::ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor)
{
	Super::ModifierWillChangeVertexData(OutTriangleIndices, OutVertexPosition, OutUV, OutColor);
	// Asked right before the text paints: the last moment a text this modifier has not registered with yet (its visual
	// was swapped, or there was none when the modifier registered) can still be told to lay out a glyph per character
	// for the geometry about to be built. Registering after the paint leaves that geometry with ligatures the
	// properties would index past, and the repaint the registration asks for is cleared with this update's flags.
	SyncRegisteredText();
}
void UDreamMeshModifierTextAnimation::ModifyUIGeometry(
	FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
)
{
	if (!CheckDreamText())return;
	if (RegisteredText.Get() != TextObject)
	{
		// Painted for a text this modifier has not told it animates the characters of, so possibly with ligatures the
		// properties cannot address by character index. ModifierWillChangeVertexData registers before every paint, so
		// this is only a guard: leave the geometry as it is rather than index past its characters.
		return;
	}
	if (InGeometry.Vertices.Num() <= 0)return;
	if (InTriangleChanged || InUVChanged || InColorChanged || InVertexPositionChanged)
	{
		if (IsValid(Selector))
		{
			if (Selector->Select(TextObject, Selection))
			{
				if (InGeometry.Vertices.Num() <= 0)return;
				// Every property reads GetCharPropertyArray()[i] for the selected range unchecked. A selector counts
				// characters from the text's tags and visible count, which the painted characters bound; keep the
				// range inside them so a count that ran ahead of the paint never reads past the array.
				const int32 PaintedCharCount = TextObject->GetCharPropertyArray().Num();
				Selection.EndCharCount = FMath::Min(Selection.EndCharCount, PaintedCharCount);
				Selection.EndCharCount = FMath::Min(Selection.EndCharCount, Selection.StartCharIndex + Selection.LerpValueArray.Num());
				if (Selection.StartCharIndex < 0 || Selection.StartCharIndex >= Selection.EndCharCount)return;
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
	// Both ends of the range, because the index arrives from Blueprint: an int pin defaults to 0 but
	// carries whatever arithmetic produced it, and a negative one indexes backwards out of the
	// allocation instead of tripping the guard.
	if (Index < 0 || Index >= Properties.Num())
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
	if (Index < 0 || Index >= Properties.Num())
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
