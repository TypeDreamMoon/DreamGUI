// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/StrongObjectPtr.h"

class ISequencer;
class UDreamUISequence;
class UDreamWidget;
class FDreamWidgetDesignerScene;
class SDreamUISequencePreviewViewport;

/**
 * The standalone editor a DreamUI Animation asset opens into: a preview viewport over a private
 * preview world holding the asset's PreviewWidgetClass, a full Sequencer, and a details panel. The
 * bindings resolve against the preview tree, so scrubbing previews live in the viewport; clicking
 * a widget selects its binding in the Sequencer and dragging the gizmo auto-keys the animation
 * (render translation when the parent's layout owns the position, authored location otherwise).
 */
class FDreamUISequenceEditorToolkit
	: public FAssetEditorToolkit
{
public:
	~FDreamUISequenceEditorToolkit();

	void Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UDreamUISequence* InSequence);

	//~ FAssetEditorToolkit interface
	virtual FText GetBaseToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.16f, 0.38f, 1.0f, 0.5f); }
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

	//~ Preview viewport interface
	UWorld* GetPreviewWorld() const;
	UDreamWidget* GetPreviewRootWidget() const { return PreviewRoot.Get(); }
	const TArray<TWeakObjectPtr<UDreamWidget>>& GetViewportSelection() const { return SelectedPreviewWidgets; }
	/** A viewport click: select the widget (null clears), and mirror it into the Sequencer. */
	void SelectWidgetFromViewport(UDreamWidget* InWidget, bool bAppend);
	/** Auto-key the transform channels a finished gizmo drag touched, creating bindings as needed. */
	void KeyTransformProperties(const TArray<UDreamWidget*>& InWidgets, bool bRenderTranslation, bool bRelativeLocation, bool bRotation, bool bScale);

private:
	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Sequencer(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);
	/** (Re)instantiates PreviewWidgetClass into the private preview world so the bindings resolve. */
	void RebuildPreviewTree();
	void DestroyPreviewTree();
	void OnObjectPropertyChanged(UObject* InObject, struct FPropertyChangedEvent& InEvent);
	/** Bindings created outside the parenting-aware paths float free; adopt them under the root. */
	void HealStrayBindings();
	void HandleSequencerSelectionChanged(TArray<FGuid> InObjectGuids);
	/**
	 * Step the sequencer's entity runtime out of the window in which its bound objects die.
	 *
	 * Sequencer groups animation entities under raw bound-object pointers and keeps two maps of those
	 * keys in lockstep; nothing repairs them when the objects behind the keys are destroyed, and once
	 * the two disagree the next empty-group free fails an ensure deep in engine code. Rebuilding the
	 * preview tree destroys every widget this sequencer has bound, so the tree may only be pulled out
	 * from under it while it holds no object-keyed entities at all.
	 *
	 * The asset's bindings resolve by PATH from the sequence's preview root, so parking that root on a
	 * childless sentinel and forcing one evaluation makes the engine unlink every entity and free
	 * every group through its own context-switch path, while everything is still coherent -- and for
	 * the rest of the window every path resolves to nothing, so nothing new is keyed. The embedded
	 * animation editor does the same across its own two windows (SDreamWidgetAnimationEditorWidget).
	 */
	void EvacuateSequencerEntities();
	/** The other half of EvacuateSequencerEntities: re-resolve against whatever tree exists by now. */
	void ResumeSequencerEvaluation();
	/** Notify once about every widget binding whose path the live preview tree cannot walk. */
	void ReportUnresolvableBindings();
	/** The binding whose stored widget path names InWidget from the preview root, if any. */
	FGuid FindBindingForWidget(const UDreamWidget* InWidget) const;

	static const FName ViewportTabId;
	static const FName SequencerMainTabId;
	static const FName DetailsTabId;

	/** Declared first so it is destroyed last: the viewport client and the preview tree live in it. */
	TUniquePtr<FDreamWidgetDesignerScene> PreviewScene;

	UDreamUISequence* Sequence = nullptr;
	TSharedPtr<ISequencer> Sequencer;
	TSharedPtr<class IDetailsView> DetailsView;
	TSharedPtr<SDreamUISequencePreviewViewport> PreviewViewport;
	TWeakObjectPtr<UDreamWidget> PreviewRoot;
	TArray<TWeakObjectPtr<UDreamWidget>> SelectedPreviewWidgets;
	/** Non-zero while this toolkit itself is pushing a selection, so the echo is not re-applied. */
	int32 SelectionSyncGuard = 0;
	FDelegateHandle PropertyChangedHandle;
	/** True between EvacuateSequencerEntities and ResumeSequencerEvaluation. */
	bool bPlaybackContextSuppressed = false;
	/** The childless widget the preview root is parked on while evacuated. Strongly held: the window
	 *  it spans can run a garbage collection, and it must still be there to resume from. Typed as
	 *  UObject so this header keeps its forward declaration of UDreamWidget. */
	TStrongObjectPtr<UObject> SuppressionContext;
};
