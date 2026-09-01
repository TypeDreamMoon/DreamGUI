// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamUIControl.h"

#include "DreamGUI.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "UObject/UnrealType.h"

void UDreamUIControl::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// A tree, then the parts in it, then the behaviours on those, then the look. The order is the
	// whole design: every step reads what the one before it produced, and only the FIRST of them
	// differs between a templated control and one that builds itself.
	if (!RealizeTemplate())
	{
		RealizeBuiltIn();
	}
	BindParts();
	WireParts();
	OnPartsReady();
	ApplyStyle();
}

bool UDreamUIControl::RealizeTemplate()
{
	// A tree that arrived on its own IS the template. That is a Blueprint subclass of a control:
	// Initialize instanced the subclass's archetype before NativeOnInitialized ever ran, and
	// building the code tree on top of it -- which is what happened before this function existed --
	// left the control with two hierarchies and its parts pointing at the second one.
	if (IsValid(WidgetTree) && IsValid(WidgetTree->RootWidget))
	{
		return true;
	}
	if (Template == nullptr)
	{
		return false;
	}
	if (Template->IsChildOf(GetClass()))
	{
		// Instancing it would run this control's own initialize inside itself, forever. Worth an
		// error rather than a stack overflow: the mistake is easy to make in a details panel, where
		// the picker offers every widget class including this one's.
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' cannot be its own template."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathDisplayName());
		return false;
	}

	UDreamWidgetTree* Archetype = UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(Template);
	if (!IsValid(Archetype) || !IsValid(Archetype->RootWidget))
	{
		// A logic-only class, or one that has never been compiled. Falling back to the built-in tree
		// would silently ignore what the author asked for, so it is said out loud and the control
		// still comes up usable.
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' names '%s' as its template, which has no hierarchy; using the built-in one."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathDisplayName(), *Template->GetName());
		return false;
	}

	// The template's TREE, instanced into this control -- the same call a class makes for its own
	// archetype, which is what makes the two roads produce the same kind of hierarchy. GetClass()
	// rather than Template as the binding class: the properties a template's own class declares are
	// that class's, and writing them into an object of this one is not a mismatch the reflection
	// system would catch.
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(this, GetClass(), Archetype);
	return IsValid(WidgetTree) && IsValid(WidgetTree->RootWidget);
}

UDreamWidget* UDreamUIControl::FindPart(FName InName) const
{
	if (InName.IsNone())
	{
		return nullptr;
	}
	// This control's own contents, stopping at nested instances: a "Face" inside a Button placed in
	// a template belongs to that Button, and driving it from here would be two controls writing one
	// widget. Same boundary FindSlotWidget draws, and for the same reason.
	const FString Wanted = InName.ToString();
	TArray<UDreamWidget*> Pending(GetChildren());
	while (Pending.Num() > 0)
	{
		UDreamWidget* Widget = Pending.Pop(EAllowShrinking::No);
		if (!IsValid(Widget))
		{
			continue;
		}
		if (Widget->GetDisplayName() == Wanted)
		{
			return Widget;
		}
		if (!Widget->IsA<UDreamUserWidget>())
		{
			Pending.Append(Widget->GetChildren());
		}
	}
	return nullptr;
}

void UDreamUIControl::BindParts()
{
	TArray<FDreamControlPart> Parts;
	CollectParts(Parts);
	for (const FDreamControlPart& Part : Parts)
	{
		if (Part.Field == nullptr)
		{
			continue;
		}
		UDreamWidget* Found = FindPart(Part.Name);
		*Part.Field = Found;
		if (Found == nullptr && Part.bRequired)
		{
			// By name, because that is the one thing the template's author can act on. A part that
			// went missing quietly is a control that comes up looking right and drives nothing: the
			// writers all null-check, so there is no crash and no line anywhere to start from.
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' found no part named '%s'; that part of the control will not work."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathDisplayName(), *Part.Name.ToString());
		}
	}
}

TArray<FName> UDreamUIControl::GetUnboundRequiredParts()
{
	TArray<FName> Missing;
	TArray<FDreamControlPart> Parts;
	CollectParts(Parts);
	for (const FDreamControlPart& Part : Parts)
	{
		if (Part.bRequired && Part.Field != nullptr && *Part.Field == nullptr)
		{
			Missing.Add(Part.Name);
		}
	}
	return Missing;
}

void DreamUI_ApplyStyleOverrides(const UScriptStruct* InStruct, void* OutBase, const void* InOverrides)
{
	if (InStruct == nullptr || OutBase == nullptr || InOverrides == nullptr)
	{
		return;
	}
	// One pass over the struct's properties, pairing each bOverride_<field> with <field>. Driven by
	// the NAME rather than by a hand-written table for the reason the part lists are: a table beside
	// the fields is a second place to forget, and the thing forgotten is silent -- a style knob that
	// simply stops being honoured.
	static const FString Prefix(TEXT("bOverride_"));
	for (const FProperty* Flag = InStruct->PropertyLink; Flag != nullptr; Flag = Flag->PropertyLinkNext)
	{
		const FBoolProperty* BoolFlag = CastField<FBoolProperty>(Flag);
		if (BoolFlag == nullptr)
		{
			continue;
		}
		const FString FlagName = BoolFlag->GetName();
		if (!FlagName.StartsWith(Prefix, ESearchCase::CaseSensitive))
		{
			continue;
		}
		if (!BoolFlag->GetPropertyValue_InContainer(InOverrides))
		{
			// Not ticked: whatever is already in the base stays. This is the whole feature.
			continue;
		}
		const FName FieldName(*FlagName.RightChop(Prefix.Len()));
		const FProperty* Field = InStruct->FindPropertyByName(FieldName);
		if (Field == nullptr)
		{
			// A bit whose field was renamed or removed. Worth saying: it means an author is ticking
			// a checkbox that decides nothing, and the panel gives no hint of that.
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d %s has a %s with no matching field; the tick decides nothing."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InStruct->GetName(), *FlagName);
			continue;
		}
		Field->CopySingleValue(
			Field->ContainerPtrToValuePtr<void>(OutBase),
			Field->ContainerPtrToValuePtr<const void>(InOverrides));
	}
}
