// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Tickable.h"
#include "Subsystems/WorldSubsystem.h"
#include "LexUIPrefabManager.generated.h"


class ULexWidget;
class ULexUIPrefab;
class ULexUIPrefabHelperObject;

UCLASS(NotBlueprintable, NotBlueprintType, Transient, NotPlaceable)
class LGUI_API ULexUIPrefabManagerObject :public UObject
{
	GENERATED_BODY()

public:
	static ULexUIPrefabManagerObject* Instance;
	ULexUIPrefabManagerObject();
	virtual void BeginDestroy()override;
	virtual bool IsEditorOnly()const override { return true; }
};

UCLASS(NotBlueprintable, NotBlueprintType, Transient, NotPlaceable)
class LGUI_API ULexUIPrefabWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

	static ULexUIPrefabWorldSubsystem* GetInstance(UWorld* World);
	DECLARE_EVENT_OneParam(ULexUIPrefabWorldSubsystem, FDeserializeSession, const FGuid&);
	FDeserializeSession OnBeginDeserializeSession;
	FDeserializeSession OnEndDeserializeSession;
private:
	/** Map actor to prefab-deserialize-section-id */
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
	TMap<ULexWidget*, FGuid> AllActors_PrefabSystemProcessing;
public:
	void BeginPrefabSystemProcessingWidget(const FGuid& InSessionId);
	void EndPrefabSystemProcessingWidget(const FGuid& InSessionId);
	void AddWidgetForPrefabSystem(ULexWidget* InWidget, const FGuid& InSessionId);
	void RemoveWidgetForPrefabSystem(ULexWidget* InWidget);
	FGuid GetPrefabSystemSessionIdForWidget(ULexWidget* InWidget);

	/**
	 * Tell if PrefabSystem is deserializing the actor, can use this function in BeginPlay, if this return true then means properties are not ready yet, then you should use ILGUIPrefabInterface and implement Awake instead of BeginPlay.
	 * PrefabSystem is deserializing actor during LoadPrefab or DuplicateActor.
	 * (This static version function is for Blueprint easily use).
	 */
	UFUNCTION(BlueprintPure, BlueprintCallable, Category = "LGUI", meta=(WorldContext = "WorldContextObject"))
		static bool IsLexUIPrefabSystemProcessingWidget(UObject* WorldContextObject, ULexWidget* InWidget);
	/**
	 * Tell if PrefabSystem is deserializing the actor, can use this function in BeginPlay, if this return true then means properties are not ready yet, then you should use ILGUIPrefabInterface and implement Awake instead of BeginPlay.
	 * PrefabSystem is deserializing actor during LoadPrefab or DuplicateActor.
	 */
	bool IsPrefabSystemProcessingWidget(ULexWidget* InWidget);
};
