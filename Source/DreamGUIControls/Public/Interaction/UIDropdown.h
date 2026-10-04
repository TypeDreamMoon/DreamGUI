// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interaction/UISelectable.h"
#include "Interaction/DreamUIFocusReturn.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/DreamDelegateDeclaration.h"
#include "UIDropdown.generated.h"

class UUIToggle;
class UDreamImage;
class UDreamWidgetContainer;
class UDreamWidget;
class UDreamText;
enum class EDreamWidgetRaycastableType : uint8;

/**
 * Dropdown option selection change.
 * @param InSelectIndex Selected item index
 * @param InSelectItem Selected item string
 */
DECLARE_DYNAMIC_DELEGATE_OneParam(FUIDropdownComponentDynamicDelegate, int32, InSelectIndex);
/**
 * Called when set data for every dropdown-option-list item.
 * @param InItemIndex Dropdown-option-list item index.
 * @param InItemScript The UIDropdownItemComponent script attached on dropdown-option-list item.
 * @param InItemWidget The dropdown-option-list item actor.
 */
DECLARE_DELEGATE_ThreeParams(FUIDropdownComponentDelegate_SetItemCustomData, int, class UUIDropdownItemComponent*, UDreamWidget*);
/**
 * Called when set data for every dropdown-option-list item.
 * @param InItemIndex Dropdown-option-list item index.
 * @param InItemScript The UIDropdownItemComponent script attached on dropdown-option-list item.
 * @param InItemWidget The dropdown-option-list item actor.
 */
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FUIDropdownComponentDynamicDelegate_SetItemCustomData, int, InItemIndex, class UUIDropdownItemComponent*, InItemScript, UDreamWidget*, InItemWidget);

UENUM(BlueprintType, Category = DreamGUI)
enum class EUIDropdownVerticalPosition : uint8
{
	Bottom,
	Middle,
	Top,
	//automatically choose bottom or top
	Automatic,
};
UENUM(BlueprintType, Category = DreamGUI)
enum class EUIDropdownHorizontalPosition : uint8
{
	Left,
	Center,
	Right,
	//automatically choose left or right
	Automatic,
};

USTRUCT(BlueprintType)
struct FUIDropdownOptionData
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
		FText Text;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
		FDreamUIImageBrush ImageBrush;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIDropdownValueChangedEvent, int32, Value);

UCLASS( ClassGroup=(DreamGUI), Blueprintable, meta=(BlueprintSpawnableComponent) )
class DREAMGUICONTROLS_API UUIDropdown : public UUISelectable, public IDreamPointerClickInterface
{
	GENERATED_BODY()

public:	
	UUIDropdown();

protected:
	virtual void Awake()override;
	/**
	 * Closes an open list, at once: a list lifted to the popup layer is no longer under this widget, so
	 * it does not go away with a dropdown that is put to sleep or destroyed. A list still fading out
	 * from an earlier close is put away at once too.
	 */
	virtual void OnDisable()override;
	/** The same, for a list opened from code on a dropdown that was never enabled. */
	virtual void OnDestroy()override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif

	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		TWeakObjectPtr<UDreamWidget> ListRoot;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		TWeakObjectPtr<UDreamText> CaptionText;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		TWeakObjectPtr<UDreamImage> CaptionImage;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		TWeakObjectPtr<UUIDropdownItemComponent> ItemTemplate;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		EUIDropdownVerticalPosition VerticalPosition = EUIDropdownVerticalPosition::Automatic;
	/** If list will overlap this button? Only valid if VerticalPosition NOT equal Middle, because Middle mode always overlay. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown", meta = (EditCondition = "VerticalPosition != EUIDropdownVerticalPosition::Middle"))
		bool VerticalOverlap = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		EUIDropdownHorizontalPosition HorizontalPosition = EUIDropdownHorizontalPosition::Center;
	
	/** Current selected option index. -1 means none selected */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		int Value = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		TArray<FUIDropdownOptionData> Options;

	/** ListRoot's max height */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown", AdvancedDisplay)
		float MaxHeight = 150;
	/**
	 * Set by SetMaxHeight, and read by Awake so it stops overwriting what it was told.
	 *
	 * Awake derives MaxHeight from ListRoot's current height, which is the right guess for a hand-wired
	 * behaviour whose list somebody drew. But a CONTROL pushes the number it wants (UDreamDropdown
	 * turns MaxVisibleItems into it, in ApplyStyle, which runs at NativeOnInitialized -- before this
	 * component's Awake at begin play), and the guess won that race: on the template road, where the
	 * list root's authored height is whatever the template's author drew, MaxVisibleItems was
	 * silently discarded. The built-in tree only escaped because the two numbers happened to agree.
	 */
	bool bMaxHeightAuthored = false;
	/**
	 * What a press outside the open list does besides closing it: on, it goes no further, as a click
	 * on the full-screen blocker this used to build went nowhere else; off, it then reaches whatever
	 * is under the pointer, as Slate's menus let it (EDreamPopupOutsideClick Consume / PassThrough).
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		bool bUseInteractionBlock = true;
	/**
	 * What Tab and Shift+Tab do in the open list besides closing it: on, the row the player is on is chosen first, as an
	 * HTML select does, and the Tab then moves on past the dropdown; off, the list only closes. Tab never stays in an open
	 * list (it is pushed with EDreamPopupTabBehavior::CloseAndContinue).
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		bool bTabCommitsHighlightedRow = true;

	bool bIsShow = false;
	bool bNeedRecreate = true;
	/** True while RecreateListItems runs: a rebuild asked for from inside it (a row's handler changing the options) waits. */
	bool bRecreatingListItems = false;
	TWeakObjectPtr<UDreamTweener> ShowOrHideTweener;
	UPROPERTY(Transient) TArray<TWeakObjectPtr<class UUIDropdownItemComponent>> CreatedItemArray;
	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDeselect_Implementation(UDreamBaseEventData* EventData)override;
	void OnSelectItem(int Index);
	void ApplyValueToVisual();
	virtual void CreateListItems();
	/** Destroy the rows the list holds and build one per option again. */
	void RecreateListItems();
	/** Whether InWidget is this dropdown or its list, or inside either: the list may be lifted out from under it. */
	bool IsPartOfDropdown(const UDreamWidget* InWidget)const;

	FDreamUIMulticastDelegateInt32 OnValueChangedCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Dropdown", DisplayName="OnValueChanged")
	FUIDropdownValueChangedEvent OnValueChangedBP;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
	FDreamUIEventDelegate OnValueChanged = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Int32);

	/** Bind this delegate and set custom data for option list item. */
	FUIDropdownComponentDelegate_SetItemCustomData OnSetItemCustomDataFunction;
	void SetValue(int InValue, bool FireEvent);
	/**
	 * Fired with true from Show -- the list built and placed under the face, and not yet lifted to the
	 * popup layer, so a control can size it first -- and with false from Hide, once focus has gone back.
	 */
	FDreamUIMulticastDelegateBool OnListVisibilityChangedCPP;
	/** Fired once a closed list is asleep and back under this widget: after its fade, or at once when it had none. */
	FSimpleMulticastDelegate OnListPutAwayCPP;

private:
	/**
	 * Close the open list: focus back on the face for every player whose focus was in the list (the popup
	 * layer's dismissal, or the capture below when the list never went up on it), the close announced,
	 * then the list faded out where it is, kept from the pointer and the pad meanwhile, and put away.
	 * bInAnimate false puts it away at once, for a dropdown going to sleep or an opener gone.
	 */
	void CloseList(bool bInAnimate);
	/** The popup layer closed the list: an outside press, Back, Tab, its opener lost, a menu opened in its place. */
	void HandleListDismissed(UDreamWidget* InList, EDreamPopupDismissReason InReason);
	/**
	 * The popup layer is about to close the list, with the player's focus still on the row they were on: on Tab, and while
	 * bTabCommitsHighlightedRow, that row is noted for HandleListDismissed to choose.
	 */
	void HandleListClosing(UDreamWidget* InList, EDreamPopupDismissReason InReason);
	/** The option index of the row player ListUserIndex has focused, or of the row holding their focus; INDEX_NONE for none. */
	int32 FindHighlightedRow() const;
	/** Open the list on the popup layer, for the player who opened it. False when the layer cannot take it. */
	bool PushListToPopupLayer();
	/** The player whose list this is: the one whose click opened it, else the one focused on the face, else the owner. */
	int32 ResolveListUserIndex() const;
	/**
	 * A closing list neither answers the pointer nor takes the pad while it fades: its rows stop being
	 * navigation targets and the list stops being hit, so a step from the face cannot land in it. Undone
	 * when the list is shown again or put away.
	 */
	void SetListInert(bool bInInert);
	/** Back under this widget and asleep, the inert state undone, and OnListPutAwayCPP told. */
	void PutListAway();
	/** The list home from the popup layer, when the layer still holds it lifted after a close. */
	void ReturnListHome();

	/** The click that is opening the list says whose it is; read by Show, through ResolveListUserIndex. */
	int32 OpeningUserIndex = INDEX_NONE;
	/** The player the open list was pushed for: whose focus picks the row a Tab chooses. */
	int32 ListUserIndex = 0;
	/** The row HandleListClosing noted for a Tab to choose, until HandleListDismissed chooses it; INDEX_NONE otherwise. */
	int32 PendingTabCommit = INDEX_NONE;
	/**
	 * Whether the open list is on the popup layer. Set before the push, not after: the push moves focus into the list, and
	 * a handler that closes the list then must find it on the layer to take it off (CloseList).
	 */
	bool bListOnPopupLayer = false;
	/** Every player's focus when the list opened, for a list the popup layer could not take; the layer keeps its own. */
	FDreamFocusReturn ListFocusReturn;
	/** See SetListPlacement. */
	FDreamPopupPlaceDelegate ListPlacement;
	/** See SetListInert: the list's own raycastable setting, and the rows it took off the navigation, to give back. */
	bool bListInert = false;
	EDreamWidgetRaycastableType ListRaycastableBeforeInert;
	TArray<TWeakObjectPtr<UUISelectable>> RowsMadeUnnavigable;

public:
	FDreamUIMulticastDelegateInt32& GetOnValueChangedEvent(){return OnValueChangedCPP;}
	FDreamUIMulticastDelegateBool& GetOnListVisibilityChangedEvent(){return OnListVisibilityChangedCPP;}
	FSimpleMulticastDelegate& GetOnListPutAwayEvent(){return OnListPutAwayCPP;}
	/**
	 * How the open list is placed while it is up on the popup layer, after its opener on every frame
	 * (FDreamPopupParams::Place). Unbound, the list keeps the offset from this dropdown it opened with.
	 */
	void SetListPlacement(const FDreamPopupPlaceDelegate& InPlacement) { ListPlacement = InPlacement; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		void Show();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		void Hide();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		int GetValue()const { return Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		EUIDropdownVerticalPosition GetVerticalPosition()const { return VerticalPosition; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		EUIDropdownHorizontalPosition GetHorizontalPosition()const { return HorizontalPosition; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		bool GetVerticalOverlap()const { return VerticalOverlap; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		const TArray<FUIDropdownOptionData>& GetOptions()const { return Options; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		FUIDropdownOptionData GetOption(int index)const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		FUIDropdownOptionData GetCurrentOption()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		float GetMaxHeight()const { return MaxHeight; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		UDreamWidget* GetListRoot()const { return ListRoot.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		bool GetUseInteractionBlock()const { return bUseInteractionBlock; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		bool GetTabCommitsHighlightedRow()const { return bTabCommitsHighlightedRow; }
	/** See bTabCommitsHighlightedRow. An open list answers the next Tab by the new setting. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
		void SetTabCommitsHighlightedRow(bool InValue) { bTabCommitsHighlightedRow = InValue; }

	/**
	 * The parts, settable from code. All four are EditAnywhere weak references the designer and .dui
	 * always reached by reflection while no caller could -- the UUIToggle transition-target hole.
	 * A part swap invalidates the built list; the caption re-applies at once.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetListRoot(UDreamWidget* InListRoot);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetCaptionText(UDreamText* InCaptionText);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetItemTemplate(UUIDropdownItemComponent* InItemTemplate);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetValue(int InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetValueWithoutNotify(int InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetVerticalPosition(EUIDropdownVerticalPosition InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetHorizontalPosition(EUIDropdownHorizontalPosition InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetVerticalOverlap(bool InValue);
	/**
	 * Replace the options. An open list is rebuilt at once, so none of its rows stands for an option
	 * that is gone; a closed one builds its rows the next time it opens.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetOptions(const TArray<FUIDropdownOptionData>& InOptions);
	/** Append options, rebuilding an open list at once as SetOptions does. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void AddOptions(const TArray<FUIDropdownOptionData>& InOptions);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetMaxHeight(float InValue) { MaxHeight = InValue; bMaxHeightAuthored = true; }
	/** See bUseInteractionBlock. An open list answers the next press by the new setting. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetUseInteractionBlock(bool InValue);

	//list items will be created at next time when show the list
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void MarkRecreateList() { bNeedRecreate = true; }
	
	/**
	 * Set custom function to customize option-list item, called when set data for every dropdown-option-list item.
	 */
	void SetItemCustomDataFunction(const FUIDropdownComponentDelegate_SetItemCustomData& InFunction);
	/**
	 * Set custom function to customize option-list item, called when set data for every dropdown-option-list item.
	 */
	void SetItemCustomDataFunction(const TFunction<void(int, class UUIDropdownItemComponent*, UDreamWidget*)>& InFunction);
	/**
	 * Set custom function to customize option-list item, called when set data for every dropdown-option-list item.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetItemCustomDataFunction(const FUIDropdownComponentDynamicDelegate_SetItemCustomData& InFunction);
	/**
	 * Clear the function set by "SetItemCustomDataFunction".
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
	void ClearItemCustomDataFunction();
};


DECLARE_DYNAMIC_DELEGATE(FUIDropdownItem_OnSelect);

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUICONTROLS_API UUIDropdownItemComponent : public UDreamUIBehaviour, public IDreamPointerClickInterface
{
	GENERATED_BODY()

public:
	UUIDropdownItemComponent();
	virtual void Awake()override;
protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		TWeakObjectPtr<UDreamText> Text;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		TWeakObjectPtr<UDreamImage> Image;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		TWeakObjectPtr<UUIToggle> Toggle;

private:
	FSimpleDelegate OnSelectCPP;
	UPROPERTY()FUIDropdownItem_OnSelect OnSelectDynamic;
public:
	/** The parts, settable from code -- the same reflection-only hole the dropdown itself had. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetText(UDreamText* InText);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetImage(UDreamImage* InImage);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetToggle(UUIToggle* InToggle);
protected:
	UFUNCTION()void DynamicDelegate_OnSelect() { OnSelectCPP.ExecuteIfBound(); }
protected:
	/**
	 * Called by UIDropdownComponent when create a item. Use this to initialize.
	 * @param Index Item's index.
	 * @param Data Item's data.
	 * @param OnSelectCallback Callback function that need to be executed by user, when select this item.
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Init"), Category = "DreamGUI-Dropdown")
	void ReceiveInit(int32 Index, const FUIDropdownOptionData& Data, const FUIDropdownItem_OnSelect& OnSelectCallback);
	/**
	 * Set this item's selection state.
	 * When select other item, then need to de-select this one.
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "SetSelectionState"), Category = "DreamGUI-Dropdown")
	void ReceiveSetSelectionState(bool InSelect);
public:
	/**
	 * Called by UIDropdownComponent when create a item. Use this to initialize.
	 * @param Index Item's index.
	 * @param Data Item's data.
	 * @param OnSelectCallback Callback function that need to be executed by user, when select this item.
	 */
	virtual void Init(int32 Index, const FUIDropdownOptionData& Data, const TFunction<void()>& OnSelectCallback);
	/**
	 * Set this item's selection state.
	 * When select other item, then need to de-select this one.
	 */
	virtual void SetSelectionState(const bool& InSelect);
	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData)override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	UDreamText* GetText()const { return Text.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	UDreamImage* GetImage()const { return Image.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	UUIToggle* GetToggle()const;
};