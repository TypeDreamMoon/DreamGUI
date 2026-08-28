// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamUIManager.h"
#include "DreamGUI.h"
#include "Engine/World.h"

namespace
{
	/**
	 * Bring a freshly built hierarchy to life, exactly as the prefab loader does at the end of a load.
	 *
	 * Building the tree is not enough on its own: an unregistered widget is inert -- no layout, no
	 * rendering, no behaviour lifecycle -- so a class that only instanced its template would produce a
	 * hierarchy that is structurally perfect and completely dead. Structure tests do not notice.
	 *
	 * BeginPlay is gated on the MANAGER having begun play, not the world. The prefab loader learned
	 * that the hard way and left a note: World->HasBegunPlay() returns false even when called from
	 * BeginPlay. When it has not, the manager's own OnWorldBeginPlay picks these up later.
	 */
	void BringHierarchyToLife(UDreamWidget* InRoot)
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
}

void UDreamUserWidget::Initialize()
{
	if (bInitialized || IsTemplate())
	{
		return;
	}
	bInitialized = true;

	// Walk up for the tree: a subclass that only adds logic declares none of its own, and has to
	// instance its parent's. Resolving this on the class rather than here keeps a native subclass
	// (which never gets a generated class at all) working the same way.
	UDreamWidgetTree* Archetype = UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(GetClass());
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(this, GetClass(), Archetype);
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
	BringHierarchyToLife(UserWidget);
	return UserWidget;
}
