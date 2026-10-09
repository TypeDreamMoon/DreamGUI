// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamWidget.h"
#include "DreamWidgetPrivate.h"
#include "Core/DreamPerspective.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Engine/World.h"
#include "DreamTweenManager.h"
#include "Core/DreamUIClipData.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamVisual.h"
#if WITH_ACCESSIBILITY
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif
#include "Components/SceneComponent.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetNavigation.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Event/DreamPointerEventData.h"
#include "GameFramework/PlayerController.h"
// FLayoutLocalization, for the Culture flow-direction preference. SlateCore is already a public
// dependency; this is the one header of it that answers "which way does the running culture read".
#include "Layout/FlowDirection.h"
#if WITH_EDITOR
#include "Event/DreamUIEventDelegate.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#endif

#if WITH_EDITOR
namespace DreamWidgetComponentBindingsLocal
{
	void RemapAuthoredEvents(UDreamWidget* InWidget, int32 InOldIndex, int32 InNewIndex)
	{
		UDreamWidget* Root = InWidget;
		while (IsValid(Root->GetParent()))
		{
			Root = Root->GetParent();
		}
		TArray<UDreamWidget*> Widgets;
		UDreamWidget::CollectChildrenWidgets(Root, Widgets, /*IncludeTarget*/true);
		TSet<UObject*> Owners;
		for (UDreamWidget* Widget : Widgets)
		{
			Owners.Add(Widget);
			ForEachObjectWithOuter(Widget, [&Owners](UObject* Object) { Owners.Add(Object); });
		}
		for (UObject* Owner : Owners)
		{
			if (!IsValid(Owner)) continue;
			// Visit all properties so a hashed container can be skipped before descending into its
			// structs. Checking only the final event property misses events nested in a struct key.
			for (TPropertyValueIterator<FProperty> It(Owner->GetClass(), Owner); It; ++It)
			{
				const FProperty* Property = It.Key();
				const FMapProperty* OwningMap = Property->GetOwner<FMapProperty>();
				if (Property->IsA<FSetProperty>() || (OwningMap != nullptr && OwningMap->KeyProp == Property))
				{
					It.SkipRecursiveProperty();
					continue;
				}
				const FStructProperty* Struct = CastField<FStructProperty>(Property);
				if (Struct != nullptr && Struct->Struct == FDreamUIEventDelegate::StaticStruct())
				{
					auto* Event = static_cast<FDreamUIEventDelegate*>(const_cast<void*>(It.Value()));
					Event->RemapBehaviourBindings(Owner, InWidget, InOldIndex, InNewIndex);
					It.SkipRecursiveProperty();
				}
			}
		}
	}
}
#endif

UDreamUIBehaviour* UDreamWidget::AddComponent(TSubclassOf<UDreamUIBehaviour> ComponentClass, UDreamUIBehaviour* ComponentTemplate)
{
	if (!*ComponentClass)
	{
		return nullptr;
	}
	if (ComponentTemplate && ComponentTemplate->GetClass() != *ComponentClass)
	{
		return nullptr;
	}

	// RF_Transactional taken from the OWNER rather than forced on, because that flag is what
	// decides whether an object can enter the transaction buffer -- and a behaviour hung on a
	// DESIGNER PREVIEW must not. The preview is a projection of the authoring tree that is thrown
	// away and rebuilt, so an undo that restored one of its objects would be restoring something
	// that no longer exists. An authored widget is transactional, so its behaviours still are.
	// UDreamWidgetGeneratedClass::InitializeWidgetStatic applies the same rule one level up, to the
	// tree, and this is the other half of it: what INSTANCING carries and what RUNTIME creates have
	// to agree, or a preview comes out half undoable.
	EObjectFlags NewComponentFlags = RF_Public | GetMaskedFlags(RF_Transactional);
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		// Components created while building class defaults must be archetype/default-subobjects.
		// They also need to be public, otherwise Blueprint-generated templates can end up
		// referencing parent CDO private archetype objects that SavePackage rejects.
		NewComponentFlags |= (RF_Public | RF_DefaultSubObject | RF_ArchetypeObject);
	}

	const FName NewComponentName = MakeUniqueObjectName(this, ComponentClass, ComponentClass->GetFName());
	auto NewComponent = NewObject<UDreamUIBehaviour>(this, ComponentClass, NewComponentName, NewComponentFlags, ComponentTemplate);
	Components.Add(NewComponent);
	if (HasRegistered())
	{
		NewComponent->Call_OnRegister();
	}
	if (HasBegunPlay())
	{
		NewComponent->BeginPlay();
	}
	OnComponentsChangedEvent.Broadcast(EDreamWidgetComponentsChangedType::Added);
	return NewComponent;
}

UDreamUIBehaviour* UDreamWidget::AddComponent(TSubclassOf<UDreamUIBehaviour> ComponentClass)
{
	return AddComponent(ComponentClass, nullptr);
}

UDreamUIBehaviour* UDreamWidget::AddComponentByTemplate(UDreamUIBehaviour* ComponentTemplate)
{
	return AddComponent(ComponentTemplate->GetClass(), ComponentTemplate);
}

void UDreamWidget::RemoveComponent(UDreamUIBehaviour* Component)
{
	auto Index = Components.Find(Component);
	if (Index < 0)return;
#if WITH_EDITOR
	DreamWidgetComponentBindingsLocal::RemapAuthoredEvents(this, Index, INDEX_NONE);
#endif
	Components.RemoveAt(Index);
	// Asked whether or not this widget is in play: a behaviour woken before its widget began play is awake
	// all the same, and the behaviour's own EndPlay only undoes what it has done.
	Component->EndPlay();
	Component->Call_OnUnregister();
	OnComponentsChangedEvent.Broadcast(EDreamWidgetComponentsChangedType::Removed);
}

void UDreamWidget::MoveComponentToIndex(UDreamUIBehaviour* Component, int32 NewIndex)
{
	const int32 SourceIndex = Components.Find(Component);
	if (SourceIndex < 0)
	{
		return;
	}

	const int32 TargetIndex = FMath::Clamp(NewIndex, 0, Components.Num() - 1);
	if (SourceIndex == TargetIndex)
	{
		return;
	}

#if WITH_EDITOR
	DreamWidgetComponentBindingsLocal::RemapAuthoredEvents(this, SourceIndex, TargetIndex);
#endif
	UDreamUIBehaviour* MovingComponent = Components[SourceIndex];
	Components.RemoveAt(SourceIndex);
	Components.Insert(MovingComponent, FMath::Clamp(TargetIndex, 0, Components.Num()));
	OnComponentsChangedEvent.Broadcast(EDreamWidgetComponentsChangedType::Reorder);
}

TArray<UDreamUIBehaviour*> UDreamWidget::GetComponents(TSubclassOf<UDreamUIBehaviour> ComponentClass)const
{
	TArray<UDreamUIBehaviour*> ResultArray;
	UClass* RequestedClass = *ComponentClass;
	if (!IsValid(RequestedClass) || !RequestedClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
	{
		return ResultArray;
	}
	for (auto& Comp : Components)
	{
		if (IsValid(Comp) && Comp->IsA(RequestedClass))
		{
			ResultArray.Add(Comp);
		}
	}
	return ResultArray;
}

UDreamUIBehaviour* UDreamWidget::GetComponent(TSubclassOf<UDreamUIBehaviour> ComponentClass)const
{
	UClass* RequestedClass = *ComponentClass;
	if (!IsValid(RequestedClass) || !RequestedClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
	{
		return nullptr;
	}
	for (auto& Comp : Components)
	{
		if (IsValid(Comp) && Comp->IsA(RequestedClass))
		{
			return Comp;
		}
	}
	return nullptr;
}

bool UDreamWidget::SyncRequiredBehavioursForLayoutContainer(const UDreamLayoutContainer* OldLayout, const UDreamLayoutContainer* NewLayout)
{
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return false;
	}
	TArray<TSubclassOf<UDreamUIBehaviour>> RequiredClasses;
	TArray<TSubclassOf<UDreamUIBehaviour>> PreviouslyRequiredClasses;
	if (IsValid(NewLayout))
	{
		NewLayout->GetRequiredBehaviourClasses(RequiredClasses);
	}
	if (IsValid(OldLayout))
	{
		OldLayout->GetRequiredBehaviourClasses(PreviouslyRequiredClasses);
	}

	TArray<UDreamUIBehaviour*> ComponentsToRemove;
	for (const TSubclassOf<UDreamUIBehaviour>& PreviousClass : PreviouslyRequiredClasses)
	{
		UClass* Class = *PreviousClass;
		const bool bStillRequired = IsValid(Class) && RequiredClasses.ContainsByPredicate([Class](const TSubclassOf<UDreamUIBehaviour>& Required)
		{
			return IsValid(*Required) && (Class->IsChildOf(*Required) || (*Required)->IsChildOf(Class));
		});
		if (!IsValid(Class) || bStillRequired)
		{
			continue;
		}
		// Every instance goes, not only the one the old container added: a leftover ContentWidget would
		// keep capping the new container at one child.
		ComponentsToRemove.Append(GetComponents(PreviousClass));
	}
	TArray<UClass*> ClassesToAdd;
	for (const TSubclassOf<UDreamUIBehaviour>& RequiredClass : RequiredClasses)
	{
		UClass* Class = *RequiredClass;
		if (IsValid(Class) && !Class->HasAnyClassFlags(CLASS_Abstract) && !IsValid(GetComponent(RequiredClass)))
		{
			ClassesToAdd.AddUnique(Class);
		}
	}
	if (ComponentsToRemove.Num() == 0 && ClassesToAdd.Num() == 0)
	{
		return false;
	}

#if WITH_EDITOR
	// Modify, without forcing RF_Transactional on first. Whether these objects belong in the undo
	// buffer is a property OF THEM: an authoring tree and its widgets are transactional and are
	// recorded here, a designer preview is not and these calls then cost nothing. Forcing the flag
	// put preview objects into the history, and an undo restored objects a rebuild had destroyed.
	if (UObject* WidgetOuter = GetOuter())
	{
		WidgetOuter->Modify();
	}
	Modify();
#endif
	for (UDreamUIBehaviour* Component : ComponentsToRemove)
	{
#if WITH_EDITOR
		Component->Modify();
#endif
		RemoveComponent(Component);
	}
	for (UClass* Class : ClassesToAdd)
	{
		if (UDreamUIBehaviour* NewComponent = AddComponent(Class))
		{
#if WITH_EDITOR
			// AddComponent already gave it this widget's own RF_Transactional; see the note there.
			NewComponent->Modify();
#endif
		}
	}
	return true;
}

UDreamUIBehaviour* UDreamWidget::GetComponentByInterface(UClass* InterfaceClass)const
{
	if (!IsValid(InterfaceClass) || !InterfaceClass->HasAnyClassFlags(CLASS_Interface))
	{
		return nullptr;
	}
	for (auto& Component : GetAllComponents())
	{
		if (IsValid(Component) && Component->GetClass()->ImplementsInterface(InterfaceClass))
		{
			return Component;
		}
	}
	return nullptr;
}

UDreamVisual* UDreamWidget::CreateNewVisual(TSubclassOf<UDreamVisual> VisualClass)
{
	auto OldVisual = Visual;
	// Undoability from the owner, never a constant. See the note in AddComponent.
	auto NewVisual = NewObject<UDreamVisual>(this, VisualClass, NAME_None, RF_Public | GetMaskedFlags(RF_Transactional));
	if (RenderCanvas.IsValid())
	{
		if (IsValid(OldVisual))
		{
			RenderCanvas->MarkVisualWillChange(OldVisual);
			RenderCanvas->UnregisterVisual(OldVisual);
		}
		if (NewVisual)
		{
			RenderCanvas->RegisterVisual(NewVisual);
		}
	}
	if (IsValid(OldVisual))
	{
		if (HasBegunPlay())
		{
			OldVisual->EndPlay();
		}
		OldVisual->Call_OnUnregister();
	}
	
	NewVisual->Call_OnRegister();
	if (HasBegunPlay())
	{
		NewVisual->BeginPlay();
	}
	Visual = NewVisual;
	return NewVisual;
}

void UDreamWidget::RemoveVisual()
{
	auto OldVisual = Visual;
	Visual = nullptr;

	if (IsValid(OldVisual))
	{
		if (HasBegunPlay())
		{
			OldVisual->EndPlay();
		}
		OldVisual->Call_OnUnregister();
		if (RenderCanvas.IsValid())
		{
			RenderCanvas->MarkVisualWillChange(OldVisual);
			RenderCanvas->UnregisterVisual(OldVisual);
		}
	}
}

UDreamLayoutContainer* UDreamWidget::CreateNewLayoutContainer(TSubclassOf<UDreamLayoutContainer> LayoutClass)
{
	UClass* RequestedClass = *LayoutClass;
	if (!IsValid(RequestedClass)
		|| !RequestedClass->IsChildOf(UDreamLayoutContainer::StaticClass())
		|| RequestedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	const UDreamLayoutContainer* RequestedLayoutDefault = Cast<UDreamLayoutContainer>(RequestedClass->GetDefaultObject());
	if (IsValid(RequestedLayoutDefault))
	{
		const int32 MaxChildren = RequestedLayoutDefault->GetMaxChildren();
		if (MaxChildren >= 0)
		{
			int32 ValidChildCount = 0;
			for (const UDreamWidget* Child : Children)
			{
				ValidChildCount += IsValid(Child) ? 1 : 0;
			}
			if (ValidChildCount > MaxChildren)
			{
				return nullptr;
			}
		}
	}
	auto OldLayout = LayoutContainer;
	// Undoability from the owner, never a constant. See the note in AddComponent.
	auto NewLayout = NewObject<UDreamLayoutContainer>(this, RequestedClass, NAME_None, RF_Public | GetMaskedFlags(RF_Transactional));
	if (!IsValid(NewLayout))
	{
		return nullptr;
	}
	const bool bInitializeScaleBoxSlots = NewLayout->IsA<UDreamLayoutContainerScaleBox>()
		&& (!IsValid(OldLayout) || !OldLayout->IsA<UDreamLayoutContainerScaleBox>());
	if (IsValid(OldLayout))
	{
		if (HasBegunPlay())
		{
			OldLayout->EndPlay();
		}
		OldLayout->Call_OnUnregister();
	}
	
	NewLayout->Call_OnRegister();
	if (HasBegunPlay())
	{
		NewLayout->BeginPlay();
	}
	LayoutContainer = NewLayout;
	if (IsValid(Cast<UDreamPanelLayoutBase>(NewLayout)))
	{
		for (UDreamWidget* Child : Children)
		{
			if (IsValid(Child))
			{
				// UMG creates a fresh ScaleBoxSlot with Center/Center defaults when the panel type changes.
				// Dream reuses its generic slot, so initialize those defaults explicitly on the same transition.
				if (bInitializeScaleBoxSlots)
				{
					if (UDreamPanelSlot* ExistingSlot = Child->GetPanelSlot(); IsValid(ExistingSlot))
					{
#if WITH_EDITOR
						if (const UWorld* World = Child->GetWorld(); !World || !World->IsGameWorld())
						{
							ExistingSlot->Modify();
						}
#endif
						ExistingSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
						ExistingSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
					}
				}
				EnsurePanelSlotForChild(this, Child, true);
			}
		}
	}
	else
	{
		for (UDreamWidget* Child : Children)
		{
			RemovePanelSlotFromChild(Child);
		}
	}
	SyncRequiredBehavioursForLayoutContainer(OldLayout, NewLayout);
	MarkLayoutForRebuild(this);
	MarkDimensionChanged(false, true, true);//change LayoutContainer could cause LayoutSelf size change
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->MarkRebuildAllLayoutTree();
	}
	return NewLayout;
}

void UDreamWidget::RemoveLayoutContainer()
{
	auto OldLayout = LayoutContainer;
	LayoutContainer = nullptr;

	if (IsValid(OldLayout))
	{
		if (HasBegunPlay())
		{
			OldLayout->EndPlay();
		}
		OldLayout->Call_OnUnregister();
	}
	for (UDreamWidget* Child : Children)
	{
		RemovePanelSlotFromChild(Child);
	}
	SyncRequiredBehavioursForLayoutContainer(OldLayout, nullptr);
	MarkLayoutForRebuild(this);
	MarkDimensionChanged(false, true, true);
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->MarkRebuildAllLayoutTree();
	}
}

UDreamLayoutSelf* UDreamWidget::CreateNewLayoutSelf(TSubclassOf<UDreamLayoutSelf> LayoutClass)
{
	UClass* RequestedClass = *LayoutClass;
	if (!IsValid(RequestedClass)
		|| !RequestedClass->IsChildOf(UDreamLayoutSelf::StaticClass())
		|| RequestedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	auto OldLayout = LayoutSelf;
	// Undoability from the owner, never a constant. See the note in AddComponent.
	auto NewLayout = NewObject<UDreamLayoutSelf>(this, RequestedClass, NAME_None, RF_Public | GetMaskedFlags(RF_Transactional));
	if (!IsValid(NewLayout))
	{
		return nullptr;
	}
	if (IsValid(OldLayout))
	{
		if (HasBegunPlay())
		{
			OldLayout->EndPlay();
		}
		OldLayout->Call_OnUnregister();
	}
	
	NewLayout->Call_OnRegister();
	if (HasBegunPlay())
	{
		NewLayout->BeginPlay();
	}
	LayoutSelf = NewLayout;
	MarkLayoutForRebuild(this);
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->MarkRebuildAllLayoutTree();
	}
	return NewLayout;
}

void UDreamWidget::RemoveLayoutSelf()
{
	auto OldLayout = LayoutSelf;
	LayoutSelf = nullptr;

	if (IsValid(OldLayout))
	{
		if (HasBegunPlay())
		{
			OldLayout->EndPlay();
		}
		OldLayout->Call_OnUnregister();
	}
	MarkLayoutForRebuild(this);
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->MarkRebuildAllLayoutTree();
	}
}

UDreamPanelSlot* UDreamWidget::CreateNewPanelSlot(TSubclassOf<UDreamPanelSlot> SlotClass)
{
	UClass* RequestedClass = *SlotClass;
	if (!IsValid(RequestedClass))
	{
		RequestedClass = UDreamPanelSlot::StaticClass();
	}
	if (!RequestedClass->IsChildOf(UDreamPanelSlot::StaticClass())
		|| RequestedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	UDreamPanelSlot* OldSlot = PanelSlot;
	// Undoability from the owner, and this is the site that made the rule necessary. A panel slot is
	// not authored -- it is per-child data the PARENT's layout hands out, minted here at registration
	// for any child whose authored counterpart has none, which the designer's content root always is.
	// So a preview grew slots that instancing never touched, they arrived RF_Transactional, and a
	// slot's Padding and alignments are among the most-edited rows in the details panel.
	UDreamPanelSlot* NewSlot = NewObject<UDreamPanelSlot>(this, RequestedClass, NAME_None, RF_Public | GetMaskedFlags(RF_Transactional));
	if (!IsValid(NewSlot))
	{
		return nullptr;
	}
	if (IsValid(OldSlot))
	{
		OldSlot->RestoreAuthoredGeometry();
		if (HasBegunPlay())
		{
			OldSlot->EndPlay();
		}
		OldSlot->Call_OnUnregister();
	}
	PanelSlot = NewSlot;
	if (IsValid(NewSlot))
	{
		NewSlot->Call_OnRegister();
		if (HasBegunPlay())
		{
			NewSlot->BeginPlay();
		}
	}
	MarkLayoutForRebuild(Parent.IsValid() ? Parent.Get() : this);
	return NewSlot;
}

void UDreamWidget::RemovePanelSlot()
{
	UDreamPanelSlot* OldSlot = PanelSlot;
	if (IsValid(OldSlot))
	{
		OldSlot->RestoreAuthoredGeometry();
		if (HasBegunPlay())
		{
			OldSlot->EndPlay();
		}
		OldSlot->Call_OnUnregister();
	}
	PanelSlot = nullptr;
	MarkLayoutForRebuild(Parent.IsValid() ? Parent.Get() : this);
}
