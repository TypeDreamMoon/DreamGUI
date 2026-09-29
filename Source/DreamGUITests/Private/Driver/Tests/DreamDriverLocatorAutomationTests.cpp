// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"

/*
 * WHICH WIDGET A TEST MEANT.
 *
 * Locators are built on UDreamWidget's own lookups rather than on a second implementation of them, so
 * two of their rules come along whether or not anyone wanted them: matching is CASE SENSITIVE, and a
 * path takes the first child of each name it meets and never backtracks. Both are the kind of thing
 * that costs an hour the first time it is met in a test that is failing for an unrelated reason, so
 * both are written down here as behaviour rather than left to be discovered.
 */
namespace DreamDriverLocatorTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D SmallSize(120.0, 60.0);
	const FVector2D LargeSize(400.0, 200.0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverLocatorNameTest,
	"DreamGUI.Driver.Locator.NameFindsEveryWidgetOfThatNameBelowTheRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverLocatorNameTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverLocatorTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Menu = Rig.MakeWidget(TEXT("Menu"), nullptr, LargeSize);
	UDreamWidget* Buttons = Rig.MakeWidget(TEXT("Buttons"), Menu, LargeSize);
	Rig.MakeWidget(TEXT("Play"), Buttons, SmallSize);
	Rig.MakeWidget(TEXT("Quit"), Buttons, SmallSize);
	UDreamWidget* Panel = Rig.MakeWidget(TEXT("Panel"), nullptr, LargeSize);
	Rig.MakeWidget(TEXT("Play"), Panel, SmallSize);
	Rig.PumpFrames(1);

	FDreamDriverRef Driver = Rig.Driver();

	// A name that is unique resolves to an element; one that is not resolves to none, because acting
	// on "whichever of these two the tree sorted first" is never what a test meant.
	TestTrue(TEXT("A unique name finds its widget"), Driver->Find(FDreamBy::Name(TEXT("Quit")))->Exists());
	TestFalse(TEXT("An ambiguous name resolves to no single widget"),
		Driver->Find(FDreamBy::Name(TEXT("Play")))->Exists());
	// FindAll is where an ambiguous name is answerable, and it finds both however deep they are.
	TestEqual(TEXT("Both widgets of that name are found, at whatever depth"),
		Driver->FindAll(FDreamBy::Name(TEXT("Play"))).Num(), 2);

	// Case sensitivity is inherited from UDreamWidget::FindChildByDisplayName and is not negotiable
	// here: a locator that matched case-insensitively would find widgets the production lookup cannot.
	TestFalse(TEXT("A name in the wrong case finds nothing"),
		Driver->Find(FDreamBy::Name(TEXT("quit")))->Exists());

	// The root is not a candidate. These search what is BELOW the root, like the lookup they are
	// built on, and the rig's own root is not part of anyone's tree under test.
	TestFalse(TEXT("The root itself is not found by name"),
		Driver->Find(FDreamBy::Name(TEXT("Root")))->Exists());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverLocatorPathTest,
	"DreamGUI.Driver.Locator.PathWalksDisplayNamesAndTakesTheFirstMatchAtEachLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverLocatorPathTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverLocatorTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Menu = Rig.MakeWidget(TEXT("Menu"), nullptr, LargeSize);
	UDreamWidget* Buttons = Rig.MakeWidget(TEXT("Buttons"), Menu, LargeSize);
	UDreamWidget* DeepPlay = Rig.MakeWidget(TEXT("Play"), Buttons, SmallSize);
	UDreamWidget* ShallowPlay = Rig.MakeWidget(TEXT("Play"), Menu, SmallSize);
	Rig.MakeWidget(TEXT("Deep"), Buttons, SmallSize);
	Rig.PumpFrames(1);

	FDreamDriverRef Driver = Rig.Driver();

	// A path says which one, where a name could not.
	TestSamePtr(TEXT("A full path reaches the deep one"),
		Driver->Find(FDreamBy::Path(TEXT("Menu/Buttons/Play")))->GetWidget(), DeepPlay);
	TestSamePtr(TEXT("A shorter path reaches the shallow one"),
		Driver->Find(FDreamBy::Path(TEXT("Menu/Play")))->GetWidget(), ShallowPlay);

	// The last segment is matched against DIRECT children only. A path is a path, not a search with
	// a prefix; "Menu/Play" above found the shallow one rather than the deep one for this reason,
	// and "Menu/Deep" finds nothing although a Deep exists one level further down.
	TestFalse(TEXT("A path does not search below its last segment"),
		Driver->Find(FDreamBy::Path(TEXT("Menu/Deep")))->Exists());
	TestTrue(TEXT("The same widget is reachable by the path that actually names it"),
		Driver->Find(FDreamBy::Path(TEXT("Menu/Buttons/Deep")))->Exists());

	TestFalse(TEXT("A path segment in the wrong case stops the walk"),
		Driver->Find(FDreamBy::Path(TEXT("menu/Buttons/Play")))->Exists());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverLocatorPathNoBacktrackTest,
	"DreamGUI.Driver.Locator.PathTakesTheFirstBranchOfADuplicateNameAndDoesNotBackTrack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverLocatorPathNoBacktrackTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverLocatorTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	// Two branches of the same name, and the thing being looked for is only in the second. The first
	// one made is the first one in the child array, and the path walk takes it and stops there.
	UDreamWidget* FirstGroup = Rig.MakeWidget(TEXT("Group"), nullptr, LargeSize);
	Rig.MakeWidget(TEXT("Decoy"), FirstGroup, SmallSize);
	UDreamWidget* SecondGroup = Rig.MakeWidget(TEXT("Group"), nullptr, LargeSize);
	UDreamWidget* Wanted = Rig.MakeWidget(TEXT("Target"), SecondGroup, SmallSize);
	Rig.PumpFrames(1);

	FDreamDriverRef Driver = Rig.Driver();

	// This is the trap, written down. A duplicate name partway along a path sends the walk into a
	// branch that does not contain the rest of it, and nothing goes back to try the other branch.
	TestFalse(TEXT("A path through a duplicated name does not reach into the other branch"),
		Driver->Find(FDreamBy::Path(TEXT("Group/Target")))->Exists());
	// The widget is there; it is the path that cannot express which Group.
	TestSamePtr(TEXT("The widget is findable by name regardless"),
		Driver->Find(FDreamBy::Name(TEXT("Target")))->GetWidget(), Wanted);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverLocatorClassAndPredicateTest,
	"DreamGUI.Driver.Locator.ClassAndPredicateSelectFromTheWholeSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverLocatorClassAndPredicateTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverLocatorTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}

	UDreamWidget* Menu = Rig.MakeWidget(TEXT("Menu"), nullptr, LargeSize);
	Rig.MakeWidget(TEXT("Play"), Menu, SmallSize);
	Rig.MakeWidget(TEXT("Quit"), Menu, SmallSize);
	Rig.PumpFrames(1);

	FDreamDriverRef Driver = Rig.Driver();

	// Three widgets were made; the root they hang from is not one of the candidates.
	TestEqual(TEXT("A class locator finds every widget below the root"),
		Driver->FindAll(FDreamBy::Class<UDreamWidget>()).Num(), 3);

	// A predicate reaches what no name or class can express.
	const TArray<FDreamElementRef> Wide = Driver->FindAll(
		FDreamBy::Predicate([](UDreamWidget* InWidget) { return InWidget->GetWidth() > 200.0f; },
			TEXT("wider than 200 units")));
	TestEqual(TEXT("Only the large widget is wider than 200 units"), Wide.Num(), 1);
	if (Wide.Num() == 1)
	{
		TestSamePtr(TEXT("And it is the one that is"), Wide[0]->GetWidget(), Menu);
	}

	// A predicate that matches nothing is not an error, and the description it was given is what a
	// timeout would have printed.
	const FDreamLocatorRef Impossible = FDreamBy::Predicate(
		[](UDreamWidget*) { return false; }, TEXT("impossible"));
	TestEqual(TEXT("A predicate that matches nothing finds nothing"),
		Driver->FindAll(Impossible).Num(), 0);
	TestTrue(TEXT("A locator can say what it was looking for"), Impossible->Describe().Contains(TEXT("impossible")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverLocatorMissingTest,
	"DreamGUI.Driver.Locator.AnElementThatMatchesNothingAnswersEveryQuestionWithNo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverLocatorMissingTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverLocatorTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	Rig.MakeWidget(TEXT("Present"), nullptr, SmallSize);
	Rig.PumpFrames(1);

	// "Not there yet" is an ordinary state -- it is what a test waits out -- so Find always answers
	// with an element and the element answers with no. Nothing here may crash or report an error.
	FDreamElementRef Missing = Rig.Driver()->Find(FDreamBy::Name(TEXT("NotHere")));
	TestFalse(TEXT("A missing element does not exist"), Missing->Exists());
	TestNull(TEXT("A missing element has no widget"), Missing->GetWidget());
	TestFalse(TEXT("A missing element is not visible"), Missing->IsVisible());
	TestFalse(TEXT("A missing element is not interactable"), Missing->IsInteractable());
	TestFalse(TEXT("A missing element is not hovered"), Missing->IsHovered());
	TestFalse(TEXT("A missing element is not pressed"), Missing->IsPressed());
	TestFalse(TEXT("A missing element is not selected"), Missing->IsSelected());
	TestFalse(TEXT("A missing element has no pixel rect"), Missing->GetPixelRect().IsSet());
	TestFalse(TEXT("A missing element has no centre pixel"), Missing->GetCentrePixel().IsSet());
	TestTrue(TEXT("A missing element can still say what it was looking for"),
		Missing->Describe().Contains(TEXT("NotHere")));

	return true;
}

#endif
