// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Text/DreamTextPaint.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Workflow/DreamUILayerStyleSnippet.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FDreamWidgetBlueprintEditor;
class UDreamWidget;

/** The preset library's three shelves. */
enum class EDreamUIPresetKind : uint8
{
	Gradient,
	Style,
	/** Effect settings: an effect's node (`Glass { … }`) or a component's line (`+ DreamMeshModifierWarp { … }`). */
	Effect,
};

/** Where a preset is kept. */
enum class EDreamUIPresetSource : uint8
{
	/** Text in the project settings: UDreamGUISettings::GradientPresets (CSS), StylePresets, EffectPresets (`.dui`). */
	ProjectSettings,
	/** An asset: a UDreamGradientAsset or a UDreamLayerStylePreset. */
	Asset,
};

/** One preset on a shelf of the library. */
struct DREAMGUIEDITOR_API FDreamUIPreset
{
	EDreamUIPresetKind Kind = EDreamUIPresetKind::Style;
	EDreamUIPresetSource Source = EDreamUIPresetSource::ProjectSettings;
	/** The settings key, or the asset's name. */
	FName Name;
	/** ProjectSettings: the stored text. */
	FString Text;
	/** Asset: the asset. */
	FSoftObjectPath Asset;

	/** The gradient a gradient preset holds. */
	bool GetGradient(FDreamGradient& OutGradient) const;
	/**
	 * What a style preset writes: its lines, or an asset's style -- followed (the layer style names the asset as its preset)
	 * when bInFollowAsset, copied otherwise.
	 */
	bool GetStyle(FDreamUILayerStyleSnippet& OutSnippet, bool bInFollowAsset = true) const;
	/** The class an effect preset is for -- a visual or a behaviour -- and whether it is a behaviour's `+` line. */
	bool GetEffectClass(UClass*& OutClass, bool& bOutIsComponent) const;
};

/**
 * The preset library: the project's gradients, layer styles and effect settings, kept as text in the project settings or as
 * assets, applied to the designer's widgets.
 *
 * Applying writes onto the authoring tree in one transaction and through the write-back, as a paste does:
 *   - a gradient: onto the widget's first paint -- a text's face (TextStyle.FacePaint, switched on; an asset is followed as
 *     the paint's Preset) -- else its first gradient property;
 *   - a style: the widget's layer style, added where it has none, and its blending when the preset has it
 *     (DreamUILayerStyleClipboard::Apply);
 *   - effect settings: the values the preset names, onto a widget of that effect or carrying that behaviour; the others
 *     are left as they are.
 */
namespace DreamUIPresetLibrary
{
	/** InKind's presets: the project settings' first, by name, then the assets the asset registry knows, by name. */
	DREAMGUIEDITOR_API void Collect(EDreamUIPresetKind InKind, TArray<FDreamUIPreset>& OutPresets);

	/** Whether InPreset can be applied to InWidget (a template or a preview widget). */
	DREAMGUIEDITOR_API bool CanApply(const UDreamWidget* InWidget, const FDreamUIPreset& InPreset);

	/** InPreset onto the templates of InPreviewWidgets, in one transaction. How many took it; 0 with the reason when none did. */
	DREAMGUIEDITOR_API int32 Apply(FDreamWidgetBlueprintEditor& InDesigner, TConstArrayView<UDreamWidget*> InPreviewWidgets,
		const FDreamUIPreset& InPreset, bool bInFollowAsset = true, FText* OutError = nullptr);

	/** InWidget's gradient, style or effect settings as the text a preset of InKind keeps. False when it has none. */
	DREAMGUIEDITOR_API bool DescribeWidget(const UDreamWidget* InWidget, EDreamUIPresetKind InKind, FString& OutText);

	/** A text preset in the project settings, written to DefaultDreamGUI.ini; one of that name is replaced. */
	DREAMGUIEDITOR_API bool SaveTextPreset(EDreamUIPresetKind InKind, FName InName, const FString& InText, FText* OutError = nullptr);
	DREAMGUIEDITOR_API bool RemoveTextPreset(EDreamUIPresetKind InKind, FName InName);
	/**
	 * A gradient or style preset kept the other way: a text preset as a new asset (a UDreamGradientAsset or a
	 * UDreamLayerStylePreset, in InPackagePath, asked for when empty), an asset as a text preset named InName.
	 */
	DREAMGUIEDITOR_API UObject* SaveAsAsset(const FDreamUIPreset& InPreset, const FString& InPackagePath, FText* OutError = nullptr);
	DREAMGUIEDITOR_API bool SaveAsText(const FDreamUIPreset& InPreset, FName InName, FText* OutError = nullptr);

	/** A text preset or a preset asset made, changed or removed. */
	DREAMGUIEDITOR_API FSimpleMulticastDelegate& OnChanged();
}

/** A preset being dragged out of the library: onto a widget in the viewport or the hierarchy, or onto a property row. */
class DREAMGUIEDITOR_API FDreamUIPresetDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDreamUIPresetDragDropOp, FDecoratedDragDropOp)

	static TSharedRef<FDreamUIPresetDragDropOp> New(const FDreamUIPreset& InPreset);

	const FDreamUIPreset& GetPreset() const { return Preset; }
	EDreamUIPresetKind GetKind() const { return Preset.Kind; }
	bool GetGradient(FDreamGradient& OutGradient) const { return Preset.GetGradient(OutGradient); }
	bool GetStyle(FDreamUILayerStyleSnippet& OutSnippet) const { return Preset.GetStyle(OutSnippet); }

	/**
	 * What the viewport and the hierarchy do with it over a widget: whether it can go there, and, dropped, the preset
	 * applied to that widget -- or to the whole selection when the widget is one of it.
	 */
	static bool CanDropOnto(const FDragDropEvent& InEvent, const UDreamWidget* InPreviewWidget);
	static bool DropOnto(const FDragDropEvent& InEvent, FDreamWidgetBlueprintEditor& InDesigner, UDreamWidget* InPreviewWidget);

private:
	FDreamUIPreset Preset;
};

DECLARE_DELEGATE_OneParam(FOnDreamUIPresetDropped, const FDreamUIPresetDragDropOp& /*Operation*/);

/** Wrap a property row's value so a preset of Kind dropped onto it is handed to OnDropped, which writes it (in a transaction). */
class DREAMGUIEDITOR_API SDreamUIPresetDropTarget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamUIPresetDropTarget) : _Kind(EDreamUIPresetKind::Gradient) {}
		SLATE_ARGUMENT(EDreamUIPresetKind, Kind)
		SLATE_EVENT(FOnDreamUIPresetDropped, OnDropped)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

private:
	bool Accepts(const FDragDropEvent& InEvent) const;

	EDreamUIPresetKind Kind = EDreamUIPresetKind::Gradient;
	FOnDreamUIPresetDropped OnDroppedDelegate;
	bool bDragOver = false;
};
