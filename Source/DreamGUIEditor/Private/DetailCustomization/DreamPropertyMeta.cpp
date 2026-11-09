// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DetailCustomization/DreamPropertyMeta.h"

#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DreamPropertyMeta"

namespace DreamPropertyMeta
{
	const FName Section(TEXT("DreamGUI_Section"));
	const FName Group(TEXT("DreamGUI_Group"));
	const FName Order(TEXT("DreamGUI_Order"));
	const FName Advanced(TEXT("DreamGUI_Advanced"));
	const FName Hidden(TEXT("DreamGUI_Hidden"));
	const FName Editor(TEXT("DreamGUI_Editor"));
	const FName Handle(TEXT("DreamGUI_Handle"));
	const FName LayerToggle(TEXT("DreamGUI_LayerToggle"));

	namespace Local
	{
		const FString* Find(const FProperty* InProperty, const FName& InKey)
		{
#if WITH_EDITORONLY_DATA
			return InProperty != nullptr ? InProperty->FindMetaData(InKey) : nullptr;
#else
			return nullptr;
#endif
		}

		/** A flag tag is on when it is there and not written false. */
		bool IsOn(const FProperty* InProperty, const FName& InKey)
		{
			const FString* Value = Find(InProperty, InKey);
			return Value != nullptr && !Value->Equals(TEXT("false"), ESearchCase::IgnoreCase) && !Value->Equals(TEXT("0"));
		}
	}

	ESection ParseSection(const FString& InText)
	{
		static const TPair<const TCHAR*, ESection> Names[] = {
			{ TEXT("Layer"), ESection::Layer },
			{ TEXT("Layout"), ESection::Layout },
			{ TEXT("Visual"), ESection::Visual },
			{ TEXT("Appearance"), ESection::Appearance },
			{ TEXT("Effect"), ESection::Effect },
			{ TEXT("Behavior"), ESection::Behavior },
			{ TEXT("Advanced"), ESection::Advanced },
		};
		const FString Trimmed = InText.TrimStartAndEnd();
		for (const TPair<const TCHAR*, ESection>& Name : Names)
		{
			if (Trimmed.Equals(Name.Key, ESearchCase::IgnoreCase))
			{
				return Name.Value;
			}
		}
		return ESection::None;
	}

	const FSectionInfo& GetSectionInfo(ESection InSection)
	{
		// The category names are the ones the widget's details always used, so a saved expansion state and every
		// customization that adds to one of them by name keep working. The order is the panel's, top to bottom.
		static const FSectionInfo Infos[] = {
			{ NAME_None, FText::GetEmpty(), 0, false },
			{ TEXT("DreamLayer"), LOCTEXT("Layer", "Layer"), -110, false },
			{ TEXT("DreamLayout"), LOCTEXT("Layout", "Layout"), -90, false },
			{ TEXT("Visual"), LOCTEXT("Visual", "Visual"), -80, false },
			{ TEXT("DreamAppearance"), LOCTEXT("Appearance", "Appearance"), -60, false },
			{ TEXT("DreamEffect"), LOCTEXT("Effect", "Effects"), -55, false },
			{ TEXT("DreamBehavior"), LOCTEXT("Behavior", "Behavior"), -50, false },
			{ TEXT("DreamAdvanced"), LOCTEXT("Advanced", "Advanced"), 100, true },
		};
		static_assert(UE_ARRAY_COUNT(Infos) == (int32)ESection::Advanced + 1, "a section without its info");
		return Infos[FMath::Clamp((int32)InSection, 0, (int32)ESection::Advanced)];
	}

	ESection GetSection(const FProperty* InProperty)
	{
		const FString* Value = Local::Find(InProperty, Section);
		return Value != nullptr ? ParseSection(*Value) : ESection::None;
	}

	TArray<FString> GetGroup(const FProperty* InProperty)
	{
		TArray<FString> Parts;
		if (const FString* Value = Local::Find(InProperty, Group))
		{
			Value->ParseIntoArray(Parts, TEXT("/"), true);
			for (FString& Part : Parts)
			{
				Part.TrimStartAndEndInline();
			}
			Parts.RemoveAll([](const FString& Part) { return Part.IsEmpty(); });
		}
		return Parts;
	}

	TOptional<int32> GetOrder(const FProperty* InProperty)
	{
		if (const FString* Value = Local::Find(InProperty, Order))
		{
			if (Value->IsNumeric())
			{
				return FCString::Atoi(**Value);
			}
		}
		return TOptional<int32>();
	}

	bool IsAdvanced(const FProperty* InProperty)
	{
		return Local::IsOn(InProperty, Advanced);
	}

	bool IsHidden(const FProperty* InProperty)
	{
		return Local::IsOn(InProperty, Hidden);
	}

	bool IsLayerToggle(const FProperty* InProperty)
	{
		return Local::IsOn(InProperty, LayerToggle);
	}

	FName GetEditor(const FProperty* InProperty)
	{
		const FString* Value = Local::Find(InProperty, Editor);
		if (Value == nullptr)
		{
			return NAME_None;
		}
		FString Name = *Value;
		int32 Colon = INDEX_NONE;
		if (Name.FindChar(TEXT(':'), Colon))
		{
			Name.LeftInline(Colon);
		}
		Name.TrimStartAndEndInline();
		return Name.IsEmpty() ? NAME_None : FName(*Name);
	}

	bool UsesEditor(const FProperty* InProperty, FName InEditor, FString* OutOption)
	{
		if (InEditor.IsNone() || GetEditor(InProperty) != InEditor)
		{
			return false;
		}
		if (OutOption)
		{
			const FString* Value = Local::Find(InProperty, Editor);
			int32 Colon = INDEX_NONE;
			*OutOption = Value != nullptr && Value->FindChar(TEXT(':'), Colon) ? Value->Mid(Colon + 1).TrimStartAndEnd() : FString();
		}
		return true;
	}

	FName GetHandle(const FProperty* InProperty, FString* OutArgs)
	{
		const FString* Value = Local::Find(InProperty, Handle);
		FString Kind = Value != nullptr ? *Value : FString();
		FString Args;
		int32 Colon = INDEX_NONE;
		if (Kind.FindChar(TEXT(':'), Colon))
		{
			Args = Kind.Mid(Colon + 1).TrimStartAndEnd();
			Kind.LeftInline(Colon);
		}
		Kind.TrimStartAndEndInline();
		if (OutArgs)
		{
			*OutArgs = MoveTemp(Args);
		}
		return Kind.IsEmpty() ? NAME_None : FName(*Kind);
	}

	bool HasPlacement(const FProperty* InProperty)
	{
		return GetSection(InProperty) != ESection::None || Local::Find(InProperty, Group) != nullptr
			|| GetOrder(InProperty).IsSet() || IsAdvanced(InProperty);
	}

	FString FindLayerTogglePath(const UStruct* InStruct)
	{
		if (InStruct == nullptr)
		{
			return FString();
		}
		for (TFieldIterator<FProperty> It(InStruct); It; ++It)
		{
			if (IsLayerToggle(*It))
			{
				return It->GetName();
			}
		}
		for (TFieldIterator<FProperty> It(InStruct); It; ++It)
		{
			if (const FStructProperty* AsStruct = CastField<FStructProperty>(*It))
			{
				if (AsStruct->ArrayDim != 1)
				{
					continue;
				}
				for (TFieldIterator<FProperty> Inner(AsStruct->Struct); Inner; ++Inner)
				{
					if (IsLayerToggle(*Inner))
					{
						return FString::Printf(TEXT("%s.%s"), *It->GetName(), *Inner->GetName());
					}
				}
			}
		}
		return FString();
	}
}

#undef LOCTEXT_NAMESPACE
