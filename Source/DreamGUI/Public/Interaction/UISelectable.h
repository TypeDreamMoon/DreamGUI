// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerSelectDeselectInterface.h"
#include "Event/Interface/DreamNavigationInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIImageBrush.h"
#include "UISelectable.generated.h"

class UUINavigationInputSelectionHandler;
class UUISelectable;
class UDreamVisual;
class UDreamTweener;
class UDreamSelectableStyle;

UENUM(BlueprintType, Category = DreamGUI)
enum class EUISelectableTransitionType:uint8
{
	None,
	Color,
	/** This mode need a DreamImage as TransitionTarget */
	ImageBrush,
	/** You can implement custom UISelectableTransition to do the transition */
	Custom,
};
UENUM(BlueprintType, Category = DreamGUI)
enum class EUISelectableSelectionState :uint8
{
	/** Not hovered by pointer, just a normal state. */
	Normal,
	/** Hovered by pointer. */
	Hovered,
	/** Pressed by pointer. */
	Pressed,
	/** Disabled, not interactable. */
	Disabled,
};
UENUM(BlueprintType, Category = DreamGUI)
enum class EUISelectableNavigationMode:uint8
{
	/** No navigation, cannot navigate out from this. */
	None,
	/** Navigation is controlled by DreamGUI. */
	Auto,
	/** Control your navigation behaviour on your own. */
	Explicit,
};


UCLASS(ClassGroup = (DreamUI), Abstract, Blueprintable, meta=(BlueprintSpawnableComponent))
class DREAMGUI_API UUITransition :public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UUITransition();
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "DreamGUI-Transition")
		TArray<TObjectPtr<UDreamTweener>> TweenerCollection;
public:
	/**
	 * Stop any transition inside TweenerCollection if playing, so remember to collect your tweener object by calling function CollectTweener.
	 * Call this before start any transition, in case of other transition is in progress.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Transition")
	virtual void StopTransition();
	/** Add tweener to TweenerCollection, so the function StopTransition will take effect. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Transition")
	virtual void CollectTweener(UDreamTweener* InItem);
	/** Add tweener set to TweenerCollection, so the function StopTransition will take effect. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Transition")
	virtual void CollectTweeners(const TSet<UDreamTweener*>& InItems);
};

UCLASS(ClassGroup = (DreamUI), Abstract, Blueprintable)
class DREAMGUI_API UUISelectableTransition :public UUITransition
{
	GENERATED_BODY()
public:

	UFUNCTION()
	UUISelectable* GetSelectableComponent()const;
protected:
	UPROPERTY(Transient, BlueprintReadOnly, Getter=GetSelectableComponent, Category = "DreamGUI-Transition", DisplayName=UISelectable)
	mutable TObjectPtr<UUISelectable> UISelectableComp;

	/** 
	 * Called when UISelectableComponent's transition state = normal.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI-Transition", meta = (DisplayName = "OnNormal"))
		void ReceiveOnNormal(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = highlighted.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI-Transition", meta = (DisplayName = "OnHovered"))
		void ReceiveOnHovered(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = pressed.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI-Transition", meta = (DisplayName = "OnPressed"))
		void ReceiveOnPressed(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = disabled.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI-Transition", meta = (DisplayName = "OnDisabled"))
		void ReceiveOnDisabled(bool InImmediateSet);
public:
	/**
	 * Called when UISelectableComponent's transition state = normal.
	 * Default will call blueprint implemented function. If you dont want that, just not use Super::OnNormal();
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	virtual void OnNormal(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = highlighted.
	 * Default will call blueprint implemented function. If you dont want that, just not use Super::OnHighlighted();
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	virtual void OnHovered(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = pressed.
	 * Default will call blueprint implemented function. If you dont want that, just not use Super::OnPressed();
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	virtual void OnPressed(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = disabled.
	 * Default will call blueprint implemented function. If you dont want that, just not use Super::OnDisabled();
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	virtual void OnDisabled(bool InImmediateSet);
};

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUISelectable : public UDreamUIBehaviour
	, public IDreamPointerEnterExitInterface
	, public IDreamPointerDownUpInterface
	, public IDreamPointerSelectDeselectInterface
	, public IDreamNavigationInterface
{
	GENERATED_BODY()
public:
	UUISelectable();
protected:

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void Awake() override;

	virtual void OnRegister()override;
	virtual void OnUnregister()override;

	friend class FUISelectableCustomization;
	
	/** inherited events of this component can bubble up? */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		bool AllowEventBubbleUp = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		bool bInteractable = true;

	virtual void OnInteractableChanged(bool IsEnabled) override;

#pragma region Transition
	/** Optional shared style. Inline values below remain the backwards-compatible fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Selectable", meta = (AllowPrivateAccess = true))
	TObjectPtr<UDreamSelectableStyle> Style = nullptr;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
	TWeakObjectPtr<UDreamVisual> TransitionTarget;
	
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
	EUISelectableTransitionType TransitionType = EUISelectableTransitionType::Color;

	UPROPERTY(EditAnywhere, Category="DreamGUI-Selectable", meta=(EditCondition="TransitionType==EUISelectableTransitionType::Custom"))
	TWeakObjectPtr<UUISelectableTransition> CustomTransition = nullptr;
	UPROPERTY(Transient)TObjectPtr<class UDreamTweener> TransitionTweener = nullptr;
	
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		FColor NormalColor = FColor(255, 255, 255, 255);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		FColor HoveredColor = FColor(200, 200, 200, 255);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		FColor PressedColor = FColor(150, 150, 150, 255);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable")
		FColor DisabledColor = FColor(150, 150, 150, 128);

	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable", meta = (DisplayThumbnail = "false"))
		FDreamUIImageBrush NormalImageBrush;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable", meta = (DisplayThumbnail = "false"))
		FDreamUIImageBrush HoveredImageBrush;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable", meta = (DisplayThumbnail = "false"))
		FDreamUIImageBrush PressedImageBrush;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable", meta = (DisplayThumbnail = "false"))
		FDreamUIImageBrush DisabledImageBrush;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable", meta = (ClampMin = "0.0"))
	float AnimDuration = 0.2f;

	EUISelectableSelectionState CurrentSelectionState = EUISelectableSelectionState::Normal;
	void ApplyPointerSelectionState(bool ImmediateSet);
	bool bIsPointerInsideThis = false;
	bool bIsPointerDown = false;
	bool CheckNavigationSelectionState();
	TWeakObjectPtr<UUINavigationInputSelectionHandler> NavigationSelection;
#pragma endregion
	/**
	 * Can we navigate from other selectable object to this one?
	 * If other selectable use EUISelectableNavigationMode.Explicit and use this selectable as specific one, then this selectable can still be navigate to.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		bool bCanNavigateHere = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationLeft = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		TWeakObjectPtr<UUISelectable> NavigationLeftSpecific;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationRight = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		TWeakObjectPtr<UUISelectable> NavigationRightSpecific;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationUp = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		TWeakObjectPtr<UUISelectable> NavigationUpSpecific;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationDown = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		TWeakObjectPtr<UUISelectable> NavigationDownSpecific;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationNext = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		TWeakObjectPtr<UUISelectable> NavigationNextSpecific;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationPrev = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Selectable-Navigation")
		TWeakObjectPtr<UUISelectable> NavigationPrevSpecific;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		UDreamVisual* GetTransitionTarget()const { return TransitionTarget.Get(); }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
	FColor GetNormalColor()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
	FColor GetHoveredColor()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
	FColor GetPressedColor()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	FColor GetDisabledColor()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
	const FDreamUIImageBrush& GetNormalImageBrush()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
	const FDreamUIImageBrush& GetHoveredImageBrush()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
	const FDreamUIImageBrush& GetPressedImageBrush()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	const FDreamUIImageBrush& GetDisabledImageBrush()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetStyle(UDreamSelectableStyle* Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Selectable")
	UDreamSelectableStyle* GetStyle()const { return Style; }
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable") 
		EUISelectableSelectionState GetSelectionState()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		void SetTransitionTarget(UDreamVisual* Value);
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetNormalColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetHoveredColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetPressedColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetDisabledColor(FColor Value);
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetNormalImageBrush(const FDreamUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetHoveredImageBrush(const FDreamUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetPressedImageBrush(const FDreamUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
	void SetDisabledImageBrush(const FDreamUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		void SetSelectionState(EUISelectableSelectionState NewState);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable")
		bool IsInteractable()const;

#pragma region Navigation
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		bool GetCanNavigateHere()const { return bCanNavigateHere; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationLeft()const { return NavigationLeft; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationRight()const { return NavigationRight; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationUp()const { return NavigationUp; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationDown()const { return NavigationDown; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationPrev()const { return NavigationPrev; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationNext()const { return NavigationNext; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		UUISelectable* GetNavigationLeftExplicit()const { return NavigationLeftSpecific.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		UUISelectable* GetNavigationRightExplicit()const { return NavigationRightSpecific.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		UUISelectable* GetNavigationUpExplicit()const { return NavigationUpSpecific.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		UUISelectable* GetNavigationDownExplicit()const { return NavigationDownSpecific.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		UUISelectable* GetNavigationPrevExplicit()const { return NavigationPrevSpecific.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		UUISelectable* GetNavigationNextExplicit()const { return NavigationNextSpecific.Get(); }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetCanNavigateHere(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationLeft(EUISelectableNavigationMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationRight(EUISelectableNavigationMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationUp(EUISelectableNavigationMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationDown(EUISelectableNavigationMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationPrev(EUISelectableNavigationMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationNext(EUISelectableNavigationMode Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationLeftExplicit(UUISelectable* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationRightExplicit(UUISelectable* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationUpExplicit(UUISelectable* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationDownExplicit(UUISelectable* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationPrevExplicit(UUISelectable* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Selectable-Navigation")
		void SetNavigationNextExplicit(UUISelectable* Value);

	/**
	 * Find UISelectable component on specific direction.
	 */
	virtual UUISelectable* FindSelectable(FVector InDirection);
	/**
	 * Find UISelectable component inside InParent on specific direction.
	 */
	virtual UUISelectable* FindSelectable(FVector InDirection, UDreamWidget* InParent);
	/**
     * Default selectable is the most "Prev" one (left top most).
	 */
	static UUISelectable* FindDefaultSelectable(UObject* WorldContextObject);
	virtual UUISelectable* FindSelectableOnLeft();
	virtual UUISelectable* FindSelectableOnRight();
	virtual UUISelectable* FindSelectableOnUp();
	virtual UUISelectable* FindSelectableOnDown();
	virtual UUISelectable* FindSelectableOnNext();
	virtual UUISelectable* FindSelectableOnPrev();
#pragma endregion
protected:
	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerSelect_Implementation(UDreamBaseEventData* EventData)override;
	virtual bool OnPointerDeselect_Implementation(UDreamBaseEventData* EventData)override;
	virtual bool CanNavigateHere_Implementation() const override;
	virtual bool OnNavigate_Implementation(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result)override;
};
