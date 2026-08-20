// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamUIMLBehaviour.generated.h"

enum class EDreamRenderMode : uint8;
class UDreamUIMLResource;
/**
 * 
 */
UCLASS(Abstract)
class DREAMGUI_API UDreamUIMLBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UDreamUIMLBehaviour();
	
	void GetUIMLData(FString& XAMLFilePath, UDreamUIMLResource*& XAMLResource)const;
	/** Resolve a source path. Relative paths are rooted at the project's Content directory. */
	static FString ResolveUIMLPath(const FString& InPath);

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=DreamWidgetPresenter)
	EDreamRenderMode DefaultRenderMode;
#endif
	
protected:
	/**
	 * Return an absolute path or a path relative to the project's Content directory.
	 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, meta=(DisplayName="GetUIMLData"))
	void ReceiveGetUIMLData(FString& XAMLFilePath, UDreamUIMLResource*& XAMLResource)const;

public:
	static UDreamUIMLBehaviour* CreateByClass(
		TSubclassOf<UDreamUIMLBehaviour> Class, UWorld* World, UDreamWidget* Parent
		, UDreamUIMLResource* Resources, bool IsSubTemplate
		, const TFunction<void(const TArray<UDreamWidget*>&)>& OnAllWidgetsCreated);
};
