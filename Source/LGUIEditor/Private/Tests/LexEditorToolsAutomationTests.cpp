// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Tests/LexUIPrefabBehaviourTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "LexUIEditorTools.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

// Deleting a widget a behaviour variable binds to used to be silent, and the loss is invisible
// until the blueprint is next compiled -- by which time nothing names the widget that went away.
// The dialog itself is modal and unreachable headlessly, so what is pinned here is the decision it
// asks: which bindings the delete would break.
namespace LexEditorToolsTestLocal
{
	/** Root, companion, and children, all kept rooted -- a widget with no referencer is GC'd mid-test. */
	struct FDeleteWarningFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<ULexWidget> Root;
		TStrongObjectPtr<ULexUIAutoBindTestBehaviour> Companion;
		TArray<TStrongObjectPtr<ULexWidget>> KeepAlive;

		FDeleteWarningFixture()
		{
			World = UWorld::CreateWorld(EWorldType::None, false);
			Root.Reset(MakeWidget(TEXT("Root")));
			Root->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
			Companion.Reset(Root->AddComponent<ULexUIAutoBindTestBehaviour>());
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

		ULexWidget* MakeWidget(const TCHAR* DisplayName)
		{
			ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Widget->SetDisplayName(DisplayName);
			KeepAlive.Add(TStrongObjectPtr<ULexWidget>(Widget));
			return Widget;
		}
		ULexWidget* AddChild(ULexWidget* Parent, const TCHAR* DisplayName)
		{
			ULexWidget* Child = MakeWidget(DisplayName);
			Child->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
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
	FLexDeleteReportsBoundVariableTest,
	"LGUI.Editor.Delete.BoundVariableIsNamedBeforeDeleting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexDeleteReportsBoundVariableTest::RunTest(const FString& Parameters)
{
	using namespace LexEditorToolsTestLocal;
	FDeleteWarningFixture Fixture;
	ULexWidget* PlayButton = Fixture.AddChild(Fixture.Root.Get(), TEXT("PlayButton"));
	ULexWidget* Bystander = Fixture.AddChild(Fixture.Root.Get(), TEXT("Bystander"));
	Fixture.Companion->PlayButton = PlayButton;

	const TArray<FText> Bindings = FLexUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { PlayButton });
	TestEqual(TEXT("one binding points at the widget being deleted"), Bindings.Num(), 1);
	TestTrue(TEXT("and it names both the variable and the widget"), Reports(Bindings, TEXT("PlayButton -> PlayButton")));

	const TArray<FText> Unaffected = FLexUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { Bystander });
	TestEqual(TEXT("deleting a widget nothing binds to breaks nothing"), Unaffected.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexDeleteReportsDescendantBindingTest,
	"LGUI.Editor.Delete.BindingToADescendantCountsToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexDeleteReportsDescendantBindingTest::RunTest(const FString& Parameters)
{
	using namespace LexEditorToolsTestLocal;
	FDeleteWarningFixture Fixture;
	ULexWidget* Panel = Fixture.AddChild(Fixture.Root.Get(), TEXT("Panel"));
	ULexWidget* PlayButton = Fixture.AddChild(Panel, TEXT("PlayButton"));
	Fixture.Companion->PlayButton = PlayButton;
	if (!TestEqual(TEXT("the grandchild attached"), Panel->GetChildren().Num(), 1))return false;

	// Deleting the panel deletes the button under it, so the binding is just as broken as if the
	// button had been selected -- which is what a designer deleting a container actually does.
	const TArray<FText> Bindings = FLexUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { Panel });
	TestTrue(TEXT("a binding to a descendant of the deleted widget is reported"), Reports(Bindings, TEXT("PlayButton -> PlayButton")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexDeleteReportsBehaviourBindingTest,
	"LGUI.Editor.Delete.BindingToABehaviourOnTheWidgetCountsToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexDeleteReportsBehaviourBindingTest::RunTest(const FString& Parameters)
{
	using namespace LexEditorToolsTestLocal;
	FDeleteWarningFixture Fixture;
	ULexWidget* Scoreboard = Fixture.AddChild(Fixture.Root.Get(), TEXT("Scoreboard"));
	Fixture.Companion->Scoreboard = Scoreboard->AddComponent<ULexUIAutoBindTargetBehaviour>();
	if (!TestNotNull(TEXT("the target behaviour attached"), Fixture.Companion->Scoreboard.Get()))return false;

	// A variable may hold the widget, its visual, or one of its behaviours; all three die with the
	// widget, so matching only the widget itself would miss two thirds of the bindings.
	const TArray<FText> Bindings = FLexUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { Scoreboard });
	TestTrue(TEXT("a binding to a behaviour is reported against its widget"), Reports(Bindings, TEXT("Scoreboard -> Scoreboard")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexDeleteIgnoresUnsavedBindingTest,
	"LGUI.Editor.Delete.TransientReferencesAreNotWorthWarningAbout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexDeleteIgnoresUnsavedBindingTest::RunTest(const FString& Parameters)
{
	using namespace LexEditorToolsTestLocal;
	FDeleteWarningFixture Fixture;
	ULexWidget* Cache = Fixture.AddChild(Fixture.Root.Get(), TEXT("RuntimeCache"));
	Fixture.Companion->RuntimeCache = Cache;

	// A transient reference is a runtime cache the prefab writer never persists, so nothing on the
	// blueprint is left dangling by the delete. Warning about one trains the designer to click
	// through the dialog, which costs more than the warning is worth.
	const TArray<FText> Bindings = FLexUIEditorTools::CollectBehaviourBindingsToWidgets(Fixture.Companion.Get(), { Cache });
	TestEqual(TEXT("a transient reference raises no warning"), Bindings.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexDeleteWithNoBindingsAsksNothingTest,
	"LGUI.Editor.Delete.NoBindingsAsksNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexDeleteWithNoBindingsAsksNothingTest::RunTest(const FString& Parameters)
{
	using namespace LexEditorToolsTestLocal;
	FDeleteWarningFixture Fixture;
	ULexWidget* Child = Fixture.AddChild(Fixture.Root.Get(), TEXT("PlayButton"));
	Fixture.Companion->PlayButton = Child;

	TestEqual(TEXT("no companion means nothing can be bound"),
		FLexUIEditorTools::CollectBehaviourBindingsToWidgets(nullptr, { Child }).Num(), 0);
	// This is the empty-binding early return and nothing more: these widgets belong to no prefab
	// helper, so the companion lookup answers null whatever it does -- FindCompanionForWidgets is
	// pinned in LexToolsAuditAutomationTests instead. Reaching FMessageDialog here would hang the
	// run on a modal window.
	TestTrue(TEXT("a delete with nothing bound goes ahead without a prompt"),
		FLexUIEditorTools::ShouldContinueDeleteOperation({ Child }));
	return true;
}

#endif
