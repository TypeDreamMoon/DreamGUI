// Copyright 2026-Present LexLiu. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Core/LexUISettings.h"
#include "Engine/World.h"
#include "LexUIBPLibrary.h"

/*
 * The never-added leak detector.
 *
 * Holding created widgets in the manager is what keeps them alive; the cost is that forgetting to
 * add one is invisible until the world tears down, where UMG would simply have collected it. This
 * closes that gap without pretending to know better than the caller: off unless asked for, and
 * when it does fire it destroys the widget properly rather than dropping the reference and letting
 * GC find a still-registered widget, which is the path that logs an error nobody can trace.
 */

namespace LexParkedSweepTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Settings are global, so a test that changes one has to put it back however it leaves. */
	struct FScopedParkedLifetime
	{
		float Previous = 0.0f;
		explicit FScopedParkedLifetime(float NewValue)
		{
			ULexUISettings* Settings = GetMutableDefault<ULexUISettings>();
			Previous = Settings->ParkedWidgetLifetimeSeconds;
			Settings->ParkedWidgetLifetimeSeconds = NewValue;
		}
		~FScopedParkedLifetime()
		{
			GetMutableDefault<ULexUISettings>()->ParkedWidgetLifetimeSeconds = Previous;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexParkedSweepIsOffByDefaultTest,
	"LGUI.Widget.Pending.SweepIsOffUnlessAskedFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexParkedSweepIsOffByDefaultTest::RunTest(const FString& Parameters)
{
	using namespace LexParkedSweepTestLocal;
	FScopedGameWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!Manager)
	{
		AddError(TEXT("No manager for the test world."));
		return false;
	}

	TestEqual(TEXT("The setting ships off"), GetDefault<ULexUISettings>()->ParkedWidgetLifetimeSeconds, 0.0f);

	FScopedParkedLifetime Lifetime(0.0f);
	ULexWidget* Waiting = ULexUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("Waiting"), nullptr);
	if (!TestNotNull(TEXT("ConstructWidget returns a widget"), Waiting))return false;

	// A caller waiting on an async load looks exactly like a leak from here. With the check off --
	// which is how it ships -- their widget is never taken away, no matter how long they take.
	TestEqual(TEXT("Nothing is swept while the check is off"), Manager->SweepExpiredParkedWidgets(), 0);
	TestTrue(TEXT("The widget is untouched"), IsValid(Waiting) && Waiting->HasRegistered());
	TestTrue(TEXT("...and still held"), Manager->IsWidgetParked(Waiting));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexParkedSweepDestroysTest,
	"LGUI.Widget.Pending.ExpiredWidgetsAreDestroyedNotDropped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexParkedSweepDestroysTest::RunTest(const FString& Parameters)
{
	using namespace LexParkedSweepTestLocal;
	FScopedGameWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!Manager)
	{
		AddError(TEXT("No manager for the test world."));
		return false;
	}

	// Nothing ticks a test world, so age it by hand rather than waiting: park at the current time,
	// then move the clock past the lifetime.
	FScopedParkedLifetime Lifetime(1.0f);
	ULexWidget* Forgotten = ULexUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("Forgotten"), nullptr);
	if (!TestNotNull(TEXT("ConstructWidget returns a widget"), Forgotten))return false;
	TestEqual(TEXT("Nothing is swept before the lifetime is up"), Manager->SweepExpiredParkedWidgets(), 0);
	TestWorld.World->TimeSeconds += 5.0;

	AddExpectedError(TEXT("was created .* ago and never added"), EAutomationExpectedErrorFlags::Contains, 0);
	TestEqual(TEXT("The forgotten widget is swept"), Manager->SweepExpiredParkedWidgets(), 1);

	// Destroyed, not merely let go of. Dropping the reference would leave a still-registered widget
	// for GC, and ULexWidget::BeginDestroy answers that with an error and an on-screen banner from a
	// stack that says nothing about where the widget came from.
	TestFalse(TEXT("It is unregistered, so BeginDestroy will stay quiet"), Forgotten->HasRegistered());
	TestFalse(TEXT("It is no longer held"), Manager->IsWidgetParked(Forgotten));
	TestEqual(TEXT("The held set is empty"), Manager->GetParkedWidgets().Num(), 0);
	TestEqual(TEXT("A second sweep finds nothing"), Manager->SweepExpiredParkedWidgets(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexParkedSweepSparesAddedTest,
	"LGUI.Widget.Pending.AddedWidgetsAreNeverSwept",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexParkedSweepSparesAddedTest::RunTest(const FString& Parameters)
{
	using namespace LexParkedSweepTestLocal;
	FScopedGameWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!Manager)
	{
		AddError(TEXT("No manager for the test world."));
		return false;
	}
	FScopedParkedLifetime Lifetime(1.0f);

	// A host that was never created through the new verbs, so it is never a sweep candidate itself
	// and cannot take its children down with it.
	ULexWidget* Host = NewObject<ULexWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Host->SetDisplayName(TEXT("Host"));
	Host->OnRegister();

	ULexWidget* Added = ULexUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("Added"), nullptr);
	ULexWidget* Forgotten = ULexUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("Forgotten"), nullptr);
	if (!TestNotNull(TEXT("Added"), Added) || !TestNotNull(TEXT("Forgotten"), Forgotten))return false;

	// Adding is what takes a widget out of the held set, so a widget in use can never be swept --
	// the property that makes it safe to turn this on in a running game at all.
	Host->AddChild(Added);
	TestFalse(TEXT("The added child is no longer held"), Manager->IsWidgetParked(Added));

	TestWorld.World->TimeSeconds += 5.0;
	AddExpectedError(TEXT("was created .* ago and never added"), EAutomationExpectedErrorFlags::Contains, 0);
	TestEqual(TEXT("Only the forgotten one is swept"), Manager->SweepExpiredParkedWidgets(), 1);

	TestTrue(TEXT("The added child survives"), IsValid(Added) && Added->HasRegistered());
	TestTrue(TEXT("...still attached"), Added->GetParent() == Host);
	TestFalse(TEXT("The forgotten one is gone"), Forgotten->HasRegistered());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexParkedSweepTakesSubtreeTest,
	"LGUI.Widget.Pending.SweepingAForgottenRootTakesItsSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexParkedSweepTakesSubtreeTest::RunTest(const FString& Parameters)
{
	using namespace LexParkedSweepTestLocal;
	FScopedGameWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!Manager)
	{
		AddError(TEXT("No manager for the test world."));
		return false;
	}
	FScopedParkedLifetime Lifetime(1.0f);

	// Building a tree and then forgetting to add its root is one mistake, not N, and the sweep
	// treats it that way: the root is the only thing still held -- adding the children un-parked
	// them -- and destroying it takes the whole subtree. Worth pinning because the alternative
	// reading, "only the root is destroyed", would leave the children alive with nobody holding
	// them, which is precisely the leak this is here to prevent.
	ULexWidget* Root = ULexUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("Root"), nullptr);
	ULexWidget* Child = ULexUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("Child"), nullptr);
	ULexWidget* Grandchild = ULexUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("Grandchild"), nullptr);
	if (!TestNotNull(TEXT("Root"), Root) || !TestNotNull(TEXT("Child"), Child)
		|| !TestNotNull(TEXT("Grandchild"), Grandchild))return false;
	Root->AddChild(Child);
	Child->AddChild(Grandchild);
	TestEqual(TEXT("Only the root is still held"), Manager->GetParkedWidgets().Num(), 1);

	TestWorld.World->TimeSeconds += 5.0;
	AddExpectedError(TEXT("was created .* ago and never added"), EAutomationExpectedErrorFlags::Contains, 0);
	TestEqual(TEXT("One sweep, one report"), Manager->SweepExpiredParkedWidgets(), 1);

	TestFalse(TEXT("The root is gone"), Root->HasRegistered());
	TestFalse(TEXT("...and so is the child"), Child->HasRegistered());
	TestFalse(TEXT("...and the grandchild, rather than being left ownerless"), Grandchild->HasRegistered());
	return true;
}

#endif
