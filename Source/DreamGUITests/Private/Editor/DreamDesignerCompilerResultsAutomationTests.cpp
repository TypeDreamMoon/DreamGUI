// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"

#include "BlueprintEditorTabs.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"

/*
 * What a rebuild the designer did not start leaves in its Compiler Results panel.
 *
 * The source watcher recompiles a class when its .dui is saved, and used to leave the panel as it was: the toolbar's
 * compile button went red and nothing said why. The messages of that compile now go where the Compile button's would.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerExternalCompileFailureOpensTheResultsTest,
	"DreamGUI.Designer.AFailedRebuildFromOutsideOpensTheCompilerResults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerExternalCompileFailureOpensTheResultsTest::RunTest(const FString&)
{
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/ExternalCompileResults_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	if (!TestNotNull(TEXT("The package was made"), Package))
	{
		return false;
	}
	Package->AddToRoot();
	UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UDreamUserWidget::StaticClass(), Package, FName(TEXT("ExternalCompileResults")), BPTYPE_Normal,
		UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
	if (!TestNotNull(TEXT("The Blueprint was made"), Blueprint))
	{
		Package->RemoveFromRoot();
		return false;
	}
	Blueprint->GetOrCreateWidgetTree();
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

	UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	Editors->OpenEditorForAsset(Blueprint);
	FDreamWidgetBlueprintEditor* Designer = static_cast<FDreamWidgetBlueprintEditor*>(
		Editors->FindEditorForAsset(Blueprint, /*bFocusIfOpen*/false));
	if (TestNotNull(TEXT("The designer opened"), Designer))
	{
		const FTabId ResultsTab(FBlueprintEditorTabs::CompilerResultsID);
		const TSharedPtr<FTabManager> Tabs = Designer->GetTabManager();
		if (TestTrue(TEXT("The designer has a tab manager"), Tabs.IsValid()))
		{
			// Closed first: a layout an earlier designer saved may have left it open, and the point is whether a failure
			// brings it up.
			if (const TSharedPtr<SDockTab> Open = Tabs->FindExistingLiveTab(ResultsTab))
			{
				Open->RequestCloseTab();
				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().Tick();
				}
			}
			TestFalse(TEXT("The panel is not showing before anything failed"), Tabs->FindExistingLiveTab(ResultsTab).IsValid());

			FCompilerResultsLog Clean;
			Designer->ShowExternalCompileResults(Clean);
			TestFalse(TEXT("A clean rebuild does not bring the panel up"), Tabs->FindExistingLiveTab(ResultsTab).IsValid());

			FCompilerResultsLog Failed;
			// Silent, so the made-up error does not reach the output log as one and fail the run.
			Failed.SetSilentMode(true);
			Failed.Error(TEXT("A rebuild from outside failed."));
			Designer->ShowExternalCompileResults(Failed);
			TestTrue(TEXT("A failed rebuild does"), Tabs->FindExistingLiveTab(ResultsTab).IsValid());
		}
	}

	Editors->CloseAllEditorsForAsset(Blueprint);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().Tick();
	}
	Package->RemoveFromRoot();
	return true;
}

#endif
