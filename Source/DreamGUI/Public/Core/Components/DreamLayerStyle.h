// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamLayerStyleTypes.h"
#include "Core/DreamUILayerStyleMath.h"
#include "DreamUIRender/DreamUILayerStyleRecord.h"
#include "DreamUIRender/DreamUILayerStyleComposite.h"
#include "DreamLayerStyle.generated.h"

class UDreamLayerStylePreset;
class UDreamCanvas;
class UDreamVisual;
struct FDreamUICompositeDesc;

/** How a layer style is drawn (UDreamLayerStyle::GetPath). */
UENUM(BlueprintType)
enum class EDreamLayerStylePath : uint8
{
	/** Nothing to draw: no effect enabled, or the style is off. */
	None,
	/** In the element's own pixels, in the batch it is drawn with: a rect block's shape is measured where it is drawn. */
	Element,
	/**
	 * The widget is drawn into a texture of its own, which is measured (a jump flood of its alpha) and styled as it is put down:
	 * any other element, a text, an image, a container's subtree, a layer group. Its outer effects blend with what is behind
	 * by their own modes. Only where the canvas draws groups apart (UDreamLayerGroup); elsewhere the style is not drawn.
	 */
	Group,
};

/**
 * An image editor's layer style on a widget: drop shadows, an outer glow, inner shadows, an inner glow, a bevel and emboss, a
 * satin, colour, gradient and pattern overlays, strokes (FDreamLayerEffects). Each effect blends by its own mode, at its own
 * opacity; the drop shadows and the outer glow lie behind the widget, the rest on it.
 *
 * What it is drawn round: the widget's own element when it has one -- a rect block in its own pixels, anything else drawn apart
 * and measured -- and the widget's subtree when it has no element of its own (a container) or is a layer group drawn apart.
 * A preset names a shared style (UDreamLayerStylePreset); while there is one, its style is drawn and this one's is not.
 *
 * A rect block's own shadows, border and gradient (UDreamRectBlock) are a second way to write four of these effects: see
 * UDreamRectBlock::GetEffectsAsLayerStyle. The style does not take part in hit tests.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "Dream Layer Style")
class DREAMGUI_API UDreamLayerStyle : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	UDreamLayerStyle();

	/** What is drawn: the preset's style while there is one, else this one's. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Layer Style")
	const FDreamLayerEffects& GetEffectiveStyle() const;
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Layer Style")
	const FDreamLayerEffects& GetStyle() const { return Style; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Layer Style")
	void SetStyle(const FDreamLayerEffects& InStyle);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Layer Style")
	UDreamLayerStylePreset* GetPreset() const { return Preset; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Layer Style")
	void SetPreset(UDreamLayerStylePreset* InPreset);
	/** Copy the preset's style into this one and stop following the preset: a starting point to edit. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Layer Style")
	void DetachFromPreset();

	/** How the style is drawn on its widget now. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Layer Style")
	EDreamLayerStylePath GetPath() const;

	/** The effective style packed for the shaders; empty when it draws nothing. Game thread. */
	const FDreamUILayerStyleRecord& GetRecord() const;
	/** The render thread's style for a group's composite (EDreamLayerStylePath::Group); null when it draws nothing. */
	FDreamUILayerStyleRef GetComposite() const;
	/** Whether the style is drawn round the widget's subtree rather than its own element. */
	bool StylesSubtree() const;

	/**
	 * Light the style's bevel by a point light instead of its own angle and altitude (a UDreamPointerLight over the widget, every
	 * frame it moves); null goes back to the style's light. Not saved.
	 */
	void SetPointLight(const FDreamUILayerStyleLight* InLight);
	bool IsPointLit() const { return PointLight.IsSet(); }

	/** The style of InWidget, when it has one that is registered: its first. */
	static UDreamLayerStyle* Find(const class UDreamWidget* InWidget);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

	/** A shared style to draw in place of Style. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Layer Style")
	TObjectPtr<UDreamLayerStylePreset> Preset = nullptr;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Layer Style",
		meta = (EditCondition = "Preset == nullptr"))
	FDreamLayerEffects Style;

private:
	/** The style changed: packed again, and the widget's element or canvas told. */
	void NoteChanged();
	void BindPreset();
	void UnbindPreset();

	FDreamUILayerStyleRecord Record;
	FDreamUILayerStyleRef Composite;
	TOptional<FDreamUILayerStyleLight> PointLight;
	bool bPacked = false;
	/** The path when the canvas last made its draw calls: a change asks for them to be made again. */
	EDreamLayerStylePath LastPath = EDreamLayerStylePath::None;
	TWeakObjectPtr<UDreamLayerStylePreset> BoundPreset;
	FDelegateHandle PresetChangedHandle;
};
