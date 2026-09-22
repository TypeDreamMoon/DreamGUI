// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Controls/DreamExpandableArea.h"
#include "Controls/DreamScrollBox.h"
#include "Controls/DreamUIControl.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWidgetRegistry.h"
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

	/**
	 * Every class DECLARE_DREAM_GUI_WIDGET put in the registry.
	 *
	 * That list rather than a TObjectIterator over UDreamUIControl, because it is the list a `.dui`
	 * can actually spell: a control nothing registered is unreachable from the language, and a
	 * Blueprint subclass brings its own tree -- the TEMPLATE road -- whose node names are somebody's
	 * asset rather than a claim this codebase gets to make.
	 */
	void CollectRegisteredControlClasses(TArray<UClass*>& OutClasses)
	{
		TArray<FDreamUIWidgetRegistry::FEntry> Entries;
		FDreamUIWidgetRegistry::GetAllEntries(Entries);
		for (const FDreamUIWidgetRegistry::FEntry& Entry : Entries)
		{
			if (Entry.Kind != FDreamUIWidgetRegistry::EKind::ScopedWidget || Entry.ClassGetter == nullptr)
			{
				continue;
			}
			UClass* Class = Entry.ClassGetter();
			if (Class == nullptr
				|| !Class->IsChildOf(UDreamUserWidget::StaticClass())
				|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			OutClasses.AddUnique(Class);
		}
	}

	/**
	 * A control's own contents, walked the way FindPart and FindSlotWidget walk them: everything
	 * under the control, stopping AT a nested instance rather than descending into it.
	 *
	 * The boundary is the whole point. A "Label" inside a Button that a dialog placed belongs to
	 * that button, and counting it here would report every control that hosts another as broken.
	 */
	void CollectOwnNodes(const UDreamUserWidget& InControl, TArray<UDreamWidget*>& OutNodes)
	{
		TArray<UDreamWidget*> Pending(InControl.GetChildren());
		while (Pending.Num() > 0)
		{
			UDreamWidget* Widget = Pending.Pop(EAllowShrinking::No);
			if (!IsValid(Widget))
			{
				continue;
			}
			OutNodes.Add(Widget);
			if (!Widget->IsA<UDreamUserWidget>())
			{
				Pending.Append(Widget->GetChildren());
			}
		}
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

	// The other half of filling a hole: something has to ARRANGE what went in it. A hole with no
	// layout container is a hole whose guest keeps the rect it was authored with -- the button's
	// content ignored the button's size and its padding alike, which is invisible in a hierarchy
	// that looks perfectly correct.
	TestNotNull(TEXT("and the hole lays it out"), Button->ContentNode->GetLayoutContainer());

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
	// An empty hole claims no size: its authored rect is what the measure walk falls back on, and
	// the default 100x100 made an empty button measure 124x108. Stated in the tree rather than by a
	// rule ApplyStyle enforces, so nothing attached later can arrive after the rule already ran.
	TestTrue(TEXT("so it is authored at zero rather than at the widget default"),
		Button->ContentNode->GetSizeDelta().IsNearlyZero());

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNativeControlSlotSweepTest,
	"DreamGUI.Controls.Slots.EveryDeclaredHoleIsTheNamedSlotNodeOfThatNameAndNothingElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNativeControlSlotSweepTest::RunTest(const FString& Parameters)
{
	using namespace DreamNativeControlSlotTestLocal;

	// GetNativeSlotNames is a PROMISE about the built tree, and nothing in the language keeps it:
	// the declaration is one list and RealizeBuiltIn is another, so a hole that was renamed, dropped
	// or shadowed reads as "no such slot" -- content bound to it is discarded with one log line, on
	// a screen nobody is watching. The button case was spelled out by hand below; this is the same
	// claim asked of every control the registry knows.
	TArray<UClass*> Classes;
	CollectRegisteredControlClasses(Classes);

	int32 HolesChecked = 0;
	for (UClass* Class : Classes)
	{
		TDreamTestControl<UDreamUserWidget> Control(NewObject<UDreamUserWidget>(GetTransientPackage(), Class));
		Control->Initialize();

		for (const FName& SlotName : Control->GetNativeSlotNames())
		{
			++HolesChecked;
			const FString Where = FString::Printf(TEXT("%s's declared hole '%s'"),
				*Class->GetName(), *SlotName.ToString());

			UDreamWidget* Hole = Control->FindSlotWidget(SlotName);
			if (Hole == nullptr)
			{
				AddError(FString::Printf(TEXT("%s: no node in the built tree answers to it."), *Where));
				continue;
			}
			if (Hole->GetDisplayName() != SlotName.ToString())
			{
				// A slot's name is its host's display name run through the variable-name sanitizer,
				// so "Header Content" declares a hole called "HeaderContent" -- reachable, and not
				// by the string the class wrote down.
				AddError(FString::Printf(TEXT("%s: answered by a node displayed as '%s'."),
					*Where, *Hole->GetDisplayName()));
			}
			if (Hole->GetComponent<UDreamNamedSlot>() == nullptr)
			{
				AddError(FString::Printf(TEXT("%s: the node carries no slot behaviour."), *Where));
			}

			// THE one this sweep exists for. Parts are bound by FindPart, holes are found by
			// FindSlotWidget, and the two walks are separate: FindPart stops at the FIRST node of
			// that display name and meets ancestors before descendants, so a control whose furniture
			// wears its hole's name binds the part field to the furniture and the slot field to the
			// same furniture. UDreamExpandableArea shipped exactly that -- its header FACE and its
			// header HOLE were both called "Header", so the empty-hole swap slept the whole header
			// of every expander that did not want a custom title, and nothing was null to notice.
			if (const UDreamUIControl* AsControl = Cast<UDreamUIControl>(Control.Get()))
			{
				UDreamWidget* Part = AsControl->FindPart(SlotName);
				if ((UObject*)Part != (UObject*)Hole)
				{
					AddError(FString::Printf(
						TEXT("%s: the part walk stops at '%s' instead, so both fields bind to the same node."),
						*Where, Part != nullptr ? *Part->GetDisplayName() : TEXT("nothing")));
				}
			}
		}
	}

	// A sweep that swept nothing passes for the wrong reason.
	TestTrue(TEXT("the sweep found the registered controls"), Classes.Num() >= 15);
	TestTrue(TEXT("and at least the holes this library ships"), HolesChecked >= 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNativeControlTreeNameSweepTest,
	"DreamGUI.Controls.Parts.NoNativeControlBuildsTwoNodesUnderOneDisplayName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNativeControlTreeNameSweepTest::RunTest(const FString& Parameters)
{
	using namespace DreamNativeControlSlotTestLocal;

	// A display name is an ADDRESS in a control: CollectParts binds by it, FindSlotWidget matches by
	// it, a template author reproduces it, the animation editor lists it. Two nodes wearing one name
	// makes that address ambiguous, and every consumer resolves the ambiguity the same silent way --
	// first hit wins, ancestors before descendants -- so the second node becomes unaddressable while
	// every field that names it points somewhere plausible. Nothing goes null; nothing is logged.
	//
	// The check is on a DEFAULT control: the pools (list rows, ring wedges, tab strips, dropdown
	// items) are copies of one template and grow from data, so a control asked for nothing builds
	// exactly the tree its class wrote down.
	TArray<UClass*> Classes;
	CollectRegisteredControlClasses(Classes);

	int32 NodesChecked = 0;
	for (UClass* Class : Classes)
	{
		TDreamTestControl<UDreamUserWidget> Control(NewObject<UDreamUserWidget>(GetTransientPackage(), Class));
		Control->Initialize();

		TArray<UDreamWidget*> Nodes;
		CollectOwnNodes(*Control.Get(), Nodes);
		NodesChecked += Nodes.Num();

		TSet<FString> Seen;
		for (const UDreamWidget* Node : Nodes)
		{
			const FString Name = Node->GetDisplayName();
			if (Name.IsEmpty())
			{
				continue;
			}
			bool bAlreadySeen = false;
			Seen.Add(Name, &bAlreadySeen);
			if (bAlreadySeen)
			{
				AddError(FString::Printf(
					TEXT("%s builds two nodes called '%s'; the second one cannot be addressed by name."),
					*Class->GetName(), *Name));
			}
		}
	}

	TestTrue(TEXT("the sweep found the registered controls"), Classes.Num() >= 15);
	TestTrue(TEXT("and walked the trees they built"), NodesChecked >= 15);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
