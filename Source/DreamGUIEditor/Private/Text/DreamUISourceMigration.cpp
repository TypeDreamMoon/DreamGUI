// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUISourceMigration.h"

#include "DreamGUIEditorModule.h"
#include "DreamGUIEngineCompat.h"
#include "Text/DreamUIBridgeService.h"
#include "Text/DreamUIDiagnosticsMailbox.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUISourceWatcher.h"
#include "Text/DreamUISymbolExport.h"

#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DreamUISourceMigration"

namespace DreamUISourceMigrationLocal
{
	FDelegateHandle GStartupHandle;
	TWeakPtr<SNotificationItem> GNotification;

	const TCHAR* const ConfigSection = TEXT("DreamSource");
	const TCHAR* const HideKey = TEXT("bHideUIMigrationNotice");

	bool IsHidden()
	{
		bool bHidden = false;
		GConfig->GetBool(ConfigSection, HideKey, bHidden, GEditorPerProjectIni);
		return bHidden;
	}

	void Hide()
	{
		GConfig->SetBool(ConfigSection, HideKey, true, GEditorPerProjectIni);
		GConfig->Flush(/*bRemoveFromCache*/false, GEditorPerProjectIni);
	}

	void Dismiss()
	{
		if (const TSharedPtr<SNotificationItem> Item = GNotification.Pin())
		{
			Item->SetExpireDuration(0.0f);
			Item->SetFadeOutDuration(0.5f);
			Item->ExpireAndFadeout();
		}
		GNotification.Reset();
	}

	void Toast(const FText& InText, bool bSuccess)
	{
		FNotificationInfo Info(InText);
		Info.ExpireDuration = bSuccess ? 6.0f : 12.0f;
		Info.bUseLargeFont = false;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	void ShowNotice()
	{
		DreamUIPaths::LogDeprecatedRootsOnce();

		// The notification is for the person at the keyboard: not a commandlet, an automation run or a process started
		// unattended beside the editor -- the same processes that leave the bridge alone.
		if (!FDreamUIBridgeService::ShouldOwnBridge() || IsHidden())
		{
			return;
		}
		const FDreamUIProjectSourceDirectory Project = DreamUIPaths::ResolveProjectSourceDirectory();
		if (!Project.bUsingLegacy && !Project.bLegacyIgnored)
		{
			return;
		}

		FNotificationInfo Info(Project.bUsingLegacy
			? FText::Format(LOCTEXT("UsingLegacy",
				"DreamGUI: '{0}' is deprecated.\nMove it to '{1}' -- Dream sources are managed together under DreamSource/.\nIt keeps working until then."),
				FText::FromString(Project.LegacyDirectory), FText::FromString(Project.ConfiguredDirectory))
			: FText::Format(LOCTEXT("LegacyIgnored",
				"DreamGUI: '{0}' is ignored because '{1}' exists.\nMerge what it still holds into '{1}', then delete it."),
				FText::FromString(Project.LegacyDirectory), FText::FromString(Project.ConfiguredDirectory)));
		Info.bFireAndForget = false;
		Info.ExpireDuration = 0.0f;
		Info.bUseLargeFont = false;
		if (Project.bUsingLegacy)
		{
			Info.ButtonDetails.Add(FNotificationButtonInfo(
				LOCTEXT("Migrate", "Migrate"),
				LOCTEXT("MigrateTip", "Move the folder now. Close VS Code windows that have files in it open first."),
				FSimpleDelegate::CreateLambda([]
				{
					Dismiss();
					FString Error;
					if (FDreamUISourceMigration::Migrate(Error))
					{
						Toast(FText::Format(LOCTEXT("Migrated", "DreamGUI: .dui sources moved to '{0}'."),
							FText::FromString(DreamUIPaths::GetProjectSourceDirectory())), true);
					}
					else
					{
						Toast(FText::Format(LOCTEXT("MigrateFailed", "DreamGUI: could not migrate: {0}"),
							FText::FromString(Error)), false);
					}
				}),
				SNotificationItem::CS_None));
		}
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("DontShowAgain", "Don't show again"),
			LOCTEXT("DontShowAgainTip", "Stop showing this notice on this machine. The Output Log still says it."),
			FSimpleDelegate::CreateLambda([] { Hide(); Dismiss(); }),
			SNotificationItem::CS_None));
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("Later", "Later"),
			FText::GetEmpty(),
			FSimpleDelegate::CreateLambda([] { Dismiss(); }),
			SNotificationItem::CS_None));

		GNotification = FSlateNotificationManager::Get().AddNotification(Info);
	}
}

void FDreamUISourceMigration::Register()
{
	using namespace DreamUISourceMigrationLocal;
	// After engine init: the settings CDO, the plugin list and Slate all have to be up.
	GStartupHandle = DreamGUICompat::OnPostEngineInit().AddLambda([] { ShowNotice(); });
}

void FDreamUISourceMigration::Unregister()
{
	using namespace DreamUISourceMigrationLocal;
	if (GStartupHandle.IsValid())
	{
		DreamGUICompat::OnPostEngineInit().Remove(GStartupHandle);
		GStartupHandle.Reset();
	}
	GNotification.Reset();
}

bool FDreamUISourceMigration::Migrate(FString& OutError)
{
	const FDreamUIProjectSourceDirectory Project = DreamUIPaths::ResolveProjectSourceDirectory();
	if (!Project.bUsingLegacy)
	{
		OutError = Project.bLegacyIgnored
			? FString::Printf(TEXT("'%s' already exists; merge '%s' into it by hand."), *Project.ConfiguredDirectory, *Project.LegacyDirectory)
			: FString::Printf(TEXT("there is no '%s' to move."), *Project.LegacyDirectory);
		return false;
	}

	IFileManager& FileManager = IFileManager::Get();
	FileManager.MakeDirectory(*FPaths::GetPath(Project.ConfiguredDirectory), /*Tree*/true);

	// A rename first: same volume, one step, nothing half-done if it fails. A directory watcher or an editor holding a
	// file open can refuse it on Windows; then copy and delete, and if the delete is what fails, both folders exist and
	// the configured one wins -- the notice then says to delete the old one, and nothing was lost.
	if (!FileManager.Move(*Project.ConfiguredDirectory, *Project.LegacyDirectory, /*Replace*/false, /*EvenIfReadOnly*/true))
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.CopyDirectoryTree(*Project.ConfiguredDirectory, *Project.LegacyDirectory, /*bOverwriteAllExisting*/false))
		{
			FileManager.DeleteDirectory(*Project.ConfiguredDirectory, /*RequireExists*/false, /*Tree*/true);
			OutError = FString::Printf(TEXT("could not move or copy '%s' to '%s'."), *Project.LegacyDirectory, *Project.ConfiguredDirectory);
			return false;
		}
		if (!FileManager.DeleteDirectory(*Project.LegacyDirectory, /*RequireExists*/false, /*Tree*/true))
		{
			UE_LOG(DreamGUIEditor, Warning,
				TEXT("[%s].%d Copied '%s' to '%s' but could not delete the old folder; delete it once nothing holds it open."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Project.LegacyDirectory, *Project.ConfiguredDirectory);
		}
	}

	// The json files ui.db replaced. They would only mislead a reader that still looks for them.
	for (const TCHAR* Stale : { TEXT(".dui-symbols.json"), TEXT(".dui-diagnostics.json") })
	{
		FileManager.Delete(*FPaths::Combine(Project.ConfiguredDirectory, Stale), /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
	}

	FDreamUISourceWatcher::EnsureWatching(Project.ConfiguredDirectory);
	FDreamUISymbolExport::ExportNow();
	FDreamUIDiagnosticsMailbox::FlushNow();
	UE_LOG(DreamGUIEditor, Display, TEXT("[%s].%d Moved '%s' to '%s'."),
		ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Project.LegacyDirectory, *Project.ConfiguredDirectory);
	return true;
}

#undef LOCTEXT_NAMESPACE
