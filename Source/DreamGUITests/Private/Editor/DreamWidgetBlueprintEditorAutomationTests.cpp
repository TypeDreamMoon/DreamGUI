// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetDesignerModes.h"
#include "Designer/DreamWidgetDesignerTabs.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"

#include "Editor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"

/*
 * Opening the designer.
 *
 * The claim is the one the whole phase is for: a UDreamWidgetBlueprint opens in an editor that has
 * BOTH a design surface and the Blueprint graph, and that editor is reachable the way a user reaches
 * it -- by asking the asset editor subsystem to open the asset, not by constructing the toolkit.
 *
 * That routing is the part most likely to be silently wrong: until the asset type actions existed,
 * a UDreamWidgetBlueprint fell through to FAssetTypeActions_Blueprint and opened a perfectly
 * functional window that simply was not this one. Nothing else here would have noticed.
 *
 * What this cannot see is pixels. An editor whose viewport opens black passes every line below.
 */

namespace DreamWidgetBlueprintEditorTestLocal
{
	struct FScopedAsset
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedAsset(const TCHAR* InName)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint != nullptr)
			{
				Blueprint->GetOrCreateWidgetTree();
				FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
			}
		}

		~FScopedAsset()
		{
			if (GEditor != nullptr && Blueprint != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint);
			}
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetBlueprintOpensInTheDesignerTest,
	"DreamGUI.Designer.AWidgetBlueprintOpensInTheDesigner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetBlueprintOpensInTheDesignerTest::RunTest(const FString&)
{
	using namespace DreamWidgetBlueprintEditorTestLocal;

	FScopedAsset Scoped(TEXT("DesignerOpens"));
	if (!TestNotNull(TEXT("The Blueprint was created"), Scoped.Blueprint))
	{
		return false;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!TestNotNull(TEXT("The asset editor subsystem is available"), AssetEditorSubsystem))
	{
		return false;
	}

	// The route a user takes. Constructing the toolkit directly would prove the toolkit works and
	// say nothing about whether anything sends this asset to it.
	TestTrue(TEXT("The asset opens"), AssetEditorSubsystem->OpenEditorForAsset(Scoped.Blueprint));

	IAssetEditorInstance* Instance = AssetEditorSubsystem->FindEditorForAsset(Scoped.Blueprint, /*bFocusIfOpen*/false);
	if (!TestNotNull(TEXT("An editor is open for it"), Instance))
	{
		return false;
	}

	// The type, not merely "an editor": falling through to the stock Blueprint editor is the failure
	// this exists to catch, and it looks like success from every other angle.
	FDreamWidgetBlueprintEditor* Designer = static_cast<FDreamWidgetBlueprintEditor*>(Instance);
	TestEqual(TEXT("It is the DreamUI designer"), Instance->GetEditorName(), FName(TEXT("DreamWidgetBlueprintEditor")));

	TestEqual(TEXT("It opens in Designer mode"), Designer->GetCurrentMode(),
		FDreamWidgetBlueprintApplicationModes::DesignerMode);
	TestEqual(TEXT("The asset it is editing is the one that was opened"),
		(UObject*)Designer->GetWidgetBlueprint(), (UObject*)Scoped.Blueprint);

	// Both halves are present. This is the phase's criterion in one line: place widgets AND wire
	// logic, in one window.
	TSharedPtr<FTabManager> TabManager = Designer->GetTabManager();
	if (TestTrue(TEXT("The editor has a tab manager"), TabManager.IsValid()))
	{
		TestTrue(TEXT("The design surface has a home"),
			TabManager->HasTabSpawner(FDreamWidgetDesignerTabs::ViewportID));
		TestTrue(TEXT("So does the hierarchy"),
			TabManager->HasTabSpawner(FDreamWidgetDesignerTabs::HierarchyID));
		TestTrue(TEXT("So does the palette"),
			TabManager->HasTabSpawner(FDreamWidgetDesignerTabs::PaletteID));
	}

	// The preview the surface draws exists, and it is the asset's own hierarchy.
	TSharedPtr<FDreamWidgetPreviewHost> Host = Designer->GetPreviewHost();
	if (TestTrue(TEXT("The designer owns a preview host"), Host.IsValid()))
	{
		TestNotNull(TEXT("The preview world exists"), Host->GetWorld());
		TestNotNull(TEXT("The design canvas exists"), Designer->GetRootAgentWidget());
		TestNotNull(TEXT("The preview hierarchy was built"), Designer->GetPreviewRootWidget());
		TestEqual(TEXT("Its root answers for the authored root"),
			Designer->GetTemplateWidget(Designer->GetPreviewRootWidget()),
			Scoped.Blueprint->WidgetTree->RootWidget.Get());
	}

	// And the graph half is reachable, which is what makes the modes worth having.
	Designer->SetCurrentMode(FDreamWidgetBlueprintApplicationModes::GraphMode);
	TestEqual(TEXT("The graph mode can be entered"), Designer->GetCurrentMode(),
		FDreamWidgetBlueprintApplicationModes::GraphMode);
	Designer->SetCurrentMode(FDreamWidgetBlueprintApplicationModes::DesignerMode);
	TestEqual(TEXT("And the designer mode can be returned to"), Designer->GetCurrentMode(),
		FDreamWidgetBlueprintApplicationModes::DesignerMode);

	return true;
}

#endif
