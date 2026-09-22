// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Event/DreamUIEventDelegate.h"
#include "UObject/StrongObjectPtr.h"
#include "DreamEventBindingTestTypes.h"
#include "DreamScopedWorld.h"

/*
 * What an event binding is written in terms of, and what it can carry.
 *
 * Three claims, all of them about the same struct:
 *
 *   - a behaviour is addressed by its POSITION in the widget's component array, never by the
 *     instance FName UE re-numbers on every preview rebuild;
 *   - a parameter may be any USTRUCT, including one that owns heap memory, which is why it travels
 *     as exported text rather than as bytes in the parameter buffer;
 *   - `EventName -> Handler` reaches an FDreamUIEventDelegate event, not only a multicast delegate,
 *     which is what makes the route the ONE way to handle any event in the plugin.
 *
 * All three are invisible in the ordinary case: one behaviour of its class, an int parameter, an
 * event that nothing routes. Each test builds the shape that can actually fail.
 */

namespace DreamEventBindingTestLocal
{
	using DreamTests::FScopedGameWorld;

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
	FDreamEventBindingAddressesBehavioursByPositionTest,
	"DreamGUI.Event.Binding.ABehaviourIsAddressedByPositionNotByItsInstanceName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventBindingAddressesBehavioursByPositionTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBindingTestLocal;
	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("a world to host the widget"), Scope.World != nullptr))return true;

	UDreamWidget* Widget = MakeWidget(Scope.World, TEXT("Host"));
	// TWO of the same class. With one, every key agrees and the test would pass on nothing.
	UDreamEventBindingTestBehaviour* First = Widget->AddComponent<UDreamEventBindingTestBehaviour>();
	UDreamEventBindingTestBehaviour* Second = Widget->AddComponent<UDreamEventBindingTestBehaviour>();
	if (!TestTrue(TEXT("two behaviours of one class on one widget"),
		First != nullptr && Second != nullptr && First != Second))return true;

	const TArray<UDreamUIBehaviour*>& Components = Widget->GetAllComponents();
	const int32 SecondIndex = Components.IndexOfByKey(Second);
	if (!TestTrue(TEXT("the second behaviour has a position"), SecondIndex != INDEX_NONE))return true;

	// By position, with no name at all: the key a preview rebuild cannot invalidate.
	FString ResolveError;
	int32 ResolvedIndex = INDEX_NONE;
	UObject* ByPosition = UDreamUIEventDelegateParameterHelper::ResolveBindingTarget(
		Widget, UDreamEventBindingTestBehaviour::StaticClass(), SecondIndex, NAME_None, &ResolvedIndex, &ResolveError);
	TestEqual(TEXT("a position names exactly one behaviour"), ByPosition, (UObject*)Second);
	TestEqual(TEXT("...and reports the position it resolved"), ResolvedIndex, SecondIndex);

	// A stale name beside a good position must not get a vote: the name is what goes wrong, and the
	// whole point of the position is that it wins.
	UObject* StaleNameIgnored = UDreamUIEventDelegateParameterHelper::ResolveBindingTarget(
		Widget, UDreamEventBindingTestBehaviour::StaticClass(), SecondIndex, FName(TEXT("NoSuchComponent_17")), nullptr, &ResolveError);
	TestEqual(TEXT("a stale name beside a good position is ignored"), StaleNameIgnored, (UObject*)Second);

	// The legacy shape -- an asset saved before the position existed -- still resolves, and reports
	// the position so the caller can write it back and stop depending on the name.
	ResolvedIndex = INDEX_NONE;
	UObject* ByLegacyName = UDreamUIEventDelegateParameterHelper::ResolveBindingTarget(
		Widget, UDreamEventBindingTestBehaviour::StaticClass(), INDEX_NONE, Second->GetFName(), &ResolvedIndex, &ResolveError);
	TestEqual(TEXT("a legacy name still resolves"), ByLegacyName, (UObject*)Second);
	TestEqual(TEXT("...and hands back the position to record"), ResolvedIndex, SecondIndex);

	// Neither key, two candidates: ambiguous, and saying so beats picking one of them.
	UObject* Ambiguous = UDreamUIEventDelegateParameterHelper::ResolveBindingTarget(
		Widget, UDreamEventBindingTestBehaviour::StaticClass(), INDEX_NONE, NAME_None, nullptr, &ResolveError);
	TestNull(TEXT("two candidates and no key resolves to nothing"), Ambiguous);
	TestTrue(TEXT("...and says why"), ResolveError.Contains(TEXT("ambiguous")));

	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventBindingCarriesAnyStructTest,
	"DreamGUI.Event.Binding.AStructParameterArrivesWithItsHeapOwningMembersIntact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventBindingCarriesAnyStructTest::RunTest(const FString& Parameters)
{
	using namespace DreamEventBindingTestLocal;
	FScopedGameWorld Scope;
	if (!TestTrue(TEXT("a world to host the widget"), Scope.World != nullptr))return true;

	UDreamWidget* Widget = MakeWidget(Scope.World, TEXT("Host"));
	UDreamEventBindingTestBehaviour* Receiver = Widget->AddComponent<UDreamEventBindingTestBehaviour>();
	if (!TestNotNull(TEXT("a behaviour to be called"), Receiver))return true;

	// A struct outside the fixed list of named ones. This used to answer "not supported" and take the
	// function off the selector entirely, so no handler taking a project's own struct could be bound.
	UFunction* Handler = Receiver->FindFunction(TEXT("TakePayload"));
	if (!TestNotNull(TEXT("the fixture declares a struct-taking function"), Handler))return true;
	EDreamUIEventDelegateParameterType ParamType = EDreamUIEventDelegateParameterType::None;
	TestTrue(TEXT("a function taking any USTRUCT is bindable"),
		UDreamUIEventDelegateParameterHelper::IsSupportedFunction(Handler, ParamType));
	TestEqual(TEXT("...as the generic struct type"), (int32)ParamType, (int32)EDreamUIEventDelegateParameterType::Struct);
	TestEqual(TEXT("and the selector names the struct rather than saying 'Struct'"),
		UDreamUIEventDelegateParameterHelper::ParameterTypeToName(ParamType, Handler),
		FDreamEventBindingTestPayload::StaticStruct()->GetName());
	// Not in the raw parameter buffer: a struct that owns heap memory cannot be a byte copy.
	TestEqual(TEXT("a struct parameter claims no raw buffer"),
		UDreamUIEventDelegateParameterHelper::GetParameterBufferSize(ParamType), 0);

	// An ordinary Empty event -- a click -- whose binding calls a function with an AUTHORED struct
	// argument. That is the shape a struct parameter exists for: the value comes from the panel, not
	// from what the event fired with.
	FDreamUIEventDelegate Event(EDreamUIEventDelegateParameterType::Empty);
	Event.AddFunctionBinding(Widget, Receiver, TEXT("TakePayload"), EDreamUIEventDelegateParameterType::Struct, /*bUseNativeParameter*/false);

	// The value the way the panel stores it: the struct's own exported text plus the struct it names.
	FDreamEventBindingTestPayload Authored;
	Authored.Count = 42;
	Authored.Label = TEXT("hello");
	Authored.Offset = FVector2D(3.0, -4.0);
	FString Exported;
	FDreamEventBindingTestPayload::StaticStruct()->ExportText(Exported, &Authored, nullptr, nullptr, PPF_None, nullptr);
	if (!TestFalse(TEXT("the struct exported to something"), Exported.IsEmpty()))return true;
	Event.SetStructParameterValue(0, FDreamEventBindingTestPayload::StaticStruct(), Exported);

	Event.FireEvent();
	TestEqual(TEXT("the handler was called once"), Receiver->PayloadCallCount, 1);
	TestEqual(TEXT("the plain member arrived"), Receiver->LastPayload.Count, 42);
	// The member that is the reason for all of this: a byte copy would have handed the callee a
	// pointer into this test's own FString.
	TestEqual(TEXT("the heap-owning member arrived"), Receiver->LastPayload.Label, FString(TEXT("hello")));
	TestEqual(TEXT("the nested struct member arrived"), Receiver->LastPayload.Offset.X, 3.0, 0.001);
	TestEqual(TEXT("...on both axes"), Receiver->LastPayload.Offset.Y, -4.0, 0.001);

	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEventBindingRouteReachesHandlerTest,
	"DreamGUI.Event.Binding.ARouteReachesTheHandlerTheSameWayAnAuthoredBindingDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEventBindingRouteReachesHandlerTest::RunTest(const FString& Parameters)
{
	// What UDreamUserWidget::BindEventBindings does when a `->` route names an FDreamUIEventDelegate
	// instead of a multicast delegate. Before this existed the route was rejected by the compiler and
	// the whole Interaction/* family of controls had no canonical way to be handled at all -- which is
	// what kept the per-instance legacy event list alive as a second, competing event UI.
	// Strongly held: nothing in this test is a UPROPERTY reference, and GC follows those rather than
	// the outer chain.
	TStrongObjectPtr<UDreamEventBindingTestHandler> HandlerHolder(NewObject<UDreamEventBindingTestHandler>(GetTransientPackage()));
	UDreamEventBindingTestHandler* Handler = HandlerHolder.Get();

	{
		FDreamUIEventDelegate EmptyEvent(EDreamUIEventDelegateParameterType::Empty);
		TestFalse(TEXT("an unrouted event is unbound"), EmptyEvent.IsBound());
		EmptyEvent.AddRuntimeRoute(Handler, TEXT("HandleEmpty"));
		TestTrue(TEXT("a route binds it"), EmptyEvent.IsBound());

		EmptyEvent.FireEvent();
		TestEqual(TEXT("the routed handler ran"), Handler->EmptyCallCount, 1);

		// BindEventBindings runs again on every re-initialise and on every designer preview rebuild.
		// One route in the file has to mean one call, not one per initialise.
		EmptyEvent.AddRuntimeRoute(Handler, TEXT("HandleEmpty"));
		EmptyEvent.FireEvent();
		TestEqual(TEXT("routing twice does not call twice"), Handler->EmptyCallCount, 2);
	}

	{
		// An event that carries a value hands it to the handler, which is the half a route gets for
		// free from bUseNativeParameter and the half an Empty event must NOT ask for.
		FDreamUIEventDelegate IntEvent(EDreamUIEventDelegateParameterType::Int32);
		IntEvent.AddRuntimeRoute(Handler, TEXT("HandleInt"));
		IntEvent.FireEvent((int32)19);
		TestEqual(TEXT("the routed handler ran"), Handler->IntCallCount, 1);
		TestEqual(TEXT("...with the value the event fired"), Handler->LastInt, 19);
	}

	return true;
}

#endif
