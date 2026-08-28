// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "DreamUIPrefabPresenterComponent.generated.h"

class UDreamWidget;
class UUINavigationInputSelectionHandler;
class UDreamCanvas;
class UDreamUserWidget;

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
#endif
	
protected:
	/**
	 * The hierarchy class to present. Replaces the prefab asset this component used to hold; a level
	 * that had one assigned comes back empty and has to be pointed at the converted class.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=DreamWidgetPresenter)
	TSubclassOf<UDreamUserWidget> WidgetClass;

public:
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	void SetWidgetClass(TSubclassOf<UDreamUserWidget> Value);
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	TSubclassOf<UDreamUserWidget> GetWidgetClass()const{return WidgetClass;}
};
