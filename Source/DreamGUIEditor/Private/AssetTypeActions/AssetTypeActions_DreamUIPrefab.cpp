// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "AssetTypeActions_DreamUIPrefab.h"
#include "Misc/PackageName.h"
#include "Algo/Transform.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabEditor/DreamWidgetBlueprintEditor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Styling/AppStyle.h"

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

	// A prefab has no editor any more. The designer edits a UDreamWidgetBlueprint, and the way to
	// get one from a prefab is to convert it -- which is a real, verified operation, unlike keeping
	// a second authoring surface alive for a format nothing produces.
	FNotificationInfo Info(NSLOCTEXT("DreamUIPrefab", "PrefabEditorRetired",
		"DreamUI prefabs are no longer edited directly. Right-click the asset and choose \"Convert to Widget Blueprint\"."));
	Info.Image = FAppStyle::GetBrush(TEXT("Icons.InfoWithColor"));
	Info.ExpireDuration = 8.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
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
