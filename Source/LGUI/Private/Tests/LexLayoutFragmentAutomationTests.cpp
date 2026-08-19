// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Tests/LexLayoutInvalidationTestTypes.h"
#include "Engine/World.h"

/*
 * A panel used to write each child's rect the instant it decided it, from inside ApplyChildRect. That is
 * what made arranging and invalidating the same act: every write walked the ancestor chain while the pass
 * that caused it was still running, which is why FLayoutWriteScope had to exist at all, and why anything
 * that measured mid-pass could read a rect the same pass had just produced.
 *
 * The arrangement now records into an FLexFragment and the base class commits it afterwards - Blink's
 * answer, where NGLayoutAlgorithm returns an immutable fragment instead of mutating the layout tree in
 * place. These tests pin the two halves: nothing is written during the pass, and everything is written
 * after it.
 */

namespace LexLayoutFragmentTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	static ULexWidget* MakeChild(UWorld* World, ULexWidget* Parent, float W, float H)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(Parent ? (UObject*)Parent : (UObject*)World);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexFragmentArrangeWritesNothingTest,
	"LGUI.Layout.Fragment.ArrangeWritesNothingUntilCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexFragmentArrangeWritesNothingTest::RunTest(const FString& Parameters)
{
	using namespace LexLayoutFragmentTestLocal;
	FScopedTestWorld TestWorld;

	// An overlay stretches its children to fill, so the arranged size differs from the authored one -
	// which is what makes "has it been written yet" an answerable question.
	ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
	Root->SetWidth(200.0f);
	Root->SetHeight(100.0f);
	ULexWidget* ChildA = MakeChild(TestWorld.World, Root, 30.0f, 40.0f);
	ULexWidget* ChildB = MakeChild(TestWorld.World, Root, 50.0f, 60.0f);

	ULexArrangeObservingOverlay* Overlay = Cast<ULexArrangeObservingOverlay>(
		Root->CreateNewLayoutContainer(ULexArrangeObservingOverlay::StaticClass()));
	if (!TestNotNull(TEXT("Observing overlay created"), Overlay))
	{
		return false;
	}
	Root->OnRegister();
	ChildA->OnRegister();
	ChildB->OnRegister();
	for (ULexWidget* Child : { ChildA, ChildB })
	{
		if (ULexPanelSlot* Slot = Child->GetPanelSlot())
		{
			Slot->SetHorizontalAlignment(ELexPanelHorizontalAlignment::Fill);
			Slot->SetVerticalAlignment(ELexPanelVerticalAlignment::Fill);
		}
	}

	ULexWidget::MarkLayoutForRebuild(Root);
	ULexWidget::RebuildLayoutImmediately(Root);

	// After the commit the children fill the panel. This is the half that must not regress: the split
	// is only interesting if the result still lands.
	TestEqual(TEXT("Child A is arranged to fill after the pass"), ChildA->GetSize(), FVector2D(200.0, 100.0));
	TestEqual(TEXT("Child B is arranged to fill after the pass"), ChildB->GetSize(), FVector2D(200.0, 100.0));

	// ...and during the pass none of it had happened yet. The overlay photographs its children at the end
	// of its own arrange, after every ApplyChildRect has been called.
	if (!TestEqual(TEXT("Both children were observed during the arrange pass"),
		Overlay->SizesDuringArrange.Num(), 2))
	{
		Root->DestroyWidget();
		return false;
	}
	TestEqual(TEXT("Child A was still authored-size during the arrange pass"),
		Overlay->SizesDuringArrange[0], FVector2D(30.0, 40.0));
	TestEqual(TEXT("Child B was still authored-size during the arrange pass"),
		Overlay->SizesDuringArrange[1], FVector2D(50.0, 60.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexFragmentArrangeIsQueryableTest,
	"LGUI.Layout.Fragment.ArrangeCanBeAskedWithoutCommitting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexFragmentArrangeIsQueryableTest::RunTest(const FString& Parameters)
{
	using namespace LexLayoutFragmentTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
	Root->SetWidth(200.0f);
	Root->SetHeight(100.0f);
	ULexWidget* Child = MakeChild(TestWorld.World, Root, 30.0f, 40.0f);
	ULexLayoutContainerOverlay* Overlay = Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
	if (!TestNotNull(TEXT("Overlay created"), Overlay))
	{
		return false;
	}
	Root->OnRegister();
	Child->OnRegister();
	if (ULexPanelSlot* Slot = Child->GetPanelSlot())
	{
		Slot->SetHorizontalAlignment(ELexPanelHorizontalAlignment::Fill);
		Slot->SetVerticalAlignment(ELexPanelVerticalAlignment::Fill);
	}

	const FVector2D SizeBefore = Child->GetSize();

	// The arrangement is now something you can ask for. A caller that wants to know what a panel would do
	// no longer has to let it do it - the whole point of returning a result instead of applying one.
	const FLexFragment Fragment = Overlay->Arrange();

	TestEqual(TEXT("The fragment carries one child rect"), Fragment.Children.Num(), 1);
	if (Fragment.Children.Num() == 1)
	{
		TestEqual(TEXT("...for the child that was arranged"), Fragment.Children[0].Child, Child);
		TestTrue(TEXT("...at the filled size"),
			Fragment.Children[0].Size.Equals(FVector2f(200.0f, 100.0f), 0.01f));
	}
	TestEqual(TEXT("Asking did not move the child"), Child->GetSize(), SizeBefore);

	Root->DestroyWidget();
	return true;
}

#endif
