// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LevelEditorMenuExtensions/LexUILevelEditorExtensions.h"
#include "Engine/EngineTypes.h"
#include "LevelEditor.h"
#include "LGUIEditorModule.h"
#include "LexUIEditorTools.h"
#include "LGUIEditorCommands.h"

#define LOCTEXT_NAMESPACE "LexUILevelEditorExtensions"

//////////////////////////////////////////////////////////////////////////

FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors LevelEditorMenuExtenderDelegate;
FDelegateHandle LevelEditorMenuExtenderDelegateHandle;

//////////////////////////////////////////////////////////////////////////

class FLexUILevelEditorExtensions_Impl
{
public:
	static void CreateLexUISubMenu(FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.AddWidget(
		FLGUIEditorModule::Get().MakeEditorToolsMenu(false, false, false, false, []()
			{
				return FLexUIEditorTools::GetFirstSelectedActor();
			}, [=](FMenuBuilder& MenuBuilder)
			{
				MenuBuilder.BeginSection("ActorAction", LOCTEXT("ActorAction", "Edit Actor With Hierarchy"));
				{
					MenuBuilder.PushCommandList(FLGUIEditorModule::Get().PluginCommands.ToSharedRef());
					{
						MenuBuilder.AddMenuEntry(FLGUIEditorCommands::Get().CopyActor);
						MenuBuilder.AddMenuEntry(FLGUIEditorCommands::Get().PasteActor);
						MenuBuilder.AddMenuEntry(FLGUIEditorCommands::Get().CutActor);
						MenuBuilder.AddMenuEntry(FLGUIEditorCommands::Get().DuplicateActor);
						MenuBuilder.AddMenuEntry(FLGUIEditorCommands::Get().DestroyActor);
						MenuBuilder.AddMenuEntry(FLGUIEditorCommands::Get().ToggleSpatiallyLoaded);
					}
					MenuBuilder.PopCommandList();
				}
				MenuBuilder.EndSection();
			})
			, FText::GetEmpty()
		);
	}
	static void CreateHelperButtons(FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.BeginSection("LexUI", LOCTEXT("LexUILevelEditorHeading", "LexUI"));
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("LexUIEditorTools", "LexUI Editor Tools"),
				FText::GetEmpty(),
				FNewMenuDelegate::CreateStatic(&FLexUILevelEditorExtensions_Impl::CreateLexUISubMenu),
				FUIAction(),
				NAME_None, EUserInterfaceActionType::None
			);
		}
		MenuBuilder.EndSection();
	}
	static TSharedRef<FExtender> OnExtendLevelEditorMenu(const TSharedRef<FUICommandList> CommandList, TArray<AActor*> SelectedActors)
	{
		TSharedRef<FExtender> Extender(new FExtender());
		if (SelectedActors.Num() == 1//only support one selection
			&& IsValid(SelectedActors[0])
			&& FLexUIEditorTools::IsActorCompatibleWithLexUIToolsMenu(SelectedActors[0])//only show menu with supported actor
			)
		{
			Extender->AddMenuExtension(
				"ActorTypeTools",
				EExtensionHook::After,
				nullptr,
				FMenuExtensionDelegate::CreateStatic(&FLexUILevelEditorExtensions_Impl::CreateHelperButtons)
			);
		}
		return Extender;
	}
};

// FLexUILevelEditorExtensions

void FLexUILevelEditorExtensions::InstallHooks()
{
	LevelEditorMenuExtenderDelegate = FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateStatic(&FLexUILevelEditorExtensions_Impl::OnExtendLevelEditorMenu);

	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	auto& MenuExtenders = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
	MenuExtenders.Add(LevelEditorMenuExtenderDelegate);
	LevelEditorMenuExtenderDelegateHandle = MenuExtenders.Last().GetHandle();
}

void FLexUILevelEditorExtensions::RemoveHooks()
{
	if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.GetAllLevelViewportContextMenuExtenders().RemoveAll([&](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& Delegate) {
			return Delegate.GetHandle() == LevelEditorMenuExtenderDelegateHandle;
		});
	}
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE