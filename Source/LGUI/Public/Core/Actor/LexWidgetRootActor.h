// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Actor/LexWidgetActor.h"
#include "GameFramework/Actor.h"
#include "LexWidgetRootActor.generated.h"

class ULexCanvas;
class ULGUIPrefab;

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
public:
	void CheckPrefabVersion();
#endif
	
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=LGUI)
	TObjectPtr<ULexCanvas> Canvas;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category=LGUI)
	TObjectPtr<ULGUIPrefab> WidgetPrefab;
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
	AActor* GetLoadedActor()const{return LoadedActor.Get();}
};
