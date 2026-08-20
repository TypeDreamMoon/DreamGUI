// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamScrollBoxInputHandler.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Engine/World.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIDropdown.h"
#include "Interaction/UISelectable.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

// What a component copy has to let go of on its way onto another widget, and what it must not.
//
// A copy carries every property the source had, transient ones included -- the archive
// CopyPropertiesForUnrelatedObjects builds is not persistent, so nothing is skipped for being
// derived state. Two of those properties are the widget the source was registered against and the
// list of item components a dropdown built for itself, and a copy that keeps either one keeps
// operating on the widget it was copied from.
//
// The functions below are defined in the details panel's own .cpp -- the panel is a Slate widget no
// headless test can construct -- so this file declares the prototypes it needs. A signature that
// drifts apart from the definition is a link error, not a silent pass.
UDreamUIBehaviour* DreamUIWidgetComponentClipboard_Snapshot(UDreamUIBehaviour* InSource);
UDreamUIBehaviour* DreamUIWidgetComponentClipboard_PasteOnto(UDreamWidget* InTargetWidget, UDreamUIBehaviour* InSource);
bool DreamUIWidgetComponentClipboard_CanPasteClass(const UClass* InComponentClass);

namespace DreamComponentClipboardTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** GC follows UPROPERTY references, not outer chains, so a widget held by nothing else goes mid-test. */
	TStrongObjectPtr<UDreamWidget> MakeWidget(UWorld* World, const TCHAR* DisplayName)
	{
		TStrongObjectPtr<UDreamWidget> Widget(NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional));
		Widget->SetDisplayName(DisplayName);
		return Widget;
	}

	/** The created-item list is protected transient state, which is the whole reason it is reached by name. */
	FArrayProperty* FindCreatedItemArrayProperty()
	{
		return FindFProperty<FArrayProperty>(UUIDropdown::StaticClass(), TEXT("CreatedItemArray"));
	}

	int32 GetCreatedItemCount(const UUIDropdown* InDropdown)
	{
		FArrayProperty* ArrayProperty = FindCreatedItemArrayProperty();
		if (ArrayProperty == nullptr)return INDEX_NONE;
		FScriptArrayHelper_InContainer ArrayHelper(ArrayProperty, InDropdown);
		return ArrayHelper.Num();
	}

	/** Authored references are protected too, and their setters do work of their own that would blur what is being recorded. */
	bool SetObjectPropertyByName(UObject* InObject, const TCHAR* InPropertyName, UObject* InValue)
	{
		FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(InObject->GetClass(), InPropertyName);
		if (Property == nullptr)return false;
		Property->SetObjectPropertyValue_InContainer(InObject, InValue);
		return true;
	}

	bool AddCreatedItem(UUIDropdown* InDropdown, UUIDropdownItemComponent* InItem)
	{
		FArrayProperty* ArrayProperty = FindCreatedItemArrayProperty();
		if (ArrayProperty == nullptr)return false;
		FObjectPropertyBase* InnerProperty = CastField<FObjectPropertyBase>(ArrayProperty->Inner);
		if (InnerProperty == nullptr)return false;
		FScriptArrayHelper_InContainer ArrayHelper(ArrayProperty, InDropdown);
		const int32 Index = ArrayHelper.AddValue();
		InnerProperty->SetObjectPropertyValue(ArrayHelper.GetElementPtr(Index), InItem);
		return true;
	}
}

// The paste runs after UDreamWidget::AddComponent has already registered the new component against the
// target widget, and UDreamUIBehaviour::OnUnregister unsubscribes through the cached widget rather than
// through GetWidget() -- so a component whose cache the paste left empty never stops listening.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPastedComponentStopsListeningWhenRemovedTest,
	"DreamGUI.Editor.ComponentClipboard.APastedComponentStopsListeningWhenItIsRemoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPastedComponentStopsListeningWhenRemovedTest::RunTest(const FString& Parameters)
{
	using namespace DreamComponentClipboardTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<UDreamWidget> Source = MakeWidget(TestWorld.World, TEXT("Source"));
	TStrongObjectPtr<UDreamWidget> Target = MakeWidget(TestWorld.World, TEXT("Target"));
	// Only a registered widget registers the components added to it, and an unregistered one would
	// subscribe nothing -- leaving nothing for the removal to fail to unsubscribe.
	Target->OnRegister();

	UUIButton* Button = Source->AddComponent<UUIButton>();
	if (!TestNotNull(TEXT("the source widget carries a button"), Button))return true;

	UDreamUIBehaviour* Pasted = DreamUIWidgetComponentClipboard_PasteOnto(Target.Get(), Button);
	if (!TestNotNull(TEXT("pasting produced a component"), Pasted))return true;
	if (!TestTrue(TEXT("the paste registered it against the target widget"),
		Target->GetTransformChangedEvent().IsBoundToObject(Pasted)))return true;

	// Nothing between the paste and the removal asks the component for its widget: whatever the paste
	// left in the cache is what the removal has to work with.
	Target->RemoveComponent(Pasted);

	TestFalse(TEXT("the removed component stopped listening for transform changes"),
		Target->GetTransformChangedEvent().IsBoundToObject(Pasted));
	TestFalse(TEXT("...and for dimension changes"),
		Target->GetDimensionChangedEvent().IsBoundToObject(Pasted));
	TestFalse(TEXT("...and for active-state changes"),
		Target->GetWidgetActiveChangedEvent().IsBoundToObject(Pasted));
	return true;
}

// Transient references live inside containers as well as in properties of their own. A dropdown's
// created-item list is one property holding many, and UUIDropdown::Show walks it without a validity
// check -- so a copy that carries the source's list either destroys the source's item widgets or
// dereferences whatever is left of them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPastedComponentDropsTransientStateInsideContainersTest,
	"DreamGUI.Editor.ComponentClipboard.TransientStateInsideContainersIsNotCarriedAcross",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPastedComponentDropsTransientStateInsideContainersTest::RunTest(const FString& Parameters)
{
	using namespace DreamComponentClipboardTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<UDreamWidget> Source = MakeWidget(TestWorld.World, TEXT("Source"));
	TStrongObjectPtr<UDreamWidget> Target = MakeWidget(TestWorld.World, TEXT("Target"));

	UUIDropdown* Dropdown = Source->AddComponent<UUIDropdown>();
	if (!TestNotNull(TEXT("the source widget carries a dropdown"), Dropdown))return true;

	// Held strongly on purpose: the list under test is weak pointers, which keep nothing alive.
	TStrongObjectPtr<UUIDropdownItemComponent> Item(NewObject<UUIDropdownItemComponent>(Source.Get()));
	if (!TestTrue(TEXT("the source dropdown has an item it created for itself"),
		AddCreatedItem(Dropdown, Item.Get()) && GetCreatedItemCount(Dropdown) == 1))return true;

	UDreamUIBehaviour* Pasted = DreamUIWidgetComponentClipboard_PasteOnto(Target.Get(), Dropdown);
	if (!TestNotNull(TEXT("pasting produced a component"), Pasted))return true;

	TestEqual(TEXT("the copy created no items of its own yet"), GetCreatedItemCount(CastChecked<UUIDropdown>(Pasted)), 0);
	TestEqual(TEXT("...and the source kept the one it made"), GetCreatedItemCount(Dropdown), 1);
	return true;
}

// Every other way of putting a component on a widget goes through the class filter -- the picker, and
// the Content Browser drop that asks the filter itself. The clipboard starts from a component instead
// of from a class, so without asking too it is the way a refused class arrives on a widget.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipboardRefusesAClassThePickerWouldRefuseTest,
	"DreamGUI.Editor.ComponentClipboard.AClassThePickerRefusesCannotBePasted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipboardRefusesAClassThePickerWouldRefuseTest::RunTest(const FString& Parameters)
{
	using namespace DreamComponentClipboardTestLocal;
	FScopedTestWorld TestWorld;

	TestTrue(TEXT("a spawnable component class is pasteable"),
		DreamUIWidgetComponentClipboard_CanPasteClass(UUIButton::StaticClass()));
	// The scroll box makes this one for itself on register; it is marked Transient so the picker never
	// offers it, and it is the shape of class a copy could otherwise smuggle onto a widget.
	TestFalse(TEXT("a class the picker hides is not"),
		DreamUIWidgetComponentClipboard_CanPasteClass(UDreamScrollBoxInputHandler::StaticClass()));
	TestFalse(TEXT("an abstract base is not"),
		DreamUIWidgetComponentClipboard_CanPasteClass(UDreamUIBehaviour::StaticClass()));
	TestFalse(TEXT("nothing is not"), DreamUIWidgetComponentClipboard_CanPasteClass(nullptr));

	TStrongObjectPtr<UDreamWidget> Source = MakeWidget(TestWorld.World, TEXT("Source"));
	TStrongObjectPtr<UDreamWidget> Target = MakeWidget(TestWorld.World, TEXT("Target"));
	UDreamUIBehaviour* Handler = Source->AddComponent(UDreamScrollBoxInputHandler::StaticClass());
	if (!TestNotNull(TEXT("the source widget carries the refused component"), Handler))return true;

	TestNull(TEXT("pasting it produces nothing"), DreamUIWidgetComponentClipboard_PasteOnto(Target.Get(), Handler));
	// A refusal that has already added the component is not a refusal.
	TestEqual(TEXT("...and leaves the target widget as it was"), Target->GetAllComponents().Num(), 0);
	return true;
}

// The rows the hierarchy picker exists for are authored references to something in another widget.
// Nothing derives them again, so the clear the paste runs is scoped to transient properties and these
// have to land on the copy still pointing where the author put them -- a component pasted next to the
// one it was copied from keeps navigating and transitioning to the same places.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPastedComponentKeepsAuthoredCrossWidgetReferencesTest,
	"DreamGUI.Editor.ComponentClipboard.AuthoredCrossWidgetReferencesSurviveAPaste",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPastedComponentKeepsAuthoredCrossWidgetReferencesTest::RunTest(const FString& Parameters)
{
	using namespace DreamComponentClipboardTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<UDreamWidget> Source = MakeWidget(TestWorld.World, TEXT("Source"));
	TStrongObjectPtr<UDreamWidget> Target = MakeWidget(TestWorld.World, TEXT("Target"));
	TStrongObjectPtr<UDreamWidget> Elsewhere = MakeWidget(TestWorld.World, TEXT("Elsewhere"));

	UUIButton* Button = Source->AddComponent<UUIButton>();
	UUIButton* ForeignSelectable = Elsewhere->AddComponent<UUIButton>();
	UDreamVisual* ForeignVisual = Elsewhere->CreateNewVisual<UDreamVisualEmpty>();
	if (!TestTrue(TEXT("the fixture has a selectable, and a selectable and a visual on another widget to point it at"),
		Button != nullptr && ForeignSelectable != nullptr && ForeignVisual != nullptr))return true;

	// Both are EditAnywhere and neither is transient, which is what separates them from the state the
	// paste does clear -- so both are named here, and a rename of either shows up as this test failing.
	if (!TestNotNull(TEXT("UUISelectable still has a NavigationNextSpecific property"),
		FindFProperty<FProperty>(UUISelectable::StaticClass(), TEXT("NavigationNextSpecific"))))return true;
	if (!TestNotNull(TEXT("UUISelectable still has a TransitionTarget property"),
		FindFProperty<FProperty>(UUISelectable::StaticClass(), TEXT("TransitionTarget"))))return true;

	Button->SetNavigationNextExplicit(ForeignSelectable);
	if (!TestTrue(TEXT("the source transitions a visual on the other widget"),
		SetObjectPropertyByName(Button, TEXT("TransitionTarget"), ForeignVisual)))return true;

	UUISelectable* Pasted = Cast<UUISelectable>(DreamUIWidgetComponentClipboard_PasteOnto(Target.Get(), Button));
	if (!TestNotNull(TEXT("pasting produced a selectable"), Pasted))return true;

	TestTrue(TEXT("the copy still navigates to the selectable on the other widget"),
		Pasted->GetNavigationNextExplicit() == ForeignSelectable);
	TestTrue(TEXT("...and still transitions the visual on the other widget"),
		Pasted->GetTransitionTarget() == ForeignVisual);
	return true;
}

#endif
