// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWidgetPresenterComponentBase.generated.h"

class UDreamWidget;
class UUINavigationInputSelectionHandler;
class UDreamCanvas;
class UDreamUIPrefab;

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamWidgetPresenterComponentBase : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamWidgetPresenterComponentBase();
	friend class FDreamWidgetPresenterBaseCustomization;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostInitProperties() override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;
	virtual void LoadWidget()PURE_VIRTUAL(UDreamWidgetPresenterComponentBase::LoadWidget, );
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	static bool bNeedCheckEventSystem;
	static bool bNeverCheckEventSystem;
	static bool bNeedCheckRaycasterSource;
	static bool bNeverCheckRaycasterSource;
public:
	void CheckNecessaryObjects();
	static void MarkNeedCheckNecessaryObjects();
	
	void ReloadWidget();
#endif
	
protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=DreamWidgetPresenter)
	TObjectPtr<UDreamCanvas> CanvasTemplate;
	
	UPROPERTY(Transient)
	TWeakObjectPtr<UDreamCanvas> RootCanvas;
	UPROPERTY(Transient)
	TWeakObjectPtr<UDreamWidget> LoadedWidget;
	/**
	 * For navigation input, show a selection widget
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=DreamWidgetPresenter)
	TObjectPtr<UDreamUIPrefab> NavigationSelectionPrefab;

	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category=DreamWidgetPresenter, AdvancedDisplay)
	TWeakObjectPtr<UUINavigationInputSelectionHandler> NavigationSelection;

public:
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	UUINavigationInputSelectionHandler* GetNavigationSelection();
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	UDreamCanvas* GetLoadedCanvas()const{return RootCanvas.Get();}
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	UDreamWidget* GetLoadedWidget()const{return LoadedWidget.Get();}
};
