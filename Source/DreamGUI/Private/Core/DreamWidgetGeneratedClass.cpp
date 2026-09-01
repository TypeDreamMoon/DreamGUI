// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamVisual.h"
#include "DreamGUI.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Animation/DreamWidgetAnimation.h"

namespace
{
	/**
	 * Given an object that lives in the archetype tree, find the object that plays the same part in an
	 * instance of it. Widgets are outered flat to the tree (UDreamWidgetTree::ConstructWidget), so the
	 * owning widget of any subobject is one hop out, and matching that one widget by name is the whole
	 * job -- the property's own type then says which of its parts was wanted.
	 */
	UObject* FindIntraTreeCounterpart(const UObject* InArchetypeValue, const UDreamWidgetTree* InInstancedTree)
	{
		const UDreamWidget* ArchetypeWidget = Cast<UDreamWidget>(InArchetypeValue);
		const bool bWantsTheWidgetItself = ArchetypeWidget != nullptr;
		if (ArchetypeWidget == nullptr)
		{
			ArchetypeWidget = InArchetypeValue->GetTypedOuter<UDreamWidget>();
		}
		if (ArchetypeWidget == nullptr)
		{
			return nullptr;
		}

		UDreamWidget* InstancedWidget = InInstancedTree->FindWidgetByVariableName(
			UDreamWidgetTree::MakeWidgetVariableName(ArchetypeWidget));
		if (InstancedWidget == nullptr)
		{
			return nullptr;
		}
		if (bWantsTheWidgetItself)
		{
			return InstancedWidget;
		}
		if (InArchetypeValue->IsA(UDreamVisual::StaticClass()))
		{
			return InstancedWidget->GetVisual();
		}
		// A behaviour: UUISelectable's six explicit-navigation members and UUIToggle::ToggleGroup all
		// name one on a sibling node. Matched by position rather than by class, because a widget may
		// carry two behaviours of the same class and only the index tells them apart -- Components is
		// UPROPERTY(Instanced), so the instance's array is the archetype's, in order.
		const int32 ComponentIndex = ArchetypeWidget->GetAllComponents().IndexOfByPredicate(
			[InArchetypeValue](const UDreamUIBehaviour* InCandidate) { return InCandidate == InArchetypeValue; });
		if (ComponentIndex != INDEX_NONE && InstancedWidget->GetAllComponents().IsValidIndex(ComponentIndex))
		{
			UDreamUIBehaviour* Counterpart = InstancedWidget->GetAllComponents()[ComponentIndex];
			if (Counterpart != nullptr && Counterpart->GetClass() == InArchetypeValue->GetClass())
			{
				return Counterpart;
			}
		}
		return nullptr;
	}

	/** Rewrite one container's object properties that still name the archetype tree. */
	void RetargetReferencesOn(UObject* InContainer, UDreamWidgetTree* InInstancedTree, const UDreamWidgetTree* InArchetypeTree)
	{
		if (!IsValid(InContainer))
		{
			return;
		}
		for (TFieldIterator<FObjectPropertyBase> It(InContainer->GetClass(), EFieldIterationFlags::Default); It; ++It)
		{
			// Soft and class references cannot name a node of a tree, and walking them would resolve
			// paths for nothing.
			if (It->IsA<FSoftObjectProperty>() || It->IsA<FClassProperty>())
			{
				continue;
			}
			UObject* Value = It->GetObjectPropertyValue_InContainer(InContainer);
			// The criterion is where the value lives, not how it is typed: anything still inside the
			// archetype tree is by construction the wrong object for this instance to be holding.
			if (Value == nullptr || !Value->IsIn(InArchetypeTree))
			{
				continue;
			}
			if (UObject* Counterpart = FindIntraTreeCounterpart(Value, InInstancedTree))
			{
				It->SetObjectPropertyValue_InContainer(InContainer, Counterpart);
			}
			else
			{
				// Left pointing into the template rather than nulled: a stale pointer at least keeps the
				// old behaviour, and the log names the property so the cause is not a mystery.
				UE_LOG(DreamGUI, Warning,
					TEXT("[%s].%d '%s.%s' points at '%s' in the class template and has no counterpart in this instance; left as authored."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InContainer->GetName(), *It->GetName(), *Value->GetName());
			}
		}
	}

	/**
	 * The instancing graph redirects only properties marked Instanced. A plain -- or weak -- pointer
	 * from one node of the tree to another is copied verbatim, so every instance would keep naming the
	 * archetype's object: one shared by all instances of the class, and one that is never drawn.
	 * UUISlider::Fill/Handle, UUISelectable::TransitionTarget and .dui node references all have this
	 * shape, and UUISelectable::Start's `if (!TransitionTarget.IsValid())` self-heal cannot see it --
	 * a pointer at the template is perfectly valid.
	 *
	 * Same defect, and same fix, as UUIRecyclableScrollView::Content in UDreamUserWidget::
	 * ResolveEachBindings: re-resolve by name, per instance.
	 */
	void RetargetIntraTreeReferences(UDreamWidgetTree* InInstancedTree, const UDreamWidgetTree* InArchetypeTree)
	{
		if (InInstancedTree == nullptr || InArchetypeTree == nullptr)
		{
			return;
		}
		InInstancedTree->ForEachWidget([&](UDreamWidget* Widget)
		{
			RetargetReferencesOn(Widget, InInstancedTree, InArchetypeTree);
			RetargetReferencesOn(Widget->GetVisual(), InInstancedTree, InArchetypeTree);
			for (UDreamUIBehaviour* Behaviour : Widget->GetAllComponents())
			{
				RetargetReferencesOn(Behaviour, InInstancedTree, InArchetypeTree);
			}
		});
	}
}

#if WITH_EDITOR
void UDreamWidgetGeneratedClass::SetWidgetTreeArchetype(UDreamWidgetTree* InWidgetTree)
{
	WidgetTree = InWidgetTree;

	if (WidgetTree != nullptr)
	{
		// The tree arrives as a duplicate of the Blueprint's authoring copy and must not inherit that
		// copy's role. RF_ArchetypeObject / RF_DefaultSubObject would make instancing treat it as a
		// subobject template of the Blueprint, and RF_Transient would drop the class's own data on save.
		WidgetTree->ClearFlags(RF_Public | RF_ArchetypeObject | RF_DefaultSubObject | RF_Transient);
	}
}
#endif

UDreamWidgetTree* UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(const UClass* InClass)
{
	for (const UClass* Walker = InClass; Walker != nullptr; Walker = Walker->GetSuperClass())
	{
		if (const UDreamWidgetGeneratedClass* GeneratedClass = Cast<UDreamWidgetGeneratedClass>(Walker))
		{
			if (GeneratedClass->WidgetTree != nullptr)
			{
				return GeneratedClass->WidgetTree;
			}
		}
	}
	return nullptr;
}

const FName UDreamWidgetGeneratedClass::BindWidgetMetaName(TEXT("BindDreamWidget"));

void UDreamWidgetGeneratedClass::InitializeWidget(UDreamUserWidget* InUserWidget) const
{
	InitializeWidgetStatic(InUserWidget, this, FindWidgetTreeArchetype(this));
}

void UDreamWidgetGeneratedClass::InitializeWidgetStatic(UDreamUserWidget* InUserWidget, const UClass* InClass, UDreamWidgetTree* InWidgetTreeArchetype)
{
	if (!IsValid(InUserWidget) || InClass == nullptr)
	{
		return;
	}
	// A CDO is the template, not an instance of one. Building into it would give the class a
	// hierarchy of its own that every later instance would then copy.
	if (InUserWidget->IsTemplate())
	{
		return;
	}
	if (InWidgetTreeArchetype == nullptr)
	{
		// Legitimate for a class that declares no hierarchy (logic-only, or not compiled yet). The
		// widget stays empty rather than half-built.
		return;
	}

	// 1. Instance the template. The instancing graph follows Instanced properties -- UDreamWidgetTree
	//    ::RootWidget and UDreamWidget::Children -- which is what carries the whole hierarchy across.
	FObjectInstancingGraph InstancingGraph;
	UDreamWidgetTree* InstancedTree = NewObject<UDreamWidgetTree>(
		InUserWidget, InWidgetTreeArchetype->GetClass(), NAME_None, RF_Transactional,
		InWidgetTreeArchetype, /*bCopyTransientsFromClassDefaults*/false, &InstancingGraph);
	InUserWidget->WidgetTree = InstancedTree;

	// 2. Parent is DuplicateTransient, so the instanced tree arrives with the structure intact and
	//    every back-pointer empty. Nothing below may run before this.
	InstancedTree->RebuildParentLinks();

	// 2b. Re-aim the pointers that run from one node of the tree to another. Done here, with the rest
	//     of "make the instanced tree whole", and before anything below reads them.
	RetargetIntraTreeReferences(InstancedTree, InWidgetTreeArchetype);

	// 3. Bind each widget to the class property of the same name -- this is BindDreamWidget, and it is the
	//    same shape UMG uses (walk the tree, look the name up in the class's object properties).
	//    Widgets are matched by DisplayName, not object name: object names here are generated.
	//
	//    Only properties declared BELOW UDreamUserWidget are candidates. Binding is driven by display
	//    names, which a designer types, so without this a widget named "Parent" would overwrite
	//    UDreamWidget::Parent and quietly detach the hierarchy from itself. Framework members are not
	//    bindings; bindings live on the class someone wrote for this hierarchy.
	TMap<FName, FObjectPropertyBase*> ObjectPropertiesByName;
	for (TFieldIterator<FObjectPropertyBase> It(const_cast<UClass*>(InClass), EFieldIterationFlags::Default); It; ++It)
	{
		const UClass* OwnerClass = It->GetOwnerClass();
		if (OwnerClass == nullptr || !OwnerClass->IsChildOf(UDreamUserWidget::StaticClass()) || OwnerClass == UDreamUserWidget::StaticClass())
		{
			continue;
		}
		ObjectPropertiesByName.Add(It->GetFName(), *It);
	}
	InstancedTree->ForEachWidget([&](UDreamWidget* Widget)
	{
		const FName VariableName = UDreamWidgetTree::MakeWidgetVariableName(Widget);
		if (FObjectPropertyBase** PropertyPtr = ObjectPropertiesByName.Find(VariableName))
		{
			FObjectPropertyBase* Property = *PropertyPtr;
			// A same-named property of an unrelated type is a mistake worth naming rather than a
			// silent skip -- the compiler declares these, so a mismatch means the two disagree.
			if (Widget->IsA(Property->PropertyClass))
			{
				Property->SetObjectPropertyValue_InContainer(InUserWidget, Widget);
			}
			else
			{
				UE_LOG(DreamGUI, Warning,
					TEXT("Widget '%s' matches property '%s' on '%s' by name but not by type (property expects %s); left unbound."),
					*Widget->GetDisplayName(), *VariableName.ToString(), *InClass->GetName(),
					*Property->PropertyClass->GetName());
			}
		}

		// The same by-name contract for this widget's animations. The compiler declared one
		// property per authored animation; the INSTANCED copy is what must land in it, because a
		// graph that plays the archetype's copy animates a tree nobody is looking at.
		for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
		{
			UDreamWidgetAnimationComponent* Animator = Cast<UDreamWidgetAnimationComponent>(Component);
			if (Animator == nullptr)
			{
				continue;
			}
			for (UDreamWidgetAnimation* Animation : Animator->GetSequenceArray())
			{
				const FName AnimationVariableName = IsValid(Animation) ? UDreamWidgetTree::MakeAnimationVariableName(Animation) : NAME_None;
				FObjectPropertyBase* const* PropertyPtr = AnimationVariableName.IsNone() ? nullptr : ObjectPropertiesByName.Find(AnimationVariableName);
				if (PropertyPtr != nullptr && Animation->IsA((*PropertyPtr)->PropertyClass))
				{
					(*PropertyPtr)->SetObjectPropertyValue_InContainer(InUserWidget, Animation);
				}
			}
		}

				// A nested user widget builds its own contents from its own class, the way UMG initializes
		// instanced sub-widgets during DuplicateAndInitializeFromWidgetTree.
		if (UDreamUserWidget* NestedUserWidget = Cast<UDreamUserWidget>(Widget))
		{
			if (NestedUserWidget != InUserWidget)
			{
				NestedUserWidget->Initialize();
			}
		}
	});

	// Filling the host's slots used to be step 3b, here. It is now UDreamUserWidget::
	// AttachNamedSlotContent, called at the end of Initialize -- late enough that a NATIVE control
	// has built the tree the content goes into, and still before registration. This function only
	// ever saw one of the two kinds of contents.

	// 4. Hang the contents under the user widget. SetParentBeforeRegister rather than TrySetParent:
	//    nothing here is registered yet, and the attach path would run layout against a half-built
	//    hierarchy and recapture authored geometry while doing it.
	if (IsValid(InstancedTree->RootWidget))
	{
		InstancedTree->RootWidget->SetParentBeforeRegister(InUserWidget);
	}
}

void UDreamWidgetGeneratedClass::CollectPropertyBindings(const UClass* InClass, TArray<FDreamWidgetPropertyBinding>& OutBindings)
{
	OutBindings.Reset();
	// Base first, so a subclass binding on the same property is applied last and wins.
	TArray<const UDreamWidgetGeneratedClass*> Chain;
	for (const UClass* Current = InClass; Current != nullptr; Current = Current->GetSuperClass())
	{
		if (const UDreamWidgetGeneratedClass* Generated = Cast<UDreamWidgetGeneratedClass>(Current))
		{
			Chain.Add(Generated);
		}
	}
	for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
	{
		OutBindings.Append(Chain[Index]->PropertyBindings);
	}
}

void UDreamWidgetGeneratedClass::CollectEventBindings(const UClass* InClass, TArray<FDreamWidgetEventBinding>& OutBindings)
{
	OutBindings.Reset();
	TArray<const UDreamWidgetGeneratedClass*> Chain;
	for (const UClass* Current = InClass; Current != nullptr; Current = Current->GetSuperClass())
	{
		if (const UDreamWidgetGeneratedClass* Generated = Cast<UDreamWidgetGeneratedClass>(Current))
		{
			Chain.Add(Generated);
		}
	}
	for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
	{
		OutBindings.Append(Chain[Index]->EventBindings);
	}
}

void UDreamWidgetGeneratedClass::CollectEachBindings(const UClass* InClass, TArray<FDreamWidgetEachBinding>& OutBindings)
{
	OutBindings.Reset();
	TArray<const UDreamWidgetGeneratedClass*> Chain;
	for (const UClass* Current = InClass; Current != nullptr; Current = Current->GetSuperClass())
	{
		if (const UDreamWidgetGeneratedClass* Generated = Cast<UDreamWidgetGeneratedClass>(Current))
		{
			Chain.Add(Generated);
		}
	}
	for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
	{
		OutBindings.Append(Chain[Index]->EachBindings);
	}
}

#if WITH_EDITOR
void UDreamWidgetGeneratedClass::SetPropertyBindings(TArray<FDreamWidgetPropertyBinding> InBindings)
{
	PropertyBindings = MoveTemp(InBindings);
}

void UDreamWidgetGeneratedClass::SetEventBindings(TArray<FDreamWidgetEventBinding> InBindings)
{
	EventBindings = MoveTemp(InBindings);
}

void UDreamWidgetGeneratedClass::SetEachBindings(TArray<FDreamWidgetEachBinding> InBindings)
{
	EachBindings = MoveTemp(InBindings);
}
#endif

void UDreamWidgetGeneratedClass::PurgeClass(bool bRecompilingOnLoad)
{
	Super::PurgeClass(bRecompilingOnLoad);

	if (WidgetTree != nullptr)
	{
		// Renaming into the transient package drops the linker's export for it; invalidating first is
		// what keeps the stale export from being resolved afterwards. Straight out of UMG's PurgeClass.
		const ERenameFlags RenameFlags = REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty;
		FLinkerLoad::InvalidateExport(WidgetTree);
		WidgetTree->Rename(nullptr, GetTransientPackage(), RenameFlags);
		WidgetTree = nullptr;
	}
}
