// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

/*
 * A widget with no world, which is an ordinary widget.
 *
 * The plugin grew up as "a UI component on an actor in a level", so `GetWorld()->IsGameWorld()`
 * got written as a bare dereference in a couple of dozen places. Two things since then made a
 * worldless widget routine rather than exotic: this test suite, which builds trees under the
 * transient package, and the class model, which gives every DreamUI Blueprint an AUTHORING tree
 * outered to the Blueprint. Every widget in the designer that is not the preview copy lives there.
 *
 * The sweeps below are the point of this file. Naming the components that were fixed would test
 * the fix; walking every concrete visual and every concrete behaviour tests the RULE, and it is
 * the next one somebody writes that this is here to catch. A crash is the failure mode -- these
 * are dereferences of null, not wrong answers -- so a sweep that merely completes has found what
 * it came for, and the assertions after it are about the contract rather than the crash.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldContextContractTest,
	"DreamGUI.WorldContext.NoWorldIsTheEditModeAnswerRatherThanAThirdCase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldContextContractTest::RunTest(const FString& Parameters)
{
	// The degenerate input, because every caller below reaches the helper through something that
	// can be null, and answering a question about nothing by crashing is what this replaced.
	TestFalse(TEXT("nothing is not in a game world"), DreamUI::IsGameWorld(nullptr));
	TestEqual(TEXT("nothing has no world type"), DreamUI::GetWorldType(nullptr), EWorldType::None);
	TestNull(TEXT("nothing has no world"), DreamUI::GetWorldSafe(nullptr));

	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Lonely"));
	if (!TestNotNull(TEXT("the tree built a widget"), Widget))
	{
		return false;
	}

	// The state the whole file is about: this is what a headless tree and a Blueprint's authoring
	// tree both look like from inside a component.
	TestNull(TEXT("a transient-package widget has no world"), Widget->GetWorld());
	TestFalse(TEXT("so it is not in a game world"), DreamUI::IsGameWorld(Widget));
	TestEqual(TEXT("and its world type is None"), DreamUI::GetWorldType(Widget), EWorldType::None);

	// None is load-bearing rather than incidental: the sites that ask something narrower than
	// IsGameWorld compare against a specific type, and they keep their meaning only because the
	// worldless answer matches none of them.
	TestNotEqual(TEXT("None is not the editor world"), DreamUI::GetWorldType(Widget), EWorldType::Editor);
	TestNotEqual(TEXT("nor the editor preview world"), DreamUI::GetWorldType(Widget), EWorldType::EditorPreview);

	Widget->DestroyWidget();
	return true;
}

namespace DreamWorldContextTestsLocal
{
	/** Concrete, native, instantiable -- the same filter the control-part sweep uses. */
	bool IsSweepable(const UClass* InClass, const UClass* InBase)
	{
		if (InClass == InBase || !InClass->IsChildOf(InBase))
		{
			return false;
		}
		if (InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return false;
		}
		// Native only. A Blueprint subclass is somebody's asset, and whether it survives having no
		// world is a claim about their graph rather than about this codebase.
		return !InClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldContextVisualSweepTest,
	"DreamGUI.WorldContext.EveryVisualRegistersOnAWidgetThatHasNoWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldContextVisualSweepTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldContextTestsLocal;

	// UDreamWidget::CreateNewVisual calls Call_OnRegister unconditionally, so this reaches every
	// visual's register path -- which is where UDreamUMGWidget asked its world whether it was
	// playing, twice, without checking there was one.
	int32 Swept = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!IsSweepable(Class, UDreamVisual::StaticClass()))
		{
			continue;
		}

		UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
		UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), Class->GetFName());
		if (Widget == nullptr)
		{
			AddError(FString::Printf(TEXT("could not build a host widget for %s"), *Class->GetName()));
			continue;
		}

		Widget->CreateNewVisual(Class);
		// The path UDreamRingMenu walked into: a control disabling one of its own parts broadcasts
		// through every component on it, and the editor-only branch that receives it dereferenced
		// the world. Ordinary enough that the designer does it whenever a control greys a part out.
		Widget->SetInteractable(EDreamWidgetInteractableType::Disabled);
		Widget->SetInteractable(EDreamWidgetInteractableType::Enabled);

		++Swept;
		Widget->DestroyWidget();
	}

	// A sweep that silently matched nothing is a green test that checks nothing, and the filter
	// above has four ways to become that after a refactor.
	TestTrue(TEXT("the sweep found visuals to sweep"), Swept >= 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldContextBehaviourSweepTest,
	"DreamGUI.WorldContext.EveryBehaviourAttachesToAWidgetThatHasNoWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldContextBehaviourSweepTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldContextTestsLocal;

	int32 Swept = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!IsSweepable(Class, UDreamUIBehaviour::StaticClass()))
		{
			continue;
		}
		// Deliberately NOT skipping visuals here, because there are none to skip: UDreamVisual
		// descends from UDreamWidgetSubObjectBehaviour, a hierarchy that is a sibling of this one
		// rather than a parent of it. The sweep above is what covers them, through CreateNewVisual.

		UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
		UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), Class->GetFName());
		if (Widget == nullptr)
		{
			AddError(FString::Printf(TEXT("could not build a host widget for %s"), *Class->GetName()));
			continue;
		}

		Widget->AddComponent(Class);
		Widget->SetInteractable(EDreamWidgetInteractableType::Disabled);
		Widget->SetInteractable(EDreamWidgetInteractableType::Enabled);
		// Enable and disable, which is the other pair of world questions on the behaviour base.
		Widget->SetWidgetActive(false);
		Widget->SetWidgetActive(true);

		++Swept;
		Widget->DestroyWidget();
	}

	TestTrue(TEXT("the sweep found behaviours to sweep"), Swept >= 8);
	return true;
}

#endif
