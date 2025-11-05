// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_LexUIPrefab.h"
#include "Misc/PackageName.h"
#include "Algo/Transform.h"
#include "PrefabSystem/LGUIPrefab.h"
#include "PrefabEditor/LGUIPrefabEditor.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_LexUIPrefab"

FAssetTypeActions_LexUIPrefab::FAssetTypeActions_LexUIPrefab(EAssetTypeCategories::Type InAssetType)
: FAssetTypeActions_Base(), AssetType(InAssetType)
{

}

FText FAssetTypeActions_LexUIPrefab::GetName() const
{
	return LOCTEXT("Name", "LexUI Prefab");
}

UClass* FAssetTypeActions_LexUIPrefab::GetSupportedClass() const
{
	return ULGUIPrefab::StaticClass();
}

void FAssetTypeActions_LexUIPrefab::OpenAssetEditor( const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor )
{
	//FAssetTypeActions_Base::OpenAssetEditor(InObjects, EditWithinLevelEditor);

	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (auto LGUIPrefab = Cast<ULGUIPrefab>(*ObjIt))
		{
			TSharedRef<FLGUIPrefabEditor> NewPrefabEditor(new FLGUIPrefabEditor());
			NewPrefabEditor->InitPrefabEditor(Mode, EditWithinLevelEditor, LGUIPrefab);
		}
	}
}

uint32 FAssetTypeActions_LexUIPrefab::GetCategories()
{
	return AssetType;
}

bool FAssetTypeActions_LexUIPrefab::CanFilter()
{
	return true;
}

#undef LOCTEXT_NAMESPACE
