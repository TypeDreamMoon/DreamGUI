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
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetFrequency()const { return Speed; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetFrequency(float Value);
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