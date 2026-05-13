// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LexWidgetPresenterComponent.generated.h"

class ULexWidget;
class UUINavigationInputSelectionHandler;
class ULexCanvas;
class ULexUIPrefab;

UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULexWidgetPresenterComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	ULexWidgetPresenterComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void PostLoad() override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;
	void LoadPrefab();
	void EnsureWidgetTreeReferences();
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
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
	UPROPERTY(VisibleAnywhere, Instanced, BlueprintReadOnly, Category=LGUI)
	TObjectPtr<ULexCanvas> RootCanvas;
	UPROPERTY(VisibleAnywhere, Instanced, BlueprintReadOnly, Category=LGUI)
	TObjectPtr<ULexWidget> RootWidget;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category=LGUI)
	TObjectPtr<ULexUIPrefab> WidgetPrefab;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=LGUI)
	TWeakObjectPtr<ULexWidget> LoadedWidget;
	/**
	 * For navigation input, show a selection widget
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=LGUI)
	TObjectPtr<ULexUIPrefab> NavigationSelectionPrefab;

	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category=LGUI, AdvancedDisplay)
	TWeakObjectPtr<UUINavigationInputSelectionHandler> NavigationSelection;

#if WITH_EDITORONLY_DATA
private:
	UPROPERTY()
	FString OverallVersionMD5;
#endif
public:
	UFUNCTION(BlueprintCallable, Category=LGUI)
	void SetPrefab(ULexUIPrefab* Value);
	UFUNCTION(BlueprintCallable, Category=LGUI)
	ULexUIPrefab* GetPrefab()const{return WidgetPrefab;}
	UFUNCTION(BlueprintCallable, Category=LGUI)
	ULexWidget* GetLoadedWidget()const{return LoadedWidget.Get();}
	UFUNCTION(BlueprintCallable, Category=LGUI)
	UUINavigationInputSelectionHandler* GetNavigationSelection();
	UFUNCTION(BlueprintCallable, Category=LGUI)
	ULexCanvas* GetRootCanvas()const{return RootCanvas.Get();}
	UFUNCTION(BlueprintCallable, Category=LGUI)
	ULexWidget* GetRootWidget()const{return RootWidget;}
};
