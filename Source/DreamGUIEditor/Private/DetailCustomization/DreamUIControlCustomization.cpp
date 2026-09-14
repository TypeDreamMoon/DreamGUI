// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DetailCustomization/DreamUIControlCustomization.h"

#include "Controls/DreamUIControl.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailGroup.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "DreamUIControlCustomization"

namespace DreamUIControlCustomizationLocal
{
	/** The one property name every control in this library gives its look. */
	static const TCHAR* StylePropertyName = TEXT("Style");
}

TSharedRef<IDetailCustomization> FDreamUIControlCustomization::MakeInstance()
{
	return MakeShareable(new FDreamUIControlCustomization);
}

bool FDreamUIControlCustomization::ResolveGroupName(const TSharedPtr<IPropertyHandle>& InChild, FString& OutGroupName)
{
	const FProperty* Property = InChild.IsValid() ? InChild->GetProperty() : nullptr;
	if (Property == nullptr)
	{
		return false;
	}
	// No WITH_EDITORONLY_DATA guard anywhere in this file: it is an editor module, so the metadata is
	// always there, and a guard would only suggest otherwise.
	const FString Category = Property->GetMetaData(TEXT("Category"));
	int32 PipeIndex = INDEX_NONE;
	if (!Category.FindLastChar(TEXT('|'), PipeIndex) || PipeIndex >= Category.Len() - 1)
	{
		return false;
	}
	OutGroupName = Category.RightChop(PipeIndex + 1).TrimStartAndEnd();
	return !OutGroupName.IsEmpty();
}

void FDreamUIControlCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const TSharedPtr<IPropertyHandle> StyleHandle =
		DetailBuilder.GetProperty(FName(DreamUIControlCustomizationLocal::StylePropertyName));
	if (!StyleHandle.IsValid() || !StyleHandle->IsValidHandle())
	{
		// A control with no Style property at all -- the native widget host is one, because what it
		// looks like is the hosted widget's business. Nothing to re-file; the default layout stands.
		return;
	}
	uint32 NumChildren = 0;
	if (StyleHandle->GetNumChildren(NumChildren) != FPropertyAccess::Success || NumChildren == 0)
	{
		return;
	}

	// Does this struct group ITSELF? Asked before anything is hidden, because the answer decides
	// whether there is a layout to improve at all -- and a customization that hid the style property
	// and then found nothing to put back would leave a control with no look to edit.
	bool bAnyGrouped = false;
	for (uint32 Index = 0; Index < NumChildren && !bAnyGrouped; ++Index)
	{
		FString Unused;
		bAnyGrouped = ResolveGroupName(StyleHandle->GetChildHandle(Index), Unused);
	}
	if (!bAnyGrouped)
	{
		return;
	}

	// The category the style already lives in, so nothing moves house: the rows gain structure where
	// they are rather than appearing somewhere new.
	FString OuterCategory;
	if (const FProperty* StyleProperty = StyleHandle->GetProperty())
	{
		OuterCategory = StyleProperty->GetMetaData(TEXT("Category"));
	}
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		OuterCategory.IsEmpty() ? FName(DreamUIControlCustomizationLocal::StylePropertyName) : FName(*OuterCategory));

	DetailBuilder.HideProperty(StyleHandle);

	// Groups in FIRST-SEEN order, which is declaration order: a style author writes geometry before
	// colours because that is the order they are read in, and sorting the groups alphabetically would
	// throw that away for no gain. AddGroup appends, so walking the children in order is all it takes.
	TMap<FString, IDetailGroup*> Groups;
	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		const TSharedPtr<IPropertyHandle> Child = StyleHandle->GetChildHandle(Index);
		if (!Child.IsValid() || !Child->IsValidHandle())
		{
			continue;
		}
		const FProperty* ChildProperty = Child->GetProperty();
		if (ChildProperty == nullptr)
		{
			continue;
		}
		if (ChildProperty->HasMetaData(TEXT("InlineEditConditionToggle")))
		{
			// The override bit. The engine draws it ON the row of the property it gates, so adding it
			// here as well would put the same checkbox on screen twice -- once inline and once as a
			// row of its own, which is the flat panel this customization exists to end.
			continue;
		}
		FString GroupName;
		if (!ResolveGroupName(Child, GroupName))
		{
			// Ungrouped fields keep their place at the top level of the category, above the groups --
			// which is where a struct's un-sectioned fields belong and where a reader looks first.
			Category.AddProperty(Child.ToSharedRef());
			continue;
		}
		IDetailGroup*& Group = Groups.FindOrAdd(GroupName);
		if (Group == nullptr)
		{
			// First field of this group: the group is made HERE, so the groups appear in the order
			// their first field is declared in -- no separate list of names to keep in step with the
			// map, which is what the leftover of one was.
			Group = &Category.AddGroup(FName(*GroupName), FText::FromString(GroupName));
		}
		Group->AddPropertyRow(Child.ToSharedRef());
	}
}

#undef LOCTEXT_NAMESPACE
