// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/LGUIPointerEnterExitInterface.h"
#include "Event/Interface/LGUIPointerDownUpInterface.h"
#include "Event/Interface/LGUIPointerSelectDeselectInterface.h"
#include "Event/Interface/LGUINavigationInterface.h"
#include "Core/LGUILifeCycleUIBehaviour.h"
#include "LGUIComponentReference.h"
#include "UISelectableComponent.generated.h"

UENUM(BlueprintType, Category = LGUI)
enum class ELexUISelectableTransitionType:uint8
{
	None,
	ColorTint,
	/** In this mode, RootComponent of TransitionActor must be a UISpriteBase Actor. The Sprite property will be override by this component. */
	SpriteSwap,
	/** You can implement a UISelectableTransitionComponent in c++ or blueprint to do the transition, and add this component to this actor */
	TransitionComponent,
};
UENUM(BlueprintType, Category = LGUI)
enum class EUISelectableSelectionState :uint8
{
	/** Not hovered by pointer, just a normal state. */
	Normal,
	/** Hovered by pointer. */
	Highlighted,
	/** Pressed by pointer. */
	Pressed,
	/** Disabled, not interactable. Check the "OnUIInteractionStateChanged" function of UISelectableComponent, to see why it is disabled. */
	Disabled,
};
UENUM(BlueprintType, Category = LGUI)
enum class EUISelectableNavigationMode:uint8
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
	void OnInitialize();
protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI-Transition", meta = (DisplayName = "OnInitialize"))
	void ReceiveOnInitialize();

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
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI-Transition", meta = (DisplayName = "OnHighlighted"))
		void ReceiveOnHighlighted(bool InImmediateSet);
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
	virtual void OnHighlighted(bool InImmediateSet);
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
class LGUI_API UUISelectableComponent : public ULGUILifeCycleUIBehaviour
	, public ILGUIPointerEnterExitInterface
	, public ILGUIPointerDownUpInterface
	, public ILGUIPointerSelectDeselectInterface
	, public ILGUINavigationInterface
{
	GENERATED_BODY()
	
protected:

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void Awake() override;

	virtual void OnRegister()override;
	virtual void OnUnregister()override;

	friend class FUISelectableCustomization;
	/** If not assigned, then use self. */
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		TWeakObjectPtr<ULexVisual> TransitionTarget;
	/** inherited events of this component can bubble up? */
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		bool AllowEventBubbleUp = false;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		bool bInteractable = true;

	virtual void OnIsEnabledChanged(bool IsEnabled) override;

#pragma region Transition
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		ELexUISelectableTransitionType Transition;

	UPROPERTY(Transient)TObjectPtr<class ULTweener> TransitionTweener = nullptr;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		FColor NormalColor = FColor(255, 255, 255, 255);
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		FColor HighlightedColor = FColor(200, 200, 200, 255);
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		FColor PressedColor = FColor(150, 150, 150, 255);
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable")
		FColor DisabledColor = FColor(150, 150, 150, 128);
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable", meta = (ClampMin = "0.0"))
		float FadeDuration = 0.2f;

	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable", meta = (DisplayThumbnail = "false"))
		TObjectPtr<ULexUISpriteData_BaseObject> NormalSprite;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable", meta = (DisplayThumbnail = "false"))
		TObjectPtr<ULexUISpriteData_BaseObject> HighlightedSprite;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable", meta = (DisplayThumbnail = "false"))
		TObjectPtr<ULexUISpriteData_BaseObject> PressedSprite;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable", meta = (DisplayThumbnail = "false"))
		TObjectPtr<ULexUISpriteData_BaseObject> DisabledSprite;
	UPROPERTY(EditAnywhere, Instanced, Category="LGUI-Selectable")
	TObjectPtr<UUISelectableTransitionComponent> TransitionComp = nullptr;

	EUISelectableSelectionState CurrentSelectionState = EUISelectableSelectionState::Normal;
	void ApplySelectionState(bool immediateSet);
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
		EUISelectableNavigationMode NavigationLeft = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		FLGUIComponentReference NavigationLeftSpecific = FLGUIComponentReference(UUISelectableComponent::StaticClass());
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationRight = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		FLGUIComponentReference NavigationRightSpecific = FLGUIComponentReference(UUISelectableComponent::StaticClass());
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationUp = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		FLGUIComponentReference NavigationUpSpecific = FLGUIComponentReference(UUISelectableComponent::StaticClass());
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationDown = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		FLGUIComponentReference NavigationDownSpecific = FLGUIComponentReference(UUISelectableComponent::StaticClass());
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationNext = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		FLGUIComponentReference NavigationNextSpecific = FLGUIComponentReference(UUISelectableComponent::StaticClass());
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		EUISelectableNavigationMode NavigationPrev = EUISelectableNavigationMode::Auto;
	UPROPERTY(EditAnywhere, Category = "LGUI-Selectable-Navigation")
		FLGUIComponentReference NavigationPrevSpecific = FLGUIComponentReference(UUISelectableComponent::StaticClass());
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		ULexVisual* GetTransitionTarget()const { return TransitionTarget.Get(); }

	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
		ULexUISpriteData_BaseObject* GetNormalSprite()const { return NormalSprite; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
		FColor GetNormalColor()const { return NormalColor; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
		ULexUISpriteData_BaseObject* GetHighlightedSprite()const { return HighlightedSprite; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
		FColor GetHighlightedColor()const { return HighlightedColor; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
		ULexUISpriteData_BaseObject* GetPressedSprite()const { return PressedSprite; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
		FColor GetPressedColor()const { return PressedColor; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		ULexUISpriteData_BaseObject* GetDisabledSprite()const { return DisabledSprite; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		FColor GetDisabledColor()const { return DisabledColor; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable") 
		EUISelectableSelectionState GetSelectionState()const;

	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		void SetTransitionTarget(ULexVisual* value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		void SetNormalSprite(ULexUISpriteData_BaseObject* NewSprite);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		void SetNormalColor(FColor NewColor);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		void SetHighlightedSprite(ULexUISpriteData_BaseObject* NewSprite);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		void SetHighlightedColor(FColor NewColor);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		void SetPressedSprite(ULexUISpriteData_BaseObject* NewSprite);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		void SetPressedColor(FColor NewColor);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		void SetDisabledSprite(ULexUISpriteData_BaseObject* NewSprite);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		void SetDisabledColor(FColor NewColor);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		void SetSelectionState(EUISelectableSelectionState NewState);

	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable")
		bool IsInteractable()const;

#pragma region Navigation
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		bool GetCanNavigateHere()const { return bCanNavigateHere; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationLeft()const { return NavigationLeft; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationRight()const { return NavigationRight; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationUp()const { return NavigationUp; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationDown()const { return NavigationDown; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationPrev()const { return NavigationPrev; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		EUISelectableNavigationMode GetNavigationNext()const { return NavigationNext; }

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
		void SetNavigationLeft(EUISelectableNavigationMode value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationRight(EUISelectableNavigationMode value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationUp(EUISelectableNavigationMode value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationDown(EUISelectableNavigationMode value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationPrev(EUISelectableNavigationMode value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Selectable-Navigation")
		void SetNavigationNext(EUISelectableNavigationMode value);

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
	virtual bool OnPointerEnter_Implementation(ULGUIPointerEventData* eventData)override;
	virtual bool OnPointerExit_Implementation(ULGUIPointerEventData* eventData)override;
	virtual bool OnPointerDown_Implementation(ULGUIPointerEventData* eventData)override;
	virtual bool OnPointerUp_Implementation(ULGUIPointerEventData* eventData)override;
	virtual bool OnPointerSelect_Implementation(ULGUIBaseEventData* eventData)override;
	virtual bool OnPointerDeselect_Implementation(ULGUIBaseEventData* eventData)override;
	virtual bool OnNavigate_Implementation(ELGUINavigationDirection direction, TScriptInterface<ILGUINavigationInterface>& result)override;
};
