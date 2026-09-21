// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The edges of the widget hierarchy: half-dead children, sibling indices that disagree with the array
 * they index, and a drawn transform built from a size that has since changed.
 *
 * Each of these is a case the ordinary tests cannot reach, because the ordinary tests build a tree and
 * then use it. These build the states a teardown, an undo or a preview rebuild leaves behind.
 */

namespace DreamCoreHierarchyRobustnessTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W = 100.0f, float H = 50.0f)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}

	/** Write a property and announce it, which is all the Details panel does. */
	template<typename T>
	bool EditAsDetailsPanel(FAutomationTestBase& Test, UDreamWidget* Widget, const TCHAR* PropertyName, const T& NewValue)
	{
		FProperty* Property = UDreamWidget::StaticClass()->FindPropertyByName(FName(PropertyName));
		if (Property == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("No property named %s on UDreamWidget."), PropertyName));
			return false;
		}
		*Property->ContainerPtrToValuePtr<T>(Widget) = NewValue;
		FPropertyChangedEvent Event(Property);
		Event.SetActiveMemberProperty(Property);
		Widget->PostEditChangeProperty(Event);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSiblingIndexReinsertStaysInRangeTest,
	"DreamGUI.Hierarchy.ReorderingIntoAParentWhoseChildrenHaveAllDiedReinsertsInRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSiblingIndexReinsertStaysInRangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamCoreHierarchyRobustnessTestLocal;
	FScopedGameWorld TestWorld;

	// ApplySiblingIndex tested Children.Num() == 0 BEFORE calling EnsureUIChildrenValid, then clamped
	// the index against Num() - 1 and inserted. When the validity sweep emptied the array -- every slot
	// dead, this widget's included -- the array was no longer empty when the test ran and was empty by
	// the time the clamp did: FMath::Clamp(x, 0, -1) answers -1 for any x >= 0, and TArray::Insert
	// guards its index with checkSlow only, which is compiled out of Development and Shipping. The
	// result was a memmove from Data - 1 and a pointer written one element before the allocation.
	//
	// COVERAGE BOUNDARY, matching DreamRemoveChildAutomationTests: the real state has slots the
	// COLLECTOR emptied, and producing those needs a CollectGarbage that would also sweep up whatever
	// earlier tests in the run left behind. Garbage-but-not-yet-swept hits the same predicate through
	// the same IsValid, one step earlier.
	TStrongObjectPtr<UDreamWidget> Root(MakeWidget(TestWorld.World, nullptr, TEXT("Root")));
	UDreamWidget* First = MakeWidget(TestWorld.World, Root.Get(), TEXT("First"));
	UDreamWidget* Second = MakeWidget(TestWorld.World, Root.Get(), TEXT("Second"));
	if (!TestEqual(TEXT("Both children attached"), Root->GetChildrenCount(), 2))
	{
		return false;
	}

	First->MarkAsGarbage();
	Second->MarkAsGarbage();

	// Second is not in the array any more once the sweep runs, so the re-insert has nothing to displace
	// and the only in-range answer is the front.
	Second->SetSiblingIndex(5);

	TestEqual(TEXT("The reordered child is back in the array exactly once"), Root->GetChildrenCount(), 1);
	TestEqual(TEXT("...at an index the array actually has"), Second->GetSiblingIndex(), 0);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPreRegisterAttachStatesItsSiblingIndexTest,
	"DreamGUI.Hierarchy.AWidgetAttachedBeforeRegisterKeepsItsPlaceOnTopAcrossAResort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPreRegisterAttachStatesItsSiblingIndexTest::RunTest(const FString& Parameters)
{
	using namespace DreamCoreHierarchyRobustnessTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	UDreamWidget* Page = MakeWidget(TestWorld.World, Root, TEXT("Page"));

	// The modal dim, the tooltip, the drag visual and the virtual cursor all attach this way, and all of
	// them mean "on top". SetParentBeforeRegister appended to Children -- which IS on top, last drawn --
	// but left SiblingIndex at INDEX_NONE, so the array and the index disagreed about the same widget.
	// Nothing read the index until something re-sorted the parent, which a preview rebuild or an undo in
	// the designer does routinely; then -1 sorted FIRST and the overlay went to the bottom of the stack.
	UDreamWidget* Overlay = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Overlay->SetDisplayName(TEXT("Overlay"));
	Overlay->SetParentBeforeRegister(Root);

	TestEqual(TEXT("It is appended"), Root->GetChildrenCount(), 2);
	TestEqual(TEXT("...and says the index the append gave it"), Overlay->GetSiblingIndex(), 1);
	TestEqual(TEXT("The widget already there is untouched"), Page->GetSiblingIndex(), 0);

	// Ask for the re-sort explicitly rather than waiting for a rebuild to do it: RestoreSiblingIndex
	// raises the parent's lazy-sort flag, and the next GetChildren() is where the disagreement showed.
	Overlay->RestoreSiblingIndex(Overlay->GetSiblingIndex());
	const TArray<UDreamWidget*>& Sorted = Root->GetChildren();
	if (TestEqual(TEXT("Still two children after the sort"), Sorted.Num(), 2))
	{
		TestTrue(TEXT("The pre-register attach is still the last child, i.e. still on top"),
			Sorted.Last() == Overlay);
	}

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderTransformFollowsAResizeTest,
	"DreamGUI.RenderTransform.ResizingAScaledWidgetMovesItsDrawnTransformToTheNewPivotPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRenderTransformFollowsAResizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamCoreHierarchyRobustnessTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 300.0f);
	UDreamWidget* Card = MakeWidget(TestWorld.World, Root, TEXT("Card"), 100.0f, 50.0f);

	// A render transform turns about a point resolved from the CURRENT size:
	// GetLocalSpaceLeft() + GetWidth() * RenderTransformPivot.X. Pivot at the widget's left/bottom
	// corner keeps that point away from the origin, which is what makes the resize observable -- with
	// both pivots centred the point IS the origin and nothing here can move.
	Card->SetRenderTransformPivot(FVector2D(0.0, 0.0));
	Card->SetRenderScale(FVector(1.0, 2.0, 2.0));

	// Pivot point is (-W/2, -H/2) in the widget's plane; the composed transform therefore translates by
	// P * (1 - Scale), i.e. (W/2, H/2) at scale 2.
	const FVector Before = Card->GetWorldTransform().GetLocation();
	TestTrue(TEXT("The scaled widget starts drawn at the pivot its current size implies"),
		FMath::IsNearlyEqual(Before.Y, 50.0, 0.01) && FMath::IsNearlyEqual(Before.Z, 25.0, 0.01));

	// A point-anchored widget's relative LOCATION does not depend on its own size, so this resize leaves
	// it alone -- SetRelativeLocation early-outs on the unchanged value and the transform cascade that
	// hangs off it never runs. That is precisely why the drawn transform used to keep the old pivot.
	Card->SetWidth(200.0f);

	const FVector After = Card->GetWorldTransform().GetLocation();
	TestTrue(TEXT("The drawn transform followed the new width"),
		FMath::IsNearlyEqual(After.Y, 100.0, 0.01));
	TestTrue(TEXT("...and left the axis that did not change alone"),
		FMath::IsNearlyEqual(After.Z, 25.0, 0.01));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDestroyedWidgetStopsBeingValidTest,
	"DreamGUI.Hierarchy.ADestroyedWidgetAndItsSubtreeStopAnsweringYesToIsValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDestroyedWidgetStopsBeingValidTest::RunTest(const FString& Parameters)
{
	using namespace DreamCoreHierarchyRobustnessTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	UDreamWidget* Doomed = MakeWidget(TestWorld.World, Root, TEXT("Doomed"));
	UDreamWidget* Under = MakeWidget(TestWorld.World, Doomed, TEXT("Under"));
	UDreamWidget* Survivor = MakeWidget(TestWorld.World, Root, TEXT("Survivor"));

	// "Destroy" used to mean "unregister and detach", so IsValid() went on answering true and every
	// IsValid check in the plugin -- all of them written to mean "is this still there" -- said yes.
	TestTrue(TEXT("Everything starts valid"),
		IsValid(Doomed) && IsValid(Under) && IsValid(Survivor));

	TestTrue(TEXT("Destroying the child reports success"), Root->DestroyChild(Doomed));

	TestFalse(TEXT("The destroyed widget is no longer valid"), IsValid(Doomed));
	TestFalse(TEXT("...nor is what was underneath it"), IsValid(Under));
	TestTrue(TEXT("Its sibling is untouched"), IsValid(Survivor) && Survivor->HasRegistered());
	TestTrue(TEXT("So is the parent"), IsValid(Root) && Root->HasRegistered());
	// The array notices too, which is the point: every IsValid-filtered walk now skips it.
	TestEqual(TEXT("The parent is down to one child"), Root->GetChildrenCount(), 1);
	TestFalse(TEXT("Destroying it a second time does nothing"), Root->DestroyChild(Doomed));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDestroyLeavesRootedWidgetsAloneTest,
	"DreamGUI.Hierarchy.DestroyingASubtreeLeavesARootedWidgetInItAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDestroyLeavesRootedWidgetsAloneTest::RunTest(const FString& Parameters)
{
	using namespace DreamCoreHierarchyRobustnessTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	UDreamWidget* Rooted = MakeWidget(TestWorld.World, Root, TEXT("Rooted"));

	// UObjectBaseUtility::MarkAsGarbage check()s that the object is not rooted, so the teardown has to
	// skip those rather than assert in a build that has checks. It still unregisters them.
	Rooted->AddToRoot();
	Root->DestroyWidget();

	TestTrue(TEXT("A rooted widget survives the teardown"), IsValid(Rooted));
	TestFalse(TEXT("...but is unregistered like everything else in it"), Rooted->HasRegistered());

	Rooted->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDetailsRenderOpacitySurvivesADeadChildTest,
	"DreamGUI.Editor.EditingRenderOpacityOverASubtreeHoldingADeadChildDoesNotWalkIntoIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDetailsRenderOpacitySurvivesADeadChildTest::RunTest(const FString& Parameters)
{
	using namespace DreamCoreHierarchyRobustnessTestLocal;
	FScopedGameWorld TestWorld;

	// PostEditChangeProperty's RenderOpacity branch walks Children recursively and dereferenced every
	// entry. Children can hold entries that are not live widgets -- the file says so in five other
	// places, and the two runtime twins of this exact lambda (SetRenderOpacity, SetPixelSnapping) both
	// guard with IsValid and explain why. The editor copy did not, and the details panel is reached
	// exactly when a widget has just been deleted, undone, or rebuilt as a preview.
	//
	// COVERAGE BOUNDARY, as in the sibling-index test above: a literal null needs a collection, so this
	// pins the same IsValid predicate on a child that is garbage but not yet swept.
	TStrongObjectPtr<UDreamWidget> Root(MakeWidget(TestWorld.World, nullptr, TEXT("Root")));
	UDreamWidget* Live = MakeWidget(TestWorld.World, Root.Get(), TEXT("Live"));
	UDreamWidget* Dead = MakeWidget(TestWorld.World, Root.Get(), TEXT("Dead"));
	UDreamWidget* UnderDead = MakeWidget(TestWorld.World, Dead, TEXT("UnderDead"));
	Dead->MarkAsGarbage();

	if (!EditAsDetailsPanel<float>(*this, Root.Get(), TEXT("RenderOpacity"), 0.25f))
	{
		return false;
	}

	TestTrue(TEXT("The edit landed on the widget"),
		FMath::IsNearlyEqual(Root->GetRenderOpacity(), 0.25f, 0.001f));
	// The live branch still inherits it, so the guard stopped the walk at the dead child rather than
	// stopping the walk altogether.
	TestTrue(TEXT("The live subtree still inherits the new opacity"),
		FMath::IsNearlyEqual(Live->GetFinalRenderOpacity(), 0.25f, 0.001f));
	TestNotNull(TEXT("The widget under the dead one is untouched, not collected"), UnderDead);

	Root->DestroyWidget();
	return true;
}

#endif
