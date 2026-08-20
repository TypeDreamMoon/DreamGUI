// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "UINavigationInputSelectionHandler.generated.h"


class UDreamCanvas;
class UDreamTweener;

UCLASS(ClassGroup=(DreamGUI), Blueprintable, meta=(BlueprintSpawnableComponent))
class DREAMGUI_API UUINavigationInputSelectionHandler : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UUINavigationInputSelectionHandler();
protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
	float AnimDuration = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI")
	TWeakObjectPtr<UDreamCanvas> ThisCanvas = nullptr;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DreamGUI", AdvancedDisplay)
	TWeakObjectPtr<UDreamWidget> CurrentSelected = nullptr;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "SelectWidget"), Category = "DreamGUI")
	void ReceiveSelectWidget(UDreamWidget* InSelected);
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "SelectNone"), Category = "DreamGUI")
	void ReceiveSelectNone();

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	TArray<TWeakObjectPtr<UDreamTweener>> TweenerCollection;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	virtual void SelectWidget(UDreamWidget* InSelected);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	virtual void SelectNone();
};
