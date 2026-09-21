// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Event/DreamUIEventDelegate.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Input/SComboButton.h"
#pragma once


class UDreamWidgetSubObjectBehaviour;
class UDreamUIBehaviour;
/**
 * 
 */
class FDreamUIEventDelegateCustomization : public IPropertyTypeCustomization
{
protected:
	TSharedPtr<IPropertyHandle> PropertyHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilites;
	static TArray<FString> CopySourceData;
	TSharedPtr<SWidget> ColorPickerParentWidget;
	TArray<TSharedRef<SWidget>> EventParameterWidgetArray;
	TSharedPtr<SBox> EventsWidget;
private:
	bool CanChangeParameterType = true;
	bool IsParameterTypeValid(EDreamUIEventDelegateParameterType InParamType)
	{
		return InParamType != EDreamUIEventDelegateParameterType::None;
	}
	FText GetEventTitleName()const;
	FText GetEventItemFunctionName(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const;
	UObject* GetEventItemTargetObject(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const;
	UDreamWidget* GetEventItemHelperWidget(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const;
	FText GetComponentDisplayName(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const;
	EVisibility GetNativeParameterWidgetVisibility(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const;
	EVisibility GetDrawFunctionParameterWidgetVisibility(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const;
	EVisibility GetNotValidParameterWidgetVisibility(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const;
public:
	FDreamUIEventDelegateCustomization(bool InCanChangeParameterType)
	{
		CanChangeParameterType = InCanChangeParameterType;
	}
	~FDreamUIEventDelegateCustomization()
	{
		
	}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FDreamUIEventDelegateCustomization(true));
	}
	/**
	 * IPropertyTypeCustomization interface.
	 *
	 * Deliberately empty: this type draws its whole UI as CHILDREN, and a header row would put the
	 * struct's name above a list that already names itself. CustomizeChildren is the real entry point.
	 */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override {};

	EDreamUIEventDelegateParameterType GetNativeParameterType()const;
	void AddNativeParameterTypeProperty(IDetailChildrenBuilder& ChildBuilder);
	EDreamUIEventDelegateParameterType GetEventDataParameterType(TSharedRef<IPropertyHandle> EventDataItemHandle)const;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)override;

	void SetEventDataParameterType(TSharedRef<IPropertyHandle> EventDataItemHandle, EDreamUIEventDelegateParameterType ParameterType);
private:

	void UpdateEventsLayout();

	/**
	 * Re-derive one row's TargetObject from its helper fields, and hand it back.
	 *
	 * The walk is the runtime's own (UDreamUIEventDelegateParameterHelper::ResolveBindingTarget), so
	 * the panel cannot show a target the runtime will not reach -- this panel used to carry its own
	 * copy of it, in two places, and both keyed behaviours by the instance FName that UE re-numbers
	 * on every preview rebuild. What is local is the write-back: it runs while the row is being DRAWN,
	 * so it goes in non-transactionally or merely opening the panel would dirty the asset.
	 *
	 * Also backfills HelperComponentIndex the first time a legacy name resolves.
	 */
	UObject* RefreshTargetObject(TSharedRef<IPropertyHandle> InItemPropertyHandle) const;

	TSharedPtr<IPropertyHandleArray> GetEventListHandle()const;
	FOptionalSize GetEventItemHeight(int itemIndex)const
	{
		// Bound-checked because this is an ATTRIBUTE on a row that outlives the array behind it: the
		// row widgets built for the old list are still in the tree, and still asked for their height,
		// during the frame in which a delete rebuilds EventParameterWidgetArray shorter. The last
		// row's index then reads off the end of the allocation.
		if (!EventParameterWidgetArray.IsValidIndex(itemIndex))
		{
			return 60;//the fixed part of a row; the row is about to be replaced anyway
		}
		return EventParameterWidgetArray[itemIndex]->GetCachedGeometry().Size.Y + 60;//60 is other's size
	}
	FOptionalSize GetEventTotalHeight()const
	{
		float result = EventParameterWidgetArray.Num() > 0 ? 32 : 40;//32 & 40 is the header and tail size
		for (int i = 0; i < EventParameterWidgetArray.Num(); i++)
		{
			result += GetEventItemHeight(i).Get();
		}
		return result;
	}
	TWeakObjectPtr<UWorld> World;
	TSharedPtr<SComboButton> WidgetPickerComboButton;
	TSharedRef<SWidget> DrawDreamWidgetSelectorForDesigner(int32 itemIndex);
	TSharedRef<SWidget> MakeComponentSelectorMenu(int32 itemIndex);
	TSharedRef<SWidget> MakeFunctionSelectorMenu(int32 itemIndex);
	void OnHelperWidgetParameterChanged(TSharedRef<IPropertyHandle> ItemPropertyHandle);
	void OnSelectWidgetSubObject(UDreamWidgetSubObjectBehaviour* SubObj, TSharedRef<IPropertyHandle> ItemPropertyHandle);
	void OnSelectComponent(UDreamUIBehaviour* Comp, TSharedRef<IPropertyHandle> ItemPropertyHandle);
	void OnSelectWidgetSelf(TSharedRef<IPropertyHandle> ItemPropertyHandle);
	void OnSelectFunction(FName FuncName, EDreamUIEventDelegateParameterType ParamType, bool UseNativeParameter, TSharedRef<IPropertyHandle> ItemPropertyHandle);
	bool IsComponentSelectorMenuEnabled(TSharedRef<IPropertyHandle> ItemPropertyHandle)const;
	bool IsFunctionSelectorMenuEnabled(TSharedRef<IPropertyHandle> ItemPropertyHandle)const;
	/**
	 * Whether this panel may AUTHOR a binding. It may not.
	 *
	 * The plugin has one event mechanism now: `EventName -> Handler`, resolved by the compiler into
	 * UDreamWidgetBlueprint::EventBindings and bound by UDreamUserWidget::BindEventBindings -- which
	 * since this change attaches to FDreamUIEventDelegate events as well as to multicast delegates,
	 * so every event in the plugin is routable and this list is no longer the only way to handle one.
	 * A route names a function on the USER WIDGET, which is where UMG puts event handling; a binding
	 * here names an arbitrary object and carries a literal argument, which UMG has no equivalent of
	 * and the `.dui` has no syntax for.
	 *
	 * Serialized bindings still fire, and are still REMOVABLE from here -- a record no UI can clear is
	 * the worse failure of the two. Only the controls that would create or widen one are off.
	 *
	 * One function rather than a scattering of `false`s so that the decision has exactly one place to
	 * be read, and one place to be changed if it is ever revisited.
	 */
	bool CanAuthorLegacyBinding()const { return false; }
	/** How many bindings this delegate carries, for the notice the panel logs when it opens. */
	int32 GetAuthoredBindingCount()const;
	bool HasAuthoredBindings()const { return GetAuthoredBindingCount() > 0; }
	void OnClickListAdd();
	void OnClickListEmpty();
	FReply OnClickAddRemove(bool AddOrRemove, int32 Index, int32 Count);
	FReply OnClickCopyPaste(bool CopyOrPaste, int32 Index);
	FReply OnClickDuplicate(int32 Index);
	FReply OnClickMoveUpDown(bool UpOrDown, int32 Index);

	TSharedRef<SWidget> DrawFunctionParameter(TSharedRef<IPropertyHandle> InDataContainerHandle, EDreamUIEventDelegateParameterType InFunctionParameterType, UFunction* InFunction);
	//function's parameter editor
	TSharedRef<SWidget> DrawFunctionReferenceParameter(TSharedRef<IPropertyHandle> InDataContainerHandle, EDreamUIEventDelegateParameterType FunctionParameterType, UFunction* InFunction);

	void ObjectValueChange(const FAssetData& InObj, TSharedPtr<IPropertyHandle> BufferHandle, TSharedPtr<IPropertyHandle> ObjectReferenceHandle, bool ObjectOrWidget);
	const UClass* GetClassValue(TSharedPtr<IPropertyHandle> ClassReferenceHandle)const;
	void ClassValueChange(const UClass* InClass, TSharedPtr<IPropertyHandle> ClassReferenceHandle);
	void EnumValueChange(int32 InValue, ESelectInfo::Type SelectionType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void BoolValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void FloatValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void DoubleValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void Int8ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void UInt8ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void Int16ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void UInt16ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void Int32ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void UInt32ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void Int64ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void UInt64ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void StringValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void NameValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void TextValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void Vector2ItemValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	TOptional<float> Vector2GetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const;
	void Vector3ItemValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	TOptional<float> Vector3GetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const;
	void Vector4ItemValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	TOptional<float> Vector4GetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const;
	/**
	 * FQuat's own pair.
	 *
	 * The row used to borrow the FVector4 pair, and IPropertyHandle::GetValue(FVector4&) REFUSES an
	 * FQuat property -- so the four boxes read the fallback and always showed 0,0,0,0, and every edit
	 * wrote an FVector4 the handle rejected. The buffer happened to be the right length, which is why
	 * nothing ever reported it as a crash.
	 */
	void QuatItemValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	TOptional<float> QuatGetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const;
	/**
	 * A full nested editor for an arbitrary USTRUCT parameter, over a real instance of it.
	 *
	 * The value travels as exported text (FDreamUIEventDelegateData::StructValue), so this imports it
	 * into an FStructOnScope, lets the property editor edit that, and exports it back on every
	 * committed change. The view has to be kept alive for as long as its widget is on screen, which is
	 * what StructParameterViews is for -- both it and EventParameterWidgetArray are emptied by
	 * UpdateEventsLayout, because the rows are rebuilt wholesale.
	 */
	TSharedRef<SWidget> MakeStructParameterEditor(TSharedPtr<IPropertyHandle> InStructValueHandle, UScriptStruct* InStruct);
	void OnStructParameterChanged(const FPropertyChangedEvent& InEvent, TSharedPtr<IPropertyHandle> InStructValueHandle, TSharedPtr<FStructOnScope> InScope, TSharedPtr<IPropertyHandle> InStructTypeHandle);
	TArray<TSharedRef<class IStructureDetailsView>> StructParameterViews;
	FLinearColor LinearColorGetValue(bool bIsLinearColor, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const;
	void LinearColorValueChange(FLinearColor NewValue, bool bIsLinearColor, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	FReply OnMouseButtonDownColorBlock(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsLinearColor, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	TOptional<float> RotatorGetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const;
	void RotatorValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
	void SetBufferValue(TSharedPtr<IPropertyHandle> BufferHandle, const TArray<uint8>& BufferArray);
	void SetBufferLength(TSharedPtr<IPropertyHandle> BufferHandle, int32 Count);
	/**
	 * Make the stored buffer the length the runtime needs for InParamType, migrating what is already
	 * there rather than clearing it. Does nothing for the types that do not use the raw buffer.
	 */
	void PrepareParameterBuffer(TSharedPtr<IPropertyHandle> BufferHandle, EDreamUIEventDelegateParameterType InParamType);
	TArray<uint8> GetBuffer(TSharedPtr<IPropertyHandle> BufferHandle);
	TArray<uint8> GetPropertyBuffer(TSharedPtr<IPropertyHandle> BufferHandle) const;
	int32 GetEnumValue(TSharedPtr<IPropertyHandle> ValueHandle)const;
	FText GetTextValue(TSharedPtr<IPropertyHandle> ValueHandle)const;
	void SetTextValue(const FText& InText, ETextCommit::Type InCommitType, TSharedPtr<IPropertyHandle> ValueHandle);
	void ClearValueBuffer(TSharedPtr<IPropertyHandle> InItemPropertyHandle);
	void ClearReferenceValue(TSharedPtr<IPropertyHandle> InItemPropertyHandle);
	void ClearObjectValue(TSharedPtr<IPropertyHandle> InItemPropertyHandle);
	void OnParameterTypeChange(TSharedRef<IPropertyHandle> InItemPropertyHandle);
	void CreateColorPicker(bool bIsLinearColor, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle);
};

