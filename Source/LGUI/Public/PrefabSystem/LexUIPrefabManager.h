// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Tickable.h"
#include "Subsystems/WorldSubsystem.h"
#include "LexUIPrefabManager.generated.h"


class ULexUIPrefab;
class ULexUIPrefabHelperObject;

UCLASS(NotBlueprintable, NotBlueprintType, Transient, NotPlaceable)
class LGUI_API ULexUIPrefabManagerObject :public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static ULexUIPrefabManagerObject* Instance;
	ULexUIPrefabManagerObject();
	virtual void BeginDestroy()override;
public:
	//begin TickableEditorObject interface
	virtual void Tick(float DeltaTime)override;
	virtual bool IsTickable() const { return Instance == this; }
	virtual bool IsTickableInEditor()const { return Instance == this; }
	virtual TStatId GetStatId() const override;
	virtual bool IsEditorOnly()const override { return true; }
	//end TickableEditorObject interface
#if WITH_EDITORONLY_DATA
private:
	TUniquePtr<FPreviewScene> PreviewSceneForPrefabPackage;
#endif
#if WITH_EDITOR
private:
	static bool InitCheck();
public:
	static ULexUIPrefabManagerObject* GetInstance(bool CreateIfNotValid = false);
	static UWorld* GetPreviewWorldForPrefabPackage();
private:
	FDelegateHandle OnAssetReimportDelegateHandle;
	void OnAssetReimport(UObject* Asset);
	FDelegateHandle OnMapOpenedDelegateHandle;
	void OnMapOpened(const FString& FileName, bool AsTemplate);
	FDelegateHandle OnPackageReloadedDelegateHandle;
	void OnPackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event);
#endif
};

UCLASS(NotBlueprintable, NotBlueprintType, Transient, NotPlaceable)
class LGUI_API ULexUIPrefabWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
	virtual void Initialize(FSubsystemCollectionBase& Collection)override {};
	virtual void Deinitialize()override {};

	static ULexUIPrefabWorldSubsystem* GetInstance(UWorld* World);
	DECLARE_EVENT_OneParam(ULexUIPrefabWorldSubsystem, FDeserializeSession, const FGuid&);
	FDeserializeSession OnBeginDeserializeSession;
	FDeserializeSession OnEndDeserializeSession;
private:
	/** Map actor to prefab-deserialize-section-id */
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
	TMap<AActor*, FGuid> AllActors_PrefabSystemProcessing;
public:
	void BeginPrefabSystemProcessingActor(const FGuid& InSessionId);
	void EndPrefabSystemProcessingActor(const FGuid& InSessionId);
	void AddActorForPrefabSystem(AActor* InActor, const FGuid& InSessionId);
	void RemoveActorForPrefabSystem(AActor* InActor, const FGuid& InSessionId);
	FGuid GetPrefabSystemSessionIdForActor(AActor* InActor);

	/**
	 * Tell if PrefabSystem is deserializing the actor, can use this function in BeginPlay, if this return true then means properties are not ready yet, then you should use ILGUIPrefabInterface and implement Awake instead of BeginPlay.
	 * PrefabSystem is deserializing actor during LoadPrefab or DuplicateActor.
	 * (This static version function is for Blueprint easily use).
	 */
	UFUNCTION(BlueprintPure, BlueprintCallable, Category = "LGUI")
		static bool IsLGUIPrefabSystemProcessingActor(AActor* InActor);
	/**
	 * Tell if PrefabSystem is deserializing the actor, can use this function in BeginPlay, if this return true then means properties are not ready yet, then you should use ILGUIPrefabInterface and implement Awake instead of BeginPlay.
	 * PrefabSystem is deserializing actor during LoadPrefab or DuplicateActor.
	 */
	bool IsPrefabSystemProcessingActor(AActor* InActor);
};
