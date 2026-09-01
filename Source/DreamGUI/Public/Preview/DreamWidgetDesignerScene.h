// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "DreamWidgetPreviewScene.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"

#if WITH_EDITOR

class UStaticMeshComponent;
class AActor;

//Encapsulates a simple scene setup for Designer.
class DREAMGUI_API FDreamWidgetDesignerScene : public FDreamWidgetPreviewScene
{
public:
	FDreamWidgetDesignerScene(ConstructionValues CVS);
	~FDreamWidgetDesignerScene();
	
	static const FString RootAgentActorName;

	/**
	 * What an axis falls back to when nothing usable was stored for it. The same 1920x1080 that
	 * FDreamWidgetDesignerData::CanvasSize defaults to -- named here because this is the other end
	 * of that default, and two spellings of one number is how they drift apart.
	 */
	static const FIntPoint DefaultCanvasSize;

	/**
	 * Create (once) the design canvas the authored hierarchy hangs under: a root widget carrying a
	 * UDreamCanvas at InCanvasSize. Idempotent -- a second call returns the agent that already exists,
	 * which is what makes it safe to ask for on every preview rebuild.
	 *
	 * A non-positive axis in InCanvasSize is REPLACED, loudly: this is the one size every preview
	 * resolves against, so a stored zero is a hierarchy that draws nothing with nothing to say why.
	 * See the body for which fallback and why it is not silent.
	 *
	 * Takes plain values rather than an asset so it serves the widget-blueprint designer as well as
	 * those values are for a prefab.
	 */
	UDreamWidget* EnsureRootAgent(FIntPoint InCanvasSize, EDreamRenderMode InRenderMode, FIntPoint InSizeInEditMode);
	void SetSkyCubeVisibility(bool bVisible);
	UDreamWidget* GetRootAgent()const { return RootAgentWidget.Get(); }
private:
	TStrongObjectPtr<UDreamWidget> RootAgentWidget = nullptr;
	UStaticMeshComponent* SkySphereComponent = nullptr;
};
#endif
