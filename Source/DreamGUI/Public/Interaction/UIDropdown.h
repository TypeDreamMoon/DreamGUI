// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interaction/UISelectable.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/DreamDelegateDeclaration.h"
#include "UIDropdown.generated.h"

class UUIToggle;
class UDreamImage;
class UDreamWidgetContainer;
class UDreamWidget;
class UDreamText;

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
class DREAMGUI_API UUIDropdown : public UUISelectable, public IDreamPointerClickInterface
{
	GENERATED_BODY()

public:	
	UUIDropdown();

protected:
	virtual void Awake()override;
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
	/** When show the list, create a overlay block to block input on other objects. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
		bool bUseInteractionBlock = true;

	bool bIsShow = false;
	bool bNeedRecreate = true;
	TWeakObjectPtr<UDreamTweener> ShowOrHideTweener;
	TWeakObjectPtr<UDreamWidget> BlockerWidget;
	UPROPERTY(Transient) TArray<TWeakObjectPtr<class UUIDropdownItemComponent>> CreatedItemArray;
	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDeselect_Implementation(UDreamBaseEventData* EventData)override;
	void OnSelectItem(int Index);
	void ApplyValueToVisual();
	virtual void CreateBlocker();
	virtual void CreateListItems();

	FDreamUIMulticastDelegateInt32 OnValueChangedCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Dropdown", DisplayName="OnValueChanged")
	FUIDropdownValueChangedEvent OnValueChangedBP;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Dropdown")
	FDreamUIEventDelegate OnValueChanged = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Int32);

	/** Bind this delegate and set custom data for option list item. */
	FUIDropdownComponentDelegate_SetItemCustomData OnSetItemCustomDataFunction;
	void SetValue(int InValue, bool FireEvent);
	/** Fired with true from Show and false from Hide, so a control can lift the list to a popup layer. */
	FDreamUIMulticastDelegateBool OnListVisibilityChangedCPP;
public:
	FDreamUIMulticastDelegateInt32& GetOnValueChangedEvent(){return OnValueChangedCPP;}
	FDreamUIMulticastDelegateBool& GetOnListVisibilityChangedEvent(){return OnListVisibilityChangedCPP;}
	
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
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetOptions(const TArray<FUIDropdownOptionData>& InOptions);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void AddOptions(const TArray<FUIDropdownOptionData>& InOptions);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Dropdown")
	void SetMaxHeight(float InValue) { MaxHeight = InValue; }
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
class DREAMGUI_API UUIDropdownItemComponent : public UDreamUIBehaviour, public IDreamPointerClickInterface
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