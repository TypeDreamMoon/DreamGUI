// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamWidgetPresenterComponentBase.h"

#include "DreamUIMLPresenterComponent.generated.h"

class UDreamUIMLBehaviour;
class UDreamUIMLResource;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), DisplayName="DreamUI XAML Presenter Component")
class DREAMGUI_API UDreamUIMLPresenterComponent : public UDreamWidgetPresenterComponentBase
{
	GENERATED_BODY()

public:
	UDreamUIMLPresenterComponent();

protected:
	virtual void BeginPlay() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void LoadWidget() override;
#if WITH_EDITOR
	void RegisterEditorFocusCheck();
	void UnregisterEditorFocusCheck();
	FDelegateHandle EditorFocusHandle;
	FString CachedFileMD5;
	/** Re-check XAML file MD5 and reload if changed. Called on editor focus. */
	void CheckAndReloadWidget();
public:
	bool bIsSpawnFromFactory = false;
#endif
	FString GetXAMLFilePath() const;

protected:
	UPROPERTY(EditAnywhere, Category=DreamWidgetPresenter)
	TSubclassOf<UDreamUIMLBehaviour> XAMLScriptClass;

public:
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	void SetScriptClass(TSubclassOf<UDreamUIMLBehaviour> Value);
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	TSubclassOf<UDreamUIMLBehaviour> GetScriptClass()const{return XAMLScriptClass;}
};
