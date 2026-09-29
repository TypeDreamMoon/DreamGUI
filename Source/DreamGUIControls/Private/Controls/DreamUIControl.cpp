// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamUIControl.h"

#include "DreamGUI.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UISelectable.h"
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
	// The Blueprint's turn, last. Super::NativeOnInitialized above has already run the graph's On
	// Initialized -- before this control had a tree, parts, behaviours or a style -- so this is the
	// only moment at which a Blueprint subclass of a native control can read what it drives.
	OnControlReady();
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

void UDreamUIControl::GetBoundParts(TArray<TPair<FName, UDreamWidget*>>& OutParts)
{
	OutParts.Reset();
	TArray<FDreamControlPart> Parts;
	CollectParts(Parts);
	for (const FDreamControlPart& Part : Parts)
	{
		// An optional part a template chose not to offer is absent rather than broken, and a caller
		// listing what can be animated wants what IS there.
		if (Part.Field != nullptr && IsValid(*Part.Field))
		{
			OutParts.Emplace(Part.Name, *Part.Field);
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

void UDreamUIControl::SetStyleSource(EDreamUIStyleSource InStyleSource)
{
	StyleSource = InStyleSource;
	// The whole of what this property does is decide what ResolveStyle answers, so the push IS the
	// effect: without it the control keeps wearing the look the previous source resolved to.
	ApplyStyle();
}

void UDreamUIControl::SetStyleVariant(FName InStyleVariant)
{
	StyleVariant = InStyleVariant;
	ApplyStyle();
}

namespace DreamUIControlStateFacesLocal
{
	/** What the control's own label wears in InState. Only ever asked while the tint bit is ticked. */
	FColor Foreground(const FDreamUIStateFaces& InFaces, EUISelectableSelectionState InState)
	{
		switch (InState)
		{
		case EUISelectableSelectionState::Hovered: return InFaces.HoveredForeground;
		case EUISelectableSelectionState::Pressed: return InFaces.PressedForeground;
		case EUISelectableSelectionState::Disabled: return InFaces.DisabledForeground;
		case EUISelectableSelectionState::Focused: return InFaces.FocusedForeground;
		default: return InFaces.NormalForeground;
		}
	}
}

void UDreamUIControl::UseStateFaces(UUISelectable* InSelectable, UDreamWidget* InFaceNode, UDreamWidget* InForegroundNode)
{
	StateFaceNode = InFaceNode;
	StateForegroundNode = InForegroundNode;
	if (StateFaceSelectable != nullptr && StateFaceSelectable != InSelectable)
	{
		StateFaceSelectable->GetOnSelectionStateChangedEvent().RemoveAll(this);
	}
	StateFaceSelectable = InSelectable;
	if (InSelectable != nullptr)
	{
		// Dropped before it is added: WireParts is the one caller, but a control re-initialized (a
		// designer recompile, a test building the same control twice) would otherwise hold two
		// bindings and repaint the same face twice for every state it enters.
		InSelectable->GetOnSelectionStateChangedEvent().RemoveAll(this);
		InSelectable->GetOnSelectionStateChangedEvent().AddUObject(this, &UDreamUIControl::HandleSelectionStateChanged);
		StateFaceState = InSelectable->GetSelectionState();
	}
}

void UDreamUIControl::PushStateFaces(const FDreamUIStateFaces& InFaces, const FDreamUIFaceBrush& InFallbackBrush)
{
	StateFaces = InFaces;
	StateFaceFallback = InFallbackBrush;
	if (StateFaceSelectable != nullptr)
	{
		StateFaceState = StateFaceSelectable->GetSelectionState();
		// The sounds are not something to LOOK at, so they do not travel through the listener: they
		// describe a transition, and the selectable is what notices one. Pushed on every style push
		// like every other knob in this family, because on the template road the selectable is one
		// this control just added and carries the library's defaults rather than the sheet's.
		StateFaceSelectable->SetHoveredSound(InFaces.HoveredSound);
		StateFaceSelectable->SetPressedSound(InFaces.PressedSound);
		StateFaceSelectable->SetClickedSound(InFaces.ClickedSound);
	}
	RefreshStateFace();
}

void UDreamUIControl::UseStateFacePadding(const FMargin& InNormalPadding)
{
	StateFaceNormalPadding = InNormalPadding;
	bStateFacePaddingStated = true;
}

void UDreamUIControl::SetStateFaceTint(FColor InTint)
{
	StateFaceTint = InTint;
	RefreshStateFace();
}

void UDreamUIControl::RefreshStateFace()
{
	// Immediate: this is a re-push of a state the control is ALREADY in, not a transition into one.
	HandleSelectionStateChanged(StateFaceState, true);
}

const FDreamUIFaceBrush& UDreamUIControl::ResolveStateBrush(EUISelectableSelectionState InState) const
{
	// The chain itself lives on the struct, because a list paints one face PER ROW and would
	// otherwise need its own copy of it -- see FDreamUIStateFaces::BrushFor.
	return StateFaces.BrushFor(InState, StateFaceFallback);
}

void UDreamUIControl::HandleSelectionStateChanged(EUISelectableSelectionState InState, bool /*bInImmediate*/)
{
	StateFaceState = InState;
	if (StateFaceNode != nullptr)
	{
		FDreamUIFaceBrush Brush = ResolveStateBrush(InState);
		Brush.Tint = TintOver(Brush.Tint, StateFaceTint);
		SkinFace(StateFaceNode, Brush);
	}
	if (StateFaces.bTintForeground && StateForegroundNode != nullptr)
	{
		if (UDreamVisual* Foreground = StateForegroundNode->GetVisual())
		{
			Foreground->SetColor(DreamUIControlStateFacesLocal::Foreground(StateFaces, InState));
		}
	}
	if (StateFaces.bUsePressedPadding && bStateFacePaddingStated && StateFaceNode != nullptr)
	{
		// Cast rather than assumed: on the template road the face carries whatever container its
		// author drew, and a pressed padding is a thing only a measuring container can honour.
		if (UDreamLayoutContainerSizeBox* Box = Cast<UDreamLayoutContainerSizeBox>(StateFaceNode->GetLayoutContainer()))
		{
			Box->SetPadding(InState == EUISelectableSelectionState::Pressed
				? StateFaces.PressedPadding
				: StateFaceNormalPadding);
		}
	}
}
