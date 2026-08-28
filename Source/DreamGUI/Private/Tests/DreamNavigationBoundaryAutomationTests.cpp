// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Interaction/UISelectable.h"
#include "Engine/World.h"

/*
 * What a directional move does at the edge of a restricted navigation area. Until now the answer was
 * always "nothing" -- FindSelectable initialised its best pick to the selectable it started from, so
 * running out of candidates and finding yourself were the same outcome and there was no third option.
 * Wrap and Escape are the two a player expects from a menu, and Stop stays the default so no existing
 * prefab changes behaviour.
 */

namespace DreamNavigationBoundaryTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Registered on creation, because a component added to an unregistered widget never registers itself. */
	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float Z, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		Widget->SetRelativeLocation(FVector(0, 0, Z));
		Widget->OnRegister();
		return Widget;
	}

	UUISelectable* MakeSelectable(UDreamWidget* Widget)
	{
		return Widget->AddComponent<UUISelectable>();
	}

	/** A three-row column inside an area widget, plus one selectable sitting below the area entirely. */
	struct FColumnFixture
	{
		UDreamWidget* Root = nullptr;
		UDreamWidget* Area = nullptr;
		UUISelectable* Top = nullptr;
		UUISelectable* Mid = nullptr;
		UUISelectable* Bottom = nullptr;
		UUISelectable* Outside = nullptr;

		explicit FColumnFixture(UWorld* World)
		{
			Root = MakeWidget(World, nullptr, TEXT("Root"), 0.0f, 400.0f, 800.0f);
			Area = MakeWidget(World, Root, TEXT("Area"), 0.0f, 200.0f, 300.0f);
			Area->SetRestrictNavigationArea(true);
			Top = MakeSelectable(MakeWidget(World, Area, TEXT("Top"), 100.0f, 80.0f, 80.0f));
			Mid = MakeSelectable(MakeWidget(World, Area, TEXT("Mid"), 0.0f, 80.0f, 80.0f));
			Bottom = MakeSelectable(MakeWidget(World, Area, TEXT("Bottom"), -100.0f, 80.0f, 80.0f));
			Outside = MakeSelectable(MakeWidget(World, Root, TEXT("Outside"), -300.0f, 80.0f, 80.0f));
		}

		~FColumnFixture() { if (Root) { Root->DestroyWidget(); } }
	};

	static const FVector Down = FVector(0, 0, -1);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNavigationBoundaryStopTest,
	"DreamGUI.Navigation.Boundary.StopIsStillTheDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNavigationBoundaryStopTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationBoundaryTestLocal;
	FScopedGameWorld TestWorld;
	FColumnFixture Fixture(TestWorld.World);

	TestEqual(TEXT("An authored area defaults to Stop"),
		Fixture.Area->GetNavigationBoundaryRule(), EDreamUINavigationBoundaryRule::Stop);
	// A move that has somewhere to go never consults the rule at all.
	TestEqual(TEXT("Down from the middle row reaches the bottom row"),
		Fixture.Mid->FindSelectable(Down, nullptr), Fixture.Bottom);
	// At the edge, Stop means the move is refused -- reported by handing back the selectable it
	// started from, which is what "no navigation happened" has always looked like here.
	TestEqual(TEXT("Down from the last row stays put"),
		Fixture.Bottom->FindSelectable(Down, nullptr), Fixture.Bottom);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNavigationBoundaryWrapTest,
	"DreamGUI.Navigation.Boundary.WrapLandsWhereWalkingBackWould",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNavigationBoundaryWrapTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationBoundaryTestLocal;
	FScopedGameWorld TestWorld;
	FColumnFixture Fixture(TestWorld.World);
	Fixture.Area->SetNavigationBoundaryRule(EDreamUINavigationBoundaryRule::Wrap);

	// Off the bottom of the column comes back on at the top -- and specifically at the row that
	// holding Up from the bottom would have arrived at, since that is the walk wrapping replays.
	TestEqual(TEXT("Down from the last row wraps to the first"),
		Fixture.Bottom->FindSelectable(Down, nullptr), Fixture.Top);
	// Wrapping is an edge rule, not a general one: an ordinary move must be unaffected by it.
	TestEqual(TEXT("Down from the middle row still just moves down"),
		Fixture.Mid->FindSelectable(Down, nullptr), Fixture.Bottom);
	// The area still holds. Wrap must not become a licence to reach the selectable outside it.
	TestNotEqual(TEXT("Wrapping never leaves the area"),
		Fixture.Bottom->FindSelectable(Down, nullptr), Fixture.Outside);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNavigationBoundaryEscapeTest,
	"DreamGUI.Navigation.Boundary.EscapeHandsTheMoveOutwards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNavigationBoundaryEscapeTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationBoundaryTestLocal;
	FScopedGameWorld TestWorld;
	FColumnFixture Fixture(TestWorld.World);
	Fixture.Area->SetNavigationBoundaryRule(EDreamUINavigationBoundaryRule::Escape);

	// Nothing below inside the area, so the move is re-run without it and finds what is below outside.
	TestEqual(TEXT("Down from the last row leaves the area"),
		Fixture.Bottom->FindSelectable(Down, nullptr), Fixture.Outside);
	// Escaping is likewise an edge rule: while the area still has rows below, they win.
	TestEqual(TEXT("Down from the middle row stays inside"),
		Fixture.Mid->FindSelectable(Down, nullptr), Fixture.Bottom);
	return true;
}

#endif
