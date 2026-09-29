// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/IntPoint.h"
#include "Templates/SharedPointer.h"

class UClass;
class UDreamWidget;
class UDreamWidgetBlueprint;
class UPackage;

namespace DreamTests
{
	class FDreamDesignerDriver;

	/**
	 * A Widget Blueprint that exists for one test and nowhere else.
	 *
	 * In a package of its own under /Temp, held on the root set for as long as the test needs it:
	 * a designer keeps its asset alive only while it is open, and a test closes the designer before it
	 * is done with the asset. The package name carries a fresh suffix every time, so a run that died
	 * half-way cannot leave a same-named asset for the next run to collide with.
	 */
	struct FDesignerTestAsset
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		bool IsValid() const { return Package != nullptr && Blueprint != nullptr; }
	};

	/**
	 * Build what the New Widget Blueprint dialog builds, compiled once so a designer can open on it.
	 *
	 * @param bGiveRootAPanel  true puts a Canvas Panel on the root, so every widget dropped on it is a
	 *                         free child placed where it was dropped; false leaves a root that arranges
	 *                         nothing, which is the state the first-drop fill rule is written for.
	 */
	FDesignerTestAsset CreateDesignerTestAsset(const TCHAR* InName, bool bGiveRootAPanel);

	/** Let the asset go. Idempotent. Call it a frame after the designer on it was closed, not before. */
	void ReleaseDesignerTestAsset(FDesignerTestAsset& InOutAsset);

	/** The authoring tree's root: the asset itself, which is what survives every preview rebuild. */
	UDreamWidget* DesignerTemplateRoot(const UDreamWidgetBlueprint* InBlueprint);

	/** The first child that is actually there; a collected null is not a child. */
	UDreamWidget* FirstLiveChildOf(const UDreamWidget* InParent);

	/** Widgets of InClass anywhere under InRoot, InRoot itself not counted. Nulls are skipped, not counted. */
	int32 CountDescendantsOfClass(const UDreamWidget* InRoot, const UClass* InClass);

	/** A hole anywhere under InRoot. Re-instancing has left one before, and it crashed the next frame. */
	bool HasNullDescendantUnder(const UDreamWidget* InRoot);

	/** The children of InParent that are actually there, in order. */
	TArray<UDreamWidget*> LiveChildrenOf(const UDreamWidget* InParent);

	/**
	 * Drop a palette row at InPixel through the driver, and answer the AUTHORED widget it created
	 * directly under the Blueprint's root -- or null when the drop was refused or landed deeper.
	 *
	 * The authored widget rather than the preview one, because every drop rebuilds the preview and
	 * no preview widget from before it survives; the driver's PreviewFor finds the current one.
	 */
	UDreamWidget* DropOntoRootAndFindTemplate(FDreamDesignerDriver& InDriver, UClass* InWidgetClass, FIntPoint InPixel);

	/**
	 * An open, sized designer for the length of one synchronous test.
	 *
	 * Creates the asset, opens its designer, gives the viewport a size -- a headless editor lays it
	 * out at none -- and pumps one frame so the preview has settled. The destructor closes the
	 * designer, ticks Slate once so the deferred close actually happens, and only then lets the asset
	 * go: a toolkit still alive still ticks, and a rebuild on a half-collected asset is its own crash.
	 * That is the order every synchronous designer fixture in this module uses.
	 */
	class FScopedDesignerSession
	{
	public:
		FScopedDesignerSession(const TCHAR* InName, bool bGiveRootAPanel, FIntPoint InViewportSize = FIntPoint(1280, 720));
		~FScopedDesignerSession();

		FScopedDesignerSession(const FScopedDesignerSession&) = delete;
		FScopedDesignerSession& operator=(const FScopedDesignerSession&) = delete;

		/** Whether the whole chain came up. When it did not, GetFailure names the missing link. */
		bool IsReady() const { return Driver.IsValid(); }
		const FString& GetFailure() const { return Failure; }

		FDreamDesignerDriver& GetDriver() const { return *Driver; }
		UDreamWidgetBlueprint* GetBlueprint() const { return Asset.Blueprint; }
		UDreamWidget* GetTemplateRoot() const { return DesignerTemplateRoot(Asset.Blueprint); }

	private:
		FDesignerTestAsset Asset;
		TSharedPtr<FDreamDesignerDriver> Driver;
		FString Failure;
	};
}
