// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_DreamUIPrefab.h"
#include "Misc/PackageName.h"
#include "Algo/Transform.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabEditor/DreamUIPrefabEditor.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DreamUIPrefab"

FAssetTypeActions_DreamUIPrefab::FAssetTypeActions_DreamUIPrefab(EAssetTypeCategories::Type InAssetType)
: FAssetTypeActions_Base(), AssetType(InAssetType)
{

}

FText FAssetTypeActions_DreamUIPrefab::GetName() const
{
	return LOCTEXT("Name", "DreamUI Prefab");
}

UClass* FAssetTypeActions_DreamUIPrefab::GetSupportedClass() const
{
	return UDreamUIPrefab::StaticClass();
}

void FAssetTypeActions_DreamUIPrefab::OpenAssetEditor( const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor )
{
	//FAssetTypeActions_Base::OpenAssetEditor(InObjects, EditWithinLevelEditor);

	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (auto DreamGUIPrefab = Cast<UDreamUIPrefab>(*ObjIt))
		{
			TSharedRef<FDreamUIPrefabEditor> NewPrefabEditor(new FDreamUIPrefabEditor());
			NewPrefabEditor->InitPrefabEditor(Mode, EditWithinLevelEditor, DreamGUIPrefab);
		}
	}
}

uint32 FAssetTypeActions_DreamUIPrefab::GetCategories()
{
	return AssetType;
}

bool FAssetTypeActions_DreamUIPrefab::CanFilter()
{
	return true;
}

#undef LOCTEXT_NAMESPACE
