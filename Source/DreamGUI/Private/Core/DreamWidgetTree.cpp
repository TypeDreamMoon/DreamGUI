// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "DreamGUI.h"

UWorld* UDreamWidgetTree::GetWorld() const
{
	// Null for a tree held as a class template, which is outered to the class rather than a world.
	// Callers must tolerate that: it is the signal that this tree is a template, not an instance.
	return GetTypedOuter<UWorld>();
}

UDreamWidget* UDreamWidgetTree::ConstructWidget(TSubclassOf<UDreamWidget> InWidgetClass, FName InName)
{
	if (!IsValid(InWidgetClass))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot construct a widget from a null class in tree '%s'."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName());
		return nullptr;
	}
	// RF_Transactional so widget creation participates in undo the same way the attach path does.
	UDreamWidget* Widget = NewObject<UDreamWidget>(this, InWidgetClass, InName, RF_Transactional);
	// Authoring is the one moment a widget's identity is born. Everything downstream -- the class
	// archetype, every preview instance -- is a copy of this object and inherits it, which is what
	// lets the designer pair a preview back to the widget the author is editing. See GetWidgetGuid.
	if (IsValid(Widget))
	{
		Widget->AssignNewWidgetGuid();
	}
	return Widget;
}

void UDreamWidgetTree::PostLoad()
{
	Super::PostLoad();
	// Parent is transient, so a tree that arrives from disk has its Children arrays intact and every
	// back-pointer empty. The tree a compile produces gets this from the compiler and an instanced one
	// from the generated class; the AUTHORING tree on the Blueprint comes straight off disk and had
	// nobody to do it -- which is why GetParent() was null for every widget in a loaded asset, and why
	// the designer's duplicate refused to copy anything in one ("the authored root has no parent").
	RebuildParentLinks();
}

void UDreamWidgetTree::RebuildParentLinks()
{
	if (IsValid(RootWidget))
	{
		RootWidget->RestoreParentLinksRecursive();
	}
}

void UDreamWidgetTree::ForEachWidget(TFunctionRef<void(UDreamWidget*)> InPredicate) const
{
	if (!IsValid(RootWidget))
	{
		return;
	}
	// Iterative rather than recursive: a malformed Children array would blow the stack, and this runs
	// over trees that arrive from disk. RestoreParentLinksRecursive is the pass that reports cycles;
	// here a revisit is simply skipped so a caller can still walk a damaged tree without hanging.
	TSet<UDreamWidget*> Visited;
	TArray<UDreamWidget*> Pending;
	Pending.Push(RootWidget);
	while (Pending.Num() > 0)
	{
		UDreamWidget* Widget = Pending.Pop(EAllowShrinking::No);
		if (!IsValid(Widget))
		{
			continue;
		}
		bool bAlreadyVisited = false;
		Visited.Add(Widget, &bAlreadyVisited);
		if (bAlreadyVisited)
		{
			continue;
		}
		InPredicate(Widget);
		const TArray<UDreamWidget*>& Children = Widget->GetChildren();
		// Push in reverse so siblings are visited in their sibling order.
		for (int32 i = Children.Num() - 1; i >= 0; i--)
		{
			Pending.Push(Children[i]);
		}
	}
}

int32 UDreamWidgetTree::CountWidgets() const
{
	int32 Count = 0;
	ForEachWidget([&Count](UDreamWidget*) { Count++; });
	return Count;
}

UDreamWidget* UDreamWidgetTree::FindWidgetByVariableName(FName InVariableName) const
{
	UDreamWidget* Found = nullptr;
	ForEachWidget([&Found, InVariableName](UDreamWidget* Widget)
	{
		if (Found == nullptr && MakeWidgetVariableName(Widget) == InVariableName)
		{
			Found = Widget;
		}
	});
	return Found;
}

FString UDreamWidgetTree::SanitizeIdentifier(const FString& InRaw)
{
	FString Result;
	Result.Reserve(InRaw.Len());
	for (TCHAR Char : InRaw)
	{
		Result.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') || Char > 0x7F ? Char : TEXT('_'));
	}
	if (Result.IsEmpty())
	{
		Result = TEXT("Element");
	}
	if (FChar::IsDigit(Result[0]))
	{
		Result.InsertAt(0, TEXT('_'));
	}
	return Result;
}

FName UDreamWidgetTree::MakeWidgetVariableName(const UDreamWidget* InWidget)
{
	return InWidget != nullptr ? FName(*SanitizeIdentifier(InWidget->GetDisplayName())) : NAME_None;
}
