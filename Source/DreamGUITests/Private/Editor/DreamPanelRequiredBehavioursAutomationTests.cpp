// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Interaction/DreamContentWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

// A Size Box, Scale Box or Safe Zone is only the UMG panel it claims to be when the widget also
// carries a ContentWidget behaviour; the container declares that through GetRequiredBehaviourClasses
// and the widget reconciles it on every assignment path. Each test below is one of those paths.

namespace DreamPanelRequiredBehavioursTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UObject* Outer, const TCHAR* DisplayName)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(Outer, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(DisplayName);
		return Widget;
	}

	int32 CountContentWidgets(const UDreamWidget* Widget)
	{
		int32 Count = 0;
		for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
		{
			Count += IsValid(Cast<UDreamContentWidget>(Component)) ? 1 : 0;
		}
		return Count;
	}

	/**
	 * The details dropdown does not call CreateNewLayoutContainer: the property editor instantiates
	 * the class, writes the pointer, and brackets the write with PreEditChange/PostEditChangeProperty.
	 * This does the same three things.
	 */
	void AssignPanelThroughPropertyEditor(UDreamWidget* Widget, UClass* PanelClass)
	{
		FProperty* Property = FindFProperty<FProperty>(UDreamWidget::StaticClass(), TEXT("LayoutContainer"));
		check(Property);
		FObjectProperty* ObjectProperty = CastFieldChecked<FObjectProperty>(Property);
		Widget->PreEditChange(Property);
		UObject* NewPanel = PanelClass ? NewObject<UObject>(Widget, PanelClass, NAME_None, RF_Public | RF_Transactional) : nullptr;
		ObjectProperty->SetObjectPropertyValue_InContainer(Widget, NewPanel);
		FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
		Widget->PostEditChangeProperty(Event);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelRequiredBehavioursCreateNewLayoutContainerTest,
	"DreamGUI.Editor.PanelRequiredBehaviours.CreateNewLayoutContainerAddsAndRemovesTheContentWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPanelRequiredBehavioursCreateNewLayoutContainerTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelRequiredBehavioursTestLocal;
	FScopedTestWorld TestWorld;
	TStrongObjectPtr<UDreamWidget> Widget(MakeWidget(TestWorld.World, TEXT("Box")));

	Widget->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>();
	TestEqual(TEXT("A Size Box brings exactly one ContentWidget"), CountContentWidgets(Widget.Get()), 1);

	Widget->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>();
	TestEqual(TEXT("Assigning the same panel again does not add a second one"), CountContentWidgets(Widget.Get()), 1);

	Widget->CreateNewLayoutContainer<UDreamLayoutContainerScaleBox>();
	TestEqual(TEXT("Switching between two single-child panels keeps the one ContentWidget"), CountContentWidgets(Widget.Get()), 1);

	Widget->CreateNewLayoutContainer<UDreamLayoutContainerSafeZone>();
	TestEqual(TEXT("Safe Zone also keeps it"), CountContentWidgets(Widget.Get()), 1);

	// A ContentWidget caps the widget at one child, so leaving it behind would turn the Overlay into a
	// single-child panel too.
	Widget->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	TestEqual(TEXT("Switching to a panel that does not need it removes the ContentWidget"), CountContentWidgets(Widget.Get()), 0);

	Widget->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>();
	Widget->RemoveLayoutContainer();
	TestEqual(TEXT("Removing the panel removes the ContentWidget"), CountContentWidgets(Widget.Get()), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelRequiredBehavioursDropdownTest,
	"DreamGUI.Editor.PanelRequiredBehaviours.TheDetailsDropdownPathReconcilesToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPanelRequiredBehavioursDropdownTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelRequiredBehavioursTestLocal;
	FScopedTestWorld TestWorld;
	TStrongObjectPtr<UDreamWidget> Widget(MakeWidget(TestWorld.World, TEXT("Box")));

	AssignPanelThroughPropertyEditor(Widget.Get(), UDreamLayoutContainerSizeBox::StaticClass());
	TestTrue(TEXT("The dropdown assignment took"), IsValid(Cast<UDreamLayoutContainerSizeBox>(Widget->GetLayoutContainer())));
	TestEqual(TEXT("The dropdown path adds the ContentWidget"), CountContentWidgets(Widget.Get()), 1);

	AssignPanelThroughPropertyEditor(Widget.Get(), UDreamLayoutContainerScaleBox::StaticClass());
	TestEqual(TEXT("Switching single-child panels through the dropdown keeps one"), CountContentWidgets(Widget.Get()), 1);

	AssignPanelThroughPropertyEditor(Widget.Get(), UDreamLayoutContainerOverlay::StaticClass());
	TestEqual(TEXT("Switching to Overlay through the dropdown removes it"), CountContentWidgets(Widget.Get()), 0);

	AssignPanelThroughPropertyEditor(Widget.Get(), UDreamLayoutContainerSizeBox::StaticClass());
	AssignPanelThroughPropertyEditor(Widget.Get(), nullptr);
	TestEqual(TEXT("Choosing None through the dropdown removes it"), CountContentWidgets(Widget.Get()), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelRequiredBehavioursUndoTest,
	"DreamGUI.Editor.PanelRequiredBehaviours.UndoRemovesTheComponentWithThePanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPanelRequiredBehavioursUndoTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelRequiredBehavioursTestLocal;
	if (!GEditor)
	{
		AddInfo(TEXT("No GEditor; undo is not testable here."));
		return true;
	}
	FScopedTestWorld TestWorld;
	TStrongObjectPtr<UDreamWidget> Widget(MakeWidget(TestWorld.World, TEXT("Box")));
	Widget->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();

	// The sync only calls Modify(); the transaction is the property editor's. Without the Modify()
	// on the widget's Components array, undo would pop the entry and leave an orphan ContentWidget.
	GEditor->BeginTransaction(FText::FromString(TEXT("Test Panel Change")));
	AssignPanelThroughPropertyEditor(Widget.Get(), UDreamLayoutContainerSizeBox::StaticClass());
	GEditor->EndTransaction();
	TestEqual(TEXT("Size Box added the ContentWidget inside the transaction"), CountContentWidgets(Widget.Get()), 1);

	GEditor->UndoTransaction();
	TestTrue(TEXT("Undo restored the Overlay"), IsValid(Cast<UDreamLayoutContainerOverlay>(Widget->GetLayoutContainer())));
	TestEqual(TEXT("Undo removed the ContentWidget with it"), CountContentWidgets(Widget.Get()), 0);

	GEditor->RedoTransaction();
	TestTrue(TEXT("Redo brought the Size Box back"), IsValid(Cast<UDreamLayoutContainerSizeBox>(Widget->GetLayoutContainer())));
	TestEqual(TEXT("Redo brought the ContentWidget back"), CountContentWidgets(Widget.Get()), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelRequiredBehavioursCapacityTest,
	"DreamGUI.Editor.PanelRequiredBehaviours.TheDropdownRefusesAPanelTheChildrenDoNotFit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPanelRequiredBehavioursCapacityTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelRequiredBehavioursTestLocal;
	FScopedTestWorld TestWorld;
	TStrongObjectPtr<UDreamWidget> Widget(MakeWidget(TestWorld.World, TEXT("Box")));
	Widget->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	UDreamWidget* First = MakeWidget(Widget.Get(), TEXT("First"));
	UDreamWidget* Second = MakeWidget(Widget.Get(), TEXT("Second"));
	First->TrySetParent(Widget.Get(), false);
	Second->TrySetParent(Widget.Get(), false);

	// CreateNewLayoutContainer already refuses this; the dropdown writes the pointer before anything
	// can object, so PostEditChangeProperty has to put the Overlay back.
	TestNull(TEXT("Control: the programmatic path refuses a Size Box over two children"),
		Widget->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>());

	AddExpectedError(TEXT("accepts at most 1 children"), EAutomationExpectedErrorFlags::Contains, 1);
	AssignPanelThroughPropertyEditor(Widget.Get(), UDreamLayoutContainerSizeBox::StaticClass());
	TestTrue(TEXT("The Overlay is kept"), IsValid(Cast<UDreamLayoutContainerOverlay>(Widget->GetLayoutContainer())));
	TestEqual(TEXT("No ContentWidget was added for the refused panel"), CountContentWidgets(Widget.Get()), 0);
	TestEqual(TEXT("Both children are still attached"), Widget->GetChildren().Num(), 2);
	return true;
}

#endif
