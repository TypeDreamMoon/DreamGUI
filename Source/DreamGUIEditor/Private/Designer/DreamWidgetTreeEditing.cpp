// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamWidgetTreeEditing.h"

#include "Designer/DreamUITextAuthoringGate.h"
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

		/**
		 * The name a refusal should quote: the one in the hierarchy panel, not the UObject's.
		 *
		 * GetNameSafe would print "DreamWidget_7", which is a name the author has never seen and cannot
		 * search their .dui for -- and the whole point of naming the file in the message is that they
		 * can go and find the line.
		 */
		FString DisplayNameOf(const UDreamWidget* InWidget)
		{
			return IsValid(InWidget) ? InWidget->GetDisplayName() : FString(TEXT("nothing"));
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
		// Before the validity checks, not after: an author whose duplicate was going to be refused for
		// some second reason still needs to be told the one they can act on. See DreamUITextAuthoringGate.
		if (DreamUITextAuthoring::RefuseStructuralEdit(InBlueprint, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
			FString::Printf(TEXT("duplicate '%s'"), *Local::DisplayNameOf(InSource))))
		{
			return nullptr;
		}
		// Every refusal below says so. A duplicate that returns null without a word is a menu item that
		// does nothing when clicked, which is what it looked like from the outside.
		if (!IsTemplateWidgetOf(InBlueprint, InSource) || !IsTemplateWidgetOf(InBlueprint, InNewParent))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot duplicate '%s' under '%s': not part of '%s' authoring tree."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(InSource), *GetNameSafe(InNewParent), *GetNameSafe(InBlueprint));
			return nullptr;
		}
		if (InNewParent == InSource || InNewParent->IsChildOf(InSource))
		{
			// Copying a subtree into itself: the copy would contain a copy of the place it is going.
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot duplicate '%s' into itself."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InSource->GetDisplayName());
			return nullptr;
		}
		if (!InNewParent->CanAcceptAdditionalChildren(1))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' cannot take another child, so '%s' was not duplicated."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InNewParent->GetDisplayName(), *InSource->GetDisplayName());
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
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Duplicating the subtree under '%s' produced nothing."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InSource->GetDisplayName());
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
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' refused the copy of '%s' as a child."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InNewParent->GetDisplayName(), *InSource->GetDisplayName());
			Copy->DestroyWidget();
			return nullptr;
		}
		NotifyStructureChanged(InBlueprint);
		return Copy;
	}

	UDreamWidget* CreateWidget(UDreamWidgetBlueprint* InBlueprint, TSubclassOf<UDreamWidget> InWidgetClass,
		UDreamWidget* InParent, int32 InSiblingIndex, const FString& InDesiredDisplayName)
	{
		if (DreamUITextAuthoring::RefuseStructuralEdit(InBlueprint, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
			FString::Printf(TEXT("create a '%s'"), *GetNameSafe(InWidgetClass))))
		{
			return nullptr;
		}
		// Same rule as the duplicate path below: every refusal says so. A command that returns null
		// without a word is a palette drop that does nothing, and the user has no way to tell that
		// from a bug in the drag.
		if (!IsValid(InBlueprint))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot create a widget: there is no Blueprint to create it in."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
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
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' cannot take another child, so nothing was created under it."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Parent->GetDisplayName());
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
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Constructing a '%s' produced nothing."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(InWidgetClass));
			return nullptr;
		}
		const FString Desired = InDesiredDisplayName.IsEmpty() ? InWidgetClass->GetName() : InDesiredDisplayName;
		Widget->SetDisplayName(MakeUniqueDisplayName(Tree, Desired, Widget));

		if (!Widget->TrySetParent(Parent, /*bKeepWorldPosition*/false, InSiblingIndex))
		{
			// The capacity check above already passed, so this is a cycle or a refusal from the panel
			// itself. Leaving a parentless widget outered to the tree would put it in no hierarchy and
			// in every save.
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' refused the new '%s' as a child; it was not created."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Parent->GetDisplayName(), *GetNameSafe(InWidgetClass));
			Widget->DestroyWidget();
			return nullptr;
		}

		NotifyStructureChanged(InBlueprint);
		return Widget;
	}

	bool DeleteWidget(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InWidget)
	{
		if (DreamUITextAuthoring::RefuseStructuralEdit(InBlueprint, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
			FString::Printf(TEXT("delete '%s'"), *Local::DisplayNameOf(InWidget))))
		{
			return false;
		}
		if (!IsTemplateWidgetOf(InBlueprint, InWidget))
		{
			// The one branch in this family W5 left silent, and it is the one that fires when a caller
			// hands over a preview widget instead of a template -- the single easiest mistake to make
			// against this API. Said out loud for the same reason as its four siblings.
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot delete '%s': not part of '%s' authoring tree."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(InWidget), *GetNameSafe(InBlueprint));
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
		// Reordering inside one parent comes through here too, and it is refused with the rest: sibling
		// order in the file IS the order in the hierarchy, so a reorder is a text edit like any other
		// structural change and there is nothing in the patcher that moves a block of lines.
		if (DreamUITextAuthoring::RefuseStructuralEdit(InBlueprint, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
			FString::Printf(TEXT("move '%s'"), *Local::DisplayNameOf(InWidget))))
		{
			return false;
		}
		if (!IsTemplateWidgetOf(InBlueprint, InWidget) || !IsTemplateWidgetOf(InBlueprint, InNewParent))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot move '%s' under '%s': not part of '%s' authoring tree."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(InWidget), *GetNameSafe(InNewParent), *GetNameSafe(InBlueprint));
			return false;
		}
		UDreamWidgetTree* Tree = Local::GetTree(InBlueprint);
		if (Tree->RootWidget == InWidget)
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Refusing to move the root of '%s'; a hierarchy has to have one."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InBlueprint->GetName());
			return false;
		}
		if (InWidget == InNewParent || InNewParent->IsChildOf(InWidget))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot move '%s' into itself or into something it contains."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InWidget->GetDisplayName());
			return false;
		}
		// Only when it is actually moving house: a reorder inside the same parent is not an arrival,
		// and asking whether there is room for one more would refuse a full panel reordering itself.
		if (InWidget->GetParent() != InNewParent && !InNewParent->CanAcceptChild(InWidget))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' cannot take '%s', so it was not moved."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InNewParent->GetDisplayName(), *InWidget->GetDisplayName());
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
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' refused '%s' as a child; it stayed where it was."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InNewParent->GetDisplayName(), *InWidget->GetDisplayName());
			return false;
		}

		NotifyStructureChanged(InBlueprint);
		return true;
	}

	FString RenameWidget(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InWidget, const FString& InDesiredDisplayName)
	{
		// A rename is structural here for a reason the other four do not have: the display name is the
		// node's `id`, which is its identity in the file, its compiler variable, its binding key and
		// its animation path all at once. Changing it from the designer would rename three things the
		// text still spells the old way -- and the language has `(was: OldId)` precisely so that a
		// rename is expressed in the file, where the fixup can see it.
		if (DreamUITextAuthoring::RefuseStructuralEdit(InBlueprint, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
			FString::Printf(TEXT("rename '%s'"), *Local::DisplayNameOf(InWidget))))
		{
			return FString();
		}
		if (!IsTemplateWidgetOf(InBlueprint, InWidget))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot rename '%s': not part of '%s' authoring tree."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(InWidget), *GetNameSafe(InBlueprint));
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
