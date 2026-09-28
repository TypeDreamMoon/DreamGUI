// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Core/DreamWidgetTreeHost.h"
#include "DreamWidgetPresenterComponentBase.generated.h"

class UDreamWidget;
class UDreamWidgetTree;
class UUINavigationInputSelectionHandler;
class UDreamCanvas;

// Abstract: LoadWidget is PURE_VIRTUAL, so an instance of this class asserts the moment it
// registers. Without this the Add Component list offers it, and picking it there is a crash
// rather than an error. Add DreamWorldWidgetComponent.
UCLASS(Abstract, ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamWidgetPresenterComponentBase : public USceneComponent, public IDreamWidgetTreeHost
{
	GENERATED_BODY()

public:
	UDreamWidgetPresenterComponentBase();
	friend class FDreamWidgetPresenterBaseCustomization;

	// IDreamWidgetTreeHost: the tree is outered to this component and held by OwnedTree.
	virtual UObject* GetTreeOuter() const override { return const_cast<UDreamWidgetPresenterComponentBase*>(this); }
	virtual void ReleaseTree(EDreamTreeReleaseReason InReason) override;
	virtual void RebuildTree() override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void BeginDestroy() override;
	virtual void LoadWidget()PURE_VIRTUAL(UDreamWidgetPresenterComponentBase::LoadWidget, );
	/**
	 * Is the tree already loaded the one this host would build right now?
	 *
	 * Asked on an editor-mode register, because a reregister is not a teardown: editing any property
	 * of the owning actor runs one, and rebuilding the hierarchy there throws away everything a live
	 * tree was holding. A host that cannot compare answers false -- rebuild -- which is the only
	 * honest answer for one that has no notion of what it would build.
	 */
	virtual bool IsLoadedWidgetCurrent() const { return false; }
	/** Tear the loaded tree down and forget it. Safe to call twice, and on a tree GC already took. */
	void DestroyLoadedWidget();
#if WITH_EDITOR
public:
	void ReloadWidget();
	virtual void PostEditUndo() override;
#endif

protected:
	/**
	 * The tree this component hosts: outered to it, held here and nowhere else, and let go by
	 * DestroyLoadedWidget. Never saved, never duplicated into a play session, never exported by Copy --
	 * the class it is built from is what the level keeps.
	 *
	 * NonTransactional, with the rest of what the component knows of its tree. A transaction records
	 * transient properties too, and an undo or redo that brought this component back, or took it away,
	 * wrote back the tree it held when the edit was recorded: one already destroyed, or none while a live
	 * one stood -- which the component then never let go. Which tree it holds is the component's own
	 * business, never undo's.
	 */
	UPROPERTY(Transient, NonTransactional, DuplicateTransient, TextExportTransient)
	TObjectPtr<UDreamWidgetTree> OwnedTree;
	UPROPERTY(Transient, NonTransactional)
	TWeakObjectPtr<UDreamCanvas> RootCanvas;
	UPROPERTY(Transient, NonTransactional)
	TWeakObjectPtr<UDreamWidget> LoadedWidget;
	/**
	 * For navigation input, show a selection widget
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=DreamWidgetPresenter)
	TSubclassOf<class UDreamUserWidget> NavigationSelectionClass;

	UPROPERTY(VisibleAnywhere, Transient, NonTransactional, BlueprintReadOnly, Category=DreamWidgetPresenter, AdvancedDisplay)
	TWeakObjectPtr<UUINavigationInputSelectionHandler> NavigationSelection;

	/**
	 * Presenter-level animation forwards. These exist so a Level Sequence possessing this component
	 * can fade, slide, and show/hide the whole widget tree without binding any widget: each setter
	 * writes through to the root widget, and a fresh load replays the stored values. Opacity rides
	 * RenderOpacity (children inherit it multiplicatively) and the offset rides the render-only
	 * translation, so neither touches layout.
	 */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadOnly, Category=DreamWidgetPresenter, Getter, Setter, meta = (UIMin = "0", UIMax = "1"))
	float WidgetOpacity = 1.0f;
	UPROPERTY(Interp, EditAnywhere, BlueprintReadOnly, Category=DreamWidgetPresenter, Getter, Setter)
	FVector WidgetOffset = FVector::ZeroVector;
	UPROPERTY(Interp, EditAnywhere, BlueprintReadOnly, Category=DreamWidgetPresenter, Getter = "GetWidgetVisible", Setter = "SetWidgetVisible")
	bool bWidgetVisible = true;

protected:
	/** Replays the presenter-level forwards onto a freshly loaded widget tree; default values are left alone. */
	void ApplyWidgetOverridesToLoadedWidget();
	/**
	 * Tells every level-sequence player in the world to re-resolve its bindings. A custom widget
	 * binding that evaluated before this tree existed stays failed otherwise: the ECS re-runs
	 * resolution only on explicit invalidation.
	 */
	void NotifyWidgetLoaded();

public:
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	UUINavigationInputSelectionHandler* GetNavigationSelection();
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	UDreamCanvas* GetLoadedCanvas()const{return RootCanvas.Get();}
	UFUNCTION(BlueprintCallable, Category=DreamGUI)
	UDreamWidget* GetLoadedWidget()const{return LoadedWidget.Get();}

	float GetWidgetOpacity()const{return WidgetOpacity;}
	void SetWidgetOpacity(float Value);
	FVector GetWidgetOffset()const{return WidgetOffset;}
	void SetWidgetOffset(const FVector& Value);
	bool GetWidgetVisible()const{return bWidgetVisible;}
	void SetWidgetVisible(bool Value);
};
