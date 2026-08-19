// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIBehaviour.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "Interaction/UIButton.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LexUIBehaviourEditorBackend.h"
#include "PrefabEditor/LexUIPrefabBehaviourUtils.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PrefabSystem/WidgetSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"

// A widget hosted by a sub-prefab instance serializes nothing but the overrides the helper
// collected for it, so the behaviour panel's two authoring paths both hinge on that rule: the
// Event+ flow has to announce the delegate it wrote, and the bind/promote menus must not offer a
// target the writer refuses. Both failures are invisible at author time -- the panel reports
// success and the binding is gone the next time the prefab loads.
namespace LexBehaviourSubPrefabGateTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** A widget that can take children: a panel-less widget refuses attachment. */
	ULexWidget* MakeWidget(UWorld* World, const TCHAR* DisplayName)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(DisplayName);
		Widget->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
		return Widget;
	}

	/** Register InWidgets as one sub-prefab instance rooted at the first, as MakePrefabAsSubPrefab would. */
	void RegisterAsSubPrefab(ULexUIPrefabHelperObject* Helper, const TArray<ULexWidget*>& InWidgets)
	{
		FLexUISubPrefabData Data;
		for (ULexWidget* Widget : InWidgets)
		{
			Data.MapGuidToObject.Add(FGuid::NewGuid(), Widget);
		}
		Helper->SubPrefabMap.Add(InWidgets[0], MoveTemp(Data));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexBehaviourAddEventHandlerNotifiesTest,
	"LGUI.Editor.Behaviour.GeneratedEventBindingIsAnnouncedAsAPropertyChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexBehaviourAddEventHandlerNotifiesTest::RunTest(const FString& Parameters)
{
	using namespace LexBehaviourSubPrefabGateTestLocal;
	FScopedTestWorld TestWorld;

	// AddEventHandler compiles a blueprint, and a blueprint compile can collect. GC follows
	// UPROPERTY references, not outer chains, so a widget whose only tie to the world is being
	// outered to it is unreachable -- these two were collected mid-call and the crash landed inside
	// AddEventHandler, reading Components off a null root. Nothing in the editor reaches this
	// because the prefab helper holds the tree; only a bare fixture can.
	TStrongObjectPtr<ULexWidget> Root(MakeWidget(TestWorld.World, TEXT("Root")));
	TStrongObjectPtr<ULexWidget> ButtonWidget(MakeWidget(TestWorld.World, TEXT("PlayButton")));
	TestTrue(TEXT("The button widget attached to the root"), ButtonWidget->TrySetParent(Root.Get(), false));
	TestNotNull(TEXT("The button widget carries a UIButton"), ButtonWidget->AddComponent<UUIButton>());

	// The companion the Event+ flow writes into: any blueprint ULexUIBehaviour on the root will do,
	// because AddEventHandler finds it by ClassGeneratedBy rather than by name.
	const FName BlueprintName = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_LexBehaviourGateTest"));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(ULexUIBehaviour::StaticClass(), GetTransientPackage(), BlueprintName,
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	TestNotNull(TEXT("The companion blueprint was created"), Blueprint);
	if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr)
	{
		return false;
	}
	UClass* CompanionClass = Blueprint->GeneratedClass;
	TestNotNull(TEXT("The companion instance attached to the root"), Root->AddComponent(CompanionClass));

	TArray<LexUIPrefabBehaviourUtils::FDiscoveredEvent> Events;
	LexUIPrefabBehaviourUtils::DiscoverEvents(ButtonWidget.Get(), Events);
	const LexUIPrefabBehaviourUtils::FDiscoveredEvent* OnClick = Events.FindByPredicate(
		[](const LexUIPrefabBehaviourUtils::FDiscoveredEvent& Candidate) { return Candidate.DisplayName == TEXT("OnClick"); });
	TestNotNull(TEXT("UIButton exposes OnClick as a bindable event"), OnClick);
	if (OnClick == nullptr)
	{
		return false;
	}

	// The helper records a sub-prefab override only from a property-changed notification -- the
	// one the details panel raises when a designer wires this event by hand. Watching for it is
	// watching for whether the generated binding can survive a save at all; the helper's own
	// collection needs a loaded prefab world, which no headless fixture has.
	int32 NotificationCount = 0;
	const UObject* WatchedComponent = OnClick->Component;
	const FProperty* WatchedProperty = OnClick->EventProperty;
	FDelegateHandle Handle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
		[&NotificationCount, WatchedComponent, WatchedProperty](UObject* ChangedObject, FPropertyChangedEvent& ChangedEvent)
		{
			if (ChangedObject == WatchedComponent && ChangedEvent.MemberProperty == WatchedProperty)
			{
				NotificationCount++;
			}
		});

	FText Message;
	const FName HandlerName = LexUIPrefabBehaviourUtils::AddEventHandler(Blueprint, Root.Get(), *OnClick,
		ELexUIBehaviourHandlerType::Function, Message);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(Handle);

	// Positive control: a refused AddEventHandler raises no notification either, so the count
	// below would be satisfied by a fixture that never got as far as writing a binding.
	TestFalse(FString::Printf(TEXT("A handler was generated (%s)"), *Message.ToString()), HandlerName.IsNone());
	TestEqual(TEXT("The delegate write was announced as a property change on the event's own component"), NotificationCount, 1);

	TArray<LexUIPrefabBehaviourUtils::FDiscoveredEvent> EventsAfter;
	LexUIPrefabBehaviourUtils::DiscoverEvents(ButtonWidget.Get(), EventsAfter);
	const LexUIPrefabBehaviourUtils::FDiscoveredEvent* OnClickAfter = EventsAfter.FindByPredicate(
		[](const LexUIPrefabBehaviourUtils::FDiscoveredEvent& Candidate) { return Candidate.DisplayName == TEXT("OnClick"); });
	TestTrue(TEXT("...and the event really is bound now"), OnClickAfter != nullptr && OnClickAfter->bIsBound);
	return true;
}

// The sub-prefab set needs a real helper object, which only exists once the prefab has been
// serialized -- LexUIPrefab lazily builds it, and only for a stamped prefab version.
// COVERAGE BOUNDARY: this pins CollectSubPrefabWidgets, which is the pre-existing inline walk
// lifted out of AutoBindAndValidate unchanged -- so it would pass against the tree before the fix
// too. What it does buy is that the bind menu and the auto-bind pass now compute their exclusion
// set from one function instead of two, so they cannot drift apart. The menu's own use of that set
// is not covered; it is built inside a Slate lambda.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexBehaviourSubPrefabWidgetSetTest,
	"LGUI.Editor.Behaviour.SubPrefabWidgetSetCoversDescendantsNotJustRoots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexBehaviourSubPrefabWidgetSetTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabSystem;
	using namespace LexBehaviourSubPrefabGateTestLocal;
	FScopedTestWorld TestWorld;

	ULexUIPrefab* Prefab = NewObject<ULexUIPrefab>();
	ULexWidget* Root = MakeWidget(TestWorld.World, TEXT("Root"));
	ULexWidget* OwnWidget = MakeWidget(TestWorld.World, TEXT("PlayButton"));
	ULexWidget* NestedRoot = MakeWidget(TestWorld.World, TEXT("HealthBar"));
	ULexWidget* NestedChild = MakeWidget(TestWorld.World, TEXT("Fill"));
	OwnWidget->TrySetParent(Root, false);
	NestedRoot->TrySetParent(Root, false);
	NestedChild->TrySetParent(NestedRoot, false);

	TMap<UObject*, FGuid> ObjectToGuid;
	ObjectToGuid.Add(Root, FGuid::NewGuid());
	TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EmptySubPrefabs;
	TestTrue(TEXT("Fixture prefab serialized"),
		WidgetSerializer::SavePrefab(Root, Prefab, ObjectToGuid, EmptySubPrefabs, true));

	ULexUIPrefabHelperObject* Helper = Prefab->GetPrefabHelperObject();
	TestNotNull(TEXT("Serialized prefab has a helper object"), Helper);
	if (Helper == nullptr)
	{
		return false;
	}

	// Control: an ordinary prefab must exclude nothing, or the bind menu would come up empty for
	// every property in every prefab that has no sub-prefab at all.
	TSet<const ULexWidget*> SubPrefabWidgets;
	LexUIPrefabBehaviourUtils::CollectSubPrefabWidgets(Prefab, SubPrefabWidgets);
	TestEqual(TEXT("A prefab with no sub-prefab excludes nothing"), SubPrefabWidgets.Num(), 0);

	RegisterAsSubPrefab(Helper, { NestedRoot, NestedChild });
	LexUIPrefabBehaviourUtils::CollectSubPrefabWidgets(Prefab, SubPrefabWidgets);

	// The whole instance is unreferenceable, not just the widget the SubPrefabMap is keyed on --
	// gating on "is a sub-prefab root" would still offer every widget inside one.
	TestTrue(TEXT("The sub-prefab root is excluded"), SubPrefabWidgets.Contains(NestedRoot));
	TestTrue(TEXT("A widget inside the sub-prefab is excluded too"), SubPrefabWidgets.Contains(NestedChild));
	TestFalse(TEXT("A widget this prefab owns stays bindable"), SubPrefabWidgets.Contains(OwnWidget));
	TestFalse(TEXT("The prefab's own root stays bindable"), SubPrefabWidgets.Contains(Root));

	Helper->ClearLoadedPrefab();
	Prefab->ClearPrefabInstanceScene();
	return true;
}

#endif
