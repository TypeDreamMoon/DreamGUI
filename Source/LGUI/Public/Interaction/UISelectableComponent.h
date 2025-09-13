// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/LexPointerEnterExitInterface.h"
#include "Event/Interface/LexPointerDownUpInterface.h"
#include "Event/Interface/LexPointerSelectDeselectInterface.h"
#include "Event/Interface/LexNavigationInterface.h"
#include "Core/LexUIBehaviour.h"
#include "LGUIComponentReference.h"
#include "Core/LexUIImageBrush.h"
#include "UISelectableComponent.generated.h"

class ULTweener;

UENUM(BlueprintType, Category = LGUI)
enum class ELexUISelectableTransitionType:uint8
{
	None,
	Color,
	/** This mode need a LexImage as TransitionTarget */
	ImageBrush,
	/** You can implement custom class to do the transition */
	Custom,
};
UENUM(BlueprintType, Category = LGUI)
enum class ELexUISelectableSelectionState :uint8
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
UENUM(BlueprintType, Category = LGUI)
enum class ELexUISelectableNavigationMode:uint8
{
	/** No navigation. */
	None,
	/** Navigation is controlled by LGUI. */
	Auto,
	/** Control your navigation behaviour on your own. */
	Explicit,
};

UCLASS(ClassGroup = (LexUI), Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API UUISelectableTransitionComponent :public UObject
{
	GENERATED_BODY()
public:
	virtual void BeginPlay();
	virtual void EndPlay();
protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI-Transition", meta = (DisplayName = "BeginPlay"))
	void ReceiveBeginPlay();
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI-Transition", meta = (DisplayName = "EndPlay"))
	void ReceiveEndPlay();

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "LGUI-Transition")
		TArray<TObjectPtr<ULTweener>> TweenerCollection;

	/** 
	 * Called when UISelectableComponent's transition state = normal.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI-Transition", meta = (DisplayName = "OnNormal"))
		void ReceiveOnNormal(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = highlighted.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI-Transition", meta = (DisplayName = "OnHovered"))
		void ReceiveOnHovered(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = pressed.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI-Transition", meta = (DisplayName = "OnPressed"))
		void ReceiveOnPressed(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = disabled.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI-Transition", meta = (DisplayName = "OnDisabled"))
		void ReceiveOnDisabled(bool InImmediateSet);
	/**
	 * This gives us an opportunity to do transition on more case than just provided above
	 * @param InTransitionName	use this to tell different event type. eg.UIToggleComponent, "On"/"Off" for toggle on/off
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI-Transition", meta = (DisplayName = "OnStartCustomTransition"))
		void ReceiveOnStartCustomTransition(FName InTransitionName, bool InImmediateSet);
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

	/**
	 * This gives us an opportunity to do transition on more case than just provided above.
	 * Default will call blueprint implemented function. If you dont want that, just not use Super::OnStartCustomTransition();
	 * @param InTransitionName: use this to tell different event type. eg.UIToggleComponent, "On"/"Off" for toggle on/off
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	virtual void OnStartCustomTransition(FName InTransitionName, bool InImmediateSet);

	/**
	 * Stop any transition inside TweenerCollection if is playing, so remember to collect your tweener object by calling function CollectTweener.
	 * Call this before start any transition, in case of other transition is in progress.
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI-Transition")
	virtual void StopTransition();
	/** Add tweener to TweenerCollection, so the function StopTransition will take effect. */
	UFUNCTION(BlueprintCallable, Category = "LGUI-Transition")
	virtual void CollectTweener(ULTweener* InItem);
	/** Add tweener set to TweenerCollection, so the function StopTransition will take effect. */
	UFUNCTION(BlueprintCallable, Category = "LGUI-Transition")
	virtual void CollectTweeners(const TSet<ULTweener*>& InItems);
};

class ULexUISpriteData_BaseObject;

UCLASS(HideCategories = (Collision, LOD, Physics, Cooking, Rendering, Activation, Actor, Input, Lighting, Mobile), ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUISelectableComponent : public ULexUIBehaviour
	, public ILexPointerEnterExitInterface
	, public ILexPointerDownUpInterface
	, public ILexPointerSelectDeselectInterface
	, public ILexNavigationInterface
{
	GENERATED_BODY()
public:
	UUISelectableComponent();
protected:

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void Awake() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void OnRegister()override;
	virtual void OnUnregister()override;

	friend class FUISelectableCustomization;
	
	/** inherited events of this component can bubble up? */
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		bool AllowEventBubbleUp = false;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		bool bInteractable = true;

	virtual void OnInteractableChanged(bool IsEnabled) override;

#pragma region Transition
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
	TWeakObjectPtr<ULexVisual> TransitionTarget;
	
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
	ELexUISelectableTransitionType Transition = ELexUISelectableTransitionType::Color;

	UPROPERTY(EditAnywhere, Instanced, Category="LGUI-Selectable")
	TObjectPtr<UUISelectableTransitionComponent> CustomTransition = nullptr;
	UPROPERTY(Transient)TObjectPtr<class ULTweener> TransitionTweener = nullptr;
	
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		FColor NormalColor = FColor(255, 255, 255, 255);
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		FColor HoveredColor = FColor(200, 200, 200, 255);
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		FColor PressedColor = FColor(150, 150, 150, 255);
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		FColor DisabledColor = FColor(150, 150, 150, 128);

	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable", meta = (DisplayThumbnail = "false"))
		FLexUIImageBrush NormalImageBrush;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable", meta = (DisplayThumbnail = "false"))
		FLexUIImageBrush HoveredImageBrush;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable", meta = (DisplayThumbnail = "false"))
		FLexUIImageBrush PressedImageBrush;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable", meta = (DisplayThumbnail = "false"))
		FLexUIImageBrush DisabledImageBrush;

	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable", meta = (ClampMin = "0.0"))
	float AnimDuration = 0.2f;

	ELexUISelectableSelectionState CurrentSelectionState = ELexUISelectableSelectionState::Normal;
	void ApplySelectionState(bool ImmediateSet);
	bool IsPointerInsideThis = false;
	bool IsPointerDown = false;
#pragma endregion
	/**
	 * Can we navigate from other selectable object to this one?
	 * If other selectable use EUISelectableNavigationMode.Explicit and use this selectable as specific one, then this selectable can still be navigate to.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		bool bCanNavigateHere = true;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		ELexUISelectableNavigationMode NavigationLeft = ELexUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		FLGUIComponentReference NavigationLeftSpecific = FLGUIComponentReference(UUISelectableComponent::StaticClass());
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		ELexUISelectableNavigationMode NavigationRight = ELexUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		FLGUIComponentReference NavigationRightSpecific = FLGUIComponentReference(UUISelectableComponent::StaticClass());
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		ELexUISelectableNavigationMode NavigationUp = ELexUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		FLGUIComponentReference NavigationUpSpecific = FLGUIComponentReference(UUISelectableComponent::StaticClass());
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		ELexUISelectableNavigationMode NavigationDown = ELexUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		FLGUIComponentReference NavigationDownSpecific = FLGUIComponentReference(UUISelectableComponent::StaticClass());
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		ELexUISelectableNavigationMode NavigationNext = ELexUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		FLGUIComponentReference NavigationNextSpecific = FLGUIComponentReference(UUISelectableComponent::StaticClass());
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		ELexUISelectableNavigationMode NavigationPrev = ELexUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		FLGUIComponentReference NavigationPrevSpecific = FLGUIComponentReference(UUISelectableComponent::StaticClass());
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		ULexVisual* GetTransitionTarget()const { return TransitionTarget.Get(); }

	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
	FColor GetNormalColor()const { return NormalColor; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
	FColor GetHoveredColor()const { return HoveredColor; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
	FColor GetPressedColor()const { return PressedColor; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
	FColor GetDisabledColor()const { return DisabledColor; }

	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
	const FLexUIImageBrush& GetNormalImageBrush()const { return NormalImageBrush; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
	const FLexUIImageBrush& GetHoveredImageBrush()const { return HoveredImageBrush; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
	const FLexUIImageBrush& GetPressedImageBrush()const { return PressedImageBrush; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
	const FLexUIImageBrush& GetDisabledImageBrush()const { return DisabledImageBrush; }
	
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
		ELexUISelectableSelectionState GetSelectionState()const;

	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		void SetTransitionTarget(ULexVisual* value);
	
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
	void SetNormalColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
	void SetHoveredColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
	void SetPressedColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
	void SetDisabledColor(FColor Value);
	
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
	void SetNormalImageBrush(const FLexUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
	void SetHoveredImageBrush(const FLexUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
	void SetPressedImageBrush(const FLexUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
	void SetDisabledImageBrush(const FLexUIImageBrush& Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		void SetSelectionState(ELexUISelectableSelectionState NewState);

	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		bool IsInteractable()const;

#pragma region Navigation
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		bool GetCanNavigateHere()const { return bCanNavigateHere; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		ELexUISelectableNavigationMode GetNavigationLeft()const { return NavigationLeft; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		ELexUISelectableNavigationMode GetNavigationRight()const { return NavigationRight; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		ELexUISelectableNavigationMode GetNavigationUp()const { return NavigationUp; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		ELexUISelectableNavigationMode GetNavigationDown()const { return NavigationDown; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		ELexUISelectableNavigationMode GetNavigationPrev()const { return NavigationPrev; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		ELexUISelectableNavigationMode GetNavigationNext()const { return NavigationNext; }

	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		UUISelectableComponent* GetNavigationLeftExplicit()const { return NavigationLeftSpecific.GetComponent<UUISelectableComponent>(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		UUISelectableComponent* GetNavigationRightExplicit()const { return NavigationRightSpecific.GetComponent<UUISelectableComponent>(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		UUISelectableComponent* GetNavigationUpExplicit()const { return NavigationUpSpecific.GetComponent<UUISelectableComponent>(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		UUISelectableComponent* GetNavigationDownExplicit()const { return NavigationDownSpecific.GetComponent<UUISelectableComponent>(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		UUISelectableComponent* GetNavigationPrevExplicit()const { return NavigationPrevSpecific.GetComponent<UUISelectableComponent>(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		UUISelectableComponent* GetNavigationNextExplicit()const { return NavigationNextSpecific.GetComponent<UUISelectableComponent>(); }

	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetCanNavigateHere(bool value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationLeft(ELexUISelectableNavigationMode value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationRight(ELexUISelectableNavigationMode value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationUp(ELexUISelectableNavigationMode value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationDown(ELexUISelectableNavigationMode value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationPrev(ELexUISelectableNavigationMode value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationNext(ELexUISelectableNavigationMode value);

	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationLeftExplicit(UUISelectableComponent* value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationRightExplicit(UUISelectableComponent* value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationUpExplicit(UUISelectableComponent* value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationDownExplicit(UUISelectableComponent* value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationPrevExplicit(UUISelectableComponent* value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationNextExplicit(UUISelectableComponent* value);

	/**
	 * Find UISelectable component on specific direction.
	 */
	virtual UUISelectableComponent* FindSelectable(FVector InDirection);
	/**
	 * Find UISelectable component inside InParent on specific direction.
	 */
	virtual UUISelectableComponent* FindSelectable(FVector InDirection, USceneComponent* InParent);
	/**
     * Default selectable is the most "Prev" one (left top most).
	 */
	static UUISelectableComponent* FindDefaultSelectable(UObject* WorldContextObject);
	virtual UUISelectableComponent* FindSelectableOnLeft();
	virtual UUISelectableComponent* FindSelectableOnRight();
	virtual UUISelectableComponent* FindSelectableOnUp();
	virtual UUISelectableComponent* FindSelectableOnDown();
	virtual UUISelectableComponent* FindSelectableOnNext();
	virtual UUISelectableComponent* FindSelectableOnPrev();
#pragma endregion
protected:
	virtual bool OnPointerEnter_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerExit_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerDown_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerUp_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerSelect_Implementation(ULexBaseEventData* eventData)override;
	virtual bool OnPointerDeselect_Implementation(ULexBaseEventData* eventData)override;
	virtual bool OnNavigate_Implementation(ELexUINavigationDirection direction, TScriptInterface<ILexNavigationInterface>& result)override;
};
