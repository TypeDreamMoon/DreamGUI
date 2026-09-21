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

	/**
	 * Fly the cursor onto a newly selected widget: position, size and rotation together.
	 *
	 * All three used to be commented out, leaving the "moved from one widget to another" branch --
	 * that is, EVERY navigation step after the first -- doing nothing but reparenting. The cursor
	 * stayed at the offset and the size it had under its previous parent, so it marked the wrong
	 * place at the wrong size for the rest of the session, and the position it should have flown to
	 * was computed and then dropped on the floor as an unused local.
	 *
	 * Each of the three follows the same rule FadeCursorTo documents: a tween that cannot be made
	 * (no world, an editor world with no game instance) is skipped, but its DESTINATION is applied
	 * outright -- the animation is how the cursor arrives, being in the right place is the outcome.
	 */
	void MoveCursorTo(UDreamWidget* InWidget, const FVector& InLocation, const FVector2D& InSize);
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	virtual void SelectWidget(UDreamWidget* InSelected);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	virtual void SelectNone();
};
