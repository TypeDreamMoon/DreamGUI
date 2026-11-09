// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamVisualDirectMesh.h"
#include "DreamNiagaraEngineAccess.h"
#include "DreamNiagaraParticles.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "DreamNiagaraSystem.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UMaterialInterface;
class UTexture;
class UDreamWidget;

/** Which of the simulation's planes is the widget's. */
UENUM(BlueprintType)
enum class EDreamNiagaraPlane : uint8
{
	/** The simulation's X is the widget's right and Z its up: an effect made with gravity along -Z falls down the screen. */
	XZ,
	/** X is right, Y up, and Z out of the screen. */
	XY,
};

/** How a renderer's particles are shaded. */
UENUM(BlueprintType)
enum class EDreamNiagaraShading : uint8
{
	/** The system's default (UDreamNiagaraSystem::DefaultShading). Only for an override. */
	Default,
	/**
	 * DreamGUI's built-in shader, no material: the texture and the blend are taken from the particle material (its Texture
	 * parameter, else the first texture it samples; additive, modulate or alpha blending), each of which an override may set.
	 * Clipped, masked and blended as any DreamGUI element is.
	 */
	BuiltIn,
	/**
	 * A UI material: the override's, else the one MaterialMap gives for the particle material -- made for it by the editor's
	 * conversion, or by hand. Where there is none, the built-in shader draws the renderer.
	 */
	Material,
};

/** One renderer's shading, set apart from the system's default. */
USTRUCT(BlueprintType)
struct DREAMGUINIAGARA_API FDreamNiagaraRendererOverride
{
	GENERATED_BODY()

	/** The emitter, by name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	FName Emitter;
	/** Which of the emitter's renderers, counting from 0 in the order the emitter lists them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI", meta = (ClampMin = "0"))
	int32 Renderer = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	EDreamNiagaraShading Shading = EDreamNiagaraShading::Default;
	/** The built-in shader's texture, in place of the particle material's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	TObjectPtr<UTexture> Texture;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI", meta = (InlineEditConditionToggle))
	bool bOverrideBlendMode = false;
	/** The built-in shader's blend mode, in place of the one the particle material's blending gives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI", meta = (EditCondition = "bOverrideBlendMode"))
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Additive;
	/** The UI material, for Material shading, in place of MaterialMap's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
	TObjectPtr<UMaterialInterface> Material;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamNiagaraBurstFinished, int32, BurstId);

/**
 * A Niagara system drawn as part of a DreamGUI canvas.
 *
 * The element owns a Niagara component it registers with its world but attaches to nothing, gives the scene no proxy for and
 * keeps out of Niagara's scalability: the component only simulates. Every frame the canvas sends its sections, the element waits for the
 * simulation's tick to finish, reads each renderer's particles through its bindings and writes them into a segment of its
 * own -- one draw call per sprite or ribbon renderer, in Niagara's renderer order -- so the particles are clipped, masked,
 * blended and grouped as every DreamGUI element is.
 *
 * The simulation's units are the canvas's: one unit is PixelsPerUnit canvas units, on the plane Plane names. The component
 * stands where the widget does on the canvas, so a local-space emitter moves with the widget and one in world space leaves its
 * particles behind on the canvas. CPU emitters with sprite and ribbon renderers are drawn; the rest are reported (see
 * GetRendererInfo) and left out.
 *
 * Reference: SourySK/NiagaraUIRenderer and liufei2008/LGUI_ParticleSystem (both MIT) were read for this design; the code is
 * written anew.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent, DisplayName = "Niagara System"))
class DREAMGUINIAGARA_API UDreamNiagaraSystem : public UDreamVisualDirectMesh
{
	GENERATED_BODY()

public:
	UDreamNiagaraSystem(const FObjectInitializer& ObjectInitializer);

	/** Where the user parameters DreamGUI writes are named, without the "User." namespace. A system that declares one is given it. */
	static const FName WidgetSizeParameter;
	static const FName PointerPositionParameter;
	static const FName PointerInsideParameter;
	static const FName TargetPositionParameter;

	//~ UDreamVisualDirectMesh
	virtual bool HaveValidData()const override;
	virtual UMaterialInterface* GetMaterial()const override { return nullptr; }
	virtual int32 GetSegmentCount()const override;
	virtual bool GetSegmentShading(int32 InSegment, FDreamUIDirectMeshShading& OutShading)const override;
	virtual bool WantsFrameRefresh()const override { return true; }
	virtual bool RefreshMeshSegments() override;
	virtual uint8 GetWidgetPropertyMark()const override;

	/** The system. Setting it makes the simulation again, and activates it when the element activates by itself. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetSystem(UNiagaraSystem* InSystem);
	UFUNCTION(BlueprintPure, Category = "DreamGUI")
	UNiagaraSystem* GetSystem()const { return System; }

	/** Starts the simulation; with bReset, from the beginning even when it runs. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void Activate(bool bReset = false);
	/** Stops spawning; the particles there are live out their lives. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void Deactivate();
	/** Clears every particle and starts again. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void ResetSystem();
	UFUNCTION(BlueprintPure, Category = "DreamGUI")
	bool IsActive()const;

	/** Pauses the simulation, its bursts included; the particles stay where they are. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetPaused(bool bInPaused);
	UFUNCTION(BlueprintPure, Category = "DreamGUI")
	bool IsPaused()const { return bPaused; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetTimeDilation(float InTimeDilation);
	UFUNCTION(BlueprintPure, Category = "DreamGUI")
	float GetTimeDilation()const { return TimeDilation; }
	/**
	 * Runs the simulation InSeconds forward now, in steps of InStep seconds, whatever its pause: for a warm-up, a test or a
	 * scrub. The particles show the result at the next frame.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void Advance(float InSeconds, float InStep = 0.0166667f);

	/** Spawns the system once more at InLocalPosition (the widget's local space, in canvas units), beside the running one. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	int32 BurstAt(FVector2D InLocalPosition);
	/**
	 * Spawns the flight system (FlightSystem, the plugin's fly-to template when none is set) at InLocalPosition, its particles
	 * heading for InTarget's centre, which it follows as the target moves (User.DreamUI_TargetPosition). Returns the burst's id,
	 * which OnBurstFinished names when its particles are gone; INDEX_NONE when no burst was made.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	int32 BurstTo(UDreamWidget* InTarget, FVector2D InLocalPosition = FVector2D::ZeroVector);
	/** A burst's particles are all gone. */
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI")
	FDreamNiagaraBurstFinished OnBurstFinished;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetVariableFloat(FName InName, float InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetVariableInt(FName InName, int32 InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetVariableBool(FName InName, bool bInValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetVariableVec2(FName InName, FVector2D InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetVariableVec3(FName InName, FVector InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetVariableLinearColor(FName InName, FLinearColor InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetVariableObject(FName InName, UObject* InValue);

	/** Takes the pointer position given here instead of the pointer's (User.DreamUI_PointerPosition), until cleared. Local, canvas units. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetPointerPositionOverride(FVector2D InLocalPosition, bool bInInside = true);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void ClearPointerPositionOverride();

	/** The component that simulates the system; null while the element is not registered with a world. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI")
	UNiagaraComponent* GetNiagaraComponent()const { return Component; }

	/** Each renderer of the system, drawn or not, with what keeps it from being drawn: the report the details panel shows. */
	const TArray<FDreamNiagaraRendererInfo>& GetRendererInfo()const;
	/** The drawn renderers, in drawing order, each a segment: index into GetRendererInfo, for segment InSegment. */
	int32 GetRendererOfSegment(int32 InSegment)const;
	/** How many particles the segment drew at its last refresh. */
	int32 GetSegmentParticleCount(int32 InSegment)const;
	/** Where the simulation's position InSimPosition is on the canvas (canvas space), for a world-space emitter. */
	FVector SimToCanvas(const FVector& InSimPosition)const;
	/** Where the canvas-space InCanvasPosition is in the simulation's world. */
	FVector CanvasToSim(const FVector& InCanvasPosition)const;
	/** How many bursts are alive. */
	int32 GetBurstCount()const { return Bursts.Num(); }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetPixelsPerUnit(float InPixelsPerUnit);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetPlane(EDreamNiagaraPlane InPlane);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetDefaultShading(EDreamNiagaraShading InShading);
	/** Every renderer the built-in shader draws blends by InBlendMode (bInOverride), unless its override says otherwise; or each by its material. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetBlendModeOverride(bool bInOverride, EDreamUIBlendMode InBlendMode);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetRendererOverrides(const TArray<FDreamNiagaraRendererOverride>& InOverrides);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetMaterialMap(const TMap<UMaterialInterface*, UMaterialInterface*>& InMap);
	const TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInterface>>& GetMaterialMap()const { return MaterialMap; }
	/** Gives a particle material its UI material, as the editor's conversion does. */
	void SetMappedMaterial(UMaterialInterface* InParticleMaterial, UMaterialInterface* InUIMaterial);

protected:
	virtual void OnRegister()override;
	virtual void OnUnregister()override;
	virtual void BeginDestroy()override;
	virtual void OnRenderCanvasChanged(UDreamCanvas* InOldCanvas, UDreamCanvas* InNewCanvas)override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif

	/** The system to simulate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UNiagaraSystem> System;
	/** Starts by itself when the widget is registered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara", meta = (AllowPrivateAccess = "true"))
	bool bAutoActivate = true;
	/** Added to every emitter's random seed: two widgets of one deterministic system made to differ. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara", meta = (AllowPrivateAccess = "true"))
	int32 RandomSeedOffset = 0;
	/** Spawned by BurstTo; the plugin's fly-to template when none is set. Its renderers are drawn after the system's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UNiagaraSystem> FlightSystem;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Time", meta = (AllowPrivateAccess = "true"))
	bool bPaused = false;
	/** How fast the simulation's time runs against the world's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Time", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	float TimeDilation = 1.0f;
	/** The simulation runs while the game is paused, as UI does. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Time", meta = (AllowPrivateAccess = "true"))
	bool bTickWhenGamePaused = true;
	/**
	 * Steps of this many seconds instead of the frame's, as many as the frame's time holds (at most 8 a frame): the same
	 * particles whatever the frame rate. 0 ticks with the frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Time", meta = (AllowPrivateAccess = "true", ClampMin = "0", Units = "s"))
	float FixedTimeStep = 0.0f;
	/** Run this far ahead when the simulation starts, so that it opens full. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Time", meta = (AllowPrivateAccess = "true", ClampMin = "0", Units = "s"))
	float WarmupTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Space", meta = (AllowPrivateAccess = "true"))
	EDreamNiagaraPlane Plane = EDreamNiagaraPlane::XZ;
	/** Canvas units per simulation unit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Space", meta = (AllowPrivateAccess = "true", ClampMin = "0.001"))
	float PixelsPerUnit = 1.0f;
	/** Draws every particle on the widget's plane, however far out of it the simulation put it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Space", meta = (AllowPrivateAccess = "true"))
	bool bFlattenDepth = true;

	/** How the renderers are shaded, unless an override says otherwise. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Shading", meta = (AllowPrivateAccess = "true"))
	EDreamNiagaraShading DefaultShading = EDreamNiagaraShading::BuiltIn;
	/** The UI material each particle material is drawn with under Material shading. The editor's conversion fills it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Shading", meta = (AllowPrivateAccess = "true"))
	TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInterface>> MaterialMap;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Shading", meta = (AllowPrivateAccess = "true", TitleProperty = "Emitter"))
	TArray<FDreamNiagaraRendererOverride> RendererOverrides;
	/** Every renderer the built-in shader draws blends by BlendMode instead of by its material, unless its override says otherwise. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Shading", meta = (AllowPrivateAccess = "true", InlineEditConditionToggle))
	bool bOverrideBlendMode = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara|Shading", meta = (AllowPrivateAccess = "true", EditCondition = "bOverrideBlendMode"))
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Additive;

	/** Writes the user parameters DreamGUI knows to every system that declares them (WidgetSizeParameter and the rest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Niagara", meta = (AllowPrivateAccess = "true"))
	bool bWriteUIParameters = true;

private:
	/** A spawn of its own: BurstAt's of the system, BurstTo's of the flight system. */
	struct FBurst
	{
		int32 Id = INDEX_NONE;
		TObjectPtr<UNiagaraComponent> Component;
		bool bFlight = false;
		/** The burst's place on the widget, local and canvas units. */
		FVector2D LocalPosition = FVector2D::ZeroVector;
		TWeakObjectPtr<UDreamWidget> Target;
		/** Frames since it was made: a burst is not taken for finished before its first tick. */
		int32 Age = 0;
		/** It has been seen running: from then on, not running is finished. Until then it waits for its system to be ready. */
		bool bStarted = false;
	};
	/** One segment's renderer and what it read last. */
	struct FSegment
	{
		/** Into RendererInfo, or into FlightRendererInfo when bFlight. */
		int32 Renderer = INDEX_NONE;
		bool bFlight = false;
		int32 LastCount = 0;
		/** The particles it read last, kept so that reading them again allocates nothing. */
		TArray<FDreamNiagaraSpriteParticle> Sprites;
		TArray<FDreamNiagaraRibbonParticle> Ribbons;
		bool bWarnedDropped = false;
	};

	void EnsureComponent();
	void DestroyComponents();
	UNiagaraComponent* MakeComponent(UNiagaraSystem* InSystem, const FVector2D& InLocalPosition)const;
	void ConfigureTime(UNiagaraComponent* InComponent)const;
	/** Whether this element drives the simulation's time itself (a fixed step, or a world that does not tick its components). */
	bool DrivesTime()const;
	void StepSimulation();
	void DescribeIfStale()const;
	void RebuildSegments();
	const FDreamNiagaraRendererInfo* RendererOfSegment(const FSegment& InSegment)const;
	const FDreamNiagaraRendererOverride* FindOverride(const FDreamNiagaraRendererInfo& InRenderer)const;
	/** The widget's transform on the canvas, and the simulation's axes taken to the widget's. */
	FMatrix SimToWidgetMatrix()const;
	FMatrix WidgetToCanvasMatrix()const;
	void PlaceComponent(UNiagaraComponent* InComponent, const FVector2D& InLocalPosition)const;
	void WriteUIParameters(UNiagaraComponent* InComponent, const UNiagaraSystem* InSystem, const FBurst* InBurst);
	void UpdateBursts();
	void MarkShadingChanged();

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Component;
	TArray<FBurst> Bursts;
	/** The burst components, held for the collector. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> BurstComponents;
	int32 NextBurstId = 0;

	mutable TArray<FDreamNiagaraRendererInfo> RendererInfo;
	mutable TArray<FDreamNiagaraRendererInfo> FlightRendererInfo;
	mutable TWeakObjectPtr<const UNiagaraSystem> DescribedSystem;
	mutable TWeakObjectPtr<const UNiagaraSystem> DescribedFlightSystem;
	TArray<FSegment> Segments;
	bool bSegmentsStale = true;

	uint64 LastSteppedFrame = MAX_uint64;
	float StepRemainder = 0.0f;
	bool bPointerOverride = false;
	/** Activate was asked for and Deactivate not since: what is retried while the system is not ready to run. */
	bool bWantsActive = false;
	bool bActivationResets = false;
	/** InComponent activated when it can be, and its own tick off when this element steps it (DrivesTime). */
	void StartWhenReady(UNiagaraComponent* InComponent, bool bInReset) const;
	bool bPointerOverrideInside = false;
	FVector2D PointerOverride = FVector2D::ZeroVector;
};
