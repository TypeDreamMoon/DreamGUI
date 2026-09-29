// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamGUIEditorSubsystem.h"
#include "DreamWidgetBlueprint.h"
#include "Engine/World.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lifecycle/DreamLifecycleFixtures.h"

#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * A CLASS RECOMPILED WHILE A PLAY SESSION RUNS IT.
 *
 * A play session's trees go and come back like the editor's: the panel placed in the level is built again
 * by its host, the page on the player's screen is shown again, and nothing registered in the play world is
 * an instance of the class the compile retired. What the trees held at run time is lost, which is what an
 * editor-driven compile costs in UMG too. Once without the compile's collection and once with it.
 */

namespace DreamLifecyclePieRecompileTestLocal
{
	/** What the test keeps of the play world between steps: weakly, as the rig requires. */
	struct FPlaced
	{
		TWeakObjectPtr<UDreamWorldWidgetComponent> Host;
	};

	const FName PageName(TEXT("PieRecompilePage"));

	int32 CountStaleRegistered(UWorld* InWorld)
	{
		int32 Count = 0;
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InWorld))
		{
			for (const UDreamWidget* Widget : Manager->GetRegisteredWidgets())
			{
				const UClass* Class = Widget->GetClass();
				Count += Class->HasAnyClassFlags(CLASS_NewerVersionExists) || Class->GetName().StartsWith(TEXT("REINST_")) ? 1 : 0;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecyclePieRecompileTest,
	"DreamGUI.Pie.RecompilingAClassWhileItPlaysBuildsItsTreesAgainFromTheNewClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecyclePieRecompileTest::RunTest(const FString& Parameters)
{
	using namespace DreamLifecyclePieRecompileTestLocal;

	const TSharedRef<DreamTests::Lifecycle::FScopedPanelClass> Panel = MakeShared<DreamTests::Lifecycle::FScopedPanelClass>(TEXT("PieRecompile"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel->GetClass()))
	{
		return false;
	}
	const TSharedRef<FPlaced> Placed = MakeShared<FPlaced>();

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Panel, Placed](FDreamDriverPieRig& InRig)
	{
		UWorld* World = InRig.GetWorld();
		// A panel placed while the level plays, and a page of the class on the player's screen.
		if (ADreamWorldWidgetActor* Actor = World->SpawnActor<ADreamWorldWidgetActor>())
		{
			Actor->GetWidgetComponent()->SetWidgetClass(Panel->GetClass());
			Placed->Host = Actor->GetWidgetComponent();
		}
		if (UDreamScreenUISubsystem* Screen = World->GetSubsystem<UDreamScreenUISubsystem>())
		{
			Screen->ShowWidgetOfClass(PageName, Panel->GetClass());
		}
	});
	Rig->Sequence()
		.Then([this, Panel, Placed](FDreamDriverContext& InContext)
		{
			UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
			UDreamScreenUISubsystem* Screen = InContext.World != nullptr ? InContext.World->GetSubsystem<UDreamScreenUISubsystem>() : nullptr;
			if (!TestNotNull(TEXT("the editor's DreamGUI subsystem"), EditorSubsystem)
				|| !TestNotNull(TEXT("the play world's screen"), Screen)
				|| !TestTrue(TEXT("the panel was placed in the play world"), Placed->Host.IsValid())
				|| !TestNotNull(TEXT("with its tree"), Placed->Host->GetLoadedWidget())
				|| !TestNotNull(TEXT("and the page is on the screen"), Screen->GetUI(PageName)))
			{
				return;
			}
			for (const bool bCollectInCompile : { false, true })
			{
				const FString Round = bCollectInCompile ? TEXT("with the compile's collection") : TEXT("without a collection");
				const TWeakObjectPtr<UDreamWidget> TreeBefore = Placed->Host->GetLoadedWidget();
				const TWeakObjectPtr<UDreamWidget> PageBefore = Screen->GetUI(PageName);

				FKismetEditorUtilities::CompileBlueprint(Panel->Blueprint,
					bCollectInCompile ? EBlueprintCompileOptions::None : EBlueprintCompileOptions::SkipGarbageCollection);

				TestFalse(FString::Printf(TEXT("%s: the compile is over"), *Round), EditorSubsystem->IsRecompiling());
				TestFalse(FString::Printf(TEXT("%s: the placed panel's tree went before the compile"), *Round),
					TreeBefore.IsValid() && TreeBefore->HasRegistered());
				TestFalse(FString::Printf(TEXT("%s: and so did the page"), *Round), PageBefore.IsValid() && PageBefore->HasRegistered());

				EditorSubsystem->RebuildReleasedTrees();
				if (!TestTrue(FString::Printf(TEXT("%s: the placed panel is still there"), *Round), Placed->Host.IsValid()))
				{
					return;
				}
				const UDreamWidget* Tree = Placed->Host->GetLoadedWidget();
				TestTrue(FString::Printf(TEXT("%s: its host built its tree again, from the new class"), *Round),
					Tree != nullptr && Tree->GetClass() == Panel->GetClass() && Tree->HasRegistered());
				const UDreamWidget* Page = Screen->GetUI(PageName);
				TestTrue(FString::Printf(TEXT("%s: the page is back, built from the new class"), *Round),
					Page != nullptr && Page->GetClass() == Panel->GetClass() && Page->HasRegistered());
				TestTrue(FString::Printf(TEXT("%s: and showing, as it was"), *Round), Screen->IsUIShowing(PageName));
				TestEqual(FString::Printf(TEXT("%s: nothing registered in the play world is of a retired class"), *Round),
					CountStaleRegistered(InContext.World), 0);
			}
			Screen->RemoveUI(PageName);
		})
		.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
