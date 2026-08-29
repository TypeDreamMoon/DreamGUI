// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamUIManager.h"
#include "DreamGUI.h"
#include "Engine/World.h"

/**
 * Bring a freshly built hierarchy to life, exactly as the prefab loader does at the end of a load.
 *
 * BeginPlay is gated on the MANAGER having begun play, not the world. The prefab loader learned
 * that the hard way and left a note: World->HasBegunPlay() returns false even when called from
 * BeginPlay. When it has not, the manager's own OnWorldBeginPlay picks these up later.
 */
void RegisterDreamWidgetHierarchy(UDreamWidget* InRoot)
{
	if (!IsValid(InRoot))
	{
		return;
	}
	TArray<UDreamWidget*> AllWidgets;
	UDreamWidget::CollectChildrenWidgets(InRoot, AllWidgets, true);

	// Parents before children, which CollectChildrenWidgets already gives us: OnRegister reads the
	// parent link to reconcile panel slots.
	for (UDreamWidget* Widget : AllWidgets)
	{
		if (IsValid(Widget))
		{
			Widget->OnRegister();
		}
	}

	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InRoot->GetWorld()))
	{
		if (Manager->HasBegunPlay())
		{
			for (UDreamWidget* Widget : AllWidgets)
			{
				if (IsValid(Widget))
				{
					Widget->BeginPlay();
				}
			}
		}
	}
}

void UDreamUserWidget::Initialize()
{
	// Walk up for the tree: a subclass that only adds logic declares none of its own, and has to
	// instance its parent's. Resolving this on the class rather than here keeps a native subclass
	// (which never gets a generated class at all) working the same way.
	InitializeFromArchetype(UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(GetClass()));
}

void UDreamUserWidget::InitializeFromArchetype(UDreamWidgetTree* InArchetype)
{
	if (bInitialized || IsTemplate())
	{
		return;
	}
	bInitialized = true;

	UDreamWidgetGeneratedClass::InitializeWidgetStatic(this, GetClass(), InArchetype);

	// After the tree exists: the bindings name widgets in it.
	ResolvePropertyBindings();
	if (ResolvedBindings.Num() > 0)
	{
		// Once now, so the first frame shows bound values rather than the authored ones.
		EvaluatePropertyBindings();
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			Manager->AddPropertyBindingUser(this);
		}
	}
}

void UDreamUserWidget::ResolvePropertyBindings()
{
	ResolvedBindings.Reset();

	TArray<FDreamWidgetPropertyBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectPropertyBindings(GetClass(), Bindings);
	if (Bindings.Num() == 0)
	{
		return;
	}

	for (const FDreamWidgetPropertyBinding& Binding : Bindings)
	{
		// The target widget is reached the same way everything else reaches one: the class property
		// the compiler named after it, which InitializeWidgetStatic has already filled in.
		FObjectPropertyBase* WidgetProperty = FindFProperty<FObjectPropertyBase>(GetClass(), Binding.WidgetName);
		if (WidgetProperty == nullptr)
		{
			continue;
		}
		UDreamWidget* TargetWidget = Cast<UDreamWidget>(WidgetProperty->GetObjectPropertyValue_InContainer(this));
		UObject* Target = ResolveDreamWidgetBindingTarget(TargetWidget, Binding.Target, Binding.BehaviourIndex);
		if (!IsValid(Target))
		{
			continue;
		}
		UFunction* SourceFunction = FindFunction(Binding.FunctionName);
		UFunction* Setter = Target->FindFunction(Binding.SetterName);
		if (SourceFunction == nullptr || Setter == nullptr)
		{
			continue;
		}

		FResolvedBinding& Resolved = ResolvedBindings.AddDefaulted_GetRef();
		Resolved.Target = Target;
		Resolved.SourceFunction = SourceFunction;
		Resolved.Setter = Setter;
	}
}

void UDreamUserWidget::EvaluatePropertyBindings()
{
	for (const FResolvedBinding& Binding : ResolvedBindings)
	{
		UObject* Target = Binding.Target.Get();
		if (!IsValid(Target) || Binding.SourceFunction == nullptr || Binding.Setter == nullptr)
		{
			continue;
		}

		// FStructOnScope rather than a raw buffer: a returned FText or FString has to be constructed
		// before ProcessEvent writes it and destroyed afterwards, and this does both.
		FStructOnScope SourceFrame(Binding.SourceFunction);
		ProcessEvent(Binding.SourceFunction, SourceFrame.GetStructMemory());

		FProperty* ReturnProperty = Binding.SourceFunction->GetReturnProperty();
		FProperty* SetterParameter = nullptr;
		for (TFieldIterator<FProperty> It(Binding.Setter); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			SetterParameter = *It;
			break;
		}
		if (ReturnProperty == nullptr || SetterParameter == nullptr
			|| !ReturnProperty->SameType(SetterParameter))
		{
			// The compiler checked this pairing; reaching here means the class moved underneath us.
			continue;
		}

		FStructOnScope SetterFrame(Binding.Setter);
		SetterParameter->CopyCompleteValue(
			SetterParameter->ContainerPtrToValuePtr<void>(SetterFrame.GetStructMemory()),
			ReturnProperty->ContainerPtrToValuePtr<void>(SourceFrame.GetStructMemory()));
		Target->ProcessEvent(Binding.Setter, SetterFrame.GetStructMemory());
	}
}

UDreamWidget* UDreamUserWidget::GetContentRoot() const
{
	return IsValid(WidgetTree) ? WidgetTree->RootWidget : nullptr;
}

UDreamUserWidget* CreateDreamWidget(UWorld* InWorld, TSubclassOf<UDreamUserWidget> InClass, UDreamWidget* InParent,
	const TFunction<void(UDreamUserWidget*)>& InCallbackBeforeAlive)
{
	if (!IsValid(InWorld))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d CreateDreamWidget needs a valid world."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	if (!IsValid(InClass))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d CreateDreamWidget needs a valid class."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}

	// Same ownership rule a prefab load follows: join the parent's tree, or mint one outered to the
	// world so GetTypedOuter<UWorld> resolves for everything inside.
	UObject* Owner = nullptr;
	UDreamWidgetTree* OwnedTree = nullptr;
	if (IsValid(InParent) && InParent->GetOuter() != nullptr)
	{
		Owner = InParent->GetOuter();
	}
	else
	{
		OwnedTree = NewObject<UDreamWidgetTree>(InWorld);
		Owner = OwnedTree;
	}

	UDreamUserWidget* UserWidget = NewObject<UDreamUserWidget>(Owner, InClass, NAME_None, RF_Transactional);
	if (OwnedTree != nullptr)
	{
		OwnedTree->RootWidget = UserWidget;
	}
	UserWidget->Initialize();
	// Parent first, then registration: OnRegister reconciles the panel slot against the parent, so
	// registering an orphan and attaching it afterwards produces a widget the parent never laid out.
	if (IsValid(InParent))
	{
		UserWidget->SetParentBeforeRegister(InParent);
	}
	// Last chance to reshape what was built before anything observes it. See the header.
	if (InCallbackBeforeAlive)
	{
		InCallbackBeforeAlive(UserWidget);
	}
	RegisterDreamWidgetHierarchy(UserWidget);
	return UserWidget;
}
