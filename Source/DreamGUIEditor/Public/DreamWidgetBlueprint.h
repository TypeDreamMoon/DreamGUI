// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "Engine/EngineTypes.h"
// EViewModeIndex, for the saved viewport state below. EngineTypes.h does not carry it, and inside a
// unity blob some neighbour always had. -SingleFile on any file that reaches this header said so.
#include "Engine/EngineBaseTypes.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "DreamWidgetBlueprint.generated.h"

class UDreamWidgetTree;
class UDreamWidget;

/**
 * What the designer remembers about one authored hierarchy, as opposed to what the hierarchy IS.
 *
 * The prefab kept the same set in FDreamUIPrefabDataForPrefabEditor, keyed by the GUIDs its helper
 * object handed out. There are no GUIDs here: a widget is identified by its object FName, which is
 * what the class archetype and every instance preserve, and which is therefore already the key the
 * template-to-preview correspondence runs on. Two keys for the same thing is how they drift.
 *
 * Grid snapping and the overlays are deliberately NOT here -- those are per-author preferences and
 * live in UDreamUIDesignerSettings (EditorPerProjectUserSettings). What stays on the asset is what
 * describes the hierarchy itself: how big its canvas is, and which of its widgets are put away.
 */
USTRUCT()
struct FDreamWidgetDesignerData
{
	GENERATED_BODY()

	UPROPERTY()
	FVector ViewLocation = FVector::ZeroVector;
	UPROPERTY()
	FRotator ViewRotation = FRotator::ZeroRotator;
	UPROPERTY()
	FVector ViewOrbitLocation = FVector::ZeroVector;

	/** The design canvas the hierarchy is authored against. */
	UPROPERTY()
	FIntPoint CanvasSize = FIntPoint(1920, 1080);
	/**
	 * Device resolution the designer picked. The hierarchy's own canvas-scaler rule turns it into
	 * CanvasSize, which is why the two differ for anything but ConstantPixelSize. Zero means nobody
	 * has picked one, in which case CanvasSize is the resolution.
	 */
	UPROPERTY()
	FIntPoint DesignViewportSize = FIntPoint::ZeroValue;
	/** Preview render mode (an EDreamRenderMode). ScreenSpaceOverlay by default, as in the designer. */
	UPROPERTY()
	uint8 CanvasRenderMode = 0;

	UPROPERTY()
	TEnumAsByte<EViewModeIndex> ViewMode = EViewModeIndex::VMI_Lit;
	/** ELevelViewportType; 2 is LVT_OrthoYZ, the plane DreamUI lays out in. */
	UPROPERTY()
	uint8 ViewportType = 2;

	/** Hierarchy rows the author has collapsed. Keyed by template widget object FName. */
	UPROPERTY()
	TSet<FName> UnexpandedWidgets;
	/** Widgets hidden in the designer only; runtime WidgetActive is untouched. */
	UPROPERTY()
	TSet<FName> HiddenWidgets;
	/** Widgets protected from selection and manipulation in the designer. */
	UPROPERTY()
	TSet<FName> LockedWidgets;
};

/**
 * The authoring asset for a DreamUI hierarchy that is a class -- UMG's UWidgetBlueprint, for DreamUI.
 *
 * It holds the hierarchy a designer edits; compiling duplicates that onto the generated class as the
 * archetype instances are built from. The two copies are deliberate and match UMG: editing the
 * archetype in place would mutate every live instance's template mid-session.
 *
 * Editor-module only, like UWidgetBlueprint -- a cooked build has the generated class and needs
 * nothing else. The designer surface for it is out of scope here: a stock Blueprint editor opens
 * this asset and can already author graphs and variables against it.
 */
UCLASS(BlueprintType, DisplayName = "DreamUI Widget Blueprint")
class DREAMGUIEDITOR_API UDreamWidgetBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
	UDreamWidgetBlueprint();

	/** The hierarchy being authored. Never handed to instances directly; the compiler duplicates it. */
	UPROPERTY()
	TObjectPtr<UDreamWidgetTree> WidgetTree = nullptr;

	/** Designer session state for this asset. Not part of the hierarchy; see the struct. */
	UPROPERTY()
	FDreamWidgetDesignerData DesignerData;

	/**
	 * Property bindings as authored. The compiler resolves each into the class's copy, and reports
	 * the ones it cannot -- so a binding that stops making sense (the widget renamed, the function
	 * deleted, the setter's type changed) surfaces on the next compile rather than at runtime.
	 */
	UPROPERTY()
	TArray<FDreamWidgetPropertyBinding> PropertyBindings;

	/** Create the tree (and its root) if this asset has none yet, so a fresh asset is editable. */
	UDreamWidgetTree* GetOrCreateWidgetTree();

	/** Every widget in the authored hierarchy, root first. Empty when nothing has been authored. */
	void GetAllSourceWidgets(TArray<UDreamWidget*>& OutWidgets) const;

	virtual UClass* GetBlueprintClass() const override;
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
	virtual bool AlwaysCompileOnLoad() const override { return true; }
#if WITH_EDITOR
	virtual void GetReparentingRules(TSet<const UClass*>& AllowedChildrenOfClasses, TSet<const UClass*>& DisallowedChildrenOfClasses) const override;
#endif
};
