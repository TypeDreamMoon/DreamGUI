// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabRawDataViewer.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "LexUIPrefabEditor.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabRawDataViewer"

void SLexUIPrefabRawDataViewer::Construct(const FArguments& InArgs, TSharedPtr<FLexUIPrefabEditor> InPrefabEditorPtr, UObject* InObject)
{
	PrefabEditorPtr = InPrefabEditorPtr;
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = false;
		DetailsViewArgs.bAllowFavoriteSystem = false;
		DetailsViewArgs.bHideSelectionTip = true;
	}
	DescriptorDetailView = EditModule.CreateDetailView(DetailsViewArgs);
	DescriptorDetailView->SetObject(InObject);
	ChildSlot
		[
			DescriptorDetailView.ToSharedRef()
		];
}

#undef LOCTEXT_NAMESPACE
