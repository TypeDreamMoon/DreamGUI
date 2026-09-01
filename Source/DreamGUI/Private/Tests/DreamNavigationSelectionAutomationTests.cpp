// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/World.h"
#include "Interaction/UINavigationInputSelectionHandler.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

/*
 * The gamepad selection cursor -- the highlight that flies between the widget navigation has focused
 * and the next one -- tested from the side that used to be untestable.
 *
 * Its guards are pinned next door in DreamInteractionExtensionAutomationTests: a handler with no
 * widget, a deselect with nothing selected, a select of nothing from nothing. What is asserted HERE
 * is everything past those guards, which until recently could not be reached from a test at all.
 * Each of the three branches asks UDreamWidget::RenderOpacityTo for a fade, and RenderOpacityTo
 * hands back null whenever UDreamTweenManager cannot reach the game instance subsystem that drives
 * tweens. SelectNone then chained ->OnComplete straight off that null, so walking into the branch
 * headless was a crash rather than a failure.
 *
 * That null is not a test artefact, which is the reason this suite is worth having. It is the answer
 * for a widget with no world -- a Blueprint's authoring tree, a dedicated server, this fixture --
 * and equally for a widget in an editor world, which HAS a world and still has no game instance. So
 * "no tween was available" is a production state on the designer's screen, and what the handler does
 * about it is a contract rather than a degradation.
 *
 * The contract these tests pin: an animation that cannot be started is skipped, but its DESTINATION
 * is not. The fade is how the cursor arrives or leaves politely; being at the right opacity, on the
 * right widget, or gone entirely is the actual outcome, and deferring an outcome to an OnComplete
 * that can never fire is how a cursor gets stranded fully opaque over the last thing it marked.
 *
 * What is deliberately NOT asserted here: anything about a fade that actually runs. Every assertion
 * below is on the no-tween path, because a fixture that could produce a real tweener would need a
 * game instance, and a tweener only advances when something ticks the manager. The branch that has
 * a tween is the branch that was already working.
 */

namespace DreamNavigationSelectionTestLocal
{
	/**
	 * Reach a protected UPROPERTY, and say so loudly if a rename has moved it.
	 *
	 * The handler's selection record is VisibleAnywhere and protected -- readable in a Details panel
	 * and settable from nowhere -- so reflection is the only way for a test to put the handler into
	 * the state where its interesting branches are reachable. A rename that turned an assertion into
	 * a silent no-op would be worse than having no test.
	 */
	template<typename T>
	T* FieldPtr(FAutomationTestBase& Test, UObject* Object, const TCHAR* PropertyName)
	{
		FProperty* Property = Object->GetClass()->FindPropertyByName(FName(PropertyName));
		if (Property == nullptr)
		{
			Test.AddError(FString::Printf(
				TEXT("%s no longer has a property named '%s', so this test is asserting against nothing."),
				*Object->GetClass()->GetName(), PropertyName));
			return nullptr;
		}
		return Property->ContainerPtrToValuePtr<T>(Object);
	}

	/** How many entries the handler is holding, which is the only way to see that it recorded one. */
	int32 ArrayNum(FAutomationTestBase& Test, UObject* Object, const TCHAR* PropertyName)
	{
		const FArrayProperty* Property = CastField<FArrayProperty>(
			Object->GetClass()->FindPropertyByName(FName(PropertyName)));
		if (Property == nullptr)
		{
			Test.AddError(FString::Printf(
				TEXT("%s no longer has an array property named '%s'."),
				*Object->GetClass()->GetName(), PropertyName));
			return INDEX_NONE;
		}
		FScriptArrayHelper Helper(Property, Property->ContainerPtrToValuePtr<void>(Object));
		return Helper.Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNavigationSelectNoneWithoutTweenTest,
	"DreamGUI.Navigation.Selection.RetiringTheCursorWithNoTweenAvailableStillRetiresIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNavigationSelectNoneWithoutTweenTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationSelectionTestLocal;

	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Target = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Target"));
	UDreamWidget* CursorWidget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Cursor"));
	if (!TestNotNull(TEXT("a widget for the cursor to mark"), Target)
		|| !TestNotNull(TEXT("a widget to be the cursor"), CursorWidget))
	{
		return false;
	}
	Tree->RootWidget = Target;
	Target->SetWidth(200.0f);
	Target->SetHeight(100.0f);

	UUINavigationInputSelectionHandler* Cursor =
		CursorWidget->AddComponent<UUINavigationInputSelectionHandler>();
	if (!TestNotNull(TEXT("the cursor widget carries a selection handler"), Cursor))
	{
		return false;
	}

	// The precondition, asserted rather than assumed. Everything below is about what happens when no
	// tween can be made, so a fixture that could quietly start making them would turn this whole
	// test into a description of the path it is not aiming at.
	TestNull(TEXT("a widget under the transient package has no world"), CursorWidget->GetWorld());
	TestTrue(TEXT("...so the tween manager will not issue it a tweener"),
		CursorWidget->RenderOpacityTo(0.5f) == nullptr);

	TWeakObjectPtr<UDreamWidget>* CurrentSelected =
		FieldPtr<TWeakObjectPtr<UDreamWidget>>(*this, Cursor, TEXT("CurrentSelected"));
	if (CurrentSelected == nullptr)
	{
		return false;
	}

	// Put the handler into the one state SelectNone acts on. A cursor that is marking something is
	// parented to it and drawn at full opacity, which is what makes the two assertions after the
	// call mean anything: both values have somewhere to move FROM.
	*CurrentSelected = Target;
	CursorWidget->TrySetParent(Target, false);
	CursorWidget->SetRenderOpacity(1.0f);
	TestTrue(TEXT("the cursor starts out parented to the widget it marks"),
		CursorWidget->GetParent() == Target);

	// The old code read `RenderOpacityTo(...)->OnComplete(...)` as one expression, so reaching the
	// next line at all is the first assertion this test makes.
	Cursor->SelectNone();

	TestFalse(TEXT("the handler is no longer marking anything"), CurrentSelected->IsValid());
	TestEqual(TEXT("nothing was recorded as animating, because nothing was animating"),
		ArrayNum(*this, Cursor, TEXT("TweenerCollection")), 0);

	// The two halves of "the animation was skipped, its destination was not". Both matter on their
	// own: an opacity left at 1 is a cursor that vanishes without fading, and a widget left alive is
	// a cursor that never leaves -- and the second one is the failure the old OnComplete produced
	// even when it did not crash, because the callback it hung the destruction off was attached to
	// a tweener that did not exist.
	TestEqual(TEXT("the cursor was taken to the opacity the fade would have ended at"),
		CursorWidget->GetRenderOpacity(), 0.0f);
	TestNull(TEXT("...and torn down rather than left parked on the widget it was marking"),
		CursorWidget->GetParent());

	// Deselecting again is ordinary input -- a second Back press, a screen closing on top of one
	// that already closed -- and must find nothing left to do.
	Cursor->SelectNone();
	TestFalse(TEXT("a second deselect still leaves nothing selected"), CurrentSelected->IsValid());
	TestEqual(TEXT("...and still starts nothing"),
		ArrayNum(*this, Cursor, TEXT("TweenerCollection")), 0);

	CursorWidget->DestroyWidget();
	Target->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNavigationSelectWidgetWithoutTweenTest,
	"DreamGUI.Navigation.Selection.ASelectionChangeRecordsOnlyTheAnimationsItActuallyStarted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNavigationSelectWidgetWithoutTweenTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationSelectionTestLocal;

	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Target = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Target"));
	UDreamWidget* CursorWidget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Cursor"));
	if (!TestNotNull(TEXT("a widget for the cursor to mark"), Target)
		|| !TestNotNull(TEXT("a widget to be the cursor"), CursorWidget))
	{
		return false;
	}
	Tree->RootWidget = Target;
	Target->SetWidth(200.0f);
	Target->SetHeight(100.0f);

	UUINavigationInputSelectionHandler* Cursor =
		CursorWidget->AddComponent<UUINavigationInputSelectionHandler>();
	if (!TestNotNull(TEXT("the cursor widget carries a selection handler"), Cursor))
	{
		return false;
	}
	TWeakObjectPtr<UDreamWidget>* CurrentSelected =
		FieldPtr<TWeakObjectPtr<UDreamWidget>>(*this, Cursor, TEXT("CurrentSelected"));
	if (CurrentSelected == nullptr)
	{
		return false;
	}

	// A cursor that has never marked anything is invisible, which is the state the fade-in exists to
	// bring it out of.
	CursorWidget->SetRenderOpacity(0.0f);

	// Appearing: something selected, nothing selected before. The fade-in cannot be started, so the
	// cursor is put at its destination instead -- an invisible highlight sitting exactly on top of
	// the focused widget is indistinguishable, to a player, from navigation being broken.
	Cursor->SelectWidget(Target);
	TestTrue(TEXT("the handler records what it is marking"), CurrentSelected->Get() == Target);
	TestEqual(TEXT("the cursor is at the opacity the fade-in would have ended at"),
		CursorWidget->GetRenderOpacity(), 1.0f);
	TestTrue(TEXT("...and has been moved onto the widget it marks"),
		CursorWidget->GetParent() == Target);

	// The collection is a list of animations in flight, and a null is not one. Storing the tween
	// manager's refusal would make the list unanswerable by counting and would hand a null to the
	// kill loop at the top of the next selection change.
	TestEqual(TEXT("no animation was recorded, because none was started"),
		ArrayNum(*this, Cursor, TEXT("TweenerCollection")), 0);

	// Leaving: nothing selected, something selected before. Unlike SelectNone this branch only
	// fades -- the cursor stays alive and stays where it was, ready to be brought back -- so the
	// assertion that it is still parented is as much the point as the opacity.
	Cursor->SelectWidget(nullptr);
	TestFalse(TEXT("the handler is no longer marking anything"), CurrentSelected->IsValid());
	TestEqual(TEXT("the cursor is at the opacity the fade-out would have ended at"),
		CursorWidget->GetRenderOpacity(), 0.0f);
	TestTrue(TEXT("...but is still alive, because a fade-out is not a retirement"),
		CursorWidget->GetParent() == Target);
	TestEqual(TEXT("and still nothing was recorded as animating"),
		ArrayNum(*this, Cursor, TEXT("TweenerCollection")), 0);

	CursorWidget->DestroyWidget();
	Target->DestroyWidget();
	return true;
}

#endif
