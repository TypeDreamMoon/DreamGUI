// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIManager.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Engine/World.h"

#if WITH_EDITOR
UDreamUISelection* UDreamUISelection::GetInstance(UWorld* InWorld)
{
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(InWorld))
	{
		return DreamUIManager->GetSelection();
	}
	return nullptr;
}

void UDreamUISelection::SelectWidget(UDreamWidget* Widget)
{
	// A widget listed twice takes every per-selection delta twice: Align and Distribute walk the
	// array, so a duplicate entry moves that widget by double the offset the others get.
	SelectedWidgetArray.AddUnique(Widget);
	OnSelectionChanged.Broadcast();
}

void UDreamUISelection::DeselectWidget(UDreamWidget* Widget)
{
	if (SelectedWidgetArray.Remove(Widget) > 0)
	{
		OnSelectionChanged.Broadcast();
	}
}

void UDreamUISelection::SelectComponent(UDreamUIBehaviour* Component)
{
	SelectedComponentArray.Add(Component);
	OnSelectionChanged.Broadcast();
}

void UDreamUISelection::ClearComponentSelection()
{
	SelectedComponentArray.Empty();
	OnSelectionChanged.Broadcast();
}

void UDreamUISelection::SelectNone()
{
	SelectedWidgetArray.Empty();
	SelectedComponentArray.Empty();
	OnSelectionChanged.Broadcast();
}

bool UDreamUISelection::IsSelected(UDreamWidget* Widget)const
{
	return SelectedWidgetArray.Contains(Widget);
}
#else
// The selection is the designer's, and a game has no designer. The reflected functions are declared in
// every build all the same -- UHT generates their thunks either way -- so outside the editor they exist,
// answer "no selection" and do nothing.
UDreamUISelection* UDreamUISelection::GetInstance(UWorld* InWorld)
{
	return nullptr;
}

void UDreamUISelection::SelectWidget(UDreamWidget* Widget)
{
}

void UDreamUISelection::DeselectWidget(UDreamWidget* Widget)
{
}

void UDreamUISelection::SelectNone()
{
}
#endif
