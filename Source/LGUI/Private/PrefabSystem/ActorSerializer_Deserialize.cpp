// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/ActorSerializer.h"
#include "PrefabSystem/LexUIObjectReaderAndWriter.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SplineComponent.h"
#include "Runtime/Launch/Resources/Version.h"
#include "PrefabSystem/LexUIPrefabManager.h"
#include "LGUI.h"
#include "Core/LexUIBehaviour.h"
#include "Core/LexUIManager.h"
#include "Core/LexUISettings.h"
#include "Core/Components/LexWidget.h"
#include "Misc/NetworkVersion.h"
#include "UObject/UObjectThreadContext.h"
#include "Serialization/MemoryReader.h"
#include "PrefabSystem/ILexUIPrefabInterface.h"
#include "PhysicsEngine/BodyInstance.h"
#if WITH_EDITOR
#include "Utils/LexUIUtils.h"
#endif



#define LOCTEXT_NAMESPACE "LexUIPrefabSystem_Deserialize"

namespace LexUIPrefabSystem
{
	ULexWidget* ActorSerializer::LoadPrefabWithExistingObjects(UWorld* InWorld, ULexUIPrefab* InPrefab, ULexWidget* Parent
		, TMap<FGuid, TObjectPtr<UObject>>& InOutMapGuidToObjects, TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& OutSubPrefabMap
	)
	{
		if (!IsValid(InWorld))
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d Not valid world!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return nullptr;
		}
		if (!IsValid(InPrefab))
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d InPrefab is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return nullptr;
		}

		bool bIsEditorOrRuntime = true;
#if !WITH_EDITOR
		bIsEditorOrRuntime = false;
#endif
		ActorSerializer serializer;
		serializer.OwnerObject = InWorld;
		for (auto& KeyValue : InOutMapGuidToObjects)//Preprocess the map, ignore invalid object
		{
			if (IsValid(KeyValue.Value))
			{
				serializer.MapGuidToObject.Add(KeyValue.Key, KeyValue.Value);
			}
		}
#if !WITH_EDITOR
		serializer.bIsEditorOrRuntime = false;
#endif
		serializer.bOverrideVersions = true;
		serializer.WriterOrReaderFunction = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, bool InIsSceneComponent) {
			auto ExcludeProperties = InIsSceneComponent ? serializer.GetSceneComponentExcludeProperties() : TSet<FName>();
			LexUIPrefabSystem::FLexUIObjectReader Reader(InOutBuffer, serializer, ExcludeProperties);
			Reader.DoSerialize(InObject);
		};
		serializer.WriterOrReaderFunctionForSubPrefabOverride = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, const TArray<FName>& InOverridePropertyNames) {
			LexUIPrefabSystem::FLexUIOverrideParameterObjectReader Reader(InOutBuffer, serializer, InOverridePropertyNames);
			Reader.DoSerialize(InObject);
		};
		auto rootActor = serializer.DeserializeActor(Parent, InPrefab, nullptr, false, FVector::ZeroVector, FQuat::Identity, FVector::OneVector);
		InOutMapGuidToObjects = serializer.MapGuidToObject;
		OutSubPrefabMap = serializer.SubPrefabMap;
		return rootActor;
	}

	ULexWidget* ActorSerializer::LoadPrefab(UWorld* InWorld, ULexUIPrefab* InPrefab, ULexWidget* Parent, bool SetRelativeTransformToIdentity, TFunction<void(ULexWidget*)> CallbackBeforeAwake)
	{
		if (!IsValid(InWorld))
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d Not valid world!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return nullptr;
		}
		if (!IsValid(InPrefab))
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d InPrefab is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return nullptr;
		}

		ActorSerializer serializer;
		serializer.OwnerObject = InWorld;
		serializer.CallbackBeforeAwake = CallbackBeforeAwake;
#if !WITH_EDITOR
		serializer.bIsEditorOrRuntime = false;
#endif
		serializer.bOverrideVersions = true;
		serializer.WriterOrReaderFunction = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, bool InIsSceneComponent) {
			auto ExcludeProperties = InIsSceneComponent ? serializer.GetSceneComponentExcludeProperties() : TSet<FName>();
			LexUIPrefabSystem::FLexUIObjectReader Reader(InOutBuffer, serializer, ExcludeProperties);
			Reader.DoSerialize(InObject);
		};
		serializer.WriterOrReaderFunctionForSubPrefabOverride = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, const TArray<FName>& InOverridePropertyNames) {
			LexUIPrefabSystem::FLexUIOverrideParameterObjectReader Reader(InOutBuffer, serializer, InOverridePropertyNames);
			Reader.DoSerialize(InObject);
		};
		ULexWidget* result = nullptr;
		if (SetRelativeTransformToIdentity)
		{
			result = serializer.DeserializeActor(Parent, InPrefab, nullptr, true);
		}
		else
		{
			result = serializer.DeserializeActor(Parent, InPrefab, nullptr);
		}
		return result;
	}
	ULexWidget* ActorSerializer::LoadPrefab(UWorld* InWorld, ULexUIPrefab* InPrefab, ULexWidget* Parent, FVector RelativeLocation, FQuat RelativeRotation, FVector RelativeScale, TFunction<void(ULexWidget*)> CallbackBeforeAwake)
	{
		if (!IsValid(InWorld))
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d Not valid world!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return nullptr;
		}
		if (!IsValid(InPrefab))
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d InPrefab is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return nullptr;
		}

		ActorSerializer serializer;
		serializer.OwnerObject = InWorld;
		serializer.CallbackBeforeAwake = CallbackBeforeAwake;
#if !WITH_EDITOR
		serializer.bIsEditorOrRuntime = false;
#endif
		serializer.bOverrideVersions = true;
		serializer.WriterOrReaderFunction = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, bool InIsSceneComponent) {
			auto ExcludeProperties = InIsSceneComponent ? serializer.GetSceneComponentExcludeProperties() : TSet<FName>();
			LexUIPrefabSystem::FLexUIObjectReader Reader(InOutBuffer, serializer, ExcludeProperties);
			Reader.DoSerialize(InObject);
		};
		serializer.WriterOrReaderFunctionForSubPrefabOverride = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, const TArray<FName>& InOverridePropertyNames) {
			LexUIPrefabSystem::FLexUIOverrideParameterObjectReader Reader(InOutBuffer, serializer, InOverridePropertyNames);
			Reader.DoSerialize(InObject);
		};
		return serializer.DeserializeActor(Parent, InPrefab, nullptr, true, RelativeLocation, RelativeRotation, RelativeScale);
	}
	ULexWidget* ActorSerializer::LoadSubPrefab(
		UObject* InOwnerObject, ULexUIPrefab* InPrefab, ULexWidget* Parent
		, const FGuid& InParentDeserializationSessionId
		, TMap<FGuid, TObjectPtr<UObject>>& InMapGuidToObject
		, const TFunction<void(ULexWidget*, const TMap<FGuid, TObjectPtr<UObject>>&, const TMap<TObjectPtr<UObject>, FGuid>&, const TArray<ULexWidget*>&, const TArray<UActorComponent*>&)>& InOnSubPrefabFinishDeserializeFunction
	)
	{
		ActorSerializer serializer;
		serializer.OwnerObject = InOwnerObject;
#if !WITH_EDITOR
		serializer.bIsEditorOrRuntime = false;
#endif
		serializer.bOverrideVersions = true;
		serializer.MapGuidToObject = InMapGuidToObject;
		serializer.DeserializationSessionId = InParentDeserializationSessionId;
		serializer.bIsSubPrefab = true;
		serializer.WriterOrReaderFunction = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, bool InIsSceneComponent) {
			auto ExcludeProperties = InIsSceneComponent ? serializer.GetSceneComponentExcludeProperties() : TSet<FName>();
			LexUIPrefabSystem::FLexUIObjectReader Reader(InOutBuffer, serializer, ExcludeProperties);
			Reader.DoSerialize(InObject);
		};
		serializer.WriterOrReaderFunctionForSubPrefabOverride = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, const TArray<FName>& InOverridePropertyNames) {
			LexUIPrefabSystem::FLexUIOverrideParameterObjectReader Reader(InOutBuffer, serializer, InOverridePropertyNames);
			Reader.DoSerialize(InObject);
		};
		serializer.OnSubPrefabFinishDeserializeFunction = InOnSubPrefabFinishDeserializeFunction;
		auto rootActor = serializer.DeserializeActor(Parent, InPrefab, nullptr, false, FVector::ZeroVector, FQuat::Identity, FVector::OneVector);
		return rootActor;
	}

#define LGUIPREFAB_LOG_DETAIL_TIME 0
	ULexWidget* ActorSerializer::DeserializeActorFromData(FLexUIPrefabSaveData& SaveData, ULexWidget* Parent, bool ReplaceTransform, FVector InLocation, FQuat InRotation, FVector InScale)
	{
#if LGUIPREFAB_LOG_DETAIL_TIME
		auto Time = FDateTime::Now();
#endif
		if (LGUIPrefabManager == nullptr)
		{
			LGUIPrefabManager = ULexUIPrefabWorldSubsystem::GetInstance(OwnerObject);
		}
		if (!bIsSubPrefab)
		{
			if (!DeserializationSessionId.IsValid())
			{
				DeserializationSessionId = FGuid::NewGuid();
				LGUIPrefabManager->BeginPrefabSystemProcessingActor(DeserializationSessionId);
			}
		}
		auto CreatedRootWidget = GenerateActorArray(SaveData.SavedActors, SaveData.SavedObjects, SaveData.MapSceneComponentToParent, FGuid());
		if (CreatedRootWidget == nullptr)
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d No actor generated!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);

			if (!bIsSubPrefab)
			{
				check(DeserializationSessionId.IsValid());
				LGUIPrefabManager->EndPrefabSystemProcessingActor(DeserializationSessionId);
			}
			return nullptr;
		}
		GenerateObjectArray(SaveData.SavedObjects, SaveData.MapSceneComponentToParent);
#if LGUIPREFAB_LOG_DETAIL_TIME
		UE_LOG(LGUI, Log, TEXT("--GenerateObject take time: %fms"), (FDateTime::Now() - Time).GetTotalMilliseconds());
		Time = FDateTime::Now();
#endif
		//properties
		for (auto& KeyValue : SaveData.SavedObjectData)
		{
			if (auto ObjectPtr = MapGuidToObject.Find(KeyValue.Key))
			{
				WriterOrReaderFunction(*ObjectPtr, KeyValue.Value, Cast<USceneComponent>(*ObjectPtr) != nullptr);
			}
		}

		//sub prefab override properties
		for (auto& Item : SubPrefabOverrideParameters)
		{
			WriterOrReaderFunctionForSubPrefabOverride(Item.Object, Item.ParameterDatas, Item.ParameterNames);
		}

#if LGUIPREFAB_LOG_DETAIL_TIME
		UE_LOG(LGUI, Log, TEXT("--DeserializeObject take time: %fms"), (FDateTime::Now() - Time).GetTotalMilliseconds());
		Time = FDateTime::Now();
#endif

		//component attachment
		for (auto& CompData : ComponentsInThisPrefab)
		{
			if (!CompData.Component->IsRegistered())
			{
				CompData.Component->RegisterComponent();
			}
		}
		for (auto& CompData : SubPrefabRootComponents)
		{
			auto SceneComp = (USceneComponent*)CompData.Component;
			if (auto ParentObjectPtr = MapGuidToObject.Find(CompData.SceneComponentParentGuid))
			{
				if (auto ParentComp = Cast<USceneComponent>(*ParentObjectPtr))
				{
					SceneComp->AttachToComponent(ParentComp, FAttachmentTransformRules::KeepRelativeTransform);
				}
			}
		}

		//attach root actor's parent
		if (Parent)
		{
			CreatedRootWidget->SetParent(Parent, false);
		}
		if (!bIsSubPrefab)//need to do this in root actor and it will propagate to children. If do this in subprefab and parent prefab override transform data on subprefab's actor, then transform goes wrong
		{
			CreatedRootWidget->CalculateObjectToWorldTransform(true);
		}
		if (ReplaceTransform)
		{
			CreatedRootWidget->SetRelativeLocationAndRotation(InLocation, InRotation);
			CreatedRootWidget->SetRelativeScale(InScale);
		}

#if WITH_EDITOR
		if (!bIsSubPrefab)//sub-prefab's RerunConstructionScripts should handle in parent after all override property, and after root actor attach to parent
		{
			auto World = OwnerObject->GetWorld();
			if (World && !World->IsGameWorld())
			{
				for (auto& Comp : AllComponents)
				{
					if (auto Widget = Cast<ULexWidget>(Comp))
					{
						Widget->CalculateTransformFromAnchor();
					}
				}
			}
		}
#endif

		if (OnSubPrefabFinishDeserializeFunction != nullptr)
		{
			OnSubPrefabFinishDeserializeFunction(CreatedRootWidget, MapGuidToObject, MapObjectToOriginGuid, AllWidgets, AllComponents);
		}
		if (CallbackBeforeAwake != nullptr)
		{
			CallbackBeforeAwake(CreatedRootWidget);
		}

#if LGUIPREFAB_LOG_DETAIL_TIME
		Time = FDateTime::Now();
#endif
		if (!bIsSubPrefab)
		{
			check(DeserializationSessionId.IsValid());
			for (auto item : AllWidgets)
			{
				LGUIPrefabManager->RemoveActorForPrefabSystem(item, DeserializationSessionId);
			}
			LGUIPrefabManager->EndPrefabSystemProcessingActor(DeserializationSessionId);

#if WITH_EDITOR
			auto World = OwnerObject->GetWorld();
			if (World && !World->IsGameWorld())
			{
				for (int i = 0; i < AllWidgets.Num(); i++)
				{
					auto& Widget = AllWidgets[i];
					if (Widget->GetClass()->ImplementsInterface(ULexUIPrefabInterface::StaticClass()))
					{
						ILexUIPrefabInterface::Execute_EditorAwake(Widget);
					}
					auto Components = Widget->GetComponents();
					for (auto& Comp : Components)
					{
						if (Comp->GetClass()->ImplementsInterface(ULexUIPrefabInterface::StaticClass()))
						{
							ILexUIPrefabInterface::Execute_EditorAwake(Comp);
						}
					}
				}
			}
			else
#endif
			{
				for (int i = 0; i < AllWidgets.Num(); i++)
				{
					auto& Widget = AllWidgets[i];
					if (Widget->GetClass()->ImplementsInterface(ULexUIPrefabInterface::StaticClass()))
					{
						ILexUIPrefabInterface::Execute_Awake(Widget);
					}
					auto Components = Widget->GetComponents();
					for (auto& Comp : Components)
					{
						if (Comp->GetClass()->ImplementsInterface(ULexUIPrefabInterface::StaticClass()))
						{
							ILexUIPrefabInterface::Execute_Awake(Comp);
						}
					}
				}
			}
		}

#if LGUIPREFAB_LOG_DETAIL_TIME
		UE_LOG(LGUI, Log, TEXT("--Call Awake (and OnEnable) take time: %fms"), (FDateTime::Now() - Time).GetTotalMilliseconds());
#endif

		return CreatedRootWidget;
	}
	ULexWidget* ActorSerializer::DeserializeActor(ULexWidget* Parent, ULexUIPrefab* InPrefab, const TFunction<void()>& InCallbackBeforeDeserialize, bool ReplaceTransform, FVector InLocation, FQuat InRotation, FVector InScale)
	{
		auto StartTime = FDateTime::Now();
		PrefabAssetPath = InPrefab->GetPathName();
#if WITH_EDITOR
		if (bIsEditorOrRuntime)
		{
			//fill new reference data
			this->ReferenceAssetList = InPrefab->ReferenceAssetList;
			this->ReferenceClassList = InPrefab->ReferenceClassList;
			this->ReferenceNameList = InPrefab->ReferenceNameList;

			this->ArchiveVersion = FPackageFileVersion(InPrefab->ArchiveVersion, (EUnrealEngineObjectUE5Version)InPrefab->ArchiveVersionUE5);
			this->ArchiveLicenseeVer = InPrefab->ArchiveLicenseeVer;
			this->ArEngineNetVer = InPrefab->ArEngineNetVer;
			this->ArGameNetVer = InPrefab->ArGameNetVer;
		}
		else
#endif
		{
			//fill new reference data
			this->ReferenceAssetList = InPrefab->ReferenceAssetListForBuild;
			this->ReferenceClassList = InPrefab->ReferenceClassListForBuild;
			this->ReferenceNameList = InPrefab->ReferenceNameListForBuild;

			this->ArchiveVersion = FPackageFileVersion(InPrefab->ArchiveVersion_ForBuild, (EUnrealEngineObjectUE5Version)InPrefab->ArchiveVersionUE5_ForBuild);
			this->ArchiveLicenseeVer = InPrefab->ArchiveLicenseeVer_ForBuild;
			this->ArEngineNetVer = InPrefab->ArEngineNetVer_ForBuild;
			this->ArGameNetVer = InPrefab->ArGameNetVer_ForBuild;
		}
		this->PrefabVersion = InPrefab->PrefabVersion;
		this->ArEngineVer = FEngineVersionBase(InPrefab->EngineMajorVersion, InPrefab->EngineMinorVersion, InPrefab->EnginePatchVersion);

		FLexUIPrefabSaveData SaveData;
		{
			auto& LoadedData =
#if WITH_EDITOR
				bIsEditorOrRuntime ? InPrefab->BinaryData :
#endif
				InPrefab->BinaryDataForBuild;

			auto FromBinary = FMemoryReader(LoadedData, false);
#if WITH_EDITOR
			if (bIsEditorOrRuntime)
			{
				FStructuredArchiveFromArchive(FromBinary).GetSlot() << SaveData;
			}
			else
#endif
			{
				FromBinary << SaveData;
			}
		}

		if (InCallbackBeforeDeserialize != nullptr)InCallbackBeforeDeserialize();
		auto CreatedRootActor = DeserializeActorFromData(SaveData, Parent, ReplaceTransform, InLocation, InRotation, InScale);

		if (GetDefault<ULexUIEditorSettings>()->bLogPrefabLoadTime)
		{
			auto TimeSpan = FDateTime::Now() - StartTime;
			UE_LOG(LGUI, Log, TEXT("Load prefab: '%s', total time: %fms"), *InPrefab->GetName(), TimeSpan.GetTotalMilliseconds());
		}

#if WITH_EDITOR
		ULexUIManagerObject::MarkBroadcastLevelActorListChanged();//UE5 will not auto refresh scene outliner and display actor label, so manually refresh it.
#endif

		return CreatedRootActor;
	}




	void ActorSerializer::GenerateObjectArray(TMap<FGuid, FLexUIObjectSaveData>& SavedObjects, TMap<FGuid, FGuid>& MapSceneComponentToParent)
	{
		auto CollectDefaultSubobjects = [&](UObject* Target, const FGuid& TargetGuid, FLexUICommonObjectSaveData& ObjectData) {
			//collect default sub object
			TArray<UObject*> DefaultSubObjects;
			Target->GetDefaultSubobjects(DefaultSubObjects);
			for (auto DefaultSubObject : DefaultSubObjects)
			{
				if (DefaultSubObject->HasAnyFlags(EObjectFlags::RF_Transient))continue;
				auto Index = ObjectData.DefaultSubObjectNameArray.IndexOfByKey(DefaultSubObject->GetFName());
				if (Index == INDEX_NONE)
				{
#if WITH_EDITOR
					if (auto Comp = Cast<UActorComponent>(DefaultSubObject))
					{
						if (Comp->IsVisualizationComponent())//visualization component no need to serialize
							continue;
					}
#endif
					UE_LOG(LGUI, Warning, TEXT("[%s].%d Missing guid for default sub object: %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(DefaultSubObject->GetFName().ToString()));
					continue;
				}
				auto DefaultSubObjectGuid = ObjectData.DefaultSubObjectGuidArray[Index];
				MapGuidToObject.Add(DefaultSubObjectGuid, DefaultSubObject);
				MapObjectToOriginGuid.Add(DefaultSubObject, DefaultSubObjectGuid);
			}
		};
		for (auto& KeyValuePair : SavedObjects)
		{
			auto& ObjectGuid = KeyValuePair.Key;
			auto& ObjectData = KeyValuePair.Value;
			UObject* CreatedNewObject = nullptr;
#if WITH_EDITOR
			//MapGuidToObject can passed from LoadPrefabWithExistingObjects, so we need to find from map first. This only needed in editor, because runtime never use LoadPrefabWithExistingObjects
			if (auto ObjectPtr = MapGuidToObject.Find(ObjectGuid))
			{
				CreatedNewObject = *ObjectPtr;
				MapObjectToOriginGuid.Add(CreatedNewObject, ObjectGuid);
				CollectDefaultSubobjects(CreatedNewObject, ObjectGuid, ObjectData);
			}
			else
#endif
			{
				if (auto ObjectClass = FindClassFromListByIndex(ObjectData.ObjectClass))
				{
					if (ObjectClass->IsChildOf(AActor::StaticClass()))
					{
						UE_LOG(LGUI, Warning, TEXT("[%s].%d Wrong object class: '%s'. Prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(ObjectClass->GetFName().ToString()), *PrefabAssetPath);
						continue;
					}

					if (auto OuterObjectPtr = MapGuidToObject.Find(ObjectData.OuterObjectGuid))
					{
#if WITH_EDITOR
						if (auto ExitObject = FindObjectWithOuter(*OuterObjectPtr, nullptr, ObjectData.ObjectName))
						{
							if (auto Comp = Cast<UActorComponent>(ExitObject))
							{
								Comp->DestroyComponent();
								Comp->Rename(nullptr, GetTransientPackage());
							}
							else if (auto Obj = Cast<UObject>(ExitObject))
							{
								Obj->ConditionalBeginDestroy();
								Obj->Rename(nullptr, GetTransientPackage());
							}
							UE_LOG(LGUI, Warning, TEXT("[%s].%d Object '%s' already exist on outer '%s', will destroy and rename exiting one. Prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(ObjectData.ObjectName.ToString()), *(OuterObjectPtr->GetPathName()), *PrefabAssetPath);
						}
#endif
						CreatedNewObject = NewObject<UObject>(*OuterObjectPtr, ObjectClass, ObjectData.ObjectName, (EObjectFlags)ObjectData.ObjectFlags);
						MapGuidToObject.Add(ObjectGuid, CreatedNewObject);
						MapObjectToOriginGuid.Add(CreatedNewObject, ObjectGuid);
						CollectDefaultSubobjects(CreatedNewObject, ObjectGuid, ObjectData);
					}
					else
					{
						UE_LOG(LGUI, Warning, TEXT("[%s].%d Missing Outer object when creating object: '%s'. Prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(ObjectData.ObjectName.ToString()), *PrefabAssetPath);
						continue;
					}
				}
			}
			if (auto CreatedNewComponent = Cast<UActorComponent>(CreatedNewObject))
			{
				FComponentDataStruct CompData;
				CompData.Component = CreatedNewComponent;
				if (auto ParentGuidPtr = MapSceneComponentToParent.Find(ObjectGuid))
				{
					CompData.SceneComponentParentGuid = *ParentGuidPtr;
				}
				ComponentsInThisPrefab.Add(CompData);
				AllComponents.Add(CreatedNewComponent);
			}
		}
	}

	ULexWidget* ActorSerializer::GenerateActorArray(TArray<FLexUIActorSaveData>& SavedActors, TMap<FGuid, FLexUIObjectSaveData>& SavedObjects, TMap<FGuid, FGuid>& MapSceneComponentToParent, FGuid ParentGuid)
	{
		ULexWidget* RootWidget = nullptr;//first actor is the RootActor
		for (int i = 0; i < SavedActors.Num(); i++)
		{
			auto& InActorData = SavedActors[i];
			if (InActorData.bIsPrefab)
			{
				auto PrefabIndex = InActorData.PrefabAssetIndex;
				if (auto PrefabAssetObject = FindAssetFromListByIndex(PrefabIndex))
				{
					if (auto SubPrefabAsset = Cast<ULexUIPrefab>(PrefabAssetObject))
					{
						ULexWidget* SubPrefabRootWidget = nullptr;
						FLexUISubPrefabData SubPrefabData;
						SubPrefabData.PrefabAsset = SubPrefabAsset;

#if WITH_EDITOR
						if (SubPrefabAsset->PrefabVersion < (uint16)ELexUIPrefabVersion::NewObjectOnNestedPrefab)
						{
							SubPrefabAsset->RecreatePrefab();//if is old version then recreate to make it new version
						}
#endif
						//sub prefab
						{
							auto& SubMapGuidToObject = SubPrefabData.MapGuidToObject;
							TMap<FGuid, FGuid> MapObjectGuidFromSubPrefabToParentPrefab;
							for (auto& KeyValue : InActorData.MapObjectGuidFromParentPrefabToSubPrefab)
							{
								MapObjectGuidFromSubPrefabToParentPrefab.Add(KeyValue.Value, KeyValue.Key);
							}
#if WITH_EDITOR
							//edit mode must check if the object already exist, because the deserialize process could happen when use revert-prefab
							if (bIsEditorOrRuntime)
							{
								for (auto& KeyValue : MapObjectGuidFromSubPrefabToParentPrefab)
								{
									auto ObjectPtr = MapGuidToObject.Find(KeyValue.Value);
									if (!SubMapGuidToObject.Contains(KeyValue.Key) && ObjectPtr != nullptr)
									{
										SubMapGuidToObject.Add(KeyValue.Key, *ObjectPtr);
									}
								}
							}
#endif
							bool bAnyGuidFrom_MapObjectIdToNewlyCreatedId = false;
							auto GetObjectGuidInParent = [&](const FGuid& GuidInSubPrefab, const FGuid& GuidInOriginPrefab) {
								FGuid GuidInParent;
								auto ObjectGuidInParentPrefabPtr = MapObjectGuidFromSubPrefabToParentPrefab.Find(GuidInSubPrefab);
								if (ObjectGuidInParentPrefabPtr == nullptr)
								{
									auto UniqueId = FLexUISubPrefabObjectUniqueIdSaveData{ InActorData.ActorGuid, GuidInOriginPrefab };
									if (auto GuidInParentPtr = InActorData.MapObjectIdToNewlyCreatedId.Find(UniqueId))
									{
										GuidInParent = *GuidInParentPtr;
									}
									else
									{
										GuidInParent = FGuid::NewGuid();
										InActorData.MapObjectIdToNewlyCreatedId.Add(UniqueId, GuidInParent);
									}
									bAnyGuidFrom_MapObjectIdToNewlyCreatedId = true;
									MapObjectGuidFromSubPrefabToParentPrefab.Add(GuidInSubPrefab, GuidInParent);
								}
								else
								{
									GuidInParent = *ObjectGuidInParentPrefabPtr;
								}
								return GuidInParent;
								};
							auto NewOnSubPrefabFinishDeserializeFunction =
								[&](ULexWidget*, const TMap<FGuid, TObjectPtr<UObject>>& InSubPrefabMapGuidToObject, const TMap<TObjectPtr<UObject>, FGuid>& InMapObjectToOriginGuid, const TArray<ULexWidget*>& InSubActors, const TArray<UActorComponent*>& InSubComponents) {
								//collect sub prefab's object and guid to parent map, so all objects are ready when set override parameters
								for (auto& KeyValue : InSubPrefabMapGuidToObject)
								{
									auto& GuidInSubPrefab = KeyValue.Key;
									auto& ObjectInSubPrefab = KeyValue.Value;

									auto GuidInParent = GetObjectGuidInParent(GuidInSubPrefab, InMapObjectToOriginGuid[ObjectInSubPrefab]);

									if (auto RecordDataPtr = InActorData.MapObjectGuidToSubPrefabOverrideParameter.Find(GuidInParent))
									{
										FLexUIPrefabOverrideParameterData OverrideDataItem;
										OverrideDataItem.MemberPropertyNames = RecordDataPtr->OverrideParameterNames;
										OverrideDataItem.Object = ObjectInSubPrefab;
										SubPrefabData.ObjectOverrideParameterArray.Add(OverrideDataItem);

										FSubPrefabObjectOverrideParameterData OverrideData;
										OverrideData.Object = ObjectInSubPrefab;
										OverrideData.ParameterDatas = RecordDataPtr->OverrideParameterData;
										OverrideData.ParameterNames = RecordDataPtr->OverrideParameterNames;
										SubPrefabOverrideParameters.Add(OverrideData);//collect override parameters, so when all objects are generated, restore these parameters will get all value back
									}

									SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Add(GuidInParent, GuidInSubPrefab);
									SubPrefabData.MapGuidToObject.Add(GuidInSubPrefab, ObjectInSubPrefab);
									if (!MapGuidToObject.Contains(GuidInParent))
									{
										MapGuidToObject.Add(GuidInParent, ObjectInSubPrefab);
									}
								}
								//if we don't need to get any guid from MapObjectIdToNewlyCreatedId, that means subprefab already have a persistent guid for all objects, then we can clear the data
								if (!bAnyGuidFrom_MapObjectIdToNewlyCreatedId)
								{
									if (InActorData.MapObjectIdToNewlyCreatedId.Num() > 0)
									{
										InActorData.MapObjectIdToNewlyCreatedId.Empty();
									}
								}
								else
								{
									//convert data to save
									for (auto& DataItem : InActorData.MapObjectIdToNewlyCreatedId)
									{
										SubPrefabData.MapObjectIdToNewlyCreatedId.Add({ DataItem.Key.RootActorGuidInParentPrefab, DataItem.Key.ObjectGuidInOriginPrefab }, DataItem.Value);
									}
								}
								//collect sub-prefab's actor to parent prefab
								AllWidgets.Append(InSubActors);
								AllComponents.Append(InSubComponents);
								MapObjectToOriginGuid.Append(InMapObjectToOriginGuid);
								};

							SubPrefabRootWidget = ActorSerializer::LoadSubPrefab(this->OwnerObject, SubPrefabAsset, nullptr, DeserializationSessionId, SubMapGuidToObject
								, NewOnSubPrefabFinishDeserializeFunction
							);
						}
						
						if (SubPrefabRootWidget != nullptr)
						{
							FComponentDataStruct CompData;
							CompData.Component = SubPrefabRootWidget;
							FGuid SubPrefabRootCompGuid;
							for (auto& KeyValue : MapGuidToObject)
							{
								if (KeyValue.Value == CompData.Component)
								{
									SubPrefabRootCompGuid = KeyValue.Key;
									break;
								}
							}
							if (auto ParentGuidPtr = MapSceneComponentToParent.Find(SubPrefabRootCompGuid))
							{
								CompData.SceneComponentParentGuid = *ParentGuidPtr;
								SubPrefabRootComponents.Add(CompData);
							}

							SubPrefabMap.Add(SubPrefabRootWidget, SubPrefabData);

							if (i == 0)
							{
								RootWidget = SubPrefabRootWidget;
							}
						}
					}
				}
			}
			else
			{
				if (auto ActorClass = FindClassFromListByIndex(InActorData.ObjectClass))
				{
					if (!ActorClass->IsChildOf(AActor::StaticClass()))//if not the right class, use default
					{
						UE_LOG(LGUI, Warning, TEXT("[%s].%d Find class: '%s' at index: %d, but is not a Actor class, use default. Prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(ActorClass->GetFName().ToString()), InActorData.ObjectClass, *PrefabAssetPath);
						ActorClass = AActor::StaticClass();
					}

					auto CollectDefaultSubobjects = [&](ULexWidget* TargetWidget) {
						//Collect default sub objects
						TArray<UObject*> DefaultSubObjects;
						TargetWidget->GetDefaultSubobjects(DefaultSubObjects);
						for (auto DefaultSubObject : DefaultSubObjects)
						{
							if (DefaultSubObject->HasAnyFlags(EObjectFlags::RF_Transient))continue;
							auto Index = InActorData.DefaultSubObjectNameArray.IndexOfByKey(DefaultSubObject->GetFName());
							if (Index == INDEX_NONE)
							{
								UE_LOG(LGUI, Warning, TEXT("[%s].%d Missing guid for default sub object: %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(DefaultSubObject->GetFName().ToString()));
								continue;
							}
							auto DefaultSubObjectGuid = InActorData.DefaultSubObjectGuidArray[Index];
							MapGuidToObject.Add(DefaultSubObjectGuid, DefaultSubObject);
							MapObjectToOriginGuid.Add(DefaultSubObject, DefaultSubObjectGuid);
						}
						};

					ULexWidget* NewWidget = nullptr;
#if WITH_EDITOR
					//MapGuidToObject can passed from LoadPrefabWithExistingObjects, so we need to find from map first. This only needed in editor, because runtime never use LoadPrefabWithExistingObjects
					if (auto ActorPtr = MapGuidToObject.Find(InActorData.ActorGuid))
					{
						NewWidget = (ULexWidget*)(*ActorPtr);
						MapObjectToOriginGuid.Add(NewWidget, InActorData.ActorGuid);
						CollectDefaultSubobjects(NewWidget);
					}
					else
#endif
					{
						NewWidget = NewObject<ULexWidget>(OwnerObject, ActorClass, NAME_None, (EObjectFlags)InActorData.ObjectFlags);
						MapGuidToObject.Add(InActorData.ActorGuid, NewWidget);
						MapObjectToOriginGuid.Add(NewWidget, InActorData.ActorGuid);
						CollectDefaultSubobjects(NewWidget);
					}
					//add actor before FinishSpawning, so it's good for component (or other default sub-object) to check if actor is processing by prefab system
					LGUIPrefabManager->AddActorForPrefabSystem(NewWidget, DeserializationSessionId);
					
					if (ParentGuid.IsValid())
					{
						FComponentDataStruct CompData;
						CompData.Component = NewWidget;
						CompData.SceneComponentParentGuid = ParentGuid;
						ComponentsInThisPrefab.Add(CompData);
					}

					AllWidgets.Add(NewWidget);

					if (i == 0)
					{
						RootWidget = NewWidget;
					}
				}
				else
				{
					UE_LOG(LGUI, Warning, TEXT("[%s].%d Class of index:%d not found! Prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, (InActorData.ObjectClass), *PrefabAssetPath);
				}
			}
		}
		return RootWidget;
	}
}

#undef LOCTEXT_NAMESPACE


