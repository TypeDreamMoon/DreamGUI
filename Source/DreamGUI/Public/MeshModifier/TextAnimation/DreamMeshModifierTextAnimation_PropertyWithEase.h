// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "../DreamMeshModifierTextAnimation.h"
#include "DreamTweener.h"
#include "DreamMeshModifierTextAnimation_PropertyWithEase.generated.h"

UCLASS(ClassGroup = (DreamGUI), Abstract, BlueprintType)
class DREAMGUI_API UDreamMeshModifierTextAnimation_PropertyWithEase : public UDreamMeshModifierTextAnimation_Property
{
	GENERATED_BODY()
private:
	friend class FUIEffectTextAnimationPropertyCustomization;
	/** Animation type, same as DreamTween ease */
	UPROPERTY(EditAnywhere, Category = "Property")
		EDreamTweenEase EaseType = EDreamTweenEase::InOutSine;
	/** Only valid if easeType = CurveFloat. Use CurveFloat to control the animation. */
	UPROPERTY(EditAnywhere, Category = "Property", meta = (EditCondition = "easeType == EDreamTweenEase::CurveFloat"))
		TObjectPtr<UCurveFloat> EaseCurve;
	FDreamTweenFunction EaseFunc;
	float EaseCurveFunction(float c, float b, float t, float d);
protected:
	const FDreamTweenFunction& GetEaseFunction();
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamTweenEase GetEaseType()const { return EaseType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UCurveFloat* GetCurveFloat()const { return EaseCurve; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEaseType(EDreamTweenEase Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEaseCurve(UCurveFloat* Value);
};

UCLASS(ClassGroup = (DreamGUI), BlueprintType, meta = (DisplayName = "Position Property (UI Effect TextAnimation)"))
class DREAMGUI_API UDreamMeshModifierTextAnimation_PositionProperty : public UDreamMeshModifierTextAnimation_PropertyWithEase
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector Position;
public:
	virtual void ApplyProperty(class UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector GetPosition()const { return Position; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetPosition(FVector Value);
};

UCLASS(ClassGroup = (DreamGUI), BlueprintType, meta = (DisplayName = "PositionRandom Property (UI Effect TextAnimation)"))
class DREAMGUI_API UDreamMeshModifierTextAnimation_PositionRandomProperty : public UDreamMeshModifierTextAnimation_PropertyWithEase
{
	GENERATED_BODY()
private:
	//random seed
	UPROPERTY(EditAnywhere, Category = "Property")
		int Seed = 0;
	//random min
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector Min = FVector(0, 0, 0);
	//random max
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector Max = FVector(0, 10, 0);
public:
	virtual void ApplyProperty(class UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		int GetSeed()const { return Seed; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector GetMin()const { return Min; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector GetMax()const { return Max; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSeed(int Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetMin(FVector Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetMax(FVector Value);
};

UCLASS(ClassGroup = (DreamGUI), BlueprintType, meta = (DisplayName = "Rotation Property (UI Effect TextAnimation)"))
class DREAMGUI_API UDreamMeshModifierTextAnimation_RotationProperty : public UDreamMeshModifierTextAnimation_PropertyWithEase
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, Category = "Property")
		FRotator rotator;
public:
	virtual void ApplyProperty(class UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FRotator GetRotator()const { return rotator; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRotator(FRotator value);
};

UCLASS(ClassGroup = (DreamGUI), BlueprintType, meta = (DisplayName = "RotationRandom Property (UI Effect TextAnimation)"))
class DREAMGUI_API UDreamMeshModifierTextAnimation_RotationRandomProperty : public UDreamMeshModifierTextAnimation_PropertyWithEase
{
	GENERATED_BODY()
private:
	//random seed
	UPROPERTY(EditAnywhere, Category = "Property")
		int Seed = 0;
	//random min
	UPROPERTY(EditAnywhere, Category = "Property")
		FRotator Min = FRotator(0, 0, 0);
	//random max
	UPROPERTY(EditAnywhere, Category = "Property")
		FRotator Max = FRotator(0, 90, 0);
public:
	virtual void ApplyProperty(class UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		int GetSeed()const { return Seed; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FRotator GetMin()const { return Min; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FRotator GetMax()const { return Max; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSeed(int Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetMin(FRotator Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetMax(FRotator Value);
};

UCLASS(ClassGroup = (DreamGUI), BlueprintType, meta = (DisplayName = "Scale Property (UI Effect TextAnimation)"))
class DREAMGUI_API UDreamMeshModifierTextAnimation_ScaleProperty : public UDreamMeshModifierTextAnimation_PropertyWithEase
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector Scale = FVector::OneVector;
public:
	virtual void ApplyProperty(class UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector GetScale()const { return Scale; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetScale(FVector Value);
};

UCLASS(ClassGroup = (DreamGUI), BlueprintType, meta = (DisplayName = "ScaleRandom Property (UI Effect TextAnimation)"))
class DREAMGUI_API UDreamMeshModifierTextAnimation_ScaleRandomProperty : public UDreamMeshModifierTextAnimation_PropertyWithEase
{
	GENERATED_BODY()
private:
	//random seed
	UPROPERTY(EditAnywhere, Category = "Property")
		int Seed = 0;
	//random min
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector Min = FVector(1, 1, 1);
	//random max
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector Max = FVector(2, 2, 2);
public:
	virtual void ApplyProperty(class UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		int GetSeed()const { return Seed; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector GetMin()const { return Min; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FVector GetMax()const { return Max; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSeed(int Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetMin(FVector Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetMax(FVector Value);
};

UCLASS(ClassGroup = (DreamGUI), BlueprintType, meta = (DisplayName = "Alpha Property (UI Effect TextAnimation)"))
class DREAMGUI_API UDreamMeshModifierTextAnimation_AlphaProperty : public UDreamMeshModifierTextAnimation_PropertyWithEase
{
	GENERATED_BODY()
private:
	/** Target alpha value, 0-1 range. */
	UPROPERTY(EditAnywhere, Category = "Property", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float Alpha;
public:
	virtual void ApplyProperty(class UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetAlpha()const { return Alpha; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetAlpha(float Value);
};

UCLASS(ClassGroup = (DreamGUI), BlueprintType, meta = (DisplayName = "Color Property (UI Effect TextAnimation)"))
class DREAMGUI_API UDreamMeshModifierTextAnimation_ColorProperty : public UDreamMeshModifierTextAnimation_PropertyWithEase
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, Category = "Property")
		FColor Color = FColor::Green;
	/** Convert color to HSV(Hue, Saturate, Value) and interpolate, then convert the result back. Interpolate two colors in HSV may look better. */
	UPROPERTY(EditAnywhere, Category = "Property")
		bool bUseHSV = true;
public:
	virtual void ApplyProperty(class UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FColor GetColor()const { return Color; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetUseHSV()const { return bUseHSV; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetUseHSV(bool Value);
};

UCLASS(ClassGroup = (DreamGUI), BlueprintType, meta = (DisplayName = "ColorRandom Property (UI Effect TextAnimation)"))
class DREAMGUI_API UDreamMeshModifierTextAnimation_ColorRandomProperty : public UDreamMeshModifierTextAnimation_PropertyWithEase
{
	GENERATED_BODY()
private:
	/** Random seed. */
	UPROPERTY(EditAnywhere, Category = "Property")
		int Seed = 0;
	/** Random min. */
	UPROPERTY(EditAnywhere, Category = "Property")
		FColor Min = FColor::Green;
	/** Random max. */
	UPROPERTY(EditAnywhere, Category = "Property")
		FColor Max = FColor::Red;
	/** convert color to linear hsv, interpolate, and convert back to color */
	UPROPERTY(EditAnywhere, Category = "Property")
		bool bUseHSV = true;
public:
	virtual void ApplyProperty(class UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		int GetSeed()const { return Seed; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FColor GetMin()const { return Min; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FColor GetMax()const { return Max; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetUseHSV()const { return bUseHSV; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSeed(int Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetMin(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetMax(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetUseHSV(bool Value);
};
