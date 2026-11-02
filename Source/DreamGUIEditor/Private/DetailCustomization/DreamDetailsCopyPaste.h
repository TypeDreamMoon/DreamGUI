// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DetailWidgetRow.h"
#include "Framework/Commands/UIAction.h"
#include "PropertyEditorClipboard.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"

/**
 * Right-click Copy / Paste for a hand-built row that stands for exactly one property.
 *
 * A custom row has no property node of its own. PropertyHandleList only tells the details view which
 * properties the row edits (reset to default, "modified" filtering); SDetailSingleItemRow works out
 * what to copy from the row's property node, finds none, and leaves Copy and Paste in the menu but
 * disabled unless the row binds its own actions. These are the two operations the view runs for a
 * property row it builds itself: the handle's formatted string, through the editor clipboard.
 *
 * Only for a row that edits one value. A row spread over several properties has no single string to
 * copy, and binding one of them would paste half a row.
 */
namespace DreamDetailsCopyPaste
{
	inline void BindToRow(FDetailWidgetRow& InRow, const TSharedPtr<IPropertyHandle>& InHandle)
	{
		InRow
		.CopyAction(FUIAction(
			FExecuteAction::CreateLambda([InHandle]()
			{
				FString Value;
				if (InHandle.IsValid() && InHandle->GetValueAsFormattedString(Value, PPF_Copy) == FPropertyAccess::Success)
				{
					FPropertyEditorClipboard::ClipboardCopy(*Value);
				}
			}),
			FCanExecuteAction::CreateLambda([InHandle]()
			{
				return InHandle.IsValid() && InHandle->IsValidHandle();
			})))
		.PasteAction(FUIAction(
			FExecuteAction::CreateLambda([InHandle]()
			{
				FString Value;
				FPropertyEditorClipboard::ClipboardPaste(Value);
				if (InHandle.IsValid() && !Value.IsEmpty())
				{
					const FScopedTransaction Transaction(NSLOCTEXT("DreamDetailsCopyPaste", "PasteProperty", "Paste Property"));
					InHandle->SetValueFromFormattedString(Value, EPropertyValueSetFlags::InstanceObjects);
				}
			}),
			FCanExecuteAction::CreateLambda([InHandle]()
			{
				return InHandle.IsValid() && InHandle->IsValidHandle() && InHandle->IsEditable();
			})));
	}
}
