// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIMenus.h"

#include "Text/DreamUISourceWatcher.h"
#include "Text/DreamUIWorkspaceService.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DreamUIMenus"

namespace DreamUIMenusLocal
{
	const FName DreamUIMenuOwner(TEXT("DreamGUI"));
	const FName SharedComboOwner(TEXT("DreamToolsShared"));
	const FName SharedMenuName(TEXT("DreamTools.Actions"));
	const FName AssetsToolBarName(TEXT("LevelEditor.LevelEditorToolBar.AssetsToolBar"));

	bool bGMenusRegistered = false;

	/**
	 * Rebuild-all, behind a confirmation. A whole-project recompile is the family's heaviest
	 * gesture and it sits one row under the workspace openers -- a mis-click costs minutes, so
	 * every rebuild entry in this menu asks first (the same rule DreamShader and DreamFX apply
	 * to theirs).
	 */
	void ConfirmedRebuildAll()
	{
		const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo,
			LOCTEXT("RebuildAllConfirm",
				"Rebuild every text-backed widget Blueprint in the project?\n\n"
				"Blueprints that are not loaded will be loaded and compiled; this can take a while."));
		if (Answer != EAppReturnType::Yes)
		{
			return;
		}

		const int32 Count = FDreamUISourceWatcher::RebuildAll();
		// A menu command with no visible reaction reads as a broken menu.
		FNotificationInfo Info(FText::Format(
			LOCTEXT("RebuildAllDone", "DreamUI: rebuilt {0} text-backed Blueprint(s)."), Count));
		Info.ExpireDuration = 4.0f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Success);
		}
	}

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
				"Dream-family language tools: open a source workspace in VSCode, or rebuild a whole source tree (DreamShader / DreamFX / DreamUI)."),
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
			Section.AddMenuEntry(
				TEXT("DreamUI.RebuildAll"),
				LOCTEXT("RebuildAllLabel", "Rebuild DUI"),
				LOCTEXT("RebuildAllTooltip",
					"Recompile every text-backed widget Blueprint in the project, loading the ones that are not. Asks first."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Refresh")),
				FUIAction(FExecuteAction::CreateStatic(&ConfirmedRebuildAll)));
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
			Section.AddMenuEntry(
				TEXT("DreamUI.RebuildAllTools"),
				LOCTEXT("RebuildAllToolsLabel", "Rebuild DUI"),
				LOCTEXT("RebuildAllTooltip",
					"Recompile every text-backed widget Blueprint in the project, loading the ones that are not. Asks first."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Refresh")),
				FUIAction(FExecuteAction::CreateStatic(&ConfirmedRebuildAll)));
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
