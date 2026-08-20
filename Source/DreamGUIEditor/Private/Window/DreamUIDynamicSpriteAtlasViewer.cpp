// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Window/DreamUIDynamicSpriteAtlasViewer.h"

#include "DetailLayoutBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "Core/Components/DreamSpriteBase.h"
#include "Core/DreamUIDynamicSpriteAtlasData.h"
#include "DreamGUIEditorModule.h"
#include "ISinglePropertyView.h"

#define LOCTEXT_NAMESPACE "DreamGUIDynamicSpriteAtlasViewer"

void SDreamUIDynamicSpriteAtlasViewer::Construct(const FArguments& Args, TSharedPtr<SDockTab> InOwnerTab)
{
	InOwnerTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateSP(this, &SDreamUIDynamicSpriteAtlasViewer::CloseTabCallback));
	if (UDreamUIDynamicSpriteAtlasManager::Instance != nullptr)
	{
		FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FDetailsViewArgs DetailsViewArgs;
		{
			DetailsViewArgs.bAllowSearch = false;
			DetailsViewArgs.bShowOptions = false;
			DetailsViewArgs.bAllowMultipleTopLevelObjects = false;
			DetailsViewArgs.bAllowFavoriteSystem = false;
			DetailsViewArgs.bHideSelectionTip = true;
		}
		//FSinglePropertyParams SinglePropertyParams;
		//TSharedPtr<ISinglePropertyView> Property = EditModule.CreateSingleProperty(UDreamGUIAtlasManager::Instance, TEXT("atlasMap"), SinglePropertyParams);
		//Property->SetObject(UDreamGUIAtlasManager::Instance);

		TSharedPtr<IDetailsView> DescriptorDetailView = EditModule.CreateDetailView(DetailsViewArgs);
		DescriptorDetailView->SetObject(UDreamUIDynamicSpriteAtlasManager::Instance);

		ChildSlot
			[
				DescriptorDetailView.ToSharedRef()
			];
	}
	else
	{
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("NoneAtlas", "None atlas texture here because none sprite have packed. Sprite will be packed when it get renderred"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
	}
}

void SDreamUIDynamicSpriteAtlasViewer::CloseTabCallback(TSharedRef<SDockTab> TabClosed)
{
	
}
#undef LOCTEXT_NAMESPACE