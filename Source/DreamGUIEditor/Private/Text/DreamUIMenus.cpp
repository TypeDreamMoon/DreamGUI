// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIMenus.h"

#include "Text/DreamUIWorkspaceService.h"

#include "Styling/AppStyle.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "DreamUIMenus"

namespace DreamUIMenusLocal
{
	const FName DreamUIMenuOwner(TEXT("DreamGUI"));
	const FName SharedComboOwner(TEXT("DreamToolsShared"));
	const FName SharedMenuName(TEXT("DreamTools.OpenInVSCode"));
	const FName AssetsToolBarName(TEXT("LevelEditor.LevelEditorToolBar.AssetsToolBar"));

	bool bGMenusRegistered = false;

	/** See FDreamUIMenus for the three-plugin convention this implements. */
	void EnsureDreamToolsCombo()
	{
		UToolMenus* ToolMenus = UToolMenus::Get();

		if (!ToolMenus->IsMenuRegistered(SharedMenuName))
		{
			FToolMenuOwnerScoped SharedOwner(SharedComboOwner);
			ToolMenus->RegisterMenu(SharedMenuName);
		}

		UToolMenu* Toolbar = ToolMenus->ExtendMenu(AssetsToolBarName);
		if (Toolbar == nullptr)
		{
			return;
		}
		FToolMenuSection& Section = Toolbar->FindOrAddSection(TEXT("DreamTools"));
		if (Section.FindEntry(TEXT("DreamTools.OpenWorkspaceCombo")) != nullptr)
		{
			return;
		}

		FToolMenuOwnerScoped SharedOwner(SharedComboOwner);
		Section.AddEntry(FToolMenuEntry::InitComboButton(
			TEXT("DreamTools.OpenWorkspaceCombo"),
			FUIAction(),
			FNewToolMenuChoice(FOnGetContent::CreateLambda([]
			{
				return UToolMenus::Get()->GenerateWidget(DreamUIMenusLocal::SharedMenuName, FToolMenuContext());
			})),
			LOCTEXT("DreamToolsComboLabel", "Dream"),
			LOCTEXT("DreamToolsComboTooltip",
				"Open a Dream-family source workspace (DreamShader / DreamFX / DreamUI) in VSCode; Notepad stands in when VSCode is unavailable."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.OpenInExternalEditor")),
				/*bInSimpleComboBox*/ true));
	}

	void RegisterMenusInternal()
	{
		if (bGMenusRegistered || IsEngineExitRequested() || GExitPurge || UToolMenus::Get() == nullptr)
		{
			return;
		}
		bGMenusRegistered = true;

		FToolMenuOwnerScoped MenuOwner(DreamUIMenuOwner);

		EnsureDreamToolsCombo();

		if (UToolMenu* SharedMenu = UToolMenus::Get()->ExtendMenu(SharedMenuName))
		{
			FToolMenuSection& Section = SharedMenu->FindOrAddSection(TEXT("DreamUI"),
				LOCTEXT("DreamUISectionLabel", "DreamUI"));
			Section.AddMenuEntry(
				TEXT("DreamUI.OpenWorkspace"),
				LOCTEXT("OpenWorkspaceLabel", "DreamUI Workspace"),
				LOCTEXT("OpenWorkspaceTooltip",
					"Rewrite DUI/DreamUI.code-workspace from the current source roots and open it in VSCode, or Notepad if VSCode is unavailable."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.OpenInExternalEditor")),
				FUIAction(FExecuteAction::CreateStatic(&FDreamUIWorkspaceService::OpenWorkspace)));
		}

		if (UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools")))
		{
			FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("DreamGUI"));
			Section.AddMenuEntry(
				TEXT("DreamUI.OpenWorkspaceTools"),
				LOCTEXT("OpenWorkspaceToolsLabel", "Open DreamUI Workspace (VSCode)"),
				LOCTEXT("OpenWorkspaceTooltip",
					"Rewrite DUI/DreamUI.code-workspace from the current source roots and open it in VSCode, or Notepad if VSCode is unavailable."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.OpenInExternalEditor")),
				FUIAction(FExecuteAction::CreateStatic(&FDreamUIWorkspaceService::OpenWorkspace)));
		}
	}
}

void FDreamUIMenus::Register()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(
		&DreamUIMenusLocal::RegisterMenusInternal));
}

void FDreamUIMenus::Unregister()
{
	if (UToolMenus* ToolMenus = UToolMenus::Get())
	{
		// Own entries only. The shared combo stays: it belongs to "DreamToolsShared" precisely so
		// no single plugin's unload can take the family's door with it.
		ToolMenus->UnregisterOwnerByName(DreamUIMenusLocal::DreamUIMenuOwner);
	}
}

#undef LOCTEXT_NAMESPACE
