// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Tests/DreamWidgetBehaviourTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "DreamUIEditorTools.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

// Deleting a widget a behaviour variable binds to used to be silent, and the loss is invisible
// until the blueprint is next compiled -- by which time nothing names the widget that went away.
// The dialog itself is modal and unreachable headlessly, so what is pinned here is the decision it
// asks: which bindings the delete would break.
namespace DreamEditorToolsTestLocal
{
	/** Root, companion, and children, all kept rooted -- a widget with no referencer is GC'd mid-test. */
	struct FDeleteWarningFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UDreamWidget> Root;
		TStrongObjectPtr<UDreamUIAutoBindTestBehaviour> Companion;
		TArray<TStrongObjectPtr<UDreamWidget>> KeepAlive;

		FDeleteWarningFixture()
		{
			World = UWorld::CreateWorld(EWorldType::None, false);
			Root.Reset(MakeWidget(TEXT("Root")));
			Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
			Companion.Reset(Root->AddComponent<UDreamUIAutoBindTestBehaviour>());
		}
		~FDeleteWarningFixture()
		{
			KeepAlive.Empty();
			Companion.Reset();
			Root.Reset();
			if (World != nullptr)
			{
				World->DestroyWorld(false);
			}
		}

		UDreamWidget* MakeWidget(const TCHAR* DisplayName)
		{
			UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Widget->SetDisplayName(DisplayName);
			KeepAlive.Add(TStrongObjectPtr<UDreamWidget>(Widget));
			return Widget;
		}
		UDreamWidget* AddChild(UDreamWidget* Parent, const TCHAR* DisplayName)
		{
			UDreamWidget* Child = MakeWidget(DisplayName);
			Child->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
			Child->TrySetParent(Parent, false);
			return Child;
		}
	};

	bool Reports(const TArray<FText>& Bindings, const TCHAR* Needle)
	{
		return Bindings.ContainsByPredicate([Needle](const FText& Binding) { return Binding.ToString().Contains(Needle); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDeleteReportsBoundVariableTest,
	"DreamGUI.Editor.Delete.BoundVariableIsNamedBeforeDeleting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDeleteReportsBoundVariableTest::RunTest(const FString& Parameters)
{
	using namespace DreamEditorToolsTestLocal;
	FDeleteWarningFixture Fixture;
	UDreamWidget* PlayButton = Fixture.AddChild(Fixture.Root.Get(), TEXT("PlayButton"));
	UDreamWidget* Bystander = Fixture.AddChild(Fixture.Root.Get(), TEXT("Bystander"));
	Fixture.Companion->PlayButton = PlayButton;

	const TArray<FText> Bindings = FDreamUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { PlayButton });
	TestEqual(TEXT("one binding points at the widget being deleted"), Bindings.Num(), 1);
	TestTrue(TEXT("and it names both the variable and the widget"), Reports(Bindings, TEXT("PlayButton -> PlayButton")));

	const TArray<FText> Unaffected = FDreamUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { Bystander });
	TestEqual(TEXT("deleting a widget nothing binds to breaks nothing"), Unaffected.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDeleteReportsDescendantBindingTest,
	"DreamGUI.Editor.Delete.BindingToADescendantCountsToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDeleteReportsDescendantBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamEditorToolsTestLocal;
	FDeleteWarningFixture Fixture;
	UDreamWidget* Panel = Fixture.AddChild(Fixture.Root.Get(), TEXT("Panel"));
	UDreamWidget* PlayButton = Fixture.AddChild(Panel, TEXT("PlayButton"));
	Fixture.Companion->PlayButton = PlayButton;
	if (!TestEqual(TEXT("the grandchild attached"), Panel->GetChildren().Num(), 1))return false;

	// Deleting the panel deletes the button under it, so the binding is just as broken as if the
	// button had been selected -- which is what a designer deleting a container actually does.
	const TArray<FText> Bindings = FDreamUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { Panel });
	TestTrue(TEXT("a binding to a descendant of the deleted widget is reported"), Reports(Bindings, TEXT("PlayButton -> PlayButton")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDeleteReportsBehaviourBindingTest,
	"DreamGUI.Editor.Delete.BindingToABehaviourOnTheWidgetCountsToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDeleteReportsBehaviourBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamEditorToolsTestLocal;
	FDeleteWarningFixture Fixture;
	UDreamWidget* Scoreboard = Fixture.AddChild(Fixture.Root.Get(), TEXT("Scoreboard"));
	Fixture.Companion->Scoreboard = Scoreboard->AddComponent<UDreamUIAutoBindTargetBehaviour>();
	if (!TestNotNull(TEXT("the target behaviour attached"), Fixture.Companion->Scoreboard.Get()))return false;

	// A variable may hold the widget, its visual, or one of its behaviours; all three die with the
	// widget, so matching only the widget itself would miss two thirds of the bindings.
	const TArray<FText> Bindings = FDreamUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { Scoreboard });
	TestTrue(TEXT("a binding to a behaviour is reported against its widget"), Reports(Bindings, TEXT("Scoreboard -> Scoreboard")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDeleteIgnoresUnsavedBindingTest,
	"DreamGUI.Editor.Delete.TransientReferencesAreNotWorthWarningAbout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDeleteIgnoresUnsavedBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamEditorToolsTestLocal;
	FDeleteWarningFixture Fixture;
	UDreamWidget* Cache = Fixture.AddChild(Fixture.Root.Get(), TEXT("RuntimeCache"));
	Fixture.Companion->RuntimeCache = Cache;

	// A transient reference is a runtime cache the prefab writer never persists, so nothing on the
	// blueprint is left dangling by the delete. Warning about one trains the designer to click
	// through the dialog, which costs more than the warning is worth.
	const TArray<FText> Bindings = FDreamUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { Cache });
	TestEqual(TEXT("a transient reference raises no warning"), Bindings.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDeleteWithNoBindingsAsksNothingTest,
	"DreamGUI.Editor.Delete.NoBindingsAsksNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDeleteWithNoBindingsAsksNothingTest::RunTest(const FString& Parameters)
{
	using namespace DreamEditorToolsTestLocal;
	FDeleteWarningFixture Fixture;
	UDreamWidget* Child = Fixture.AddChild(Fixture.Root.Get(), TEXT("PlayButton"));
	Fixture.Companion->PlayButton = Child;

	TestEqual(TEXT("no companion means nothing can be bound"),
		FDreamUIEditorTools::CollectBehaviourBindingsToWidgets(nullptr, { Child }).Num(), 0);
	// This is the empty-binding early return and nothing more: these widgets belong to no prefab
	// helper, so the companion lookup answers null whatever it does -- FindCompanionForWidgets is
	// pinned in DreamToolsAuditAutomationTests instead. Reaching FMessageDialog here would hang the
	// run on a modal window.
	TestTrue(TEXT("a delete with nothing bound goes ahead without a prompt"),
		FDreamUIEditorTools::ShouldContinueDeleteOperation({ Child }));
	return true;
}

#endif
