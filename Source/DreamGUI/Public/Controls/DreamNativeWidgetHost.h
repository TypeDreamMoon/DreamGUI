// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamNativeWidgetHost.generated.h"

class UDreamWidget;
class UDreamUMGWidget;
class UUserWidget;

/**
 * A hole in a DreamGUI hierarchy that a UMG widget fills.
 *
 * UMG's NativeWidgetHost is the reverse bridge -- a UMG widget wrapping a raw Slate one -- and this
 * is the same idea pointed the other way, which is the direction that matters here: the DreamGUI
 * tree is the outer one, and what a project needs is a way to keep a UMG screen it already owns
 * (an engine widget, a plugin's panel, a Slate-only editor surface) inside it.
 *
 * The machinery already existed and had no control: UDreamUMGWidget renders a UUserWidget (or a bare
 * SWidget) to a render target and draws it as this library's own geometry, and
 * UDreamUMGWidgetInteraction routes pointer events into it. Both are components an author had to
 * know to add, wire and size. This class is the one tag that does it.
 *
 * WHAT IT IS NOT: a way to mix UMG layout with DreamGUI layout. The hosted widget is drawn into a
 * texture at this control's size and knows nothing about what is around it -- it cannot push the
 * DreamGUI layout around and DreamGUI cannot reach inside it. That is the bargain the render-target
 * bridge makes, it is the same one UMG's own world-space widget component makes, and a control that
 * pretended otherwise would be lying about which layout pass owns what.
 *
 * NEEDS A WORLD, like everything that instances a UserWidget: with none (a headless test, the parts
 * of the designer that build no world) the host node exists, sized and placed, and simply draws
 * nothing. That is the same answer UDreamListViewBase gives for a row template class.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Native Widget Host")
class DREAMGUI_API UDreamNativeWidgetHost : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/** The UMG widget class to instance and draw. Null draws nothing, which is an empty hole. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetWidgetClass", BlueprintSetter = "SetWidgetClass", Category = "Native Widget Host")
	TSubclassOf<UUserWidget> WidgetClass = nullptr;

	/**
	 * How many render-target pixels one local unit is worth. One is pixel-for-pixel.
	 *
	 * Here rather than in a style because it is not a LOOK, it is a fidelity-versus-memory decision
	 * about one particular hosted widget, and a project sheet has nothing useful to say about it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Native Widget Host", meta = (ClampMin = "0.05"))
	float ResolutionScale = 1.0f;

	/** What shows through where the hosted widget draws nothing. Transparent by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Native Widget Host")
	FLinearColor BackgroundColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	/** The node whose visual is the bridge. Public because everything under a control is reachable. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Native Widget Host")
	TObjectPtr<UDreamWidget> HostNode = nullptr;

	/** The bridge itself, for everything this control does not wrap. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Native Widget Host")
	TObjectPtr<UDreamUMGWidget> HostVisual = nullptr;

	UFUNCTION(BlueprintPure, Category = "Native Widget Host")
	TSubclassOf<UUserWidget> GetWidgetClass() const { return WidgetClass; }

	/** Replace the hosted class and re-instance it. Null empties the hole. */
	UFUNCTION(BlueprintCallable, Category = "Native Widget Host")
	void SetWidgetClass(TSubclassOf<UUserWidget> InWidgetClass);

	/** The instance the bridge made, or null while there is no world or no class. */
	UFUNCTION(BlueprintPure, Category = "Native Widget Host")
	UUserWidget* GetHostedWidget() const;

	virtual void ApplyStyle() override;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;
};
