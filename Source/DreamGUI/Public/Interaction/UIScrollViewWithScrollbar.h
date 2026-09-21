// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "UIScrollView.h"
#include "UIScrollViewWithScrollbar.generated.h"

class UUIScrollbar;

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUIScrollViewScrollbarVisibility :uint8
{
	//Always visible.
	Permanent,
	//Auto hide scrollbar when content's size less than viewport's size.
	AutoHide,
};

/**
 * SUPERSEDED by UDreamScrollBox (`Native.ScrollBox`), and unreferenced by the plugin as of the
 * control library: nothing in Source/ constructs one, and the palette has never offered it.
 *
 * It pairs a scroll view with scrollbars, which is what UDreamScrollBox does -- except the
 * control owns a real UDreamScrollBar rather than driving a bar somebody else placed, so the
 * handle geometry and the drag scale are computed from the same numbers. This class is the
 * reason UDreamScrollBar's minimum handle size lives on the behaviour: two owners of one
 * measurement is the bug that move was made to prevent.
 */
//ScrollView with scrollbars
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIScrollViewWithScrollbar : public UUIScrollView
{
	GENERATED_BODY()

public:
	UUIScrollViewWithScrollbar();

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
protected:
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)override;
private:
	friend class FUIScrollViewWithScrollBarCustomization;
	//For scrollbars to expand or shrink viewport
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollViewWithScrollbar")
		TWeakObjectPtr<UDreamWidget> Viewport;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollViewWithScrollbar")
		TWeakObjectPtr<UUIScrollbar> HorizontalScrollbar;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollViewWithScrollbar")
		EDreamUIScrollViewScrollbarVisibility HorizontalScrollbarVisibility = EDreamUIScrollViewScrollbarVisibility::AutoHide;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollViewWithScrollbar")
		TWeakObjectPtr<UUIScrollbar> VerticalScrollbar;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollViewWithScrollbar")
		EDreamUIScrollViewScrollbarVisibility VerticalScrollbarVisibility = EDreamUIScrollViewScrollbarVisibility::AutoHide;

	virtual void CalculateHorizontalRange()override;
	virtual void CalculateVerticalRange()override;
	virtual bool CheckValidHit(UDreamWidget* InHitComp)override;
	virtual void UpdateProgress(bool InFireEvent = true)override;
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerScroll_Implementation(UDreamPointerEventData* EventData)override;
	UPROPERTY(Transient)TWeakObjectPtr<UDreamWidget> HorizontalScrollbarWidget;
	UPROPERTY(Transient)TWeakObjectPtr<UDreamWidget> VerticalScrollbarWidget;
	bool CheckScrollbarParameter();
	void OnHorizontalScrollbar(float InScrollValue);
	void OnVerticalScrollbar(float InScrollValue);
	FDelegateHandle HorizontalScrollbarDelegateHandle;
	FDelegateHandle VerticalScrollbarDelegateHandle;

public:

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollViewWithScrollbar")
		UDreamWidget* GetViewport()const { return Viewport.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollViewWithScrollbar")
		UUIScrollbar* GetHorizontalScrollbar()const { return HorizontalScrollbar.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollViewWithScrollbar")
		EDreamUIScrollViewScrollbarVisibility GetHorizontalScrollbarVisibility()const { return HorizontalScrollbarVisibility; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollViewWithScrollbar")
		UUIScrollbar* GetVerticalScrollbar()const { return VerticalScrollbar.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollViewWithScrollbar")
		EDreamUIScrollViewScrollbarVisibility GetVerticalScrollbarVisibility()const { return VerticalScrollbarVisibility; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollViewWithScrollbar")
		void SetHorizontalScrollbarVisibility(EDreamUIScrollViewScrollbarVisibility value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollViewWithScrollbar")
		void SetVerticalScrollbarVisibility(EDreamUIScrollViewScrollbarVisibility value);
};
