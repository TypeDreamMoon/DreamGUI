// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "DreamGUI.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"

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

	// 3b. Fill the slots the HOST bound. The content objects belong to the host's tree and arrived
	//     with it; all that is left is to hang each under the UDreamNamedSlot of that name inside
	//     this instance. Done here rather than by the host, because only this class knows where its
	//     own slots are -- and done before registration, so nothing lays out a half-filled shell.
	for (const TPair<FName, TObjectPtr<UDreamWidget>>& Binding : InUserWidget->NamedSlotContent)
	{
		UDreamWidget* Content = Binding.Value;
		if (!IsValid(Content))
		{
			continue;
		}
		UDreamWidget* SlotWidget = InUserWidget->FindSlotWidget(Binding.Key);
		if (!IsValid(SlotWidget))
		{
			// The class dropped or renamed a slot the host still binds. Silently discarding it is how
			// content disappears from a screen with nothing in the log to say why; the compiler
			// reports this as an error on the host too, but a class can change after that compile.
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' has no slot named '%s'; the content bound to it is not shown."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InClass->GetName(), *Binding.Key.ToString());
			continue;
		}
		if (Content->HasRegistered())
		{
			Content->TrySetParent(SlotWidget, false);
		}
		else
		{
			Content->SetParentBeforeRegister(SlotWidget);
		}
	}

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
