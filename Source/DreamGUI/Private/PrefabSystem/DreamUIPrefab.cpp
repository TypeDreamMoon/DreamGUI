// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/DreamUIPrefab.h"
#include "DreamGUI.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"

#include LEXUIPREFAB_SERIALIZER_NEWEST_INCLUDE
#include "Utils/DreamUIUtils.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "Engine/Engine.h"
#include "UObject/ObjectSaveContext.h"

#define LOCTEXT_NAMESPACE "DreamGUIPrefab"

FString FDreamUIPrefabSchemaMigrationReport::ToString() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(
		TEXT("Schema %u -> %u | %d changed object(s)"),
		FromVersion, ToVersion, ChangedObjectCount));
	if (bSchemaVersionUpdated)
	{
		Lines.Add(TEXT("Schema version will be updated."));
	}
	auto AppendSection = [&Lines](const TCHAR* Heading, const TArray<FString>& Entries)
	{
		if (Entries.IsEmpty())
		{
			return;
		}
		Lines.Add(FString::Printf(TEXT("%s:"), Heading));
		for (const FString& Entry : Entries)
		{
			Lines.Add(FString::Printf(TEXT("  - %s"), *Entry));
		}
	};
	AppendSection(TEXT("Actions"), Actions);
	AppendSection(TEXT("Warnings"), Warnings);
	AppendSection(TEXT("Errors"), Errors);
	if (!HasChanges() && !HasErrors())
	{
		Lines.Add(TEXT("No migration is required."));
	}
	return FString::Join(Lines, TEXT("\n"));
}

namespace DreamUIPrefabSchemaLocal
{
	FString BuildWidgetPath(const UDreamWidget* Widget)
	{
		TArray<FString> Segments;
		TSet<const UDreamWidget*> Visited;
		const UDreamWidget* Current = Widget;
		while (IsValid(Current) && !Visited.Contains(Current))
		{
			Visited.Add(Current);
			Segments.Insert(Current->GetName(), 0);
			Current = Current->GetParent();
		}
		return FString::Printf(TEXT("/%s"), *FString::Join(Segments, TEXT("/")));
	}

	void MigrateWidgetRecursive(
		UDreamWidget* Widget,
		TSet<UDreamWidget*>& Visited,
		TSet<UDreamWidget*>& ActivePath,
		TSet<UDreamWidget*>& ChangedWidgets,
		FDreamUIPrefabSchemaMigrationReport& Report,
		bool bApplyChanges)
	{
		if (!IsValid(Widget))
		{
			Report.Warnings.Add(TEXT("Skipped an invalid widget reference."));
			return;
		}
		if (ActivePath.Contains(Widget))
		{
			Report.Errors.Add(FString::Printf(
				TEXT("Hierarchy cycle detected at %s."), *BuildWidgetPath(Widget)));
			return;
		}
		if (Visited.Contains(Widget))
		{
			Report.Errors.Add(FString::Printf(
				TEXT("Widget is referenced more than once: %s."), *BuildWidgetPath(Widget)));
			return;
		}

		Visited.Add(Widget);
		ActivePath.Add(Widget);
		const FString WidgetPath = BuildWidgetPath(Widget);
		UDreamWidget* Parent = Widget->GetParent();
		const bool bParentOwnsPanelSlot = IsValid(Parent)
			&& IsValid(Cast<UDreamPanelLayoutBase>(Parent->GetLayoutContainer()));
		UDreamPanelSlot* Slot = Widget->GetPanelSlot();
		if (bParentOwnsPanelSlot)
		{
			if (!IsValid(Slot))
			{
				ChangedWidgets.Add(Widget);
				Report.Actions.Add(FString::Printf(TEXT("Created parent-owned PanelSlot for %s."), *WidgetPath));
				if (bApplyChanges)
				{
					Slot = Widget->CreateNewPanelSlot<UDreamPanelSlot>();
					if (IsValid(Slot))
					{
						Slot->CaptureAuthoredGeometry(true);
					}
					else
					{
						Report.Errors.Add(FString::Printf(TEXT("Could not create PanelSlot for %s."), *WidgetPath));
					}
				}
			}
			else if (!Slot->HasAuthoredGeometry())
			{
				ChangedWidgets.Add(Widget);
				Report.Actions.Add(FString::Printf(TEXT("Captured authored geometry for %s."), *WidgetPath));
				if (bApplyChanges)
				{
					Slot->CaptureAuthoredGeometry(true);
				}
			}
		}
		else if (IsValid(Slot))
		{
			ChangedWidgets.Add(Widget);
			Report.Warnings.Add(FString::Printf(TEXT("Removed stale PanelSlot from %s."), *WidgetPath));
			if (bApplyChanges)
			{
				Widget->RemovePanelSlot();
			}
		}

		for (UDreamWidget* Child : Widget->GetChildren())
		{
			if (IsValid(Child) && Child->GetParent() != Widget)
			{
				Report.Errors.Add(FString::Printf(
					TEXT("Parent link mismatch under %s for child %s."), *WidgetPath, *Child->GetName()));
				continue;
			}
			MigrateWidgetRecursive(Child, Visited, ActivePath, ChangedWidgets, Report, bApplyChanges);
		}
		ActivePath.Remove(Widget);
	}
}


FDreamUISubPrefabData::FDreamUISubPrefabData()
{
#if WITH_EDITORONLY_DATA
	EditorIdentifyColor = FLinearColor::MakeRandomColor();
#endif
}
void FDreamUISubPrefabData::AddMemberProperty(UObject* InObject, FName InPropertyName)
{
	auto Index = ObjectOverrideParameterArray.IndexOfByPredicate([=](const FDreamUIPrefabOverrideParameterData& Item) {
		return Item.Object == InObject;
		});
	if (Index == INDEX_NONE)
	{
		FDreamUIPrefabOverrideParameterData DataItem;
		DataItem.Object = InObject;
		DataItem.MemberPropertyNames.Add(InPropertyName);
		ObjectOverrideParameterArray.Add(DataItem);
	}
	else
	{
		auto& DataItem = ObjectOverrideParameterArray[Index];
		if (!DataItem.MemberPropertyNames.Contains(InPropertyName))
		{
			DataItem.MemberPropertyNames.Add(InPropertyName);
		}
	}
}

void FDreamUISubPrefabData::AddMemberProperty(UObject* InObject, const TArray<FName>& InPropertyNames)
{
	auto Index = ObjectOverrideParameterArray.IndexOfByPredicate([=](const FDreamUIPrefabOverrideParameterData& Item) {
		return Item.Object == InObject;
		});
	if (Index == INDEX_NONE)
	{
		FDreamUIPrefabOverrideParameterData DataItem;
		DataItem.Object = InObject;
		DataItem.MemberPropertyNames = InPropertyNames;
		ObjectOverrideParameterArray.Add(DataItem);
	}
	else
	{
		auto& DataItem = ObjectOverrideParameterArray[Index];
		for (auto& NameItem : InPropertyNames)
		{
			if (!DataItem.MemberPropertyNames.Contains(NameItem))
			{
				DataItem.MemberPropertyNames.Add(NameItem);
			}
		}
	}
}

void FDreamUISubPrefabData::RemoveMemberProperty(UObject* InObject, FName InPropertyName)
{
	auto Index = ObjectOverrideParameterArray.IndexOfByPredicate([=](const FDreamUIPrefabOverrideParameterData& Item) {
		return Item.Object == InObject;
		});
	if (Index != INDEX_NONE)
	{
		auto& DataItem = ObjectOverrideParameterArray[Index];
		if (DataItem.MemberPropertyNames.Contains(InPropertyName))
		{
			DataItem.MemberPropertyNames.Remove(InPropertyName);
		}
		if (DataItem.MemberPropertyNames.Num() <= 0)
		{
			ObjectOverrideParameterArray.RemoveAt(Index);
		}
	}
}

void FDreamUISubPrefabData::RemoveMemberProperty(UObject* InObject)
{
	auto Index = ObjectOverrideParameterArray.IndexOfByPredicate([=](const FDreamUIPrefabOverrideParameterData& Item) {
		return Item.Object == InObject;
		});
	if (Index != INDEX_NONE)
	{
		ObjectOverrideParameterArray.RemoveAt(Index);
	}
}

bool FDreamUISubPrefabData::CheckParameters()
{
	bool AnythingChanged = false;
	for (int i = 0; i < ObjectOverrideParameterArray.Num(); i++)
	{
		auto& DataItem = ObjectOverrideParameterArray[i];
		if (!DataItem.Object.IsValid())
		{
			ObjectOverrideParameterArray.RemoveAt(i);
			i--;
			AnythingChanged = true;
		}
		else
		{
			TSet<FName> PropertyNamesToRemove;
			auto Object = DataItem.Object;
			for (auto PropertyName : DataItem.MemberPropertyNames)
			{
				auto Property = FindFProperty<FProperty>(Object->GetClass(), PropertyName);
				if (Property == nullptr)
				{
					PropertyNamesToRemove.Add(PropertyName);
				}
			}
			for (auto PropertyName : PropertyNamesToRemove)
			{
				DataItem.MemberPropertyNames.Remove(PropertyName);
				AnythingChanged = true;
			}
		}
	}
	return AnythingChanged;
}

UDreamUIPrefab::UDreamUIPrefab()
{

}

FDreamUIPrefabSchemaMigrationReport UDreamUIPrefab::ApplySchemaMigration(UDreamWidget* RootWidget)
{
	const FDreamUIPrefabSchemaMigrationReport ValidationReport = EvaluateSchemaMigration(RootWidget, false);
	if (ValidationReport.HasErrors())
	{
		return ValidationReport;
	}
	return EvaluateSchemaMigration(RootWidget, true);
}

FDreamUIPrefabSchemaMigrationReport UDreamUIPrefab::EvaluateSchemaMigration(UDreamWidget* RootWidget, bool bApplyChanges)
{
	FDreamUIPrefabSchemaMigrationReport Report;
	Report.FromVersion = PrefabSchemaVersion;
	if (PrefabSchemaVersion > LEXUI_CURRENT_PREFAB_SCHEMA_VERSION)
	{
		Report.Errors.Add(FString::Printf(
			TEXT("Prefab schema %u is newer than the supported schema %u."),
			PrefabSchemaVersion, LEXUI_CURRENT_PREFAB_SCHEMA_VERSION));
		return Report;
	}
	if (!IsValid(RootWidget))
	{
		Report.Errors.Add(TEXT("Prefab has no valid root widget."));
		return Report;
	}

	TSet<UDreamWidget*> Visited;
	TSet<UDreamWidget*> ActivePath;
	TSet<UDreamWidget*> ChangedWidgets;
	DreamUIPrefabSchemaLocal::MigrateWidgetRecursive(
		RootWidget, Visited, ActivePath, ChangedWidgets, Report, bApplyChanges);
	Report.ChangedObjectCount = ChangedWidgets.Num();
	if (!Report.HasErrors() && PrefabSchemaVersion != LEXUI_CURRENT_PREFAB_SCHEMA_VERSION)
	{
		Report.bSchemaVersionUpdated = true;
		if (bApplyChanges)
		{
			PrefabSchemaVersion = LEXUI_CURRENT_PREFAB_SCHEMA_VERSION;
		}
	}
	return Report;
}

#if WITH_EDITOR

void UDreamUIPrefab::SetRootWidgetNameFromPrefab()
{
	if (GetPrefabHelperObject() && PrefabHelperObject->LoadedRootWidget)
	{
		auto RootWidgetDisplayName = this->GetName();
		if (RootWidgetDisplayName.RemoveFromStart(TEXT("Default__")))
		{
			UE_LOG(DreamGUI, Display, TEXT("[%s] Rename Default__"), ANSI_TO_TCHAR(__FUNCTION__));
		}
		if (RootWidgetDisplayName.RemoveFromStart(TEXT("REINST__")))
		{
			UE_LOG(DreamGUI, Display, TEXT("[%s] Rename REINST__"), ANSI_TO_TCHAR(__FUNCTION__));
		}
		auto FindIndex = RootWidgetDisplayName.Find("_C", ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (FindIndex != INDEX_NONE)
		{
			RootWidgetDisplayName = RootWidgetDisplayName.Left(FindIndex);
		}
		PrefabHelperObject->LoadedRootWidget->SetDisplayName(RootWidgetDisplayName);
		PrefabHelperObject->SavePrefab();
	}
}

FDreamUIPrefabInstanceScene* UDreamUIPrefab::GetPrefabInstanceScene()
{
	if (!PrefabInstanceScene)
	{
		auto CSV = FDreamUIPrefabInstanceScene::ConstructionValues();
		CSV.Name = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), FName(*FString::Printf(TEXT("PrefabInstanceScene_%s"), *GetName())));
		PrefabInstanceScene = MakeUnique<FDreamUIPrefabInstanceScene>(
				   FDreamUIPrefabInstanceScene::ConstructionValues()
				   .AllowAudioPlayback(true)
				   .ShouldSimulatePhysics(false)
				   .SetEditor(true)
				   );
	}
	return PrefabInstanceScene.Get();
}

void UDreamUIPrefab::ClearPrefabInstanceScene()
{
	if (PrefabInstanceScene.IsValid())
	{
		PrefabInstanceScene.Reset();
	}
}

void UDreamUIPrefab::EnsureInstanceObjects()
{
	if (PrefabVersion >= (uint16)EDreamUIPrefabVersion::BuiltinFArchive)
	{
		if (!IsValid(PrefabHelperObject))
		{
			PrefabHelperObject = NewObject<UDreamUIPrefabHelperObject>(
				GetTransientPackage(),
				NAME_None,
				RF_Transient | RF_Transactional);
			PrefabHelperObject->Init(this, GetPrefabInstanceScene());
		}
	}
}

struct FDreamUIPrefabVersionScope
{
public:
	uint16 PrefabVersion = 0;
	uint16 PrefabSchemaVersion = 0;
	uint16 EngineMajorVersion = 0;
	uint16 EngineMinorVersion = 0;
	uint16 EnginePatchVersion = 0;
	int32 ArchiveVersion = 0;
	int32 ArchiveLicenseeVer = 0;
	uint32 ArEngineNetVer = 0;
	uint32 ArGameNetVer = 0;

	UDreamUIPrefab* Prefab = nullptr;
	FDreamUIPrefabVersionScope(UDreamUIPrefab* InPrefab)
	{
		Prefab = InPrefab;
		this->EngineMajorVersion = Prefab->EngineMajorVersion;
		this->EngineMinorVersion = Prefab->EngineMinorVersion;
		this->EnginePatchVersion = Prefab->EnginePatchVersion;
		this->PrefabVersion = Prefab->PrefabVersion;
		this->PrefabSchemaVersion = Prefab->PrefabSchemaVersion;
		this->ArchiveVersion = Prefab->ArchiveVersion;
		this->ArchiveLicenseeVer = Prefab->ArchiveLicenseeVer;
		this->ArEngineNetVer = Prefab->ArEngineNetVer;
		this->ArGameNetVer = Prefab->ArGameNetVer;
	}
	~FDreamUIPrefabVersionScope()
	{
		Prefab->EngineMajorVersion = this->EngineMajorVersion;
		Prefab->EngineMinorVersion = this->EngineMinorVersion;
		Prefab->EnginePatchVersion = this->EnginePatchVersion;
		Prefab->PrefabVersion = this->PrefabVersion;
		Prefab->PrefabSchemaVersion = this->PrefabSchemaVersion;
		Prefab->ArchiveVersion = this->ArchiveVersion;
		Prefab->ArchiveLicenseeVer = this->ArchiveLicenseeVer;
		Prefab->ArEngineNetVer = this->ArEngineNetVer;
		Prefab->ArGameNetVer = this->ArGameNetVer;
	}
};

UDreamUIPrefabHelperObject* UDreamUIPrefab::GetPrefabHelperObject()
{
	EnsureInstanceObjects();
	return PrefabHelperObject;
}

void UDreamUIPrefab::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	BinaryDataForBuild.Empty();
	ReferenceAssetListForBuild.Empty();
	ReferenceClassListForBuild.Empty();
	ReferenceNameListForBuild.Empty();
	ReferenceTextListForBuild.Empty();
	if (PrefabVersion < static_cast<uint16>(EDreamUIPrefabVersion::BuiltinFArchive))
	{
		UE_LOG(DreamGUI, Error,
			TEXT("Cannot cook prefab '%s' with unsupported format version %u. Open and upgrade the asset before cooking."),
			*GetPathName(), PrefabVersion);
		return;
	}

	EnsureInstanceObjects();
	if (!IsValid(PrefabHelperObject) || !IsValid(PrefabHelperObject->LoadedRootWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("Cannot cook prefab '%s' because its editable hierarchy could not be loaded."), *GetPathName());
		return;
	}

	//serialize to runtime data
	{
		FDreamUIPrefabVersionScope VersionProtect(this);
		//check override parameter. although parameter is refreshed when sub prefab change, but what if sub prefab is changed outside of editor?
		bool AnythingChange = false;
		for (auto& KeyValue : PrefabHelperObject->SubPrefabMap)
		{
			if (KeyValue.Value.CheckParameters())
			{
				AnythingChange = true;
			}
		}
		if (AnythingChange)
		{
			UE_LOG(DreamGUI, Log, TEXT("[%s].%d Something changed in sub prefab override parameter, refresh it. Prefab: '%s'."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetPathName()));
		}

		TMap<UObject*, FGuid> MapObjectToGuid;
		for (auto& KeyValue : PrefabHelperObject->MapGuidToObject)
		{
			if (IsValid(KeyValue.Value))
			{
				MapObjectToGuid.Add(KeyValue.Value, KeyValue.Key);
			}
		}
		const bool bCookSerializationSucceeded = this->SavePrefab(PrefabHelperObject->LoadedRootWidget
			, MapObjectToGuid, PrefabHelperObject->SubPrefabMap
			, false
		);
		PrefabHelperObject->MapGuidToObject.Empty();
		for (auto KeyValue : MapObjectToGuid)
		{
			PrefabHelperObject->MapGuidToObject.Add(KeyValue.Value, KeyValue.Key);
		}
		if (!bCookSerializationSucceeded || BinaryDataForBuild.IsEmpty())
		{
			BinaryDataForBuild.Empty();
			ReferenceAssetListForBuild.Empty();
			ReferenceClassListForBuild.Empty();
			ReferenceNameListForBuild.Empty();
			ReferenceTextListForBuild.Empty();
			UE_LOG(DreamGUI, Error, TEXT("Cook serialization failed for prefab '%s'; no runtime prefab data was produced."), *GetPathName());
			return;
		}
	}
}
void UDreamUIPrefab::WillNeverCacheCookedPlatformDataAgain()
{
	if (PrefabVersion >= (uint16)EDreamUIPrefabVersion::BuiltinFArchive)
	{
		BinaryDataForBuild.Empty();
		ReferenceAssetListForBuild.Empty();
		ReferenceClassListForBuild.Empty();
		ReferenceNameListForBuild.Empty();
		ReferenceTextListForBuild.Empty();
	}
}
void UDreamUIPrefab::ClearCachedCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	if (PrefabVersion >= (uint16)EDreamUIPrefabVersion::BuiltinFArchive)
	{
		BinaryDataForBuild.Empty();
		ReferenceAssetListForBuild.Empty();
		ReferenceClassListForBuild.Empty();
		ReferenceNameListForBuild.Empty();
		ReferenceTextListForBuild.Empty();
	}
}

void UDreamUIPrefab::PostInitProperties()
{
	Super::PostInitProperties();
}
void UDreamUIPrefab::PostCDOContruct()
{
	Super::PostCDOContruct();
}

void UDreamUIPrefab::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);
	if (this->GetName().Contains(TEXT("SKEL_")) || this->GetName().Contains(TEXT("TRASH_")))
		return;
	if (OldOuter->IsA(UPackage::StaticClass()))//is asset
	{
		// Moving to another folder is a rename into a new package under the SAME name, and the root
		// widget's display name is derived from the asset name alone -- so a move has nothing to
		// sync. Skipping it matters because the sync is anything but cheap: it builds an editor
		// world, deserializes the whole prefab and every sub-prefab, rewrites the asset, then tears
		// the world down. Paid once per moved asset, that is what makes dragging prefabs between
		// folders look like the Content Browser has hung, and it rewrote the asset's bytes for a
		// change nobody asked for.
		if (OldName != GetFName())
		{
			SetRootWidgetNameFromPrefab();
		}
	}
	if (IsValid(PrefabHelperObject))
	{
		PrefabHelperObject->ConditionalBeginDestroy();
		PrefabHelperObject = nullptr;
	}
	ClearPrefabInstanceScene();
}
void UDreamUIPrefab::PreDuplicate(FObjectDuplicationParameters& DupParams)
{
	Super::PreDuplicate(DupParams);
}

void UDreamUIPrefab::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	if (this->GetName().Contains(TEXT("SKEL_")) || this->GetName().Contains(TEXT("TRASH_")))
		return;
	if (GetOuter()->IsA(UPackage::StaticClass()))//is asset
	{
		SetRootWidgetNameFromPrefab();
		// A duplicate must not keep pointing at the original's behaviour Blueprint. Variants are made
		// by duplicating -- card faces, seat plates, panel variants -- so sharing the class means
		// editing the copy's script silently edits the original's, and nothing about the copy looks
		// shared. Cleared rather than duplicated so the existing on-demand BP_<PrefabName> path
		// creates a fresh one when the author first opens the behaviour, instead of inventing an
		// asset for every copy that never needed a script.
		BehaviourClass = nullptr;
	}
}

void UDreamUIPrefab::PostLoad()
{
	Super::PostLoad();
}

void UDreamUIPrefab::BeginDestroy()
{
	if (this->GetName() == TEXT("NewDreamUIPrefab"))
	{
		UE_LOG(DreamGUI, Error, TEXT(""));
	}
#if WITH_EDITOR
	if (IsValid(PrefabHelperObject))
	{
		if (IsValid(PrefabHelperObject->LoadedRootWidget))
		{
			PrefabHelperObject->LoadedRootWidget->DestroyWidget();
			PrefabHelperObject->LoadedRootWidget = nullptr;
		}
		PrefabHelperObject->ConditionalBeginDestroy();
	}
	if (PrefabInstanceScene.IsValid())
	{
		PrefabInstanceScene.Reset();
	}
#endif
	Super::BeginDestroy();
}

void UDreamUIPrefab::FinishDestroy()
{
	Super::FinishDestroy();
}

void UDreamUIPrefab::PostEditUndo()
{
	Super::PostEditUndo();
	EnsureInstanceObjects();
}

void UDreamUIPrefab::PreSave(FObjectPreSaveContext SaveContext)
{
	UObject::PreSave(SaveContext);
	if (IsValid(PrefabHelperObject) && IsValid(PrefabHelperObject->LoadedRootWidget))
	{
		PrefabHelperObject->SavePrefab();
	}
}

#endif

UDreamWidget* UDreamUIPrefab::LoadPrefab(UWorld* InWorld, UDreamWidget* InParent, const TFunction<void(UDreamWidget*)>& InCallbackBeforeAwake, bool SetRelativeTransformToIdentity)
{
	UDreamWidget* LoadedRootWidget = nullptr;
	if (InWorld)
	{
		switch ((EDreamUIPrefabVersion)PrefabVersion)
		{
		default:
		case EDreamUIPrefabVersion::FTextAsReference:
		case EDreamUIPrefabVersion::NewObjectOnNestedPrefab:
		{
			LoadedRootWidget = LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::LoadPrefab(InWorld, InWorld, this, InParent, SetRelativeTransformToIdentity, InCallbackBeforeAwake);
		}
		break;
		}
	}
	return LoadedRootWidget;
}

UDreamWidget* UDreamUIPrefab::LoadPrefab(UObject* WorldContextObject, UDreamWidget* InParent, const FDreamUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake, bool SetRelativeTransformToIdentity)
{
	if (auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		return LoadPrefab(World, InParent, [&InCallbackBeforeAwake](UDreamWidget* RootWidget) {
			InCallbackBeforeAwake.ExecuteIfBound(RootWidget);
			}, SetRelativeTransformToIdentity);
	}
	return nullptr;
}
UDreamWidget* UDreamUIPrefab::LoadPrefabWithTransform(UObject* WorldContextObject, UDreamWidget* InParent, FVector Location, FRotator Rotation, FVector Scale, const FDreamUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake)
{
	UDreamWidget* LoadedRootWidget = nullptr;
	if (auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		auto CallbackBeforeAwake = [&InCallbackBeforeAwake](UDreamWidget* RootWidget) {
			InCallbackBeforeAwake.ExecuteIfBound(RootWidget);
			};
		switch ((EDreamUIPrefabVersion)PrefabVersion)
		{
		default:
		case EDreamUIPrefabVersion::FTextAsReference:
		case EDreamUIPrefabVersion::NewObjectOnNestedPrefab:
		{
			LoadedRootWidget = LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::LoadPrefab(World, World, this, InParent, Location, Rotation.Quaternion(), Scale, CallbackBeforeAwake);
		}
		break;
		}
	}
	return LoadedRootWidget;
}
UDreamWidget* UDreamUIPrefab::LoadPrefabWithReplacement(UObject* WorldContextObject, UDreamWidget* InParent, const TMap<UObject*, UObject*>& InReplaceAssetMap, const TMap<UClass*, UClass*>& InReplaceClassMap, const FDreamUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake)
{
	UDreamWidget* LoadedRootWidget = nullptr;
	if (auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		TSet<TTuple<int, UObject*>> ReplacedAssets;
		TSet<TTuple<int, UClass*>> ReplacedClasses;
		if (InReplaceAssetMap.Num() > 0)
		{
			auto& List =
#if WITH_EDITOR
				ReferenceAssetList;
#else
				ReferenceAssetListForBuild;
#endif
			for (int i = 0; i < List.Num(); i++)
			{
				if (auto ReplaceAssetPtr = InReplaceAssetMap.Find(List[i]))
				{
					ReplacedAssets.Add({ i, List[i] });
					List[i] = *ReplaceAssetPtr;
				}
			}
		}
		if (InReplaceClassMap.Num() > 0)
		{
			auto& List =
#if WITH_EDITOR
				ReferenceClassList;
#else
				ReferenceClassListForBuild;
#endif
			for (int i = 0; i < List.Num(); i++)
			{
				if (auto ReplaceClassPtr = InReplaceClassMap.Find(List[i]))
				{
					ReplacedClasses.Add({ i, List[i] });
					List[i] = *ReplaceClassPtr;
				}
			}
		}
		auto CallbackBeforeAwake = [&InCallbackBeforeAwake](UDreamWidget* RootWidget) {
			InCallbackBeforeAwake.ExecuteIfBound(RootWidget);
			};
		switch ((EDreamUIPrefabVersion)PrefabVersion)
		{
		default:
		case EDreamUIPrefabVersion::FTextAsReference:
		case EDreamUIPrefabVersion::NewObjectOnNestedPrefab:
		{
			LoadedRootWidget = LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::LoadPrefab(World, World, this, InParent, false, CallbackBeforeAwake);
		}
		break;
		}
		if (ReplacedAssets.Num() > 0)
		{
			auto& List =
#if WITH_EDITOR
				ReferenceAssetList;
#else
				ReferenceAssetListForBuild;
#endif
			for (auto& Item : ReplacedAssets)
			{
				List[Item.Key] = Item.Value;
			}
		}
		if (ReplacedClasses.Num() > 0)
		{
			auto& List =
#if WITH_EDITOR
				ReferenceClassList;
#else
				ReferenceClassListForBuild;
#endif
			for (auto& Item : ReplacedClasses)
			{
				List[Item.Key] = Item.Value;
			}
		}
	}
	return LoadedRootWidget;
}
UDreamWidget* UDreamUIPrefab::LoadPrefabWithTransform(UObject* WorldContextObject, UDreamWidget* InParent, FVector Location, FQuat Rotation, FVector Scale, const TFunction<void(UDreamWidget*)>& InCallbackBeforeAwake)
{
	UDreamWidget* LoadedRootWidget = nullptr;
	if (auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		switch ((EDreamUIPrefabVersion)PrefabVersion)
		{
		default:
		case EDreamUIPrefabVersion::FTextAsReference:
		case EDreamUIPrefabVersion::NewObjectOnNestedPrefab:
		{
			LoadedRootWidget = LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::LoadPrefab(World, World, this, InParent, Location, Rotation, Scale, InCallbackBeforeAwake);
		}
		break;
		}
	}
	return LoadedRootWidget;
}

#if WITH_EDITOR
UDreamWidget* UDreamUIPrefab::LoadPrefabWithExistingObjects(UWorld* InWorld, UObject* InOuter, UDreamWidget* InParent
	, TMap<FGuid, TObjectPtr<UObject>>& InOutMapGuidToObject, TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& OutSubPrefabMap
)
{
	UDreamWidget* LoadedRootWidget = nullptr;
	switch ((EDreamUIPrefabVersion)PrefabVersion)
	{
	default:
	case EDreamUIPrefabVersion::FTextAsReference:
	case EDreamUIPrefabVersion::NewObjectOnNestedPrefab:
	{
		LoadedRootWidget = LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::LoadPrefabWithExistingObjects(InWorld, InOuter, this, InParent
			, InOutMapGuidToObject, OutSubPrefabMap
		);
	}
	break;
	}
	return LoadedRootWidget;
}

bool UDreamUIPrefab::IsPrefabBelongsToThisSubPrefab(UDreamUIPrefab* InPrefab, bool InRecursive)
{
	if (!IsValid(InPrefab) || this == InPrefab)return false;
	TSet<const UDreamUIPrefab*> VisitedPrefabs;
	TFunction<bool(UDreamUIPrefab*)> ContainsPrefab = [&](UDreamUIPrefab* ParentPrefab)
	{
		if (!IsValid(ParentPrefab) || VisitedPrefabs.Contains(ParentPrefab))return false;
		VisitedPrefabs.Add(ParentPrefab);
		ParentPrefab->EnsureInstanceObjects();
		if (!IsValid(ParentPrefab->PrefabHelperObject))return false;
		for (const TPair<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& Pair : ParentPrefab->PrefabHelperObject->SubPrefabMap)
		{
			if (Pair.Value.PrefabAsset == InPrefab)
			{
				return true;
			}
		}
		if (InRecursive)
		{
			for (const TPair<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& Pair : ParentPrefab->PrefabHelperObject->SubPrefabMap)
			{
				if (ContainsPrefab(Pair.Value.PrefabAsset.Get()))return true;
			}
		}
		return false;
	};
	return ContainsPrefab(this);
}

void UDreamUIPrefab::CopyDataTo(UDreamUIPrefab* TargetPrefab)
{
	TargetPrefab->BehaviourClass = this->BehaviourClass;
	TargetPrefab->ReferenceAssetList = this->ReferenceAssetList;
	TargetPrefab->ReferenceClassList = this->ReferenceClassList;
	TargetPrefab->ReferenceNameList = this->ReferenceNameList;
	TargetPrefab->ReferenceTextList = this->ReferenceTextList;
	TargetPrefab->BinaryData = this->BinaryData;
	TargetPrefab->PrefabVersion = this->PrefabVersion;
	TargetPrefab->PrefabSchemaVersion = this->PrefabSchemaVersion;
	TargetPrefab->EngineMajorVersion = this->EngineMajorVersion;
	TargetPrefab->EngineMinorVersion = this->EngineMinorVersion;
	TargetPrefab->EnginePatchVersion = this->EnginePatchVersion;
	TargetPrefab->ArchiveVersion = this->ArchiveVersion;
	TargetPrefab->ArchiveLicenseeVer = this->ArchiveLicenseeVer;
	TargetPrefab->ArEngineNetVer = this->ArEngineNetVer;
	TargetPrefab->ArGameNetVer = this->ArGameNetVer;
	TargetPrefab->PrefabDataForPrefabEditor = this->PrefabDataForPrefabEditor;
}

FString UDreamUIPrefab::GenerateOverallVersionMD5()
{
	struct LOCAL
	{
		static void CollectOverallPrefab(UDreamUIPrefab* Parent, TArray<UDreamUIPrefab*>& Collection, TSet<const UDreamUIPrefab*>& VisitedPrefabs)
		{
			if (!IsValid(Parent) || VisitedPrefabs.Contains(Parent))return;
			VisitedPrefabs.Add(Parent);
			Collection.Add(Parent);
			for (auto& Item : Parent->ReferenceAssetList)
			{
				if (auto SubPrefab = Cast<UDreamUIPrefab>(Item))
				{
					CollectOverallPrefab(SubPrefab, Collection, VisitedPrefabs);
				}
			}
		}
	};
	TArray<UDreamUIPrefab*> Collection;
	TSet<const UDreamUIPrefab*> VisitedPrefabs;
	LOCAL::CollectOverallPrefab(this, Collection, VisitedPrefabs);
	Collection.Sort([](const UDreamUIPrefab& A, const UDreamUIPrefab& B) {
		return A.CreateTime > B.CreateTime;
		});

	FString CreateTimeOverall;
	for (auto& Item : Collection)
	{
		CreateTimeOverall += Item->CreateTime.ToIso8601();
	}
	return FDreamUIUtils::GetMD5String(FDreamUIUtils::GetMD5(CreateTimeOverall));
}

FDreamUIPrefabSchemaMigrationReport UDreamUIPrefab::PreviewSchemaUpgrade()
{
	UDreamUIPrefab* PreviewPrefab = DuplicateObject<UDreamUIPrefab>(this, GetTransientPackage());
	if (!IsValid(PreviewPrefab))
	{
		FDreamUIPrefabSchemaMigrationReport Report;
		Report.FromVersion = PrefabSchemaVersion;
		Report.Errors.Add(TEXT("Could not create an isolated prefab copy for migration preview."));
		return Report;
	}

	UDreamUIPrefabHelperObject* SourceHelper = GetPrefabHelperObject();
	return PreviewPrefab->EvaluateSchemaMigration(
		IsValid(SourceHelper) ? SourceHelper->LoadedRootWidget : nullptr, false);
}

FDreamUIPrefabSchemaMigrationReport UDreamUIPrefab::UpgradeSchema()
{
	UDreamUIPrefabHelperObject* Helper = GetPrefabHelperObject();
	if (!IsValid(Helper) || !IsValid(Helper->LoadedRootWidget))
	{
		FDreamUIPrefabSchemaMigrationReport Report;
		Report.FromVersion = PrefabSchemaVersion;
		Report.Errors.Add(TEXT("Prefab has no editable hierarchy to upgrade."));
		return Report;
	}

	Modify();
	FDreamUIPrefabSchemaMigrationReport Report = ApplySchemaMigration(Helper->LoadedRootWidget);
	if (!Report.HasErrors() && Report.HasChanges() && !Helper->SavePrefab())
	{
		Report.Errors.Add(TEXT("Migration succeeded in memory, but the prefab could not be saved."));
	}
	return Report;
}

bool UDreamUIPrefab::SavePrefab(UDreamWidget* RootWidget
	, TMap<UObject*, FGuid>& InOutMapObjectToGuid, TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& InSubPrefabMap
	, bool InForEditorOrRuntimeUse
)
{
	return LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::SavePrefab(RootWidget, this
		, InOutMapObjectToGuid, InSubPrefabMap
		, InForEditorOrRuntimeUse
	);
}

void UDreamUIPrefab::RecreatePrefab()
{
	TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
	TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> SubPrefabMap;
	auto RootWidget = this->LoadPrefabWithExistingObjects(GetPrefabInstanceScene()->GetWorld(), GetPrefabInstanceScene()->GetWorld(), nullptr
		, MapGuidToObject, SubPrefabMap
	);
	TMap<UObject*, FGuid> MapObjectToGuid;
	for (auto KeyValue : MapGuidToObject)
	{
		MapObjectToGuid.Add(KeyValue.Value, KeyValue.Key);
	}
	this->SavePrefab(RootWidget, MapObjectToGuid, SubPrefabMap);
	this->EnsureInstanceObjects();
}

UDreamWidget* UDreamUIPrefab::LoadPrefabInEditor(UWorld* InWorld, UObject* InOuter, UDreamWidget* InParent)
{
	UDreamWidget* LoadedRootWidget = nullptr;
	switch ((EDreamUIPrefabVersion)PrefabVersion)
	{
	default:
	case EDreamUIPrefabVersion::FTextAsReference:
	case EDreamUIPrefabVersion::NewObjectOnNestedPrefab:
	{
		TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
		TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> SubPrefabMap;
		LoadedRootWidget = LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::LoadPrefabWithExistingObjects(InWorld, InOuter, this
			, InParent, MapGuidToObject, SubPrefabMap
		);
	}
	break;
	}
	return LoadedRootWidget;
}

UDreamWidget* UDreamUIPrefab::LoadPrefabInEditor(UWorld* InWorld, UObject* InOuter, UDreamWidget* InParent, TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& OutSubPrefabMap, TMap<FGuid, TObjectPtr<UObject>>& OutMapGuidToObject, bool SetRelativeTransformToIdentity)
{
	UDreamWidget* LoadedRootWidget = nullptr;
	switch ((EDreamUIPrefabVersion)PrefabVersion)
	{
	default:
	case EDreamUIPrefabVersion::FTextAsReference:
	case EDreamUIPrefabVersion::NewObjectOnNestedPrefab:
	{
		LoadedRootWidget = LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::LoadPrefabWithExistingObjects(InWorld, InOuter, this
			, InParent, OutMapGuidToObject, OutSubPrefabMap
		);
	}
	break;
	}
	return LoadedRootWidget;
}

#endif

#undef LOCTEXT_NAMESPACE
