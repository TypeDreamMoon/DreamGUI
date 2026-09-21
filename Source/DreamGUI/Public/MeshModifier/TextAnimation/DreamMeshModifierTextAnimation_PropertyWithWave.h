// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "../DreamMeshModifierTextAnimation.h"
#include "DreamMeshModifierTextAnimation_PropertyWithWave.generated.h"

UCLASS(ClassGroup = (DreamGUI), Abstract, BlueprintType)
class DREAMGUI_API UDreamMeshModifierTextAnimation_PropertyWithWave : public UDreamMeshModifierTextAnimation_Property
{
	GENERATED_BODY()
protected:
	/** Higher frequency will generate smaller wavelength. */
	UPROPERTY(EditAnywhere, Category = "Property")
		float Frequency = 1.0f;
	/** Move speed of the wave. */
	UPROPERTY(EditAnywhere, Category = "Property")
		float Speed = 1.0f;
	/** Flip move speed direction of the wave. */
	UPROPERTY(EditAnywhere, Category = "Property")
		bool FlipDirection = false;
	TWeakObjectPtr<class UDreamTweener> UpdateTweener;
	virtual void OnUpdate(float deltaTime);
	UPROPERTY(Transient)TObjectPtr<class UDreamText> TextObject;
public:
	virtual void Init()override;
	virtual void Deinit()override;
	/**
	 * Frequency and Speed are two different numbers -- one sets the wavelength along the string of
	 * characters, the other how fast the wave travels along it -- and this pair used to read and
	 * write Speed under the Frequency name, leaving Frequency with no accessor at all. Corrected
	 * here rather than left alone, because a getter that returns the wrong property cannot be worked
	 * around by a caller who knows about it.
	 *
	 * The counter-intuitive part is that this is a silent change for anyone who was calling
	 * SetFrequency: the node still exists and still compiles, and now moves a different property.
	 * Authored content is unaffected either way -- both properties are EditAnywhere and serialise
	 * independently, so nothing on disk changes meaning -- and the alternative, renaming the pair to
	 * match what it did, breaks those call sites instead while leaving the real Frequency
	 * unreachable. Only Blueprint call sites are in question, and the pair is reachable from
	 * Blueprint only by pulling an instanced property out of a text animation's array and casting it.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetFrequency()const { return Frequency; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetFrequency(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetSpeed()const { return Speed; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSpeed(float Value);
};

UCLASS(ClassGroup = (DreamGUI), BlueprintType, meta = (DisplayName = "PositionWave Property (UI Effect TextAnimation)"))
class DREAMGUI_API UDreamMeshModifierTextAnimation_PositionWaveProperty : public UDreamMeshModifierTextAnimation_PropertyWithWave
{
	GENERATED_BODY()
private:
	/** Max position value for sin wave. Sin function generate values from -1 to 1, so the result will be from -position to position. */
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector Position;
public:
	virtual void ApplyProperty(class UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector GetPosition()const { return Position; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetPosition(FVector Value);
};

UCLASS(ClassGroup = (DreamGUI), BlueprintType, meta = (DisplayName = "RotationWave Property (UI Effect TextAnimation)"))
class DREAMGUI_API UDreamMeshModifierTextAnimation_RotationWaveProperty : public UDreamMeshModifierTextAnimation_PropertyWithWave
{
	GENERATED_BODY()
private:
	/** Max rotator value for sin wave. Sin function generate values from -1 to 1, so the result will be from -rotator to rotator. */
	UPROPERTY(EditAnywhere, Category = "Property")
		FRotator Rotator;
public:
	virtual void ApplyProperty(class UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FRotator GetRotator()const { return Rotator; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRotator(FRotator Value);
};

UCLASS(ClassGroup = (DreamGUI), BlueprintType, meta = (DisplayName = "ScaleWave Property (UI Effect TextAnimation)"))
class DREAMGUI_API UDreamMeshModifierTextAnimation_ScaleWaveProperty : public UDreamMeshModifierTextAnimation_PropertyWithWave
{
	GENERATED_BODY()
private:
	/** Max scale value for sin wave. Sin function generate values from -1 to 1, so the result will be from -scale to scale. */
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector Scale = FVector::OneVector;
public:
	virtual void ApplyProperty(class UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector GetScale()const { return Scale; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetScale(FVector Value);
};