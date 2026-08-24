// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class ISequencer;
class UDreamUISequence;

/**
 * The standalone editor a DreamUI Animation asset opens into: one tab hosting a full Sequencer.
 * The playback context is the editor world, so a level that contains the matching presenter
 * resolves the bindings live and the viewport previews the animation while scrubbing; without
 * one, tracks and keys still edit normally against unresolved bindings.
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

private:
	TSharedRef<SDockTab> SpawnTab_Sequencer(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);
	/** (Re)instantiates PreviewPrefab in the editor world so the bindings resolve while editing. */
	void RebuildPreviewTree();
	void DestroyPreviewTree();
	void OnObjectPropertyChanged(UObject* InObject, struct FPropertyChangedEvent& InEvent);

	static const FName SequencerMainTabId;
	static const FName DetailsTabId;

	UDreamUISequence* Sequence = nullptr;
	TSharedPtr<ISequencer> Sequencer;
	TSharedPtr<class IDetailsView> DetailsView;
	TWeakObjectPtr<class UDreamWidget> PreviewRoot;
	FDelegateHandle PropertyChangedHandle;
};
