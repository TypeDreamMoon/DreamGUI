// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamLayerStyleTypes.h"
#include "UObject/WeakObjectPtr.h"
#include "Workflow/DreamUIStylePresets.h"
#include "DreamUILayerStyleDialog.generated.h"

class FDreamWidgetBlueprintEditor;
class SDreamUILayerStyleDialog;
class SWindow;
class UDreamLayerStyle;
class UDreamWidget;

/**
 * The layer's own blending, on the style dialog's Blending Options page: its element's blend mode and its opacity. The page's
 * other values -- the fill opacity, the scale of every size, the global light -- are the style's own.
 */
USTRUCT()
struct DREAMGUIEDITOR_API FDreamUIStyleDialogBlending
{
	GENERATED_BODY()

	/** The element's blend mode (its visual's BlendMode). */
	UPROPERTY(EditAnywhere, Category = "General Blending")
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;
	/** The widget's opacity, its effects included (RenderOpacity). */
	UPROPERTY(EditAnywhere, Category = "General Blending", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 1.0f;
};

/**
 * What the style dialog edits, as the details view sees it: the working style and the layer's blending. The parameters are
 * shown as fields of this -- an effect's angle is Style.DropShadows[1].Angle, as it is a layer style's in a widget's details
 * -- so an editor that finds its way about the style from a field (a light angle that turns the style's global light) works
 * here as it does there.
 */
USTRUCT()
struct DREAMGUIEDITOR_API FDreamUIStyleDialogModel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Layer Style")
	FDreamLayerEffects Style;
	UPROPERTY(EditAnywhere, Category = "Layer Style")
	FDreamUIStyleDialogBlending Blending;
};

/** One row of the style dialog's list: Blending Options, an effect, or one sub-page of the bevel. */
UENUM()
enum class EDreamUIStyleDialogPage : uint8
{
	Blending,
	BevelEmboss,
	BevelContour,
	BevelTexture,
	Stroke,
	InnerShadow,
	InnerGlow,
	Satin,
	ColorOverlay,
	GradientOverlay,
	PatternOverlay,
	OuterGlow,
	DropShadow,
};

/** A row: its page, and for an effect there can be several of, which one (INDEX_NONE: the kind has none yet). */
struct FDreamUIStyleDialogRow
{
	EDreamUIStyleDialogPage Page = EDreamUIStyleDialogPage::Blending;
	int32 Index = INDEX_NONE;

	bool operator==(const FDreamUIStyleDialogRow& Other) const { return Page == Other.Page && Index == Other.Index; }
};

/**
 * What one opening of the style dialog edits: a working copy of a layer style and of the layer's blending, previewed live
 * on the designer's widgets and written to them -- or not -- when the dialog closes.
 *
 * THE PREVIEW IS THE PREVIEW'S. Every change is put on the preview widgets the designer draws (their layer style, added
 * when they have none, their element's blend mode, their opacity), never on the authoring tree: the asset and its `.dui`
 * are untouched until Commit, and Cancel puts the preview objects back as they were found -- the file is byte for byte what
 * it was. Commit puts the preview back, then writes the working copy onto the widgets' authoring templates through
 * DreamUILayerStyleClipboard::Apply: one transaction, written into the `.dui`, one undo.
 *
 * SEVERAL WIDGETS. The working copy starts from the first widget's style and blending; the preview and the write go to all
 * of them. A Commit with nothing changed writes nothing. The blending is written only once it was changed, so a style
 * given to several widgets leaves each its own blend mode unless the Blending Options page was edited. A style that followed
 * a preset asset is, once edited, a style of the widget's own.
 *
 * A rebuild of the designer's preview (a compile, an undo) replaces the preview widgets: the session finds the new ones by
 * the widgets' names and previews on them again.
 */
class DREAMGUIEDITOR_API FDreamUILayerStyleSession : public TSharedFromThis<FDreamUILayerStyleSession>
{
public:
	/** A session on InPreviewWidgets of InDesigner's preview. Null when none of them has a template to write to. */
	static TSharedPtr<FDreamUILayerStyleSession> Begin(FDreamWidgetBlueprintEditor& InDesigner, TConstArrayView<UDreamWidget*> InPreviewWidgets);
	/**
	 * The session, not yet finished, that edits InPreviewWidget's layer style, if one does. While one does, the layer style the
	 * widget shows is the session's working copy: what else edits it there -- a viewport handle on a shadow or on the global
	 * light -- edits the working copy (TryStyle, EndTry) and leaves the writing to the session.
	 */
	static TSharedPtr<FDreamUILayerStyleSession> FindEditing(const UDreamWidget* InPreviewWidget);
	/** Whether InPreviewWidget is one of the widgets this session previews on. */
	bool IsEditing(const UDreamWidget* InPreviewWidget) const;
	~FDreamUILayerStyleSession();

	/** The rows, in an image editor's order: Blending Options, Bevel and Emboss (Contour, Texture), Stroke, ... Drop Shadow. */
	TArray<FDreamUIStyleDialogRow> GetRows() const;
	FText GetRowLabel(const FDreamUIStyleDialogRow& InRow) const;
	/** Whether the row has a checkbox (every row but Blending Options) and whether it is ticked. */
	bool HasCheckbox(const FDreamUIStyleDialogRow& InRow) const;
	bool IsChecked(const FDreamUIStyleDialogRow& InRow) const;
	/** Tick or untick: an effect's bEnabled, the bevel's contour or texture; ticking a kind with none adds its first. */
	void SetChecked(const FDreamUIStyleDialogRow& InRow, bool bInChecked);
	/** Whether the row's kind can be there more than once (drop shadows, inner shadows, colour and gradient overlays, strokes). */
	static bool IsMultiInstance(EDreamUIStyleDialogPage InPage);

	/** Another instance of InRow's kind, after it: a copy of it, or the kind's defaults. The new instance's row. */
	FDreamUIStyleDialogRow AddInstance(const FDreamUIStyleDialogRow& InRow);
	/** Take InRow's instance out. */
	bool RemoveInstance(const FDreamUIStyleDialogRow& InRow);
	/** Move an instance to another place among its kind (the order they draw in, a later one over an earlier one). */
	bool MoveInstance(EDreamUIStyleDialogPage InPage, int32 InFrom, int32 InTo);

	/** What the row's parameters are: the struct and its memory in the working copy. Null for a kind with none. */
	void* GetPageMemory(const FDreamUIStyleDialogRow& InRow, const UScriptStruct*& OutStruct);
	/** Where the row's parameters are in the model: the field of the working style ("DropShadows"); none for Blending Options. */
	static FName GetPageMember(EDreamUIStyleDialogPage InPage);
	/**
	 * The fields of the page's struct it shows, in order: every one but bEnabled, which is the row's checkbox; the bevel's
	 * page without its contour's and texture's, which are pages of their own. Empty for Blending Options, made of two structs.
	 */
	static TArray<FName> GetPageFields(EDreamUIStyleDialogPage InPage);

	/**
	 * The working copy changed -- InProperty (the field edited, however deep) of InPage's parameters: preview it. On the
	 * Blending Options page, BlendMode and Opacity (or a change not named) are the layer's blending, written from then on.
	 */
	void NotifyChanged(EDreamUIStyleDialogPage InPage, FName InProperty = NAME_None);
	/**
	 * The working copy holds a value being tried -- a drag in progress, a blend mode hovered in a list: preview it, without
	 * counting it as a change. A drag ends, and a pick is made, with NotifyChanged; a hover puts the value back and calls this
	 * again. The blending the widgets show is the working copy's only while it differs from where it started, or was changed.
	 */
	void PreviewChanged(EDreamUIStyleDialogPage InPage, FName InProperty = NAME_None);

	/**
	 * A style tried from outside the dialog -- a viewport handle being dragged: the working copy becomes InStyle and is
	 * previewed, not yet counted as a change. EndTry ends the trying: kept, it is a change OK will write; not kept, the
	 * working copy is what it was before the first try. Nothing happens on an EndTry with no try before it.
	 */
	void TryStyle(const FDreamLayerEffects& InStyle);
	void EndTry(bool bInKeep);

	/** Preview on the widgets, or show them as they are. On by default. */
	void SetPreviewEnabled(bool bInEnabled);
	bool IsPreviewEnabled() const { return bPreview; }

	/** The style and blending being edited. */
	const FDreamLayerEffects& GetStyle() const { return Model.Style; }
	FDreamLayerEffects& GetMutableStyle() { return Model.Style; }
	const FDreamUIStyleDialogBlending& GetBlending() const { return Model.Blending; }
	FDreamUIStyleDialogBlending& GetMutableBlending() { return Model.Blending; }
	/** Both, as the dialog's details view shows them. */
	FDreamUIStyleDialogModel& GetModel() { return Model; }
	bool IsDirty() const { return bDirty; }

	/** The widgets the dialog writes to, by name. */
	TArray<FString> GetTargetNames() const { return TargetNames; }
	FDreamWidgetBlueprintEditor* GetDesigner() const;

	/** Put the preview back and write the working copy onto the widgets: one undo step. How many widgets took it; 0 when nothing had changed. */
	int32 Commit();
	/** Put the preview back as it was found. Nothing was written. */
	void Cancel();
	bool IsFinished() const { return bFinished; }

	/** What New Style saves: the working style, and the blending. */
	FDreamUIStylePresetSaveArgs MakeSaveArgs() const;

private:
	FDreamUILayerStyleSession() = default;

	struct FPreviewTarget
	{
		TWeakObjectPtr<UDreamWidget> Widget;
		/** The layer style the preview is shown on: the widget's own, or the one added for it (bAdded). */
		TWeakObjectPtr<UDreamLayerStyle> Style;
		bool bAdded = false;
		FDreamLayerEffects OriginalStyle;
		TWeakObjectPtr<class UDreamLayerStylePreset> OriginalPreset;
		bool bHadBlendMode = false;
		EDreamUIBlendMode OriginalBlendMode = EDreamUIBlendMode::Alpha;
		float OriginalOpacity = 1.0f;
	};

	void FindPreviews();
	void ApplyPreview();
	void RestorePreview();
	void OnPreviewRebuilt();

	TWeakPtr<FDreamWidgetBlueprintEditor> Designer;
	TArray<FString> TargetNames;
	TArray<FPreviewTarget> Previews;
	FDreamUIStyleDialogModel Model;
	/** The blending the dialog opened with (the first widget's). */
	FDreamUIStyleDialogBlending InitialBlending;
	/** The working style before the first of a run of tries (TryStyle), to put back when they are not kept. */
	TOptional<FDreamLayerEffects> StyleBeforeTry;
	bool bPreview = true;
	bool bDirty = false;
	bool bBlendingChanged = false;
	bool bFinished = false;
	FDelegateHandle PreviewRebuiltHandle;
};

/**
 * An image editor's Layer Style dialog, for the designer's widgets: a non-modal window with the effects listed on the left
 * -- ticked, added to, reordered -- the selected one's parameters beside them, a live preview on the widgets, and OK,
 * Cancel and New Style.
 */
namespace DreamUILayerStyleDialog
{
	/**
	 * Open the dialog on InPreviewWidgets (the designer's selection, or one widget). A dialog already open for this
	 * designer is cancelled first. The window, or null when none of the widgets can be written to.
	 */
	DREAMGUIEDITOR_API TSharedPtr<SWindow> Open(FDreamWidgetBlueprintEditor& InDesigner, TConstArrayView<UDreamWidget*> InPreviewWidgets);
	/**
	 * Open the dialog from one of a designer's preview widgets -- its row in the details panel, its mark in the hierarchy:
	 * on the designer's whole selection when the widget is one of it, on the widget alone when it is not. Null when the
	 * widget is no designer's.
	 */
	DREAMGUIEDITOR_API TSharedPtr<SWindow> OpenFrom(UDreamWidget* InPreviewWidget);
	/** The dialog open for InDesigner, if one is. */
	DREAMGUIEDITOR_API TSharedPtr<SDreamUILayerStyleDialog> Find(const FDreamWidgetBlueprintEditor& InDesigner);
}
