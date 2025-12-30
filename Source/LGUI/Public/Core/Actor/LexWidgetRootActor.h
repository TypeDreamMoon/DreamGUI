// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Actor/LexWidgetActor.h"
#include "GameFramework/Actor.h"
#include "LexWidgetRootActor.generated.h"

class ULexCanvas;
class ULexUIPrefab;

UCLASS()
class LGUI_API ALexWidgetRootActor : public ALexWidgetActor
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
	void CheckNecessaryObjects();
	void ApplyListInSceneOutliner();
	static bool bNeedCheckEventSystem;
	static bool bNeverCheckEventSystem;
	static bool bNeedCheckRaycasterSource;
	static bool bNeverCheckRaycasterSource;
public:
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
};
