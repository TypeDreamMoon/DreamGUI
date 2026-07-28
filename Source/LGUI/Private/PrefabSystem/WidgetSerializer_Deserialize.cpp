// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/WidgetSerializer.h"
#include "PrefabSystem/LexUIObjectReaderAndWriter.h"
#include "Engine/World.h"
#include "LGUI.h"
#include "Core/LexUIBehaviour.h"
#include "Core/LexUIManager.h"
#include "Core/LexUISettings.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"
#include "Serialization/MemoryReader.h"



#define LOCTEXT_NAMESPACE "LexUIPrefabSystem_Deserialize"

namespace LexUIPrefabSystem
{
	namespace WidgetSerializerLoadGuard
	{
		thread_local TArray<const ULexUIPrefab*> ActivePrefabs;

		class FScopedPrefabLoad
		{
		public:
			explicit FScopedPrefabLoad(const ULexUIPrefab* InPrefab)
				: Prefab(InPrefab)
				, bEntered(IsValid(InPrefab) && !ActivePrefabs.Contains(InPrefab))
			{
				if (bEntered)
				{
					ActivePrefabs.Add(InPrefab);
				}
			}

			~FScopedPrefabLoad()
			{
				if (bEntered)
				{
					ActivePrefabs.RemoveSingleSwap(Prefab, EAllowShrinking::No);
				}
			}

			bool WasEntered() const { return bEntered; }

		private:
			const ULexUIPrefab* Prefab = nullptr;
			bool bEntered = false;
		};
	}

	ULexWidget* WidgetSerializer::LoadPrefabWithExistingObjects(UWorld* InWorld, UObject* InOuter, ULexUIPrefab* InPrefab, ULexWidget* Parent
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
		
		WidgetSerializer serializer;
		serializer.World = InWorld;
		serializer.OwnerObject = InOuter;
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
		serializer.WriterOrReaderFunction = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer) {
			LexUIPrefabSystem::FLexUIObjectReader Reader(InOutBuffer, serializer, {});
			Reader.DoSerialize(InObject);
		};
		serializer.WriterOrReaderFunctionForSubPrefabOverride = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, const TArray<FName>& InOverridePropertyNames) {
			LexUIPrefabSystem::FLexUIOverrideParameterObjectReader Reader(InOutBuffer, serializer, InOverridePropertyNames);
			Reader.DoSerialize(InObject);
		};
		auto rootWidget = serializer.DeserializeWidget(Parent, InPrefab, nullptr, false, FVector::ZeroVector, FQuat::Identity, FVector::OneVector);
		InOutMapGuidToObjects = serializer.MapGuidToObject;
		OutSubPrefabMap = serializer.SubPrefabMap;
		return rootWidget;
	}

	ULexWidget* WidgetSerializer::LoadPrefab(UWorld* InWorld, UObject* InOuter, ULexUIPrefab* InPrefab, ULexWidget* Parent, bool SetRelativeTransformToIdentity, TFunction<void(ULexWidget*)> CallbackBeforeAwake)
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

		WidgetSerializer serializer;
		serializer.World = InWorld;
		serializer.OwnerObject = InOuter;
		serializer.CallbackBeforeAwake = CallbackBeforeAwake;
#if !WITH_EDITOR
		serializer.bIsEditorOrRuntime = false;
#endif
		serializer.bOverrideVersions = true;
		serializer.WriterOrReaderFunction = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer) {
			LexUIPrefabSystem::FLexUIObjectReader Reader(InOutBuffer, serializer, {});
			Reader.DoSerialize(InObject);
		};
		serializer.WriterOrReaderFunctionForSubPrefabOverride = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, const TArray<FName>& InOverridePropertyNames) {
			LexUIPrefabSystem::FLexUIOverrideParameterObjectReader Reader(InOutBuffer, serializer, InOverridePropertyNames);
			Reader.DoSerialize(InObject);
		};
		ULexWidget* result = nullptr;
		if (SetRelativeTransformToIdentity)
		{
			result = serializer.DeserializeWidget(Parent, InPrefab, nullptr, true);
		}
		else
		{
			result = serializer.DeserializeWidget(Parent, InPrefab, nullptr);
		}
		return result;
	}
	ULexWidget* WidgetSerializer::LoadPrefab(UWorld* InWorld, UObject* InOuter, ULexUIPrefab* InPrefab, ULexWidget* Parent, FVector RelativeLocation, FQuat RelativeRotation, FVector RelativeScale, TFunction<void(ULexWidget*)> CallbackBeforeAwake)
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

		WidgetSerializer serializer;
		serializer.World = InWorld;
		serializer.OwnerObject = InOuter;
		serializer.CallbackBeforeAwake = CallbackBeforeAwake;
#if !WITH_EDITOR
		serializer.bIsEditorOrRuntime = false;
#endif
		serializer.bOverrideVersions = true;
		serializer.WriterOrReaderFunction = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer) {
			LexUIPrefabSystem::FLexUIObjectReader Reader(InOutBuffer, serializer, {});
			Reader.DoSerialize(InObject);
		};
		serializer.WriterOrReaderFunctionForSubPrefabOverride = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, const TArray<FName>& InOverridePropertyNames) {
			LexUIPrefabSystem::FLexUIOverrideParameterObjectReader Reader(InOutBuffer, serializer, InOverridePropertyNames);
			Reader.DoSerialize(InObject);
		};
		return serializer.DeserializeWidget(Parent, InPrefab, nullptr, true, RelativeLocation, RelativeRotation, RelativeScale);
	}
	ULexWidget* WidgetSerializer::LoadSubPrefab(UWorld* InWorld,
		UObject* InOwnerObject, ULexUIPrefab* InPrefab, ULexWidget* Parent
		, TMap<FGuid, TObjectPtr<UObject>>& InMapGuidToObject
		, const TFunction<void(ULexWidget*, const TMap<FGuid, TObjectPtr<UObject>>&, const TMap<TObjectPtr<UObject>, FGuid>&, const TArray<ULexWidget*>&)>& InOnSubPrefabFinishDeserializeFunction
	)
	{
		WidgetSerializer serializer;
		serializer.World = InWorld;
		serializer.OwnerObject = InOwnerObject;
#if !WITH_EDITOR
		serializer.bIsEditorOrRuntime = false;
#endif
		serializer.bOverrideVersions = true;
		serializer.MapGuidToObject = InMapGuidToObject;
		serializer.bIsSubPrefab = true;
		serializer.WriterOrReaderFunction = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer) {
			LexUIPrefabSystem::FLexUIObjectReader Reader(InOutBuffer, serializer, {});
			Reader.DoSerialize(InObject);
		};
		serializer.WriterOrReaderFunctionForSubPrefabOverride = [&serializer](UObject* InObject, TArray<uint8>& InOutBuffer, const TArray<FName>& InOverridePropertyNames) {
			LexUIPrefabSystem::FLexUIOverrideParameterObjectReader Reader(InOutBuffer, serializer, InOverridePropertyNames);
			Reader.DoSerialize(InObject);
		};
		serializer.OnSubPrefabFinishDeserializeFunction = InOnSubPrefabFinishDeserializeFunction;
		auto rootWidget = serializer.DeserializeWidget(Parent, InPrefab, nullptr, false, FVector::ZeroVector, FQuat::Identity, FVector::OneVector);
		return rootWidget;
	}

#define LGUIPREFAB_LOG_DETAIL_TIME 0
	ULexWidget* WidgetSerializer::DeserializeWidgetFromData(FLexUIPrefabSaveData& SaveData, ULexWidget* Parent, bool ReplaceTransform, FVector InLocation, FQuat InRotation, FVector InScale)
	{
#if LGUIPREFAB_LOG_DETAIL_TIME
		auto Time = FDateTime::Now();
#endif
		auto CreatedRootWidget = GenerateWidgetArray(SaveData.SavedWidgets, SaveData.SavedObjects, SaveData.MapWidgetToParent, FGuid());
		if (CreatedRootWidget == nullptr)
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d No actor generated!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return nullptr;
		}
		GenerateObjectArray(SaveData.SavedObjects, SaveData.MapWidgetToParent);
#if LGUIPREFAB_LOG_DETAIL_TIME
		UE_LOG(LGUI, Log, TEXT("--GenerateObject take time: %fms"), (FDateTime::Now() - Time).GetTotalMilliseconds());
		Time = FDateTime::Now();
#endif
		//properties
		for (auto& KeyValue : SaveData.SavedObjectData)
		{
			if (auto ObjectPtr = MapGuidToObject.Find(KeyValue.Key))
			{
				WriterOrReaderFunction(*ObjectPtr, KeyValue.Value);
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
		for (auto& CompData : WidgetAttachmentArray)
		{
			auto Widget = CompData.Widget;
			ULexWidget* ParentWidget = nullptr;
			if (CompData.ParentGuid.IsValid())
			{
				if (auto ParentObjectPtr = MapGuidToObject.Find(CompData.ParentGuid))
				{
					ParentWidget = Cast<ULexWidget>(*ParentObjectPtr);
				}
			}
			if (!ParentWidget)
			{
				UE_LOG(LGUI, Error, TEXT("[%s].%d Missing parent for widget:%s at prefab:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Widget->GetPathDisplayName(), *PrefabAssetPath);
				ParentWidget = CreatedRootWidget;
			}
			if (Widget->HasRegistered())
			{
				// Reflection already restored the authored SiblingIndex; the attach path overwrites it
				// with a tail index (Children.Num()-1), which both loses the authored position and can
				// collide with another child's restored index. Re-assert the authored value and let the
				// parent's lazy sort put the array in order.
				const int32 AuthoredSiblingIndex = Widget->GetSiblingIndex();
				Widget->SetParentFromPrefab(ParentWidget, false);
				if (AuthoredSiblingIndex >= 0)
				{
					Widget->RestoreSiblingIndexFromPrefab(AuthoredSiblingIndex);
				}
			}
			else
			{
				Widget->SetParentBeforeRegister(ParentWidget);
			}
		}
		// Restored indices may carry holes or duplicates (legacy assets, cross-parent moves). Normalize
		// the whole tree once: stable-order by authored indices, renumber contiguously.
		if (IsValid(CreatedRootWidget))
		{
			CreatedRootWidget->ApplySiblingIndexFromPrefab_Recursive();
		}

		// Parent-owned slots do not exist while a nested prefab is deserialized
		// without its parent. Recreate and restore them after attachment but
		// before widget registration/layout begins.
		for (FSubPrefabRootPanelSlotOverrideData& SlotOverride : SubPrefabRootPanelSlotOverrides)
		{
			ULexWidget* RootWidget = SlotOverride.RootWidget;
			if (!IsValid(RootWidget) || !RootWidget->GetParent())
			{
				continue;
			}

			ULexPanelSlot* PanelSlot = RootWidget->GetPanelSlot();
			if (!IsValid(PanelSlot)
				&& RootWidget->GetParent()->GetLayoutContainer()
				&& RootWidget->GetParent()->GetLayoutContainer()->IsA<ULexPanelLayoutBase>())
			{
				PanelSlot = RootWidget->CreateNewPanelSlot<ULexPanelSlot>();
			}
			if (!IsValid(PanelSlot))
			{
				continue;
			}

			MapGuidToObject.Add(SlotOverride.PanelSlotGuid, PanelSlot);
			MapObjectToOriginGuid.Add(PanelSlot, SlotOverride.PanelSlotGuid);
			if (FLexUISubPrefabData* SubPrefabData = SubPrefabMap.Find(RootWidget))
			{
				SubPrefabData->MapGuidToObject.Add(GetSubPrefabRootPanelSlotOriginGuid(), PanelSlot);
				SubPrefabData->MapObjectGuidFromParentPrefabToSubPrefab.Add(
					SlotOverride.PanelSlotGuid, GetSubPrefabRootPanelSlotOriginGuid());

				FLexUISubPrefabObjectUniqueId PanelSlotId;
				PanelSlotId.RootWidgetGuidInParentPrefab = SlotOverride.RootWidgetGuid;
				PanelSlotId.ObjectGuidInOriginPrefab = GetSubPrefabRootPanelSlotOriginGuid();
				SubPrefabData->MapObjectIdToNewlyCreatedId.Add(PanelSlotId, SlotOverride.PanelSlotGuid);
			}

			if (!SlotOverride.ParameterNames.IsEmpty())
			{
				WriterOrReaderFunctionForSubPrefabOverride(
					PanelSlot, SlotOverride.ParameterDatas, SlotOverride.ParameterNames);
			}
		}
		//attach root actor's parent
		if (Parent)
		{
			CreatedRootWidget->SetParent(Parent, false);
		}

#if LGUIPREFAB_LOG_DETAIL_TIME
		Time = FDateTime::Now();
#endif
		if (!bIsSubPrefab)
		{
			for (int i = 0; i < AllWidgetArray.Num(); i++)
			{
				auto& Widget = AllWidgetArray[i];
				Widget->OnRegister();
			}
		}
		
		if (ReplaceTransform)
		{
			CreatedRootWidget->SetRelativeLocationAndRotation(InLocation, InRotation);
			CreatedRootWidget->SetRelativeScale(InScale);
		}

		if (OnSubPrefabFinishDeserializeFunction != nullptr)
		{
			OnSubPrefabFinishDeserializeFunction(CreatedRootWidget, MapGuidToObject, MapObjectToOriginGuid, AllWidgetArray);
		}
		if (CallbackBeforeAwake != nullptr)
		{
			CallbackBeforeAwake(CreatedRootWidget);
		}

		if (!bIsSubPrefab)
		{
			if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(World))
			{
				//originally I use World->HasBegunPlay to tell if the world has begun play but it not work well, the World->HasBegunPlay return false even i do LoadPrefab in BeginPlay
				if (LexUIManager->HasBegunPlay())
				{
					for (int i = 0; i < AllWidgetArray.Num(); i++)
					{
						auto& Widget = AllWidgetArray[i];
						Widget->BeginPlay();
					}
				}
			}
		}

#if LGUIPREFAB_LOG_DETAIL_TIME
		UE_LOG(LGUI, Log, TEXT("--Call Awake (and OnEnable) take time: %fms"), (FDateTime::Now() - Time).GetTotalMilliseconds());
#endif

		return CreatedRootWidget;
	}
	ULexWidget* WidgetSerializer::DeserializeWidget(ULexWidget* Parent, ULexUIPrefab* InPrefab, const TFunction<void()>& InCallbackBeforeDeserialize, bool ReplaceTransform, FVector InLocation, FQuat InRotation, FVector InScale)
	{
		WidgetSerializerLoadGuard::FScopedPrefabLoad LoadScope(InPrefab);
		if (!LoadScope.WasEntered())
		{
			UE_LOG(LGUI, Error, TEXT("Circular prefab reference detected while loading '%s'."), *GetPathNameSafe(InPrefab));
			return nullptr;
		}

		auto StartTime = FDateTime::Now();
		PrefabAssetPath = InPrefab->GetPathName();
#if WITH_EDITOR
		if (bIsEditorOrRuntime)
		{
			//fill new reference data
			this->ReferenceAssetList = InPrefab->ReferenceAssetList;
			this->ReferenceClassList = InPrefab->ReferenceClassList;
			this->ReferenceNameList = InPrefab->ReferenceNameList;
			this->ReferenceTextList = InPrefab->ReferenceTextList;

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
			this->ReferenceTextList = InPrefab->ReferenceTextListForBuild;

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
		auto CreatedRootWidget = DeserializeWidgetFromData(SaveData, Parent, ReplaceTransform, InLocation, InRotation, InScale);

		if (GetDefault<ULexUIEditorSettings>()->bLogPrefabLoadTime)
		{
			auto TimeSpan = FDateTime::Now() - StartTime;
			UE_LOG(LGUI, Log, TEXT("Load prefab: '%s', total time: %fms"), *InPrefab->GetName(), TimeSpan.GetTotalMilliseconds());
		}

		return CreatedRootWidget;
	}




#if WITH_EDITOR
	bool WidgetSerializer::ReleaseNameFromExistingObject(UObject* InExistingObject)
	{
		if (!IsValid(InExistingObject))
		{
			return true;//nothing is holding the name
		}
		if (InExistingObject->HasAnyFlags(RF_BeginDestroyed) || InExistingObject->IsUnreachable())
		{
			// Already on its way out. Renaming here is the case that fatals: Rename ends in
			// UnhashObject, and an object past BeginDestroy is no longer in the hash table it would
			// be removed from. Leave it; the caller falls back to a different name.
			return false;
		}

		// Rename BEFORE tearing down, not after. Freeing the name is the point of this call, and the
		// engine's own ReleaseUniquelyNamedObject does exactly and only this. Doing it second means
		// renaming an object that has already begun destruction.
		// The flags matter: this is scratch work in a throwaway world, so it must not leave a
		// redirector, dirty a package, or record an undo step for an object about to be destroyed.
		InExistingObject->Rename(nullptr, GetTransientPackage(),
			REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);

		if (auto Widget = Cast<ULexWidget>(InExistingObject))
		{
			Widget->DestroyWidget();
		}
		else if (auto WidgetComponent = Cast<ULexUIBehaviour>(InExistingObject))
		{
			WidgetComponent->DestroyComponent();
		}
		else
		{
			InExistingObject->ConditionalBeginDestroy();
		}
		return true;
	}
#endif

	void WidgetSerializer::GenerateObjectArray(TMap<FGuid, FLexUIObjectSaveData>& SavedObjects, TMap<FGuid, FGuid>& MapWidgetToParent)
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
					UE_LOG(LGUI, Warning, TEXT("[%s].%d Missing guid for default sub object: %s. Prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(DefaultSubObject->GetFName().ToString()), *PrefabAssetPath);
					continue;
				}
				auto DefaultSubObjectGuid = ObjectData.DefaultSubObjectGuidArray[Index];
				MapGuidToObject.Add(DefaultSubObjectGuid, DefaultSubObject);
				MapObjectToOriginGuid.Add(DefaultSubObject, DefaultSubObjectGuid);
			}
		};
		TArray<FGuid> PendingObjectGuids;
		SavedObjects.GenerateKeyArray(PendingObjectGuids);
		while (!PendingObjectGuids.IsEmpty())
		{
			bool bMadeProgress = false;
			for (int32 PendingIndex = PendingObjectGuids.Num() - 1; PendingIndex >= 0; --PendingIndex)
			{
				const FGuid ObjectGuid = PendingObjectGuids[PendingIndex];
				FLexUIObjectSaveData* ObjectData = SavedObjects.Find(ObjectGuid);
				if (!ObjectData)
				{
					PendingObjectGuids.RemoveAtSwap(PendingIndex);
					bMadeProgress = true;
					continue;
				}

				UClass* ObjectClass = FindClassFromListByIndex(ObjectData->ObjectClass);
				if (!ObjectClass)
				{
					UE_LOG(LGUI, Warning, TEXT("[%s].%d Missing class when creating object: '%s'. Prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(ObjectData->ObjectName.ToString()), *PrefabAssetPath);
					PendingObjectGuids.RemoveAtSwap(PendingIndex);
					bMadeProgress = true;
					continue;
				}
				if (ObjectClass->HasAnyClassFlags(CLASS_Abstract))
				{
					UE_LOG(LGUI, Warning, TEXT("[%s].%d Bad class %s when creating object: '%s'. Prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(ObjectClass->GetPathName()), *(ObjectData->ObjectName.ToString()), *PrefabAssetPath);
					PendingObjectGuids.RemoveAtSwap(PendingIndex);
					bMadeProgress = true;
					continue;
				}

				auto OuterObjectPtr = MapGuidToObject.Find(ObjectData->OuterObjectGuid);
				if (!OuterObjectPtr)
				{
					continue;
				}

				UObject* CreatedNewObject = nullptr;
#if WITH_EDITOR
				// LoadPrefabWithExistingObjects can supply an already-created object for this guid.
				if (auto ObjectPtr = MapGuidToObject.Find(ObjectGuid))
				{
					UObject* ExistingObject = ObjectPtr->Get();
					if (IsValid(ExistingObject)
						&& ExistingObject->GetClass() == ObjectClass
						&& ExistingObject->GetOuter() == OuterObjectPtr->Get())
					{
						CreatedNewObject = ExistingObject;
						MapObjectToOriginGuid.Add(CreatedNewObject, ObjectGuid);
						CollectDefaultSubobjects(CreatedNewObject, ObjectGuid, *ObjectData);
						PendingObjectGuids.RemoveAtSwap(PendingIndex);
						bMadeProgress = true;
						continue;
					}

					UE_LOG(LGUI, Warning,
						TEXT("Discarding incompatible existing object mapping for guid '%s' in prefab '%s'."),
						*ObjectGuid.ToString(), *PrefabAssetPath);
					MapGuidToObject.Remove(ObjectGuid);
				}
#endif
#if WITH_EDITOR
				if (auto ExistingObject = FindObjectWithOuter(*OuterObjectPtr, nullptr, ObjectData->ObjectName))
				{
					const bool bNameIsFree = WidgetSerializer::ReleaseNameFromExistingObject(Cast<UObject>(ExistingObject));
					UE_LOG(LGUI, Warning, TEXT("[%s].%d Object '%s' already exist on outer '%s', will destroy and rename exiting one%s. Prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(ObjectData->ObjectName.ToString()), *(*OuterObjectPtr)->GetPathName(), bNameIsFree ? TEXT("") : TEXT(" (FAILED, the new object will be given a different name)"), *PrefabAssetPath);
				}
#endif
				CreatedNewObject = NewObject<UObject>(*OuterObjectPtr, ObjectClass, ObjectData->ObjectName, (EObjectFlags)ObjectData->ObjectFlags);
				MapGuidToObject.Add(ObjectGuid, CreatedNewObject);
				MapObjectToOriginGuid.Add(CreatedNewObject, ObjectGuid);
				CollectDefaultSubobjects(CreatedNewObject, ObjectGuid, *ObjectData);
				PendingObjectGuids.RemoveAtSwap(PendingIndex);
				bMadeProgress = true;
			}

			if (!bMadeProgress)
			{
				for (const FGuid& ObjectGuid : PendingObjectGuids)
				{
					const FLexUIObjectSaveData* ObjectData = SavedObjects.Find(ObjectGuid);
					UE_LOG(LGUI, Warning, TEXT("[%s].%d Missing Outer object when creating object: '%s'. Prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ObjectData ? *(ObjectData->ObjectName.ToString()) : TEXT("Unknown"), *PrefabAssetPath);
				}
				break;
			}
		}
	}

	ULexWidget* WidgetSerializer::GenerateWidgetArray(TArray<FLexUIWidgetSaveData>& SavedWidgets, TMap<FGuid, FLexUIObjectSaveData>& SavedObjects, TMap<FGuid, FGuid>& MapWidgetToParent, FGuid ParentGuid)
	{
		ULexWidget* RootWidget = nullptr;//first actor is the RootWidget
		for (int i = 0; i < SavedWidgets.Num(); i++)
		{
			auto& InWidgetData = SavedWidgets[i];
			if (InWidgetData.bIsPrefab)
			{
				auto PrefabIndex = InWidgetData.PrefabAssetIndex;
				if (auto PrefabAssetObject = FindAssetFromListByIndex(PrefabIndex))
				{
					if (auto SubPrefabAsset = Cast<ULexUIPrefab>(PrefabAssetObject))
					{
						ULexWidget* SubPrefabRootWidget = nullptr;
						FLexUISubPrefabData SubPrefabData;
						SubPrefabData.PrefabAsset = SubPrefabAsset;
						FGuid RootPanelSlotGuid;
						FLexUIPrefabOverrideParameterSaveData RootPanelSlotOverride;
						{
							const FLexUISubPrefabObjectUniqueIdSaveData PanelSlotId{
								InWidgetData.WidgetGuid, GetSubPrefabRootPanelSlotOriginGuid() };
							if (const FGuid* StoredGuid = InWidgetData.MapObjectIdToNewlyCreatedId.Find(PanelSlotId))
							{
								RootPanelSlotGuid = *StoredGuid;
								if (const FLexUIPrefabOverrideParameterSaveData* StoredOverride =
									InWidgetData.MapObjectGuidToSubPrefabOverrideParameter.Find(RootPanelSlotGuid))
								{
									RootPanelSlotOverride = *StoredOverride;
								}
							}
						}

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
							for (auto& KeyValue : InWidgetData.MapObjectGuidFromParentPrefabToSubPrefab)
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
							// The parent-owned root slot never has a persistent GUID in the
							// source prefab, so its unique-id mapping must survive every load.
							bool bAnyGuidFrom_MapObjectIdToNewlyCreatedId = RootPanelSlotGuid.IsValid();
							auto GetObjectGuidInParent = [&](const FGuid& GuidInSubPrefab, const FGuid& GuidInOriginPrefab) {
								FGuid GuidInParent;
								auto ObjectGuidInParentPrefabPtr = MapObjectGuidFromSubPrefabToParentPrefab.Find(GuidInSubPrefab);
								if (ObjectGuidInParentPrefabPtr == nullptr)
								{
									auto UniqueId = FLexUISubPrefabObjectUniqueIdSaveData{ InWidgetData.WidgetGuid, GuidInOriginPrefab };
									if (auto GuidInParentPtr = InWidgetData.MapObjectIdToNewlyCreatedId.Find(UniqueId))
									{
										GuidInParent = *GuidInParentPtr;
									}
									else
									{
										GuidInParent = FGuid::NewGuid();
										InWidgetData.MapObjectIdToNewlyCreatedId.Add(UniqueId, GuidInParent);
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
								[&](ULexWidget*, const TMap<FGuid, TObjectPtr<UObject>>& InSubPrefabMapGuidToObject, const TMap<TObjectPtr<UObject>, FGuid>& InMapObjectToOriginGuid, const TArray<ULexWidget*>& InSubWidgets) {
								//collect sub prefab's object and guid to parent map, so all objects are ready when set override parameters
								for (auto& KeyValue : InSubPrefabMapGuidToObject)
								{
									auto& GuidInSubPrefab = KeyValue.Key;
									auto& ObjectInSubPrefab = KeyValue.Value;
									const FGuid* GuidInOriginPrefab = InMapObjectToOriginGuid.Find(ObjectInSubPrefab);
									if (!GuidInOriginPrefab)
									{
										// Existing-object reloads can carry parent-owned objects, such as the
										// nested root panel slot, which have no GUID in the child prefab.
										continue;
									}

									auto GuidInParent = GetObjectGuidInParent(GuidInSubPrefab, *GuidInOriginPrefab);

									if (auto RecordDataPtr = InWidgetData.MapObjectGuidToSubPrefabOverrideParameter.Find(GuidInParent))
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
									if (InWidgetData.MapObjectIdToNewlyCreatedId.Num() > 0)
									{
										InWidgetData.MapObjectIdToNewlyCreatedId.Empty();
									}
								}
								else
								{
									//convert data to save
									for (auto& DataItem : InWidgetData.MapObjectIdToNewlyCreatedId)
									{
										SubPrefabData.MapObjectIdToNewlyCreatedId.Add({ DataItem.Key.RootWidgetGuidInParentPrefab, DataItem.Key.ObjectGuidInOriginPrefab }, DataItem.Value);
									}
								}
								//collect sub-prefab's actor to parent prefab
								AllWidgetArray.Append(InSubWidgets);
								MapObjectToOriginGuid.Append(InMapObjectToOriginGuid);
								};

							SubPrefabRootWidget = WidgetSerializer::LoadSubPrefab(this->World, this->OwnerObject, SubPrefabAsset, nullptr, SubMapGuidToObject
								, NewOnSubPrefabFinishDeserializeFunction
							);
						}
						
						if (SubPrefabRootWidget != nullptr)
						{
							FWidgetAttachment CompData;
							CompData.Widget = SubPrefabRootWidget;
							FGuid SubPrefabRootCompGuid;
							for (auto& KeyValue : MapGuidToObject)
							{
								if (KeyValue.Value == SubPrefabRootWidget)
								{
									SubPrefabRootCompGuid = KeyValue.Key;
									break;
								}
							}
			if (auto ParentGuidPtr = MapWidgetToParent.Find(SubPrefabRootCompGuid))
			{
				CompData.ParentGuid = *ParentGuidPtr;
				WidgetAttachmentArray.Add(CompData);
			}

							SubPrefabMap.Add(SubPrefabRootWidget, SubPrefabData);
							if (RootPanelSlotGuid.IsValid())
							{
								FSubPrefabRootPanelSlotOverrideData& PendingSlot =
									SubPrefabRootPanelSlotOverrides.AddDefaulted_GetRef();
								PendingSlot.RootWidget = SubPrefabRootWidget;
								PendingSlot.RootWidgetGuid = InWidgetData.WidgetGuid;
								PendingSlot.PanelSlotGuid = RootPanelSlotGuid;
								PendingSlot.ParameterDatas = MoveTemp(RootPanelSlotOverride.OverrideParameterData);
								PendingSlot.ParameterNames = MoveTemp(RootPanelSlotOverride.OverrideParameterNames);
							}

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
				if (auto WidgetClass = FindClassFromListByIndex(InWidgetData.ObjectClass))
				{
					auto CollectDefaultSubobjects = [&](ULexWidget* TargetWidget) {
						//Collect default sub objects
						TArray<UObject*> DefaultSubObjects;
						TargetWidget->GetDefaultSubobjects(DefaultSubObjects);
						for (auto DefaultSubObject : DefaultSubObjects)
						{
							if (DefaultSubObject->HasAnyFlags(EObjectFlags::RF_Transient))continue;
							auto Index = InWidgetData.DefaultSubObjectNameArray.IndexOfByKey(DefaultSubObject->GetFName());
							if (Index == INDEX_NONE)
							{
								UE_LOG(LGUI, Warning, TEXT("[%s].%d Missing guid for default sub object: %s. Prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(DefaultSubObject->GetFName().ToString()), *PrefabAssetPath);
								continue;
							}
							auto DefaultSubObjectGuid = InWidgetData.DefaultSubObjectGuidArray[Index];
							MapGuidToObject.Add(DefaultSubObjectGuid, DefaultSubObject);
							MapObjectToOriginGuid.Add(DefaultSubObject, DefaultSubObjectGuid);
						}
						};

					ULexWidget* NewWidget = nullptr;
#if WITH_EDITOR
					//MapGuidToObject can passed from LoadPrefabWithExistingObjects, so validate the candidate before reuse.
					if (auto WidgetPtr = MapGuidToObject.Find(InWidgetData.WidgetGuid))
					{
						ULexWidget* ExistingWidget = Cast<ULexWidget>(WidgetPtr->Get());
						if (IsValid(ExistingWidget)
							&& ExistingWidget->GetClass() == WidgetClass
							&& ExistingWidget->GetOuter() == OwnerObject)
						{
							NewWidget = ExistingWidget;
							MapObjectToOriginGuid.Add(NewWidget, InWidgetData.WidgetGuid);
							CollectDefaultSubobjects(NewWidget);
						}
						else
						{
							UE_LOG(LGUI, Warning,
								TEXT("Discarding incompatible existing widget mapping for guid '%s' in prefab '%s'."),
								*InWidgetData.WidgetGuid.ToString(), *PrefabAssetPath);
							MapGuidToObject.Remove(InWidgetData.WidgetGuid);
						}
					}
#endif
					if (!NewWidget)
					{
						NewWidget = NewObject<ULexWidget>(OwnerObject, WidgetClass, NAME_None, (EObjectFlags)InWidgetData.ObjectFlags);
						MapGuidToObject.Add(InWidgetData.WidgetGuid, NewWidget);
						MapObjectToOriginGuid.Add(NewWidget, InWidgetData.WidgetGuid);
						CollectDefaultSubobjects(NewWidget);
					}

					FWidgetAttachment CompData;
					CompData.Widget = NewWidget;
					if (auto ParentGuidPtr = MapWidgetToParent.Find(InWidgetData.WidgetGuid))
					{
						CompData.ParentGuid = *ParentGuidPtr;
						WidgetAttachmentArray.Add(CompData);
					}

					AllWidgetArray.Add(NewWidget);

					if (i == 0)
					{
						RootWidget = NewWidget;
					}
				}
				else
				{
					UE_LOG(LGUI, Warning, TEXT("[%s].%d Class of index:%d not found! Prefab: '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, (InWidgetData.ObjectClass), *PrefabAssetPath);
				}
			}
		}
		return RootWidget;
	}
}

#undef LOCTEXT_NAMESPACE


