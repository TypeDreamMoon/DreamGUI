// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "DreamGUI.h"
#include "Engine/World.h"

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

UDreamUserWidget* CreateDreamWidget(UWorld* InWorld, TSubclassOf<UDreamUserWidget> InClass, UDreamWidget* InParent)
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
	if (IsValid(InParent))
	{
		UserWidget->SetParentBeforeRegister(InParent);
	}
	return UserWidget;
}
