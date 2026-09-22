// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "Engine/EngineTypes.h"
#include "DreamWorldWidgetComponent.generated.h"

class UDreamUserWidget;

/** Which renderer draws a world-space hierarchy. Maps to the root canvas's render mode. */
UENUM(BlueprintType)
enum class EDreamWorldWidgetBackend : uint8
{
	/**
	 * DreamUI's own renderer: the hierarchy is drawn by the view extension, after the scene, with
	 * DreamUI's sorting. Sharper text and exact draw order, no scene lighting or post processing.
	 */
	DreamUIRenderer,
	/**
	 * UE's renderer: the meshes are ordinary primitives in the scene, so they light, cast, fog and
	 * occlude like anything else, and sort by translucency rules rather than by DreamUI's.
	 */
	UERenderer,
};

/**
 * Hosts a DreamUI hierarchy in the world: the component that puts a panel, a health bar or a sign
 * where an actor is.
 *
 * It is the one host for world-space UI, and it owns every decision a world-space root has to make,
 * because none of them can come from the hierarchy itself: how big it is in centimetres (a screen
 * stretches to a viewport, a world panel has no viewport to stretch to), which renderer draws it,
 * what it sorts against, and which trace channel a pointer must use to reach it. The hierarchy is
 * instanced from WidgetClass unchanged -- its own root canvas is kept, so whatever the designer set
 * there is what the level gets, and a hierarchy authored with no canvas is given one.
 *
 * The properties are live: changing the size, the pivot, the backend or the sort order re-applies to
 * the tree that is already loaded. Only a change of class rebuilds it.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "DreamUI World Widget")
class DREAMGUI_API UDreamWorldWidgetComponent : public UDreamWidgetPresenterComponentBase
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void LoadWidget() override;
	virtual bool IsLoadedWidgetCurrent() const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** The hierarchy class to present. Changing it is the one edit that rebuilds the tree. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Widget")
	TSubclassOf<UDreamUserWidget> WidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Widget")
	EDreamWorldWidgetBackend Backend = EDreamWorldWidgetBackend::DreamUIRenderer;

	/**
	 * Take the size from the class instead of from DrawSize. On by default: the canvas the hierarchy
	 * was authored against is the only size anyone has actually chosen for it, and a host that lands
	 * at that size is a host that looks in the level like it looked in the designer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Widget")
	bool bUseDesignSize = true;

	/** The root's size in world units, when it is not taken from the class. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Widget", meta = (EditCondition = "!bUseDesignSize"))
	FVector2D DrawSize = FVector2D(1920.0, 1080.0);

	/** Where on the root this component sits: (0.5, 0.5) centres the panel on the actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Widget")
	FVector2D Pivot = FVector2D(0.5, 0.5);

	/** Draw order against the other canvases in the world. Written through to the root canvas. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Widget")
	int32 SortOrder = 0;

	/**
	 * The channel a world-space raycaster must be set to for its rays to reach this hierarchy, and
	 * the channel its occlusion trace uses. Visibility by default, which is what a raycaster created
	 * for a player defaults to, so the two meet without anyone configuring either.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Widget")
	TEnumAsByte<ETraceTypeQuery> TraceChannel = TraceTypeQuery1;

public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	void SetWidgetClass(TSubclassOf<UDreamUserWidget> InClass);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	TSubclassOf<UDreamUserWidget> GetWidgetClass() const { return WidgetClass; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	void SetBackend(EDreamWorldWidgetBackend InBackend);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	EDreamWorldWidgetBackend GetBackend() const { return Backend; }

	/** Sets an explicit size, which means turning the design-size default off. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	void SetDrawSize(FVector2D InSize);
	/** The size the root is actually given: the class's design size while bUseDesignSize is on. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	FVector2D GetDrawSize() const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	void SetUseDesignSize(bool bInUse);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	bool GetUseDesignSize() const { return bUseDesignSize; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	void SetPivot(FVector2D InPivot);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	FVector2D GetPivot() const { return Pivot; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	void SetSortOrder(int32 InSortOrder);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	int32 GetSortOrder() const { return SortOrder; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	void SetTraceChannel(TEnumAsByte<ETraceTypeQuery> InChannel);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	TEnumAsByte<ETraceTypeQuery> GetTraceChannel() const { return TraceChannel; }

private:
	/** Push the canvas-level settings onto the loaded root canvas. */
	void ApplyCanvasSettings();
	/** Push the geometry -- anchors, pivot, size -- onto the loaded root widget. */
	void ApplyGeometryToLoadedWidget();
	/** Re-read DrawSize from the class, for when either the class or bUseDesignSize changed. */
	void RefreshDrawSizeFromClass();
};
