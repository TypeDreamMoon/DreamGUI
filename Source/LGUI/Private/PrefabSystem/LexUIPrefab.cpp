// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/LexUIPrefab.h"
#include "LGUI.h"
#if WITH_EDITOR
#include "Core/Actor/LexWidgetActor.h"
#endif

#include LGUIPREFAB_SERIALIZER_NEWEST_INCLUDE
#include "Utils/LexUIUtils.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "Engine/Engine.h"
#include "UObject/ObjectSaveContext.h"

#define LOCTEXT_NAMESPACE "LGUIPrefab"


FLexUISubPrefabData::FLexUISubPrefabData()
{
#if WITH_EDITORONLY_DATA
	EditorIdentifyColor = FLinearColor::MakeRandomColor();
#endif
}
void FLexUISubPrefabData::AddMemberProperty(UObject* InObject, FName InPropertyName)
{
	auto Index = ObjectOverrideParameterArray.IndexOfByPredicate([=](const FLexUIPrefabOverrideParameterData& Item) {
		return Item.Object == InObject;
		});
	if (Index == INDEX_NONE)
	{
		FLexUIPrefabOverrideParameterData DataItem;
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

void FLexUISubPrefabData::AddMemberProperty(UObject* InObject, const TArray<FName>& InPropertyNames)
{
	auto Index = ObjectOverrideParameterArray.IndexOfByPredicate([=](const FLexUIPrefabOverrideParameterData& Item) {
		return Item.Object == InObject;
		});
	if (Index == INDEX_NONE)
	{
		FLexUIPrefabOverrideParameterData DataItem;
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

void FLexUISubPrefabData::RemoveMemberProperty(UObject* InObject, FName InPropertyName)
{
	auto Index = ObjectOverrideParameterArray.IndexOfByPredicate([=](const FLexUIPrefabOverrideParameterData& Item) {
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

void FLexUISubPrefabData::RemoveMemberProperty(UObject* InObject)
{
	auto Index = ObjectOverrideParameterArray.IndexOfByPredicate([=](const FLexUIPrefabOverrideParameterData& Item) {
		return Item.Object == InObject;
		});
	if (Index != INDEX_NONE)
	{
		ObjectOverrideParameterArray.RemoveAt(Index);
	}
}

bool FLexUISubPrefabData::CheckParameters()
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

ULexUIPrefab::ULexUIPrefab()
{

}

#if WITH_EDITOR

void ULexUIPrefab::SetRootActorNameFromPrefab()
{
	if (GetPrefabHelperObject() && PrefabHelperObject->LoadedRootActor)
	{
		auto RootWidgetDisplayName = this->GetName();
		if (RootWidgetDisplayName.RemoveFromStart(TEXT("Default__")))
		{
			UE_LOG(LGUI, Display, TEXT("[%s] Rename Default__"), ANSI_TO_TCHAR(__FUNCTION__));
		}
		if (RootWidgetDisplayName.RemoveFromStart(TEXT("REINST__")))
		{
			UE_LOG(LGUI, Display, TEXT("[%s] Rename REINST__"), ANSI_TO_TCHAR(__FUNCTION__));
		}
		auto FindIndex = RootWidgetDisplayName.Find("_C", ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (FindIndex != INDEX_NONE)
		{
			RootWidgetDisplayName = RootWidgetDisplayName.Left(FindIndex);
		}
		PrefabHelperObject->LoadedRootActor->SetActorLabel(RootWidgetDisplayName);
		PrefabHelperObject->SavePrefab();
	}
}

FLexUIPrefabInstanceScene* ULexUIPrefab::GetPrefabInstanceScene()
{
	if (!PrefabInstanceScene)PrefabInstanceScene = MakeUnique<FLexUIPrefabInstanceScene>(
				FLexUIPrefabInstanceScene::ConstructionValues()
				.AllowAudioPlayback(true)
				.ShouldSimulatePhysics(false)
				.SetEditor(true)
				);
	return PrefabInstanceScene.Get();
}

void ULexUIPrefab::ClearPrefabInstanceScene()
{
	if (PrefabInstanceScene.IsValid())
	{
		PrefabInstanceScene.Reset();
	}
}

void ULexUIPrefab::EnsureInstanceObjects()
{
	if (PrefabVersion >= (uint16)ELexUIPrefabVersion::BuildinFArchive)
	{
		if (!IsValid(PrefabHelperObject))
		{
			PrefabHelperObject = NewObject<ULexUIPrefabHelperObject>(this, "PrefabHelper");
			PrefabHelperObject->Init(this, GetPrefabInstanceScene());
		}
	}
}

struct FLexUIPrefabVersionScope
{
public:
	uint16 PrefabVersion = 0;
	uint16 EngineMajorVersion = 0;
	uint16 EngineMinorVersion = 0;
	uint16 EnginePatchVersion = 0;
	int32 ArchiveVersion = 0;
	int32 ArchiveLicenseeVer = 0;
	uint32 ArEngineNetVer = 0;
	uint32 ArGameNetVer = 0;

	ULexUIPrefab* Prefab = nullptr;
	FLexUIPrefabVersionScope(ULexUIPrefab* InPrefab)
	{
		Prefab = InPrefab;
		this->EngineMajorVersion = Prefab->EngineMajorVersion;
		this->EngineMinorVersion = Prefab->EngineMinorVersion;
		this->PrefabVersion = Prefab->PrefabVersion;
		this->ArchiveVersion = Prefab->ArchiveVersion;
		this->ArchiveLicenseeVer = Prefab->ArchiveLicenseeVer;
		this->ArEngineNetVer = Prefab->ArEngineNetVer;
		this->ArGameNetVer = Prefab->ArGameNetVer;
	}
	~FLexUIPrefabVersionScope()
	{
		Prefab->EngineMajorVersion = this->EngineMajorVersion;
		Prefab->EngineMinorVersion = this->EngineMinorVersion;
		Prefab->PrefabVersion = this->PrefabVersion;
		Prefab->ArchiveVersion = this->ArchiveVersion;
		Prefab->ArchiveLicenseeVer = this->ArchiveLicenseeVer;
		Prefab->ArEngineNetVer = this->ArEngineNetVer;
		Prefab->ArGameNetVer = this->ArGameNetVer;
	}
};

ULexUIPrefabHelperObject* ULexUIPrefab::GetPrefabHelperObject()
{
	EnsureInstanceObjects();
	return PrefabHelperObject;
}

void ULexUIPrefab::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	BinaryDataForBuild.Empty();
	if (!IsValid(PrefabHelperObject) || !IsValid(PrefabHelperObject->LoadedRootActor))
	{
		UE_LOG(LGUI, Log, TEXT("[%s].%d AgentObjects not valid, recreate it! prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetPathName()));
		EnsureInstanceObjects();
	}

	//serialize to runtime data
	{
		FLexUIPrefabVersionScope VersionProtect(this);
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
			UE_LOG(LGUI, Log, TEXT("[%s].%d Something changed in sub prefab override parameter, refresh it. Prefab: '%s'."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(this->GetPathName()));
		}

		TMap<UObject*, FGuid> MapObjectToGuid;
		for (auto& KeyValue : PrefabHelperObject->MapGuidToObject)
		{
			if (IsValid(KeyValue.Value))
			{
				MapObjectToGuid.Add(KeyValue.Value, KeyValue.Key);
			}
		}
		this->SavePrefab(PrefabHelperObject->LoadedRootActor
			, MapObjectToGuid, PrefabHelperObject->SubPrefabMap
			, false
		);
		PrefabHelperObject->MapGuidToObject.Empty();
		for (auto KeyValue : MapObjectToGuid)
		{
			PrefabHelperObject->MapGuidToObject.Add(KeyValue.Value, KeyValue.Key);
		}
	}
}
void ULexUIPrefab::WillNeverCacheCookedPlatformDataAgain()
{
	if (PrefabVersion >= (uint16)ELexUIPrefabVersion::BuildinFArchive)
	{
		BinaryDataForBuild.Empty();
		ReferenceAssetListForBuild.Empty();
		ReferenceClassListForBuild.Empty();
		ReferenceNameListForBuild.Empty();
	}
}
void ULexUIPrefab::ClearCachedCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	if (PrefabVersion >= (uint16)ELexUIPrefabVersion::BuildinFArchive)
	{
		BinaryDataForBuild.Empty();
		ReferenceAssetListForBuild.Empty();
		ReferenceClassListForBuild.Empty();
		ReferenceNameListForBuild.Empty();
	}
}

void ULexUIPrefab::PostInitProperties()
{
	Super::PostInitProperties();
}
void ULexUIPrefab::PostCDOContruct()
{
	Super::PostCDOContruct();
}

void ULexUIPrefab::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);
	if (this->GetName().Contains(TEXT("SKEL_")) || this->GetName().Contains(TEXT("TRASH_")))
		return;
	if (OldOuter->IsA(UPackage::StaticClass()))//is asset
	{
		SetRootActorNameFromPrefab();
	}
}
void ULexUIPrefab::PreDuplicate(FObjectDuplicationParameters& DupParams)
{
	Super::PreDuplicate(DupParams);
}

void ULexUIPrefab::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	if (this->GetName().Contains(TEXT("SKEL_")) || this->GetName().Contains(TEXT("TRASH_")))
		return;
	if (GetOuter()->IsA(UPackage::StaticClass()))//is asset
	{
		SetRootActorNameFromPrefab();
	}
}

void ULexUIPrefab::PostLoad()
{
	Super::PostLoad();
}

void ULexUIPrefab::BeginDestroy()
{
	if (this->GetName() == TEXT("NewLexUIPrefab"))
	{
		UE_LOG(LGUI, Error, TEXT(""));
	}
#if WITH_EDITOR
	if (IsValid(PrefabHelperObject))
	{
		PrefabHelperObject->ConditionalBeginDestroy();
	}
	if (PrefabInstanceScene.IsValid())
	{
		PrefabInstanceScene.Reset();
	}
#endif
	Super::BeginDestroy();
}

void ULexUIPrefab::FinishDestroy()
{
	Super::FinishDestroy();
}

void ULexUIPrefab::PostEditUndo()
{
	Super::PostEditUndo();
	EnsureInstanceObjects();
}

void ULexUIPrefab::PreSave(FObjectPreSaveContext SaveContext)
{
	UObject::PreSave(SaveContext);
	if (IsValid(PrefabHelperObject))
	{
		PrefabHelperObject->SavePrefab();
	}
}

#endif

AActor* ULexUIPrefab::LoadPrefab(UWorld* InWorld, USceneComponent* InParent, bool SetRelativeTransformToIdentity, const TFunction<void(AActor*)>& InCallbackBeforeAwake)
{
	AActor* LoadedRootActor = nullptr;
	if (InWorld)
	{
		switch ((ELexUIPrefabVersion)PrefabVersion)
		{
		default:
		case ELexUIPrefabVersion::NewObjectOnNestedPrefab:
		{
			LoadedRootActor = LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::LoadPrefab(InWorld, this, InParent, SetRelativeTransformToIdentity, InCallbackBeforeAwake);
		}
		break;
		}
	}
	return LoadedRootActor;
}

AActor* ULexUIPrefab::LoadPrefab(UObject* WorldContextObject, USceneComponent* InParent, const FLexUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake, bool SetRelativeTransformToIdentity)
{
	auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (World)
	{
		return LoadPrefab(World, InParent, SetRelativeTransformToIdentity, [&InCallbackBeforeAwake](AActor* RootActor) {
			InCallbackBeforeAwake.ExecuteIfBound(RootActor);
			});
	}
	return nullptr;
}
AActor* ULexUIPrefab::LoadPrefabWithTransform(UObject* WorldContextObject, USceneComponent* InParent, FVector Location, FRotator Rotation, FVector Scale, const FLexUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake)
{
	AActor* LoadedRootActor = nullptr;
	auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (World)
	{
		auto CallbackBeforeAwake = [&InCallbackBeforeAwake](AActor* RootActor) {
			InCallbackBeforeAwake.ExecuteIfBound(RootActor);
			};
		switch ((ELexUIPrefabVersion)PrefabVersion)
		{
		default:
		case ELexUIPrefabVersion::NewObjectOnNestedPrefab:
		{
			LoadedRootActor = LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::LoadPrefab(World, this, InParent, Location, Rotation.Quaternion(), Scale, CallbackBeforeAwake);
		}
		break;
		}
	}
	return LoadedRootActor;
}
AActor* ULexUIPrefab::LoadPrefabWithReplacement(UObject* WorldContextObject, USceneComponent* InParent, const TMap<UObject*, UObject*>& InReplaceAssetMap, const TMap<UClass*, UClass*>& InReplaceClassMap, const FLexUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake)
{
	AActor* LoadedRootActor = nullptr;
	auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (World)
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
		auto CallbackBeforeAwake = [&InCallbackBeforeAwake](AActor* RootActor) {
			InCallbackBeforeAwake.ExecuteIfBound(RootActor);
			};
		switch ((ELexUIPrefabVersion)PrefabVersion)
		{
		default:
		case ELexUIPrefabVersion::NewObjectOnNestedPrefab:
		{
			LoadedRootActor = LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::LoadPrefab(World, this, InParent, false, CallbackBeforeAwake);
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
	return LoadedRootActor;
}
AActor* ULexUIPrefab::LoadPrefabWithTransform(UObject* WorldContextObject, USceneComponent* InParent, FVector Location, FQuat Rotation, FVector Scale, const TFunction<void(AActor*)>& InCallbackBeforeAwake)
{
	AActor* LoadedRootActor = nullptr;
	auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (World)
	{
		switch ((ELexUIPrefabVersion)PrefabVersion)
		{
		default:
		case ELexUIPrefabVersion::NewObjectOnNestedPrefab:
		{
			LoadedRootActor = LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::LoadPrefab(World, this, InParent, Location, Rotation, Scale, InCallbackBeforeAwake);
		}
		break;
		}
	}
	return LoadedRootActor;
}

#if WITH_EDITOR
AActor* ULexUIPrefab::LoadPrefabWithExistingObjects(UWorld* InWorld, USceneComponent* InParent
	, TMap<FGuid, TObjectPtr<UObject>>& InOutMapGuidToObject, TMap<TObjectPtr<AActor>, FLexUISubPrefabData>& OutSubPrefabMap
)
{
	AActor* LoadedRootActor = nullptr;
	switch ((ELexUIPrefabVersion)PrefabVersion)
	{
	default:
	case ELexUIPrefabVersion::NewObjectOnNestedPrefab:
	{
		LoadedRootActor = LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::LoadPrefabWithExistingObjects(InWorld, this, InParent
			, InOutMapGuidToObject, OutSubPrefabMap
		);
	}
	break;
	}
	return LoadedRootActor;
}

bool ULexUIPrefab::IsPrefabBelongsToThisSubPrefab(ULexUIPrefab* InPrefab, bool InRecursive)
{
	EnsureInstanceObjects();
	if (!PrefabHelperObject)return false;
	if (this == InPrefab)return false;
	for (auto& KeyValue : PrefabHelperObject->SubPrefabMap)
	{
		if (KeyValue.Value.PrefabAsset == InPrefab)
		{
			return true;
		}
	}
	if (InRecursive)
	{
		for (auto& KeyValue : PrefabHelperObject->SubPrefabMap)
		{
			if (KeyValue.Value.PrefabAsset->IsPrefabBelongsToThisSubPrefab(InPrefab, InRecursive))
			{
				return true;
			}
		}
	}
	return false;
}

void ULexUIPrefab::CopyDataTo(ULexUIPrefab* TargetPrefab)
{
	TargetPrefab->ReferenceAssetList = this->ReferenceAssetList;
	TargetPrefab->ReferenceClassList = this->ReferenceClassList;
	TargetPrefab->ReferenceNameList = this->ReferenceNameList;
	TargetPrefab->BinaryData = this->BinaryData;
	TargetPrefab->PrefabVersion = this->PrefabVersion;
	TargetPrefab->EngineMajorVersion = this->EngineMajorVersion;
	TargetPrefab->EngineMinorVersion = this->EngineMinorVersion;
	TargetPrefab->EnginePatchVersion = this->EnginePatchVersion;
	TargetPrefab->ArchiveVersion = this->ArchiveVersion;
	TargetPrefab->ArchiveLicenseeVer = this->ArchiveLicenseeVer;
	TargetPrefab->ArEngineNetVer = this->ArEngineNetVer;
	TargetPrefab->ArGameNetVer = this->ArGameNetVer;
	TargetPrefab->PrefabDataForPrefabEditor = this->PrefabDataForPrefabEditor;
}

FString ULexUIPrefab::GenerateOverallVersionMD5()
{
	struct LOCAL
	{
		static void CollectOverallPrefab(ULexUIPrefab* Parent, TArray<ULexUIPrefab*>& Collection)
		{
			Collection.Add(Parent);
			for (auto& Item : Parent->ReferenceAssetList)
			{
				if (auto SubPrefab = Cast<ULexUIPrefab>(Item))
				{
					CollectOverallPrefab(SubPrefab, Collection);
				}
			}
		}
	};
	TArray<ULexUIPrefab*> Collection;
	LOCAL::CollectOverallPrefab(this, Collection);
	Collection.Sort([](const ULexUIPrefab& A, const ULexUIPrefab& B) {
		return A.CreateTime > B.CreateTime;
		});

	FString CreateTimeOverall;
	for (auto& Item : Collection)
	{
		CreateTimeOverall += Item->CreateTime.ToIso8601();
	}
	return FLexUIUtils::GetMD5String(FLexUIUtils::GetMD5(CreateTimeOverall));
}

bool ULexUIPrefab::SavePrefab(AActor* RootActor
	, TMap<UObject*, FGuid>& InOutMapObjectToGuid, TMap<TObjectPtr<AActor>, FLexUISubPrefabData>& InSubPrefabMap
	, bool InForEditorOrRuntimeUse
)
{
	return LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::SavePrefab(RootActor, this
		, InOutMapObjectToGuid, InSubPrefabMap
		, InForEditorOrRuntimeUse
	);
}

void ULexUIPrefab::RecreatePrefab()
{
	TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
	TMap<TObjectPtr<AActor>, FLexUISubPrefabData> SubPrefabMap;
	auto RootActor = this->LoadPrefabWithExistingObjects(GetPrefabInstanceScene()->GetWorld(), nullptr
		, MapGuidToObject, SubPrefabMap
	);
	TMap<UObject*, FGuid> MapObjectToGuid;
	for (auto KeyValue : MapGuidToObject)
	{
		MapObjectToGuid.Add(KeyValue.Value, KeyValue.Key);
	}
	this->SavePrefab(RootActor, MapObjectToGuid, SubPrefabMap);
	this->EnsureInstanceObjects();
}

AActor* ULexUIPrefab::LoadPrefabInEditor(UWorld* InWorld, USceneComponent* InParent, bool SetRelativeTransformToIdentity)
{
	AActor* LoadedRootActor = nullptr;
	switch ((ELexUIPrefabVersion)PrefabVersion)
	{
	default:
	case ELexUIPrefabVersion::NewObjectOnNestedPrefab:
	{
		TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
		TMap<TObjectPtr<AActor>, FLexUISubPrefabData> SubPrefabMap;
		LoadedRootActor = LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::LoadPrefabWithExistingObjects(InWorld, this
			, InParent, MapGuidToObject, SubPrefabMap
		);
	}
	break;
	}
	return LoadedRootActor;
}

AActor* ULexUIPrefab::LoadPrefabInEditor(UWorld* InWorld, USceneComponent* InParent, TMap<TObjectPtr<AActor>, FLexUISubPrefabData>& OutSubPrefabMap, TMap<FGuid, TObjectPtr<UObject>>& OutMapGuidToObject, bool SetRelativeTransformToIdentity)
{
	AActor* LoadedRootActor = nullptr;
	switch ((ELexUIPrefabVersion)PrefabVersion)
	{
	default:
	case ELexUIPrefabVersion::NewObjectOnNestedPrefab:
	{
		LoadedRootActor = LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::LoadPrefabWithExistingObjects(InWorld, this
			, InParent, OutMapGuidToObject, OutSubPrefabMap
		);
	}
	break;
	}
	return LoadedRootActor;
}

#endif

#undef LOCTEXT_NAMESPACE