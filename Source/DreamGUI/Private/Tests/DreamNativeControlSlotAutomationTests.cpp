// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Controls/DreamExpandableArea.h"
#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Interaction/DreamContentWidget.h"
#include "DreamControlTestScope.h"
#include "UObject/Package.h"

/*
 * Holes in a control whose hierarchy is code.
 *
 * A named slot used to be reachable only by a class built from an archetype. The filling step lived
 * inside UDreamWidgetGeneratedClass::InitializeWidgetStatic behind an `InWidgetTreeArchetype !=
 * nullptr` gate, and a native control never has one -- so a slot declared in a native control's tree
 * was a hole nothing could ever fill, and both UDreamExpandableArea and UDreamTabView carry (carried)
 * a paragraph saying so. Every consumer asked the same question the same wrong way: the compiler, the
 * designer's hierarchy rows and the runtime all read slots off a widget TREE, and a native control's
 * is null.
 *
 * Three things had to move, and this file is one test per thing:
 *
 *   - The filling step, from InitializeWidgetStatic to the end of Initialize, which is the first
 *     moment BOTH kinds of contents exist and still ahead of registration.
 *   - FindSlotWidget, from "walk the widget tree" to "walk the widget tree, or this widget's own
 *     children when there is no tree".
 *   - CollectDeclaredSlotNames, from asking an archetype to asking a CLASS -- which is what a
 *     native control can actually answer, through GetNativeSlotNames.
 *
 * Plus the sugar the whole thing exists to enable: content that arrives with no slot name at all
 * (.dui nesting, a designer drop) goes to the control's default slot. That road is what replaced
 * UDreamExpandableArea::AdoptAuthoredChildren, so one test here is a straight regression guard on
 * the behaviour that hand-written pass used to provide.
 *
 * No world anywhere. Initialize needs one only for property bindings, and a control assembled in C++
 * carries none. TDreamTestControl rather than TStrongObjectPtr for anything that hosts another
 * control: a registered tree logs an Error from BeginDestroy if nobody destroyed it, and the Error
 * lands on whatever test is running when the collector gets there.
 */
namespace DreamNativeControlSlotTestLocal
{
	/** A plain widget standing in for whatever a host nests on a control. */
	UDreamWidget* MakeGuest(const TCHAR* InName)
	{
		UDreamWidget* Guest = NewObject<UDreamWidget>(GetTransientPackage());
		Guest->SetDisplayName(InName);
		return Guest;
	}

	/**
	 * Hang InGuest on InControl the way the text builder does, BEFORE the control is initialized.
	 *
	 * That ordering is the whole mechanism: BuildNode attaches a node's children to the widget it
	 * just built, so nested content is already there when the control starts making its own -- which
	 * is exactly what lets Initialize tell guests from furniture without any control naming its own
	 * root.
	 */
	void NestBeforeInitialize(UDreamUserWidget* InControl, UDreamWidget* InGuest)
	{
		InGuest->SetParentBeforeRegister(InControl);
	}

	bool ClassDeclaresSlot(const UClass* InClass, FName InSlotName)
	{
		TArray<FName> Declared;
		UDreamUserWidget::CollectDeclaredSlotNames(InClass, Declared);
		return Declared.Contains(InSlotName);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNativeControlSlotDeclarationTest,
	"DreamGUI.Controls.Slots.ANativeControlCanSayWhichHolesItOpens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNativeControlSlotDeclarationTest::RunTest(const FString& Parameters)
{
	using namespace DreamNativeControlSlotTestLocal;

	// The class overload, which is the one every consumer should now reach for. Asking the ARCHETYPE
	// -- what the compiler and the hierarchy panel used to do -- returns nothing for all four of
	// these, because a native control has no archetype to ask.
	TestTrue(TEXT("the button declares a content hole"),
		ClassDeclaresSlot(UDreamButton::StaticClass(), UDreamButton::ContentSlotName));
	TestTrue(TEXT("the dialog declares a body hole"),
		ClassDeclaresSlot(UDreamDialog::StaticClass(), UDreamDialog::BodySlotName));
	TestTrue(TEXT("the scroll box declares a content hole"),
		ClassDeclaresSlot(UDreamScrollBox::StaticClass(), UDreamScrollBox::ContentSlotName));

	// Two, and both listed: an expander's body is what nesting fills and its header is the deliberate
	// case, so a consumer that only ever saw one of them could not offer the other.
	TestTrue(TEXT("the expander declares a content hole"),
		ClassDeclaresSlot(UDreamExpandableArea::StaticClass(), UDreamExpandableArea::ContentSlotName));
	TestTrue(TEXT("and a header hole"),
		ClassDeclaresSlot(UDreamExpandableArea::StaticClass(), UDreamExpandableArea::HeaderSlotName));

	// The declaration is a promise about the built tree, and FindSlotWidget is what has to keep it.
	// A name that no UDreamNamedSlot answers to is the silent half of this feature: content bound to
	// it is dropped, with one log line, on a screen nobody is reading.
	TDreamTestControl<UDreamButton> Button(NewObject<UDreamButton>(GetTransientPackage()));
	Button->Initialize();
	UDreamWidget* Hole = Button->FindSlotWidget(UDreamButton::ContentSlotName);
	if (!TestNotNull(TEXT("the declared name finds a real node"), Hole))
	{
		return false;
	}
	TestTrue(TEXT("and it is the node the control kept"), (UObject*)Hole == (UObject*)Button->ContentNode.Get());
	TestNotNull(TEXT("which carries the slot behaviour"), Hole->GetComponent<UDreamNamedSlot>());

	// The default is a separate question from the list: a control may open holes and still take no
	// position on where unnamed content goes.
	TestEqual(TEXT("the button's default hole is its content one"),
		Button->GetDefaultSlotName(), UDreamButton::ContentSlotName);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNativeControlSlotNestingTest,
	"DreamGUI.Controls.Slots.NestedContentReachesTheDefaultHole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNativeControlSlotNestingTest::RunTest(const FString& Parameters)
{
	using namespace DreamNativeControlSlotTestLocal;

	TDreamTestControl<UDreamButton> Button(NewObject<UDreamButton>(GetTransientPackage()));
	UDreamWidget* Guest = MakeGuest(TEXT("Icon"));
	NestBeforeInitialize(Button.Get(), Guest);

	Button->Initialize();

	// Not "still a child of the control", which is where it was left before any of this existed and
	// is a place nothing draws it: beside the control's own root, outside every panel.
	TestTrue(TEXT("the nested widget ended up in the hole"),
		(UObject*)Guest->GetParent() == (UObject*)Button->ContentNode.Get());

	// The other half of filling a hole. The stock label and the supplied content are two answers to
	// "what is on this button" laid over each other in the same overlay, so exactly one is awake --
	// and this is the assertion that would fail if the style push ran only at the end of
	// NativeOnInitialized, where the hole is structurally still empty.
	TestFalse(TEXT("the stock label stood down"), Button->LabelNode->GetWidgetActive());
	TestTrue(TEXT("and the hole woke up"), Button->ContentNode->GetWidgetActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNativeControlSlotFurnitureTest,
	"DreamGUI.Controls.Slots.AControlDoesNotAdoptItsOwnFurniture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNativeControlSlotFurnitureTest::RunTest(const FString& Parameters)
{
	TDreamTestControl<UDreamButton> Button(NewObject<UDreamButton>(GetTransientPackage()));
	Button->Initialize();

	// Nothing was nested, so nothing moves -- and in particular the control's OWN root, which
	// Initialize hung under the control between the snapshot and the adoption pass, is not a guest.
	// Adopting it would put the button's face inside the button's face.
	TestTrue(TEXT("the face still hangs directly on the control"),
		(UObject*)Button->FaceNode->GetParent() == (UObject*)Button.Get());
	TestEqual(TEXT("and the hole is still empty"), Button->ContentNode->GetChildrenCount(), 0);
	TestTrue(TEXT("so the stock label is the one showing"), Button->LabelNode->GetWidgetActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNativeControlSlotSeveralTest,
	"DreamGUI.Controls.Slots.AHoleThatIsAlreadyAPanelTakesSeveral",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNativeControlSlotSeveralTest::RunTest(const FString& Parameters)
{
	using namespace DreamNativeControlSlotTestLocal;

	// The regression guard on replacing UDreamExpandableArea::AdoptAuthoredChildren. That pass moved
	// EVERY nested child into the content column; a named slot takes one by default, so swapping it
	// for the generic road without bAcceptsSeveral would have placed the first line of every expander
	// in the codebase and silently refused the rest.
	TDreamTestControl<UDreamExpandableArea> Area(NewObject<UDreamExpandableArea>(GetTransientPackage()));
	UDreamWidget* First = MakeGuest(TEXT("Line1"));
	UDreamWidget* Second = MakeGuest(TEXT("Line2"));
	NestBeforeInitialize(Area.Get(), First);
	NestBeforeInitialize(Area.Get(), Second);

	Area->Initialize();

	TestTrue(TEXT("the first nested widget is in the column"),
		(UObject*)First->GetParent() == (UObject*)Area->ContentNode.Get());
	TestTrue(TEXT("and so is the second"),
		(UObject*)Second->GetParent() == (UObject*)Area->ContentNode.Get());

	// A button's hole is the default kind and keeps the default rule: one, because a hole that took
	// several would be a panel and a panel is a thing the author puts IN the hole.
	TDreamTestControl<UDreamButton> Button(NewObject<UDreamButton>(GetTransientPackage()));
	Button->Initialize();
	const UDreamNamedSlot* PlainHole = Button->ContentNode->GetComponent<UDreamNamedSlot>();
	if (TestNotNull(TEXT("the button's hole carries the slot behaviour"), PlainHole))
	{
		TestFalse(TEXT("and it is not a panel"), PlainHole->bAcceptsSeveral);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNativeControlSlotBindingTest,
	"DreamGUI.Controls.Slots.ANamedBindingReachesTheHoleItNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNativeControlSlotBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamNativeControlSlotTestLocal;

	// The other road in, and the one the designer uses: an explicit binding by name rather than
	// nesting. The dialog is the case worth testing it on, because its hole and its built-in message
	// are OVERLAY SIBLINGS -- the message is not inside the hole, precisely so that "is this filled"
	// can be read off the hole's children without the furniture answering yes on its behalf.
	TDreamTestControl<UDreamDialog> Dialog(NewObject<UDreamDialog>(GetTransientPackage()));
	UDreamWidget* Form = MakeGuest(TEXT("Form"));

	if (!TestTrue(TEXT("the host can bind content to a native control's hole"),
		Dialog->SetContentForNamedSlot(UDreamDialog::BodySlotName, Form)))
	{
		return false;
	}
	Dialog->Initialize();

	TestTrue(TEXT("the bound content is in the hole"),
		(UObject*)Form->GetParent() == (UObject*)Dialog->BodyNode.Get());
	TestFalse(TEXT("the built-in message stood down"), Dialog->MessageNode->GetWidgetActive());
	TestTrue(TEXT("and the hole woke up"), Dialog->BodyNode->GetWidgetActive());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
