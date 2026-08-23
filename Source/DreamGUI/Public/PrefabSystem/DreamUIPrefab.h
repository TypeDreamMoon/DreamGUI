// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once
#include "CoreMinimal.h"
#include "Misc/NetworkVersion.h"
#include "Engine/EngineBaseTypes.h"
#include "DreamUIPrefabInstanceScene.h"
#include "DreamUIPrefab.generated.h"

#define LEXUIPREFAB_SERIALIZER_NEWEST_INCLUDE "PrefabSystem/WidgetSerializer.h"
#define LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE DreamUIPrefabSystem

class UDreamWidget;
class UDreamUIBehaviour;

enum class EDreamUIPrefabVersion : uint16
{
	/** Version 2: Support ActorGuid (start from 4.26). */
	OldVersion = 2,
	/**
	 * Version 3: Use UE's build-in FArchive to serialize/deserialize.
	 *		Compare to version2: 1. About 2~3 times faster when deserialize.
	 *							 2. Smaller disc space.
	 *							 3. Support CoreRedirects.
	 *							 4. Support object flags.
	 *							 5. Support all object serialization and reference, include default sub object and component.
	 */
	BuiltinFArchive = 3,
	/** Support nested default sub object. */
	NestedDefaultSubObject = 4,
	/** Support UObject name. */
	ObjectName = 5,
	/** Support common actor types, not just UI actor. */
	CommonActor = 6,
	/** Support new actor under sub-prefab's actor. */
	ActorAttachToSubPrefab = 7,
	/**
	 * This version is mainly to solve the case:
	 *		There are Prefabs, A is origin prefab, B contains A, C contains B,
	 *		open A and add a new object O to A (new Actor or ActorComponent or other UObject), apply A then close,
	 *		then open C and modify property on object O, apply C then close,
	 *		open C again, here error happens, because O is not exist in B yet, so B will always create new guid for O, then pass to C as sub-prefab, so when open C again, the modified property on O will not serialize, because sub-prefab's guid on O is changed.
	 * Solution:
	 *		Use a map data D, map from object's unique id (sub-prefab's root actor's guid and new created object's origin guid --origin guid means the object's guid in root prefab) to created guid,
	 *		when load sub-prefab, if not find guid then create a new guid and store it in data D, next time when load sub-prefab if still don't find the guid (because B create a new guid for it) then search in data D and use existing guid,
	 *		so the guid can persist.
	 */
	NewObjectOnNestedPrefab = 8,
	/**
	 * Serialize FText as reference, to solve problem about FText serialization from 5.7 to 5.8
	 * This version also use WidgetSerializer, just change DreamUIObjectReaderAndWriter's FArchive<<FText to serialize FText as reference, so it is not compatible with previous version.
	 * Note: This version is not compatible with previous version, so if you want to use this version, you need to re-create all prefab assets.
	 */
	FTextAsReference = 9,

	/** new version must be added before this line. */
	MAX_NO_USE,
	NEWEST = MAX_NO_USE - 1,
};

/**
 * Current prefab system version
 */
#define LEXUI_CURRENT_PREFAB_VERSION (uint16)EDreamUIPrefabVersion::NEWEST

/** Object-model migrations are versioned separately from the binary serializer format. */
enum class EDreamUIPrefabSchemaVersion : uint16
{
	Unversioned = 0,
	PanelSlotOwnership = 1,

	MAX_NO_USE,
	NEWEST = MAX_NO_USE - 1,
};

#define LEXUI_CURRENT_PREFAB_SCHEMA_VERSION (uint16)EDreamUIPrefabSchemaVersion::NEWEST

class UDreamUIPrefab;
class UDreamUIPrefabHelperObject;

struct DREAMGUI_API FDreamUIPrefabSchemaMigrationReport
{
	uint16 FromVersion = 0;
	uint16 ToVersion = LEXUI_CURRENT_PREFAB_SCHEMA_VERSION;
	int32 ChangedObjectCount = 0;
	bool bSchemaVersionUpdated = false;
	TArray<FString> Actions;
	TArray<FString> Warnings;
	TArray<FString> Errors;

	bool HasChanges() const { return bSchemaVersionUpdated || ChangedObjectCount > 0; }
	bool HasErrors() const { return !Errors.IsEmpty(); }
	FString ToString() const;
};

USTRUCT(NotBlueprintType)
struct DREAMGUI_API FDreamUIPrefabOverrideParameterData
{
	GENERATED_BODY()
public:
	FDreamUIPrefabOverrideParameterData() {};

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TWeakObjectPtr<UObject> Object;
	/** UObject's member property name */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TArray<FName> MemberPropertyNames;
};

/** Unique id for newly created object in sub-prefab, just for store data here. Check description on EDreamUIPrefabVersion.NewObjectOnNestedPrefab */
USTRUCT(NotBlueprintType)
struct FDreamUISubPrefabObjectUniqueId
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FGuid RootWidgetGuidInParentPrefab;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FGuid ObjectGuidInOriginPrefab;

	bool operator==(const FDreamUISubPrefabObjectUniqueId& other)const
	{
		return this->RootWidgetGuidInParentPrefab == other.RootWidgetGuidInParentPrefab && this->ObjectGuidInOriginPrefab == other.ObjectGuidInOriginPrefab;
	}
	friend FORCEINLINE uint32 GetTypeHash(const FDreamUISubPrefabObjectUniqueId& other)
	{
		return HashCombine(GetTypeHash(other.RootWidgetGuidInParentPrefab), GetTypeHash(other.ObjectGuidInOriginPrefab));
	}
};

USTRUCT(NotBlueprintType)
struct DREAMGUI_API FDreamUISubPrefabData
{
	GENERATED_BODY()
public:
	FDreamUISubPrefabData();
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")TObjectPtr<UDreamUIPrefab> PrefabAsset = nullptr;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")TArray<FDreamUIPrefabOverrideParameterData> ObjectOverrideParameterArray;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")TMap<FGuid, FGuid> MapObjectGuidFromParentPrefabToSubPrefab;
	/** Check description on EDreamUIPrefabVersion.NewObjectOnNestedPrefab */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")TMap<FDreamUISubPrefabObjectUniqueId, FGuid> MapObjectIdToNewlyCreatedId;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
#if WITH_EDITORONLY_DATA
	/** For level editor, combine all create time (include all sub prefab) to create this MD5, to tell if this prefab is latest version. */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")FString OverallVersionMD5;
	/** Temporary color for quick identify in editor */
	FLinearColor EditorIdentifyColor;
#endif
public:
	void AddMemberProperty(UObject* InObject, FName InPropertyName);
	void AddMemberProperty(UObject* InObject, const TArray<FName>& InPropertyNames);
	void RemoveMemberProperty(UObject* InObject, FName InPropertyName);
	void RemoveMemberProperty(UObject* InObject);
	/** 
	 * Check parameters, remove invalid.
	 * @return true if anything changed.
	 */
	bool CheckParameters();
};

USTRUCT(NotBlueprintType)
struct FDreamUIPrefabDataForPrefabEditor
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FVector ViewLocation = FVector::ZeroVector;
	UPROPERTY()
	FRotator ViewRotation = FRotator::ZeroRotator;
	UPROPERTY()
	FVector ViewOrbitLocation = FVector::ZeroVector;
	UPROPERTY()
	FIntPoint CanvasSize = FIntPoint(1920, 1080);
	/**
	 * Device resolution the designer picked. The prefab's own canvas-scaler rule turns it into
	 * CanvasSize, which is why the two differ for anything but ConstantPixelSize. Zero means an
	 * asset authored before the picker knew about the rule, where CanvasSize WAS the picked size.
	 */
	UPROPERTY()
	FIntPoint DesignViewportSize = FIntPoint::ZeroValue;
	/**
	 * Preview render mode for the prefab editor (an EDreamRenderMode value). ScreenSpaceOverlay by
	 * default, so a fresh prefab previews through the canvas's OWN virtual camera -- the projection
	 * play actually uses, and the only one where a declared Perspective can show itself; a
	 * world-space preview is projected by the editor camera, where Perspective is correctly inert.
	 * Note that delta serialization makes prefabs saved while matching the old WorldSpace default
	 * inherit this one, which is the intended direction; the toolbar toggle switches per asset.
	 */
	UPROPERTY()
	uint8 CanvasRenderMode = 0;
	UPROPERTY()
	TEnumAsByte<EViewModeIndex> ViewMode = EViewModeIndex::VMI_Lit;//editor viewport's view-mode
	UPROPERTY()
	uint8 ViewportType = 2;//ELevelViewportType::LVT_OrthoYZ
	UPROPERTY()
	TSet<FGuid> UnexpandedWidgetSet;
	/** Widgets hidden only in the prefab designer; runtime WidgetActive is not changed. */
	UPROPERTY()
	TSet<FGuid> HiddenWidgetSet;
	/** Widgets protected from selection and manipulation in the designer. */
	UPROPERTY()
	TSet<FGuid> LockedWidgetSet;
	UPROPERTY()
	bool bDesignerGridSnapEnabled = true;
	UPROPERTY()
	float DesignerGridSize = 10.0f;
	UPROPERTY()
	bool bShowDesignerGuides = true;
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIPrefab_LoadPrefabCallback, UDreamWidget*, LoadedRootWidget);

/**
 * Similar to Unity3D's Prefab. Store actor and it's hierarchy then serialize to asset, deserialize and restore when needed.
 * If you don't want to package the prefab for runtime (only use in editor), you can put the prefab in a folder named "EditorOnly".
 */
UCLASS(ClassGroup = (DreamGUI), BlueprintType, DisplayName="DreamUI Prefab")
class DREAMGUI_API UDreamUIPrefab : public UObject
{
	GENERATED_BODY()

public:
	UDreamUIPrefab();
	friend class FDreamUIPrefabCustomization;
	friend class UDreamUIPrefabFactory;
	friend class FDreamUIPrefabEditor;

private:
	/** The single root behaviour used as this prefab's logic host. The component instance remains part of BinaryData. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (AllowPrivateAccess = true))
	TSubclassOf<UDreamUIBehaviour> BehaviourClass;
public:
	UFUNCTION(BlueprintPure, Category = "DreamUI|Prefab")
	TSubclassOf<UDreamUIBehaviour> GetBehaviourClass() const { return BehaviourClass; }
#if WITH_EDITOR
	void SetBehaviourClass(TSubclassOf<UDreamUIBehaviour> InClass) { BehaviourClass = InClass; }
#endif

#if WITH_EDITORONLY_DATA
private:
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		bool bIsPrefabVariant = false;
public:
	/** put actual UObject in this array, and store index in prefab */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
		TArray<TObjectPtr<UObject>> ReferenceAssetList;
	/** put actual UClass in this array, and store index in prefab */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
		TArray<TObjectPtr<UClass>> ReferenceClassList;
	/** put actual FName in this array, and store index in prefab */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
		TArray<FName> ReferenceNameList;
	/** put actual FText in this array, and store index in prefab */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	TArray<FText> ReferenceTextList;
#endif

#if WITH_EDITORONLY_DATA
public:
	/** serialized data for editor use, this data contains editor-only property include property's name, will compare property name when deserialize form this */
	UPROPERTY()
		TArray<uint8> BinaryData;
	/** The time point when create/save this prefab. Use UtcNow from prefab version 6. */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		FDateTime CreateTime;
#endif
	/** Prefab system's version when creating this prefab */
	UPROPERTY()
		uint16 PrefabVersion;
	/** Hierarchy and component-contract version, independent from PrefabVersion's binary format. */
	UPROPERTY()
		uint16 PrefabSchemaVersion;
	/** Engine's major version when creating this prefab */
	UPROPERTY()
		uint16 EngineMajorVersion;
	/** Engine's minor version when creating this prefab */
	UPROPERTY()
		uint16 EngineMinorVersion;
	UPROPERTY()
		uint16 EnginePatchVersion;
#if WITH_EDITORONLY_DATA
	UPROPERTY()int32 ArchiveVersion = (int32)EUnrealEngineObjectUE4Version::VER_UE4_CORRECT_LICENSEE_FLAG;//this default version is the time when DreamGUIPrefab support FArchive version
	UPROPERTY()int32 ArchiveVersionUE5 = -1;
	UPROPERTY()int32 ArchiveLicenseeVer = (int32)EUnrealEngineObjectLicenseeUEVersion::VER_LIC_NONE;
	UPROPERTY()uint32 ArEngineNetVer = (uint32)FEngineNetworkCustomVersion::ReplayDormancy;
	UPROPERTY()uint32 ArGameNetVer = 0;
#endif
	UPROPERTY()int32 ArchiveVersion_ForBuild = (int32)EUnrealEngineObjectUE4Version::VER_UE4_CORRECT_LICENSEE_FLAG;//this default version is the time when DreamGUIPrefab support FArchive version
	UPROPERTY()int32 ArchiveVersionUE5_ForBuild = -1;
	UPROPERTY()int32 ArchiveLicenseeVer_ForBuild = (int32)EUnrealEngineObjectLicenseeUEVersion::VER_LIC_NONE;
	UPROPERTY()uint32 ArEngineNetVer_ForBuild = (uint32)FEngineNetworkCustomVersion::ReplayDormancy;
	UPROPERTY()uint32 ArGameNetVer_ForBuild = 0;

	/**
	 * Size of the design canvas this prefab was authored on -- the surface the prefab editor shows.
	 * A prefab loaded with no parent widget (a world-space presenter, LoadPrefab with a null parent)
	 * sizes its root to this, since a stretched root has nothing else to stretch to. A prefab placed
	 * under another widget, or shown as a screen-space page, fills its parent and ignores it.
	 * The editor's screen-size picker writes it; it can also be edited here directly.
	 */
	UPROPERTY(EditAnywhere, Category = "Canvas", meta = (ClampMin = "1"))
	FIntPoint CanvasSize = FIntPoint(1920, 1080);
	/** build version for ReferenceAssetList */
	UPROPERTY()
		TArray<TObjectPtr<UObject>> ReferenceAssetListForBuild;
	/** build version for ReferenceClassList */
	UPROPERTY()
		TArray<TObjectPtr<UClass>> ReferenceClassListForBuild;
	/** build version for ReferenceNameList */
	UPROPERTY()
		TArray<FName> ReferenceNameListForBuild;
	/** build version for ReferenceTextList */
	UPROPERTY()
	TArray<FText> ReferenceTextListForBuild;
	/**
	 * serialized data for publish, not contain property name and editor only property. much more faster than BinaryData when deserialize
	 */
	UPROPERTY()
		TArray<uint8> BinaryDataForBuild;
#if WITH_EDITORONLY_DATA
	UPROPERTY(Instanced, Transient)
		TObjectPtr<class UThumbnailInfo> ThumbnailInfo;
	UPROPERTY(Transient)
		bool bThumbnailDirty = false;
	UPROPERTY()
		FDreamUIPrefabDataForPrefabEditor PrefabDataForPrefabEditor;
private:
	UPROPERTY(VisibleAnywhere, Transient, Category = "DreamGUI", DuplicateTransient)
		TObjectPtr<UDreamUIPrefabHelperObject> PrefabHelperObject = nullptr;
	TUniquePtr<FDreamUIPrefabInstanceScene> PrefabInstanceScene;
#endif
public:
	/**
	 * LoadPrefab to create actor.
	 * Awake function in DreamUIBehaviour and DreamGUIPrefabInterface will be called right after LoadPrefab is done.
	 * @param InParent Parent scene component that the created root actor will be attached to. Can be null so the created root actor will not attach to anyone.
	 * @param InCallbackBeforeAwake This callback function will execute before Awake event, parameter "Actor" is the loaded root actor.
	 * @param SetRelativeTransformToIdentity Set created root actor's transform to zero after load.
	 */
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "InCallbackBeforeAwake,SetRelativeTransformToIdentity", UnsafeDuringActorConstruction = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "InCallbackBeforeAwake"), Category = DreamGUI)
		UDreamWidget* LoadPrefab(UObject* WorldContextObject, UDreamWidget* InParent, const FDreamUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake, bool SetRelativeTransformToIdentity = false);
	/**
	 * LoadPrefab to create actor.
	 * Awake function in DreamUIBehaviour and DreamUIPrefabInterface will be called right after LoadPrefab is done.
	 * @param InParent Parent scene component that the created root actor will be attached to. Can be null so the created root actor will not attach to anyone.
	 * @param Location Set created root actor's location after load.
	 * @param Rotation Set created root actor's rotation after load.
	 * @param Scale Set created root actor's scale after load.
	 */
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "InCallbackBeforeAwake", UnsafeDuringActorConstruction = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "InCallbackBeforeAwake"), Category = DreamGUI)
		UDreamWidget* LoadPrefabWithTransform(UObject* WorldContextObject, UDreamWidget* InParent, FVector Location, FRotator Rotation, FVector Scale, const FDreamUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake);
	UDreamWidget* LoadPrefabWithTransform(UObject* WorldContextObject, UDreamWidget* InParent, FVector Location, FQuat Rotation, FVector Scale, const TFunction<void(UDreamWidget*)>& InCallbackBeforeAwake);
	/**
	 * LoadPrefab to create actor.
	 * Awake function in DreamUIBehaviour and DreamUIPrefabInterface will be called right after LoadPrefab is done.
	 * @param InParent Parent widget that the created root actor will be attached to. Can be null so the created root actor will not attach to anyone.
	 * @param InReplaceAssetMap Replace source asset to dest before load the prefab.
	 * @param InReplaceClassMap Replace source class to dest before load the prefab.
	 */
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "InCallbackBeforeAwake", UnsafeDuringActorConstruction = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "InCallbackBeforeAwake"), Category = DreamGUI)
		UDreamWidget* LoadPrefabWithReplacement(UObject* WorldContextObject, UDreamWidget* InParent, const TMap<UObject*, UObject*>& InReplaceAssetMap, const TMap<UClass*, UClass*>& InReplaceClassMap, const FDreamUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake);
	/**
	 * LoadPrefab to create actor.
	 * Awake function in DreamUIBehaviour and DreamUIPrefabInterface will be called right after LoadPrefab is done.
	 * @param InParent Parent scene component that the created root actor will be attached to. Can be null so the created root actor will not attach to anyone.
	 * @param SetRelativeTransformToIdentity Set created root actor's transform to zero after load.
	 * @param InCallbackBeforeAwake This callback function will execute before Awake event, parameter "Actor" is the loaded root actor.
	 */
	UDreamWidget* LoadPrefab(UWorld* InWorld, UDreamWidget* InParent, const TFunction<void(UDreamWidget*)>& InCallbackBeforeAwake = nullptr, bool SetRelativeTransformToIdentity = false);
	/**
	 * LoadPrefab and keep reference of source objects.
	 */
	UDreamWidget* LoadPrefabWithExistingObjects(UWorld* InWorld, UObject* InOuter, UDreamWidget* InParent
		, TMap<FGuid, TObjectPtr<UObject>>& InOutMapGuidToObject, TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& OutSubPrefabMap
	);
	bool IsPrefabBelongsToThisSubPrefab(UDreamUIPrefab* InPrefab, bool InRecursive);
	/** Repair and validate the current hierarchy before it is serialized. */
	FDreamUIPrefabSchemaMigrationReport ApplySchemaMigration(UDreamWidget* RootWidget);
#if WITH_EDITOR
	void CopyDataTo(UDreamUIPrefab* TargetPrefab);
	bool GetIsPrefabVariant()const { return bIsPrefabVariant; }
	FString GenerateOverallVersionMD5();
	/** Evaluate migration against an isolated duplicate without changing this asset or its live hierarchy. */
	FDreamUIPrefabSchemaMigrationReport PreviewSchemaUpgrade();
	/** Upgrade the live hierarchy and persist it into this asset. */
	FDreamUIPrefabSchemaMigrationReport UpgradeSchema();
#endif
private:
	FDreamUIPrefabSchemaMigrationReport EvaluateSchemaMigration(UDreamWidget* RootWidget, bool bApplyChanges);
public:
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	void SetRootWidgetNameFromPrefab();
public:
	FDreamUIPrefabInstanceScene* GetPrefabInstanceScene();
	void ClearPrefabInstanceScene();
	void EnsureInstanceObjects();
	UDreamUIPrefabHelperObject* GetPrefabHelperObject();

	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)override;
	virtual void WillNeverCacheCookedPlatformDataAgain()override;
	virtual void ClearCachedCookedPlatformData(const ITargetPlatform* TargetPlatform)override;
	virtual void PostInitProperties()override;
	virtual void PostCDOContruct()override;
	virtual void PostRename(UObject* OldOuter, const FName OldName)override;
	virtual void PreDuplicate(FObjectDuplicationParameters& DupParams)override;
	virtual void PostDuplicate(bool bDuplicateForPIE)override;
	virtual void PostLoad()override;
	virtual void BeginDestroy()override;
	virtual void FinishDestroy()override;
	virtual void PostEditUndo()override;
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;

	bool SavePrefab(UDreamWidget* RootWidget
		, TMap<UObject*, FGuid>& InOutMapObjectToGuid, TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& InSubPrefabMap
		, bool InForEditorOrRuntimeUse = true
	);
	void RecreatePrefab();
	/**
	 * @todo: There is a more efficient way for dealing with sub prefab in runtime: break sub prefab and store all actors (with override parameters) in root prefab.
	 */
	//void SavePrefabForRuntime(AActor* RootActor, TMap<AActor*, FDreamGUISubPrefabData>& InSubPrefabMap);
	/**
	 * LoadPrefab in editor, will not keep reference of source prefab, So we can't apply changes after modify it.
	 */
	UDreamWidget* LoadPrefabInEditor(UWorld* InWorld, UObject* InOuter, UDreamWidget* Parent);
	UDreamWidget* LoadPrefabInEditor(UWorld* InWorld, UObject* InOuter, UDreamWidget* Parent, TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& OutSubPrefabMap, TMap<FGuid, TObjectPtr<UObject>>& OutMapGuidToObject, bool SetRelativeTransformToIdentity = true);
#endif
};
