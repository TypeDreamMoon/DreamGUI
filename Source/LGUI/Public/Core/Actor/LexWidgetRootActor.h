// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Actor/LexWidgetContainer.h"
#include "GameFramework/Actor.h"
#include "LexWidgetRootActor.generated.h"

class UUINavigationInputSelectionHandler;
class ULexCanvas;
class ULexUIPrefab;

UCLASS()
class LGUI_API ALexWidgetRootActor : public ULexWidgetContainer
{
	GENERATED_BODY()

public:
	ALexWidgetRootActor();

protected:
	virtual void BeginPlay() override;
	virtual void PostRegisterAllComponents() override;
	void LoadPrefab();
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void ApplyListInSceneOutliner();
	static bool bNeedCheckEventSystem;
	static bool bNeverCheckEventSystem;
	static bool bNeedCheckRaycasterSource;
	static bool bNeverCheckRaycasterSource;
public:
	bool bIsSpawnFromPrefabFactory = false;
	void CheckNecessaryObjects();
	static void MarkNeedCheckNecessaryObjects();
	void CheckPrefabVersion();
#endif
	
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=LGUI)
	TObjectPtr<ULexCanvas> Canvas;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category=LGUI)
	TObjectPtr<ULexUIPrefab> WidgetPrefab;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=LGUI)
	TWeakObjectPtr<AActor> LoadedActor;
	/**
	 * For navigation input, show a selection widget
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=LGUI)
	TObjectPtr<ULexUIPrefab> NavigationSelectionPrefab;

	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category=LGUI, AdvancedDisplay)
	TWeakObjectPtr<UUINavigationInputSelectionHandler> NavigationSelection;

#if WITH_EDITORONLY_DATA
private:
	UPROPERTY(EditAnywhere, Category=LGUI)
	bool bListInSceneOutliner = false;
	UPROPERTY()
	FString OverallVersionMD5;
#endif
public:
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category=LGUI)
	void SetPrefab(ULexUIPrefab* Value);
	UFUNCTION(BlueprintCallable, Category=LGUI)
	ULexUIPrefab* GetPrefab()const{return WidgetPrefab;}
	UFUNCTION(BlueprintCallable, Category=LGUI)
	AActor* GetLoadedActor()const{return LoadedActor.Get();}
	UFUNCTION(BlueprintCallable, Category=LGUI)
	UUINavigationInputSelectionHandler* GetNavigationSelection();
};
