// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamWidgetBlueprint.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIAnchorData.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "DreamGUIEditorModule.h"

#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"

UDreamWidgetBlueprint::UDreamWidgetBlueprint()
{
	ParentClass = UDreamUserWidget::StaticClass();
}

UClass* UDreamWidgetBlueprint::GetBlueprintClass() const
{
	return UDreamWidgetGeneratedClass::StaticClass();
}

UDreamWidgetTree* UDreamWidgetBlueprint::GetOrCreateWidgetTree(bool bEnsureRootWidget)
{
	// Both branches WRITE, and callers run inside a transaction (DreamWidgetTreeEditing does), so the
	// object being written has to be recorded first or undo restores the tree without the pointer
	// that reaches it. Marked only when actually creating something, so the common "it already
	// exists" call still leaves the asset clean.
	if (!IsValid(WidgetTree))
	{
		Modify();
		WidgetTree = NewObject<UDreamWidgetTree>(this, UDreamWidgetTree::StaticClass(), NAME_None, RF_Transactional);
	}
	if (bEnsureRootWidget && !IsValid(WidgetTree->RootWidget))
	{
		WidgetTree->Modify();
		UDreamWidget* Root = WidgetTree->ConstructWidget<UDreamWidget>();
		Root->SetDisplayName(TEXT("Root"));
		WidgetTree->RootWidget = Root;
	}
	return WidgetTree;
}

void UDreamWidgetBlueprint::GetAllSourceWidgets(TArray<UDreamWidget*>& OutWidgets) const
{
	OutWidgets.Reset();
	if (IsValid(WidgetTree))
	{
		WidgetTree->ForEachWidget([&OutWidgets](UDreamWidget* Widget) { OutWidgets.Add(Widget); });
	}
}

bool UDreamWidgetBlueprint::AdoptRootSizeAsDefaultCanvas()
{
	// A resolution somebody picked is theirs, and so is a canvas somebody changed without picking one (an asset from
	// before the picker recorded a resolution keeps only its canvas size).
	// A canvas this has taken from the root before goes on following it, so a card resized in its file is not left on the
	// old size -- which is also the class's design size, and a world widget's.
	const bool bResolutionChosen = DesignerData.DesignViewportSize.X > 0 && DesignerData.DesignViewportSize.Y > 0;
	if (bResolutionChosen || (!DesignerData.bCanvasFollowsRoot && DesignerData.CanvasSize != FDreamWidgetDesignerData().CanvasSize))
	{
		return false;
	}
	if (!IsValid(WidgetTree) || !IsValid(WidgetTree->RootWidget))
	{
		return false;
	}
	const FDreamUIAnchorData& Anchors = WidgetTree->RootWidget->GetAnchorData();
	// A stretched axis has no size of its own: SizeDelta is then a margin, and the root takes whatever its host gives.
	if (Anchors.IsHorizontalStretched() || Anchors.IsVerticalStretched())
	{
		return false;
	}
	// What a root nobody sized comes out as. Indistinguishable from a root sized 100x100 on purpose, and the first is
	// the far commoner of the two.
	if (Anchors.SizeDelta == FDreamUIAnchorData().SizeDelta)
	{
		return false;
	}
	const FIntPoint RootSize(FMath::RoundToInt32(Anchors.SizeDelta.X), FMath::RoundToInt32(Anchors.SizeDelta.Y));
	if (RootSize.X < 1 || RootSize.Y < 1 || (DesignerData.bCanvasFollowsRoot && DesignerData.CanvasSize == RootSize))
	{
		return false;
	}
	// Recorded by a transaction when one is open, and not marked dirty: the size is derived from the file, so
	// there is nothing the author needs to save for the next compile to arrive at the same one.
	Modify(/*bAlwaysMarkDirty*/ false);
	DesignerData.CanvasSize = RootSize;
	DesignerData.bCanvasFollowsRoot = true;
	return true;
}

FIntPoint UDreamWidgetBlueprint::GetDesignerCanvasSize() const
{
	return DesignerData.CanvasSize;
}

bool UDreamWidgetBlueprint::SetDesignerCanvasSize(FIntPoint InSize)
{
	if (InSize.X < 1 || InSize.Y < 1)
	{
		UE_LOG(DreamGUIEditor, Warning,
			TEXT("[%s].%d Refused a designer canvas of %d x %d for '%s'; both sides must be at least 1."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InSize.X, InSize.Y, *GetName());
		return false;
	}

	// An open designer owns a live canvas that has to follow, with its undo step and its scaler rule; it writes the
	// asset as a side effect. Found by name for the same reason every other lookup of this editor is: the interface
	// it returns is anybody's.
	UAssetEditorSubsystem* Editors = GEditor != nullptr ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	IAssetEditorInstance* Instance = Editors != nullptr ? Editors->FindEditorForAsset(this, /*bFocusIfOpen*/false) : nullptr;
	if (Instance != nullptr && Instance->GetEditorName() == TEXT("DreamWidgetBlueprintEditor"))
	{
		FDreamWidgetBlueprintEditor* Designer = static_cast<FDreamWidgetBlueprintEditor*>(Instance);
		Designer->SetDesignerViewportSize(InSize);
		// The editor skips a size its canvas already has, which would leave the resolution unrecorded -- and the next
		// compile free to replace the canvas with the root's -- and the asset's canvas behind the one on screen.
		const FIntPoint Canvas = Designer->GetDesignerCanvasSize();
		if (DesignerData.DesignViewportSize != InSize || DesignerData.CanvasSize != Canvas || DesignerData.bCanvasFollowsRoot)
		{
			Modify();
			DesignerData.DesignViewportSize = InSize;
			DesignerData.CanvasSize = Canvas;
			DesignerData.bCanvasFollowsRoot = false;
		}
		return true;
	}

	Modify();
	DesignerData.CanvasSize = InSize;
	DesignerData.DesignViewportSize = InSize;
	DesignerData.bCanvasFollowsRoot = false;
	return true;
}

#if WITH_EDITOR
void UDreamWidgetBlueprint::GetReparentingRules(TSet<const UClass*>& AllowedChildrenOfClasses, TSet<const UClass*>& DisallowedChildrenOfClasses) const
{
	// A hierarchy class can only ever derive from something that knows how to build one.
	AllowedChildrenOfClasses.Add(UDreamUserWidget::StaticClass());
}
#endif
