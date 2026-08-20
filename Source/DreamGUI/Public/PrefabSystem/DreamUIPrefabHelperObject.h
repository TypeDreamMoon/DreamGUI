// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "DreamGUI.h"
#include "Components/SceneComponent.h"
#include "DreamUIPrefab.h"
#include "DreamUIPrefabHelperObject.generated.h"

class AActor;

/**
 * helper object for manage prefab's load/save
 */
UCLASS(ClassGroup = (DreamGUI), NotBlueprintType, NotBlueprintable)
class DREAMGUI_API UDreamUIPrefabHelperObject : public UObject
{
	GENERATED_BODY()

public:	
	UDreamUIPrefabHelperObject();

	/** Prefab object asset, null means this is a level prefab */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TObjectPtr<UDreamUIPrefab> PrefabAsset = nullptr;
	/** Root widget of this prefab, null means this is a level prefab */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TObjectPtr<UDreamWidget> LoadedRootWidget = nullptr;
	/** Map from guid to object, include all sub-prefab's object. Note object guid is not equals to sub-prefab's same object's guid. */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
	/** Map to sub prefab */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> SubPrefabMap;
#if WITH_EDITORONLY_DATA
	/** Broken widget-sub-prefab collection, only for level's sub-prefab */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TSet<TObjectPtr<UDreamWidget>> MissingPrefab;
#endif

#if WITH_EDITOR
	virtual void BeginDestroy()override;
#if WITH_EDITORONLY_DATA
	TWeakObjectPtr<UWorld> PrefabInstanceWorld = nullptr;
#endif

	void Init(UDreamUIPrefab* InPrefab, class FDreamUIPrefabInstanceScene* InPrefabInstanceScene);
	
	UDreamUIPrefab* GetSubPrefabAsset(UDreamWidget* InSubPrefabWidget);
	bool SavePrefab();
	void ClearLoadedPrefab();
	bool IsWidgetBelongsToSubPrefab(const UDreamWidget* InWidget);
	/**
	 * Actor was belongs to a sub prefab, but the prefab asset is missing..
	 * Only call this function after IsActorBelongsToSubPrefab.
	 */
	bool IsWidgetBelongsToMissingSubPrefab(const UDreamWidget* InWidget);
	bool IsSubPrefabRootWidget(const UDreamWidget* InWidget);
	bool IsWidgetBelongsToThis(const UDreamWidget* InWidget);
	bool ClearInvalidObjectAndGuid();
	/** Remove valid GUID objects whose owning widget is no longer reachable from LoadedRootWidget. */
	int32 CleanupObjectsOutsideRootHierarchy();
	void AddMemberPropertyToSubPrefab(UDreamWidget* InSubPrefabWidget, UObject* InObject, FName InPropertyName);
	void RemoveMemberPropertyFromSubPrefab(UDreamWidget* InSubPrefabWidget, UObject* InObject, FName InPropertyName);
	void RemoveAllMemberPropertyFromSubPrefab(UDreamWidget* InSubPrefabActor, bool InIncludeRootTransform);
	FDreamUISubPrefabData GetSubPrefabData(UDreamWidget* InSubPrefabWidget);
	UDreamWidget* GetSubPrefabRootWidget(UDreamWidget* InSubPrefabWidget);
	/** For parent prefab. When parent prefab want to apply override parameter to subprefab, but the parameter belongs to subprefab's subprefab, then we need to mark override parameter for subprefab. */
	void MarkOverrideParameterFromParentPrefab(UObject* InObject, const TArray<FName>& InPropertyNames);
	void MarkOverrideParameterFromParentPrefab(UObject* InObject, FName InPropertyName);

	/** If sub prefab changed, then update parent prefab */
	bool RefreshOnSubPrefabDirty(UDreamUIPrefab* InSubPrefab, UDreamWidget* InSubPrefabRootWidget = nullptr);

	void CopyRootObjectParentAnchorData(UObject* InObject, UObject* OriginObject);

	void RevertPrefabPropertyValue(UObject* ContextObject, FProperty* Property, void* ContainerPointerInSrc, void* ContainerPointerInDst, const FDreamUISubPrefabData& SubPrefabData, int RawArrayIndex = 0, bool IsInsideRawArray = false);
	void ApplyPrefabPropertyValue(UObject* ContextObject, FProperty* Property, void* ContainerPointerInSrc, void* ContainerPointerInDst, const FDreamUISubPrefabData& SubPrefabData, int RawArrayIndex = 0, bool IsInsideRawArray = false);
	FName GetExtraRelatedPropertyForApplyOrRevert(UObject* InObject, FName InPropertyName);
	void AfterObjectPropertyApplyOrRevert(UObject* InObject, FName InPropertyName);

	void RevertPrefabOverride(UObject* InObject, const TArray<FName>& InPropertyNames);
	void RevertAllPrefabOverride(UObject* InObject);
	void ApplyPrefabOverride(UObject* InObject, const TArray<FName>& InPropertyNames);
	void ApplyAllOverrideToPrefab(UObject* InObject);

	void RefreshSubPrefabVersion(UDreamWidget* InSubPrefabRootWidget);

	void MakePrefabAsSubPrefab(UDreamUIPrefab* InPrefab, UDreamWidget* InWidget, const TMap<FGuid, TObjectPtr<UObject>>& InSubMapGuidToObject, const TArray<FDreamUIPrefabOverrideParameterData>& InObjectOverrideParameterArray);
	void RemoveSubPrefabByRootWidget(UDreamWidget* InPrefabRootWidget);
	void RemoveSubPrefabByAnyWidgetOfSubPrefab(UDreamWidget* InPrefabWidget);
	UDreamUIPrefab* GetPrefabAssetBySubPrefabObject(UObject* InObject);
	bool GetAnythingDirty()const;
	void SetAnythingDirty();
	void CheckPrefabVersion();
	FSimpleMulticastDelegate OnSubPrefabNewVersionUpdated;
	/**
	 * @return	true if anything changed
	 */
	bool CleanupInvalidSubPrefab();
private:
	bool bAnythingDirty = false;
	bool bCanCollectProperty = true;
	bool bCanNotifyComponentCreateDelete = true;
	bool bAlreadyShowMessageAtThisFrame = false;

	void OnObjectPropertyChanged(UObject* InObject, struct FPropertyChangedEvent& InPropertyChangedEvent);
	void OnPreObjectPropertyChanged(UObject* InObject, const class FEditPropertyChain& InEditPropertyChain);
	void TryCollectPropertyToOverride(UObject* InObject, FProperty* InMemberProperty);

	UWorld* GetPrefabWorld()const;

	/**
	 * For Level prefab only. Object link could still exist if delete sub-prefab's root widget by UE's delete function.
	 * @return true if anything change
	 */
	bool CleanupInvalidLinkToSubPrefabObject();

public:
	static UDreamUIPrefabHelperObject* GetPrefabHelperObject_WhichManageThisWidget(UDreamWidget* InWidget);
#endif
};
