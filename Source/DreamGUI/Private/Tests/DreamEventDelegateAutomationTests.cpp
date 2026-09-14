// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Event/DreamUIEventDelegate.h"
#include "Tests/DreamPointerEventTestTypes.h"

/*
 * What an authored event binding actually hands the function it calls.
 *
 * FDreamUIEventDelegate is how every control in the plugin lets a designer wire a click to a
 * behaviour, and in a hundred-odd test files it had none of its own: the binding is resolved by
 * reflection and invoked through ProcessEvent, so nothing short of a real UFUNCTION reading its own
 * parameter can say whether the right bytes arrived.
 *
 * The float/double seam is the sharp edge. A delegate declares the type it fires with and a binding
 * declares the type its function takes, and where those differ by width the value is converted on the
 * way through -- into a temporary that the code then went on to use after leaving the block it was
 * declared in. The conversion itself is what these tests pin down; the lifetime bug behind it is not
 * something an assertion can see (reading a dead stack slot usually returns what was written there),
 * which is exactly why it survived.
 */

namespace DreamEventDelegateTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, const TCHAR* Name)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(100.0f);
		Widget->OnRegister();
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventDelegateWidthConversionTest,
	"DreamGUI.Event.Delegate.AValueNarrowedOrWidenedOnTheWayThroughArrivesIntact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventDelegateWidthConversionTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventDelegateTestLocal;

	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("A world to host the widget"), Scope.World != nullptr))
	{
		return false;
	}
	UDreamWidget* Widget = MakeWidget(Scope.World, TEXT("Host"));
	UDreamEventDelegateReceiver* Receiver = Widget->AddComponent<UDreamEventDelegateReceiver>();
	if (!TestNotNull(TEXT("A behaviour to be called"), Receiver))
	{
		return false;
	}

	// Converting logs an error every time, on purpose -- a mismatch is something the author should fix
	// rather than rely on -- and this test's whole subject is that path, so both messages are expected.
	AddExpectedError(TEXT("automatic convert it from double to float"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("automatic convert it from float to double"), EAutomationExpectedErrorFlags::Contains, 0);

	// An event that fires doubles, bound to a function that takes a float. This is the ordinary case
	// after the engine's float-to-double migration, not an exotic one: half the plugin's own events
	// carry doubles and plenty of authored handlers still take floats.
	{
		FDreamUIEventDelegate DoubleEvent(EDreamUIEventDelegateParameterType::Double);
		DoubleEvent.AddFunctionBinding(Widget, Receiver, TEXT("TakeFloat"), EDreamUIEventDelegateParameterType::Float, true);
		TestTrue(TEXT("The binding took"), DoubleEvent.IsBound());

		DoubleEvent.FireEvent(2.5);
		TestEqual(TEXT("The narrowed value arrives"), Receiver->FloatCallCount, 1);
		TestEqual(TEXT("...as the value that was fired"), Receiver->LastFloat, 2.5f);

		// Again, because the second call goes through the cached UFunction rather than resolving it
		// afresh -- a different path through the same conversion.
		DoubleEvent.FireEvent(-7.25);
		TestEqual(TEXT("A second fire reaches the same function"), Receiver->FloatCallCount, 2);
		TestEqual(TEXT("...with the new value, through the cached lookup"), Receiver->LastFloat, -7.25f);
	}

	// And the other direction, which has the same shape and the same fix.
	{
		FDreamUIEventDelegate FloatEvent(EDreamUIEventDelegateParameterType::Float);
		FloatEvent.AddFunctionBinding(Widget, Receiver, TEXT("TakeDouble"), EDreamUIEventDelegateParameterType::Double, true);

		FloatEvent.FireEvent(0.5f);
		TestEqual(TEXT("The widened value arrives"), Receiver->DoubleCallCount, 1);
		TestEqual(TEXT("...as the value that was fired"), Receiver->LastDouble, 0.5);
	}

	// A width that matches needs no conversion at all, and must not be disturbed by the branch that
	// handles the ones that do not.
	{
		FDreamUIEventDelegate ExactEvent(EDreamUIEventDelegateParameterType::Float);
		ExactEvent.AddFunctionBinding(Widget, Receiver, TEXT("TakeFloat"), EDreamUIEventDelegateParameterType::Float, true);
		Receiver->LastFloat = 0.0f;

		ExactEvent.FireEvent(11.75f);
		TestEqual(TEXT("A matching width goes straight through"), Receiver->LastFloat, 11.75f);
	}

	Widget->DestroyWidget();
	return true;
}

#endif
