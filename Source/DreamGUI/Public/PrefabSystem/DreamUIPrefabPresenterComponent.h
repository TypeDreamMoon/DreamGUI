// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "DreamUIPrefabPresenterComponent.generated.h"

class UDreamWidget;
class UUINavigationInputSelectionHandler;
class UDreamCanvas;
class UDreamUIPrefab;

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName="DreamUI Prefab Presenter Component")
class DREAMGUI_API UDreamUIPrefabPresenterComponent : public UDreamWidgetPresenterComponentBase
{
	GENERATED_BODY()

public:
	UDreamUIPrefabPresenterComponent();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void LoadWidget() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
public:
	bool bIsSpawnFromFactory = false;
	void CheckPrefabVersion();
#endif
	
protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=DreamWidgetPresenter)
	TObjectPtr<UDreamUIPrefab> WidgetPrefab;
	
#if WITH_EDITORONLY_DATA
private:
	UPROPERTY()
	FString OverallVersionMD5;
#endif
public:
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	void SetPrefab(UDreamUIPrefab* Value);
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	UDreamUIPrefab* GetPrefab()const{return WidgetPrefab;}
};
