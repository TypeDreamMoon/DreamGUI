// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "UIScrollViewComponent.h"
#include "UIScrollViewWithScrollbarComponent.generated.h"

class UUIScrollbarComponent;

UENUM(BlueprintType, Category = LGUI)
enum class ELexUIScrollViewScrollbarVisibility :uint8
{
	//Always visible.
	Permanent,
	//Auto hide scrollbar when content's size less than viewport's size.
	AutoHide,
	//Same like AutoHide, but also expand viewport size when hide scrollbar.
	//For this mode, viewport and scrollbar must directly attach to ScrollViewWithScrollBar.
	AutoHideAndExpandViewport,
};

//ScrollView with scrollbars
UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUIScrollViewWithScrollbarComponent : public UUIScrollViewComponent
{
	GENERATED_BODY()

public:
	UUIScrollViewWithScrollbarComponent();
protected:
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)override;
protected:
	friend class FUIScrollViewWithScrollBarCustomization;
	//For scrollbars to expand or shrink viewport
	UPROPERTY(EditAnywhere, Category = "LGUI-ScrollViewWithScrollbar")
		TWeakObjectPtr<ULexWidget> Viewport;
	UPROPERTY(EditAnywhere, Category = "LGUI-ScrollViewWithScrollbar")
		TWeakObjectPtr<UUIScrollbarComponent> HorizontalScrollbar;
	UPROPERTY(EditAnywhere, Category = "LGUI-ScrollViewWithScrollbar")
		ELexUIScrollViewScrollbarVisibility HorizontalScrollbarVisibility = ELexUIScrollViewScrollbarVisibility::AutoHideAndExpandViewport;
	UPROPERTY(EditAnywhere, Category = "LGUI-ScrollViewWithScrollbar")
		TWeakObjectPtr<UUIScrollbarComponent> VerticalScrollbar;
	UPROPERTY(EditAnywhere, Category = "LGUI-ScrollViewWithScrollbar")
		ELexUIScrollViewScrollbarVisibility VerticalScrollbarVisibility = ELexUIScrollViewScrollbarVisibility::AutoHideAndExpandViewport;

	virtual void CalculateHorizontalRange()override;
	virtual void CalculateVerticalRange()override;
	virtual bool CheckValidHit(USceneComponent* InHitComp)override;
	virtual void UpdateProgress(bool InFireEvent = true)override;
	virtual bool OnPointerDrag_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerScroll_Implementation(ULexPointerEventData* eventData)override;
	UPROPERTY(Transient)TWeakObjectPtr<ULexWidget> HorizontalScrollbarWidget;
	UPROPERTY(Transient)TWeakObjectPtr<ULexWidget> VerticalScrollbarWidget;
	bool CheckScrollbarParameter();
	void OnHorizontalScrollbar(float InScrollValue);
	void OnVerticalScrollbar(float InScrollValue);
	enum class EScrollbarLayoutAction :uint8
	{
		None,
		NeedToShow,
		NeedToHide,
	};
	EScrollbarLayoutAction HorizontalScrollbarLayoutActionType = EScrollbarLayoutAction::None;
	EScrollbarLayoutAction VerticalScrollbarLayoutActionType = EScrollbarLayoutAction::None;

	void OnScrollbarSiblingIndexChanged();
	void OnScrollbarAttachmentChanged();
	void LateUpdateScrollbarLayout();
	
	void UpdateScrollbarLayout();
public:

	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		ULexWidget* GetViewport()const { return Viewport.Get(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		ULexWidget* GetHorizontalScrollbar()const { return HorizontalScrollbarWidget.Get(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		ELexUIScrollViewScrollbarVisibility GetHorizontalScrollbarVisibility()const { return HorizontalScrollbarVisibility; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		ULexWidget* GetVerticalScrollbar()const { return VerticalScrollbarWidget.Get(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		ELexUIScrollViewScrollbarVisibility GetVerticalScrollbarVisibility()const { return VerticalScrollbarVisibility; }

	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		void SetHorizontalScrollbarVisibility(ELexUIScrollViewScrollbarVisibility value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		void SetVerticalScrollbarVisibility(ELexUIScrollViewScrollbarVisibility value);
};
