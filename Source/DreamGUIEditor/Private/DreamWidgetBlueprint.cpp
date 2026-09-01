// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamWidgetBlueprint.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/Components/DreamWidget.h"

UDreamWidgetBlueprint::UDreamWidgetBlueprint()
{
	ParentClass = UDreamUserWidget::StaticClass();
}

UClass* UDreamWidgetBlueprint::GetBlueprintClass() const
{
	return UDreamWidgetGeneratedClass::StaticClass();
}

UDreamWidgetTree* UDreamWidgetBlueprint::GetOrCreateWidgetTree(bool bEnsureRootWidget)
{
	if (!IsValid(WidgetTree))
	{
		WidgetTree = NewObject<UDreamWidgetTree>(this, UDreamWidgetTree::StaticClass(), NAME_None, RF_Transactional);
	}
	if (bEnsureRootWidget && !IsValid(WidgetTree->RootWidget))
	{
		UDreamWidget* Root = WidgetTree->ConstructWidget<UDreamWidget>();
		Root->SetDisplayName(TEXT("Root"));
		WidgetTree->RootWidget = Root;
	}
	return WidgetTree;
}

void UDreamWidgetBlueprint::GetAllSourceWidgets(TArray<UDreamWidget*>& OutWidgets) const
{
	OutWidgets.Reset();
	if (IsValid(WidgetTree))
	{
		WidgetTree->ForEachWidget([&OutWidgets](UDreamWidget* Widget) { OutWidgets.Add(Widget); });
	}
}

#if WITH_EDITOR
void UDreamWidgetBlueprint::GetReparentingRules(TSet<const UClass*>& AllowedChildrenOfClasses, TSet<const UClass*>& DisallowedChildrenOfClasses) const
{
	// A hierarchy class can only ever derive from something that knows how to build one.
	AllowedChildrenOfClasses.Add(UDreamUserWidget::StaticClass());
}
#endif
