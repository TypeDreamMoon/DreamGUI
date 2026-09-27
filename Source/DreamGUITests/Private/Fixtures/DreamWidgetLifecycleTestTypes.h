// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamUIBehaviour.h"
#include "Core/DreamWidgetTreeHost.h"
#include "Core/Components/DreamImage.h"
#include "DreamWidgetLifecycleTestTypes.generated.h"

class UDreamCanvas;
class UDreamWidget;

UCLASS()
class UDreamWidgetHierarchyMutationBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	void Configure(UDreamWidget* InWidgetToDetach, UDreamWidget* InWidgetToAttach, UDreamWidget* InExternalParent)
	{
		WidgetToDetach = InWidgetToDetach;
		WidgetToAttach = InWidgetToAttach;
		ExternalParent = InExternalParent;
	}

protected:
	virtual void OnUnregister() override;

private:
	UPROPERTY()
	TObjectPtr<UDreamWidget> WidgetToDetach;

	UPROPERTY()
	TObjectPtr<UDreamWidget> WidgetToAttach;

	UPROPERTY()
	TObjectPtr<UDreamWidget> ExternalParent;
};

/** An image that records each change of the canvas it draws in, and each time it is marked to be written whole. */
UCLASS()
class UDreamWidgetCanvasProbeVisual : public UDreamImage
{
	GENERATED_BODY()

public:
	virtual void OnRenderCanvasChanged(UDreamCanvas* InOldCanvas, UDreamCanvas* InNewCanvas) override;
	virtual void MarkAllDirty() override;

	/** Old and new canvas of each change, compared by address only: either may be gone by the time a test looks. */
	TArray<TPair<const UDreamCanvas*, const UDreamCanvas*>> CanvasChanges;
	int32 MarkAllDirtyCount = 0;
};

/** Something a widget made and keeps as one of its parts. It takes no part in registration or play. */
UCLASS()
class UDreamWidgetLifecyclePart : public UObject
{
	GENERATED_BODY()
};

/** A behaviour that counts how often its widget registers and unregisters it. */
UCLASS()
class UDreamWidgetLifecycleCountingBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	int32 RegisterCount = 0;
	int32 UnregisterCount = 0;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};

/** A tree host that owns no tree and records what it is asked, for the tests of the contract itself. */
UCLASS()
class UDreamTreeHostProbe : public UObject, public IDreamWidgetTreeHost
{
	GENERATED_BODY()

public:
	virtual UObject* GetTreeOuter() const override
	{
		return const_cast<UDreamTreeHostProbe*>(this);
	}
	virtual void ReleaseTree(EDreamTreeReleaseReason InReason) override
	{
		Releases.Add(InReason);
	}
	virtual void RebuildTree() override
	{
		++Rebuilds;
	}

	TArray<EDreamTreeReleaseReason> Releases;
	int32 Rebuilds = 0;
};
