// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserMenuContexts.h"
#include "DesktopPlatformModule.h"
#include "DreamWidgetBlueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDesktopPlatform.h"
#include "Import/DreamPsdImporter.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DreamGUIPsd"

DEFINE_LOG_CATEGORY_STATIC(LogDreamGUIPsd, Log, All);

namespace DreamGUIPsdModuleLocal
{
	void Notify(const FDreamPsdImportReport& InReport)
	{
		const FString Text = InReport.ToString();
		UE_LOG(LogDreamGUIPsd, Log, TEXT("%s"), *Text);
		FNotificationInfo Info(InReport.bSucceeded
			? FText::Format(LOCTEXT("Imported", "Imported {0}"), FText::FromString(FPaths::GetCleanFilename(InReport.DuiFile)))
			: LOCTEXT("NotImported", "The layered document was not imported"));
		Info.SubText = FText::FromString(InReport.Map.Summarize() + (InReport.Merge.Conflicts.Num() > 0
			? FString::Printf(TEXT("\n%d conflicts: see the Output Log."), InReport.Merge.Conflicts.Num()) : FString()));
		Info.ExpireDuration = 8.0f;
		Info.bUseSuccessFailIcons = true;
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(InReport.bSucceeded ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	void ImportInto(const FString& InFolder)
	{
		IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
		if (Desktop == nullptr)
		{
			return;
		}
		TArray<FString> Files;
		const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		if (!Desktop->OpenFileDialog(Parent, LOCTEXT("PickDocument", "Import a layered document as a DreamUI widget").ToString(), FPaths::ProjectDir(),
			TEXT(""), TEXT("Layered documents (*.psd;*.psb)|*.psd;*.psb"), EFileDialogFlags::Multiple, Files))
		{
			return;
		}
		for (const FString& File : Files)
		{
			FDreamPsdImportRequest Request;
			Request.SourceFile = FPaths::ConvertRelativePathToFull(File);
			Request.AssetFolder = InFolder;
			FDreamPsdImportReport Report;
			DreamPsdImporter::Import(Request, Report);
			Notify(Report);
		}
	}
}

class FDreamGUIPsdModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDreamGUIPsdModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped Owner(this);
		if (UToolMenu* Folder = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.FolderContextMenu")))
		{
			FToolMenuSection& Section = Folder->FindOrAddSection(TEXT("DreamGUI"), LOCTEXT("DreamGUISection", "DreamGUI"));
			Section.AddDynamicEntry(TEXT("DreamGUIPsdImport"), FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				const UContentBrowserFolderContext* Context = InSection.FindContext<UContentBrowserFolderContext>();
				if (Context == nullptr || Context->GetSelectedPackagePaths().Num() != 1)
				{
					return;
				}
				const FString Folder = Context->GetSelectedPackagePaths()[0];
				InSection.AddMenuEntry(TEXT("ImportLayeredDocument"),
					LOCTEXT("ImportLayeredDocument", "Import Layered Document as DreamUI Widget..."),
					LOCTEXT("ImportLayeredDocumentTip", "Read a .psd or .psb and make a DreamUI widget Blueprint of its layers, a .dui and textures."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Folder]() { DreamGUIPsdModuleLocal::ImportInto(Folder); })));
			}));
		}
		if (UToolMenu* Asset = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.DreamWidgetBlueprint")))
		{
			FToolMenuSection& Section = Asset->FindOrAddSection(TEXT("GetAssetActions"));
			Section.AddDynamicEntry(TEXT("DreamGUIPsdReimport"), FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
				if (Context == nullptr)
				{
					return;
				}
				// The menu is built without loading anything; the Blueprints are loaded when the entry is chosen, and the
				// ones no import made say so then.
				const TArray<FAssetData> Selected = Context->SelectedAssets;
				InSection.AddMenuEntry(TEXT("ReimportLayeredDocument"),
					LOCTEXT("ReimportLayeredDocument", "Reimport Layered Document"),
					LOCTEXT("ReimportLayeredDocumentTip", "Import the document again into the .dui it made: the importer's lines are rewritten, everything you added is kept. Only for a Blueprint a layered document import made."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Selected]()
					{
						for (const FAssetData& Asset : Selected)
						{
							UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(Asset.GetAsset());
							FDreamPsdImportReport Report;
							DreamPsdImporter::Reimport(Blueprint, Report);
							DreamGUIPsdModuleLocal::Notify(Report);
						}
					})));
			}));
		}
	}
};

IMPLEMENT_MODULE(FDreamGUIPsdModule, DreamGUIPsd)

#undef LOCTEXT_NAMESPACE
