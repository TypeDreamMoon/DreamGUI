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

	/**
	 * Fade the cursor towards InOpacity and hand back the tween that will do it, or null -- in which
	 * case the cursor has already been put at InOpacity outright.
	 *
	 * The distinction is the caller's business because a tween is the only thing an OnComplete can
	 * hang off, and there is frequently no tween to be had: UDreamTweenManager answers null whenever
	 * it cannot reach the game instance subsystem that drives tweens, which covers a widget with no
	 * world at all AND a widget in an editor world, which has a world but no game instance. Both are
	 * ordinary rather than exotic -- an authoring tree and a headless test are the first, the
	 * designer preview is the second.
	 */
	UDreamTweener* FadeCursorTo(UDreamWidget* InWidget, float InOpacity);
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	virtual void SelectWidget(UDreamWidget* InSelected);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	virtual void SelectNone();
};
