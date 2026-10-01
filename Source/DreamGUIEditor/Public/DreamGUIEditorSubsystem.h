// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Containers/Ticker.h"
#include "Text/DreamUISourceWatcher.h"
#include "DreamGUIEditorSubsystem.generated.h"

class AActor;
class UActorComponent;
class UBlueprint;
class UDreamScreenUISubsystem;
class UDreamUIFontData_BaseObject;
class UDreamUIManagerWorldSubsystem;
class UPackage;
class UTexture2D;

/**
 * A tree the editor builds outside every world host -- the designer's preview, the UI sequence editor's --
 * as a recompile sees it: let the tree go before the reinstancer can copy it, and build it again once the
 * compile is over. The preview enrols itself with UDreamGUIEditorSubsystem for as long as it exists.
 */
class DREAMGUIEDITOR_API IDreamRecompilePreview
{
public:
	virtual ~IDreamRecompilePreview() = default;

	/** Whether the preview's tree holds an instance of InClass, or of a class derived from it. */
	virtual bool UsesClass(const UClass* InClass) const = 0;
	/** Tear the tree down now: the class it uses is about to be recompiled. */
	virtual void ReleaseForRecompile() = 0;
	/** Build it again, from the classes as they are now that the compile is over. */
	virtual void RebuildAfterRecompile() = 0;
};

/**
 * The editor's side of DreamGUI, for the length of an editor session: what a recompile of a widget class
 * does to the trees built from it, what a texture reimport refreshes, the source watcher's queue, and the
 * few things the details panels remember between one panel and the next.
 *
 * RECOMPILE, the way UMG does it. A Blueprint compile reinstances every live object of the class with a
 * property copy, and a copy of a widget tree is a husk: its own widget tree is DuplicateTransient, its
 * registration is not a property, and whatever it had built is left behind registered and unowned. So the
 * trees go before the reinstancer ever sees them. On OnBlueprintPreCompile of a widget class, every tree in
 * every world that holds an instance of it is let go by whoever owns it -- a world host
 * (IDreamWidgetTreeHost) or a screen page, which remember how to build it again, a preview
 * (IDreamRecompilePreview), or, for an instance nothing can rebuild, destroyed outright. The reinstancer
 * finds only garbage. A tick after OnBlueprintCompiled, the owners that are still there build again from
 * the new class, and whatever is still registered from an old class is reported.
 *
 * There is no policy for a play session: its trees go and come back like the editor's. Blocking the
 * release would not block the compile -- the engine compiles regardless -- it would only hand the
 * reinstancer the trees to copy, which is the husk this exists to prevent.
 */
UCLASS()
class DREAMGUIEDITOR_API UDreamGUIEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/** The session's instance, or null outside the editor. */
	static UDreamGUIEditorSubsystem* Get();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * From a widget class's PreCompile to the announcement that the compile is over -- which the editor
	 * makes for every compile, failed ones included, so a flag still set afterwards is one somebody lost.
	 */
	bool IsRecompiling() const { return bRecompiling; }
	/** Whether trees a recompile let go of are still waiting to be built again (the tick after the compile). */
	bool HasPendingRebuild() const
	{
		return RebuildTickerHandle.IsValid() || ReleasedHosts.Num() > 0 || ReleasedScreens.Num() > 0 || ReleasedPreviews.Num() > 0;
	}

	void RegisterPreview(IDreamRecompilePreview* InPreview);
	void UnregisterPreview(IDreamRecompilePreview* InPreview);

	/** Build again, now, whatever a recompile let go of; what the tick after OnBlueprintCompiled does. */
	void RebuildReleasedTrees();

	/**
	 * Mark InPackages dirty when the compile running now announces it is over.
	 *
	 * For a compile that edits assets on purpose -- a `(was:)` rename carried into the graphs. The
	 * compilation manager puts every compiled package's dirty flag back the way it found it, because a
	 * compile is not an edit, so a package marked from inside the compile is clean again before
	 * anything could prompt for it. Marked on OnBlueprintCompiled, which comes after that restore and
	 * before a save that follows the compile in the same frame (the designer's Save button).
	 */
	void MarkPackagesDirtyWhenCompileEnds(TConstArrayView<UPackage*> InPackages);

	/** The source watcher's queue and watches for this session; see FDreamUISourceWatcher. */
	FDreamUISourceWatcherState& GetSourceWatcher() { return SourceWatcher; }

	/** The sprite icon a details panel shows for InPath, loaded once per session. */
	UTexture2D* GetSpriteIconTexture(const FString& InPath, TFunctionRef<UTexture2D*()> InLoad);

	/** What a component reference's details row copied, for its Paste: weakly, it may be gone by then. */
	struct FCopiedComponentReference
	{
		TWeakObjectPtr<AActor> HelperActor;
		TWeakObjectPtr<UActorComponent> TargetComp;
		TWeakObjectPtr<UClass> HelperClass;
	};
	FCopiedComponentReference CopiedComponentReference;

	/**
	 * The font last chosen for a text in a details panel. The editor gives it to the texts it creates
	 * afterwards (FDreamUIEditorTools::ApplyEditorDefaults); a text made anywhere else -- in a play
	 * session, a test, a preview built from a class -- takes the project's default, whatever was clicked.
	 */
	UDreamUIFontData_BaseObject* GetLastPickedFont() const { return LastPickedFont.Get(); }

private:
	void HandleBlueprintPreCompile(UBlueprint* InBlueprint);
	void HandleBlueprintCompiled();
	/** Marks dirty and forgets every package MarkPackagesDirtyWhenCompileEnds queued. */
	void MarkQueuedPackagesDirty();
	void HandleObjectsReplaced(const TMap<UObject*, UObject*>& InReplacementMap);
	void HandleAssetReimport(UObject* InAsset);
	void HandleObjectPropertyChanged(UObject* InObject, struct FPropertyChangedEvent& InEvent);
	/** Let go of every tree InManager's world has that holds an instance of InClass. */
	void ReleaseTreesUsing(UDreamUIManagerWorldSubsystem& InManager, const UClass* InClass);
	/** Report every widget still registered anywhere whose class a compile has retired. */
	void ReportStaleInstances() const;

	FDelegateHandle PreCompileHandle;
	FDelegateHandle CompiledHandle;
	FDelegateHandle ObjectsReplacedHandle;
	FDelegateHandle ReimportHandle;
	FDelegateHandle PropertyChangedHandle;
	FTSTicker::FDelegateHandle RebuildTickerHandle;
	/** The frame-later pass of MarkPackagesDirtyWhenCompileEnds. */
	FTSTicker::FDelegateHandle DirtyTickerHandle;

	bool bRecompiling = false;
	TArray<IDreamRecompilePreview*> Previews;
	TArray<IDreamRecompilePreview*> ReleasedPreviews;
	/** Hosts (IDreamWidgetTreeHost) a recompile asked for their trees, to build again. */
	TArray<TWeakObjectPtr<UObject>> ReleasedHosts;
	/** Screens whose pages a recompile took down, to show again. */
	TArray<TWeakObjectPtr<UDreamScreenUISubsystem>> ReleasedScreens;
	/** See MarkPackagesDirtyWhenCompileEnds. Weak: a package can go before the compile ends. */
	TArray<TWeakObjectPtr<UPackage>> PackagesToDirtyAfterCompile;

	FDreamUISourceWatcherState SourceWatcher;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> SpriteIconTextures;
	TWeakObjectPtr<UDreamUIFontData_BaseObject> LastPickedFont;
};
