// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamWidgetTreeEditing.h"

#include "DreamWidgetBlueprint.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "DreamGUI.h"

#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "DreamWidgetTreeEditing"

namespace DreamWidgetTreeEditing
{
	namespace Local
	{
		/** The tree InBlueprint authors, or null when it has none yet. Never creates one. */
		UDreamWidgetTree* GetTree(const UDreamWidgetBlueprint* InBlueprint)
		{
			return IsValid(InBlueprint) && IsValid(InBlueprint->WidgetTree) ? InBlueprint->WidgetTree.Get() : nullptr;
		}
	}

	bool IsTemplateWidgetOf(const UDreamWidgetBlueprint* InBlueprint, const UDreamWidget* InWidget)
	{
		const UDreamWidgetTree* Tree = Local::GetTree(InBlueprint);
		if (Tree == nullptr || !IsValid(InWidget))
		{
			return false;
		}
		bool bFound = false;
		Tree->ForEachWidget([&bFound, InWidget](const UDreamWidget* Widget)
		{
			bFound |= (Widget == InWidget);
		});
		return bFound;
	}

	void NotifyStructureChanged(UDreamWidgetBlueprint* InBlueprint)
	{
		if (!IsValid(InBlueprint))
		{
			return;
		}
		// Structurally, not just modified: the compiler declares one variable per widget, so adding or
		// removing one changes the class's members and every dependent has to be told.
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);
	}

	FString MakeUniqueDisplayName(const UDreamWidgetTree* InTree, const FString& InDesired, const UDreamWidget* InIgnore)
	{
		const FString Base = InDesired.IsEmpty() ? TEXT("Widget") : InDesired;
		if (InTree == nullptr)
		{
			return Base;
		}

		TSet<FString> Taken;
		InTree->ForEachWidget([&Taken, InIgnore](const UDreamWidget* Widget)
		{
			if (Widget != InIgnore)
			{
				Taken.Add(Widget->GetDisplayName());
			}
		});
		if (!Taken.Contains(Base))
		{
			return Base;
		}
		// UMG's shape: Name, Name_1, Name_2. Not a GUID -- this is the name a designer reads in the
		// hierarchy and types into the graph, and the compiler derives the variable from it.
		for (int32 Suffix = 1; ; Suffix++)
		{
			const FString Candidate = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
			if (!Taken.Contains(Candidate))
			{
				return Candidate;
			}
		}
	}

	void ForEachWidgetInSubtree(UDreamWidget* InRoot, TFunctionRef<void(UDreamWidget*)> InPredicate)
	{
		if (!IsValid(InRoot))
		{
			return;
		}
		InPredicate(InRoot);
		// A copy of the array: the predicate is allowed to reparent or destroy what it is handed.
		const TArray<UDreamWidget*> Children = InRoot->GetChildren();
		for (UDreamWidget* Child : Children)
		{
			ForEachWidgetInSubtree(Child, InPredicate);
		}
	}

	UDreamWidget* DuplicateWidget(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InSource,
		UDreamWidget* InNewParent, int32 InSiblingIndex)
	{
		if (!IsTemplateWidgetOf(InBlueprint, InSource) || !IsTemplateWidgetOf(InBlueprint, InNewParent))
		{
			return nullptr;
		}
		if (InNewParent == InSource || InNewParent->IsChildOf(InSource))
		{
			// Copying a subtree into itself: the copy would contain a copy of the place it is going.
			return nullptr;
		}
		if (!InNewParent->CanAcceptAdditionalChildren(1))
		{
			return nullptr;
		}

		UDreamWidgetTree* Tree = Local::GetTree(InBlueprint);
		InBlueprint->Modify();
		Tree->Modify();
		InNewParent->Modify();

		// NOT DuplicateObject. Every widget in a tree is outered flat to the UDreamWidgetTree, and
		// duplication follows the OUTER chain: a widget's children are not its subobjects, so
		// DuplicateObject copied the source alone and left the copy's Children array pointing at the
		// ORIGINAL children -- which RestoreParentLinksRecursive then re-parented onto the copy,
		// taking them out of the hierarchy they were in. Duplicating a leaf hides all of it.
		UDreamWidget* Copy = UDreamWidget::DuplicateSubtree(Tree, InSource);
		if (!IsValid(Copy))
		{
			return nullptr;
		}

		// Re-home flat, with fresh names. See the header for why this is not cosmetic.
		TArray<UDreamWidget*> Copied;
		ForEachWidgetInSubtree(Copy, [&Copied](UDreamWidget* Widget) { Copied.Add(Widget); });
		for (UDreamWidget* Widget : Copied)
		{
			Widget->Rename(*MakeUniqueObjectName(Tree, Widget->GetClass()).ToString(), Tree, REN_DontCreateRedirectors);
			Widget->SetDisplayName(MakeUniqueDisplayName(Tree, Widget->GetDisplayName(), Widget));
		}

		if (!Copy->TrySetParent(InNewParent, /*bKeepWorldPosition*/false, InSiblingIndex))
		{
			Copy->DestroyWidget();
			return nullptr;
		}
		NotifyStructureChanged(InBlueprint);
		return Copy;
	}

	UDreamWidget* CreateWidget(UDreamWidgetBlueprint* InBlueprint, TSubclassOf<UDreamWidget> InWidgetClass,
		UDreamWidget* InParent, int32 InSiblingIndex, const FString& InDesiredDisplayName)
	{
		if (!IsValid(InBlueprint))
		{
			return nullptr;
		}
		if (!IsValid(InWidgetClass) || InWidgetClass->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot create a widget of class '%s'."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(InWidgetClass));
			return nullptr;
		}

		UDreamWidgetTree* Tree = InBlueprint->GetOrCreateWidgetTree();
		UDreamWidget* Parent = InParent != nullptr ? InParent : Tree->RootWidget.Get();
		if (!IsTemplateWidgetOf(InBlueprint, Parent))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d The parent does not belong to '%s'."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InBlueprint->GetName());
			return nullptr;
		}
		if (!Parent->CanAcceptAdditionalChildren(1))
		{
			return nullptr;
		}

		// The tree as well as the parent: the widget is about to be outered to it, and undoing the
		// creation has to be able to put the tree back the way it was.
		InBlueprint->Modify();
		Tree->Modify();
		Parent->Modify();

		UDreamWidget* Widget = Tree->ConstructWidget(InWidgetClass);
		if (!IsValid(Widget))
		{
			return nullptr;
		}
		const FString Desired = InDesiredDisplayName.IsEmpty() ? InWidgetClass->GetName() : InDesiredDisplayName;
		Widget->SetDisplayName(MakeUniqueDisplayName(Tree, Desired, Widget));

		if (!Widget->TrySetParent(Parent, /*bKeepWorldPosition*/false, InSiblingIndex))
		{
			// The capacity check above already passed, so this is a cycle or a refusal from the panel
			// itself. Leaving a parentless widget outered to the tree would put it in no hierarchy and
			// in every save.
			Widget->DestroyWidget();
			return nullptr;
		}

		NotifyStructureChanged(InBlueprint);
		return Widget;
	}

	bool DeleteWidget(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InWidget)
	{
		if (!IsTemplateWidgetOf(InBlueprint, InWidget))
		{
			return false;
		}
		UDreamWidgetTree* Tree = Local::GetTree(InBlueprint);
		if (Tree->RootWidget == InWidget)
		{
			// A hierarchy with no root is not a state the compiler or the designer has an answer for.
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Refusing to delete the root of '%s'; delete its children instead."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InBlueprint->GetName());
			return false;
		}

		InBlueprint->Modify();
		Tree->Modify();
		if (UDreamWidget* Parent = InWidget->GetParent())
		{
			Parent->Modify();
		}
		InWidget->Modify();

		// Not MarkAsGarbage: the transaction buffer is holding this subtree so undo can put it back,
		// and a garbage object cannot be restored. Detached is what makes it absent from the class and
		// from the save; collection follows once nothing references it.
		InWidget->DestroyWidget();

		NotifyStructureChanged(InBlueprint);
		return true;
	}

	bool ReparentWidget(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InWidget, UDreamWidget* InNewParent, int32 InSiblingIndex)
	{
		if (!IsTemplateWidgetOf(InBlueprint, InWidget) || !IsTemplateWidgetOf(InBlueprint, InNewParent))
		{
			return false;
		}
		UDreamWidgetTree* Tree = Local::GetTree(InBlueprint);
		if (Tree->RootWidget == InWidget)
		{
			return false;
		}
		if (InWidget == InNewParent || InNewParent->IsChildOf(InWidget))
		{
			return false;
		}
		// Only when it is actually moving house: a reorder inside the same parent is not an arrival,
		// and asking whether there is room for one more would refuse a full panel reordering itself.
		if (InWidget->GetParent() != InNewParent && !InNewParent->CanAcceptChild(InWidget))
		{
			return false;
		}

		InBlueprint->Modify();
		Tree->Modify();
		if (UDreamWidget* OldParent = InWidget->GetParent())
		{
			OldParent->Modify();
		}
		InNewParent->Modify();
		InWidget->Modify();

		// Never keep world position on a template: a template is inert, so its drawn transform is
		// whatever it was last told rather than where it is, and writing that back is how an authored
		// position turns into a stale one.
		if (!InWidget->TrySetParent(InNewParent, /*bKeepWorldPosition*/false, InSiblingIndex))
		{
			return false;
		}

		NotifyStructureChanged(InBlueprint);
		return true;
	}

	FString RenameWidget(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InWidget, const FString& InDesiredDisplayName)
	{
		if (!IsTemplateWidgetOf(InBlueprint, InWidget))
		{
			return FString();
		}
		UDreamWidgetTree* Tree = Local::GetTree(InBlueprint);
		const FString Applied = MakeUniqueDisplayName(Tree, InDesiredDisplayName, InWidget);
		if (Applied == InWidget->GetDisplayName())
		{
			return Applied;
		}

		InBlueprint->Modify();
		InWidget->Modify();
		InWidget->SetDisplayName(Applied);

		// Structural, not merely modified: the display name IS the variable name, so a rename removes
		// one member from the class and adds another. Anything bound to the old one has to be told.
		NotifyStructureChanged(InBlueprint);
		return Applied;
	}
}

#undef LOCTEXT_NAMESPACE
