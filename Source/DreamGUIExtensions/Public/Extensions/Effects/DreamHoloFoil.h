// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamLayerStyleTypes.h"
#include "DreamHoloFoil.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture;
class UTexture2D;

/** Where a holo foil takes its angle of view from. */
UENUM(BlueprintType)
enum class EDreamHoloFoilView : uint8
{
	/** The pointer's offset from the element's centre. */
	Pointer,
	/** The tilt of a tilt card on the same widget (UDreamTiltCard). */
	Tilt,
	/** ManualView, set or animated. */
	Manual,
};

/**
 * A trading card's holo foil over an image, a sprite, a texture or a rect block: rainbow bands that slide as the angle of view
 * changes, a glare across the card and sparkles that twinkle as it turns, where the foil mask says. The angle of view is the
 * pointer's offset from the centre, a tilt card's tilt, or set by hand.
 *
 * Drawn by a material of its own (DreamUI_HoloFoil, made by DreamShader from DShader/DreamUI_HoloFoil.dsm), which the element
 * draws with while the foil is on; the material shades the element as the default one does and adds the foil. A material is
 * a draw call of its own, which a few cards on screen can afford.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "Dream Holo Foil")
class DREAMGUIEXTENSIONS_API UDreamHoloFoil : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	UDreamHoloFoil();

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Holo Foil")
	bool IsEnabled() const { return bEnabled; }
	/** Off, the element draws with its own material again; on, with the foil's. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Holo Foil")
	void SetEnabled(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Holo Foil")
	void SetManualView(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Holo Foil")
	void SetViewSource(EDreamHoloFoilView Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Holo Foil")
	void SetIntensity(float Value);
	/** The angle of view now, -1..1 across and up. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Holo Foil")
	FVector2D GetCurrentView() const { return CurrentView; }

	/** The material the element draws with while the foil is on: the foil's own instance. Null while unregistered. */
	UMaterialInstanceDynamic* GetFoilMaterial() const { return FoilMaterial; }
	/** The view taken and the material's parameters set, now: what every frame does, for a test. */
	void Update(float InDeltaTime);
	/** The rainbow baked into its table's texels, 0..1 along it: what the texture holds. */
	static void BakeRainbow(const FDreamStyleGradient& InGradient, TArray<FColor>& OutTexels, int32 InWidth = 256);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void Tick(float DeltaTime) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Off, the element is drawn as if there were no foil: to compare with and without it. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", meta = (DreamGUI_LayerToggle))
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil")
	EDreamHoloFoilView ViewSource = EDreamHoloFoilView::Pointer;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", meta = (EditCondition = "ViewSource == EDreamHoloFoilView::Manual", EditConditionHides))
	FVector2D ManualView = FVector2D::ZeroVector;
	/** Where the foil is: its red channel; none is everywhere. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil")
	TObjectPtr<UTexture> FoilMask = nullptr;
	/** The bands' colours, along a band. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil")
	FDreamStyleGradient Rainbow;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", meta = (ClampMin = "0", UIMax = "2"))
	float Intensity = 0.7f;
	/** How many bands cross the element. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", meta = (ClampMin = "0.1", UIMax = "12"))
	float BandFrequency = 2.5f;
	/** The bands' angle, degrees from across. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", meta = (UIMin = "-90", UIMax = "90"))
	float BandAngle = 35.0f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", meta = (ClampMin = "0", UIMax = "4"))
	float SparkleIntensity = 1.0f;
	/** How many cells across the sparkles are laid in. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", meta = (ClampMin = "1", UIMax = "200"))
	float SparkleCells = 48.0f;
	/** The share of the cells that sparkle. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", meta = (ClampMin = "0", ClampMax = "1"))
	float SparkleDensity = 0.08f;
	/** How quickly the view follows its source: higher is tighter, 0 is at once. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", meta = (ClampMin = "0", UIMax = "60"))
	float ViewSharpness = 12.0f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", AdvancedDisplay)
	int32 UserIndex = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", AdvancedDisplay)
	int32 PointerId = 0;

private:
	/** The element drawn with the foil's material, or with what it drew with before. */
	void Attach();
	void Detach();
	void BakeRainbowTexture();

	// Made by this foil: a copy makes its own when it attaches.
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
	TObjectPtr<UMaterialInstanceDynamic> FoilMaterial = nullptr;
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
	TObjectPtr<UTexture2D> RainbowTexture = nullptr;
	FVector2D CurrentView = FVector2D::ZeroVector;
	FDelegateHandle EditorTickHandle;
};
