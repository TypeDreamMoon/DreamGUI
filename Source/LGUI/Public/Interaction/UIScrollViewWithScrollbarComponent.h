// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Core/Components/UISprite.h"
#include "Event/LGUIEventDelegate.h"
#include "UIScrollViewComponent.h"
#include "UIScrollViewWithScrollbarComponent.generated.h"

UENUM(BlueprintType, Category = LGUI)
enum class EScrollViewScrollbarVisibility :uint8
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
		TWeakObjectPtr<ALexWidgetActor> Viewport;
	UPROPERTY(EditAnywhere, Category = "LGUI-ScrollViewWithScrollbar")
		TWeakObjectPtr<ALexWidgetActor> HorizontalScrollbar;
	UPROPERTY(EditAnywhere, Category = "LGUI-ScrollViewWithScrollbar")
		EScrollViewScrollbarVisibility HorizontalScrollbarVisibility = EScrollViewScrollbarVisibility::AutoHideAndExpandViewport;
	UPROPERTY(EditAnywhere, Category = "LGUI-ScrollViewWithScrollbar")
		TWeakObjectPtr<ALexWidgetActor> VerticalScrollbar;
	UPROPERTY(EditAnywhere, Category = "LGUI-ScrollViewWithScrollbar")
		EScrollViewScrollbarVisibility VerticalScrollbarVisibility = EScrollViewScrollbarVisibility::AutoHideAndExpandViewport;

	virtual void CalculateHorizontalRange()override;
	virtual void CalculateVerticalRange()override;
	virtual bool CheckValidHit(USceneComponent* InHitComp)override;
	virtual void UpdateProgress(bool InFireEvent = true)override;
	virtual bool OnPointerDrag_Implementation(ULGUIPointerEventData* eventData)override;
	virtual bool OnPointerScroll_Implementation(ULGUIPointerEventData* eventData)override;
	UPROPERTY(Transient)TWeakObjectPtr<class UUIScrollbarComponent> HorizontalScrollbarComp = nullptr;
	UPROPERTY(Transient)TWeakObjectPtr<class UUIScrollbarComponent> VerticalScrollbarComp = nullptr;
	bool CheckScrollbarParameter();
	void OnHorizontalScrollbar(float InScrollValue);
	void OnVerticalScrollbar(float InScrollValue);
	uint8 bLayoutDirty : 1;
	enum class EScrollbarLayoutAction :uint8
	{
		None,
		NeedToShow,
		NeedToHide,
	};
	EScrollbarLayoutAction HorizontalScrollbarLayoutActionType = EScrollbarLayoutAction::None;
	EScrollbarLayoutAction VerticalScrollbarLayoutActionType = EScrollbarLayoutAction::None;

	void OnChildSiblingIndexChanged();
	void OnChildAttachmentChanged();
	
	void OnUpdateLayout_Implementation();
	void MarkLayoutDirty(){bLayoutDirty = true;}
public:

	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		ALexWidgetActor* GetViewport()const { return Viewport.Get(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		ALexWidgetActor* GetHorizontalScrollbar()const { return HorizontalScrollbar.Get(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		EScrollViewScrollbarVisibility GetHorizontalScrollbarVisibility()const { return HorizontalScrollbarVisibility; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		ALexWidgetActor* GetVerticalScrollbar()const { return VerticalScrollbar.Get(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		EScrollViewScrollbarVisibility GetVerticalScrollbarVisibility()const { return VerticalScrollbarVisibility; }

	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		void SetHorizontalScrollbarVisibility(EScrollViewScrollbarVisibility value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-ScrollViewWithScrollbar")
		void SetVerticalScrollbarVisibility(EScrollViewScrollbarVisibility value);
};
