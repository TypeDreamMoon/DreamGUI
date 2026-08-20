// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamUIPlayTween.h"
#include "DreamUIPlayTween_Params.generated.h"


UCLASS(BlueprintType, meta = (DisplayName = "DreamUIPlayTween Float (Single)"))
class DREAMGUI_API UDreamUIPlayTween_Float : public UDreamUIPlayTween
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Property")
		float From = 0.0f;
	UPROPERTY(EditAnywhere, Category = "Property")
		float To = 1.0f;
	/** parameter float is interpolated value From->To */
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnUpdateValue = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Float);

	virtual void OnUpdate(float progress)override
	{
		OnUpdateValue.FireEvent(FMath::Lerp(From, To, progress));
	}
};

UCLASS(BlueprintType, meta = (DisplayName = "DreamUIPlayTween Float (Double)"))
class DREAMGUI_API UDreamUIPlayTween_Double : public UDreamUIPlayTween
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Property")
		double From = 0.0f;
	UPROPERTY(EditAnywhere, Category = "Property")
		double To = 1.0f;
	/** parameter float is interpolated value From->To */
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnUpdateValue = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Double);

	virtual void OnUpdate(float progress)override
	{
		OnUpdateValue.FireEvent(FMath::Lerp(From, To, progress));
	}
};

UCLASS(BlueprintType, meta = (DisplayName = "DreamUIPlayTween Color"))
class DREAMGUI_API UDreamUIPlayTween_Color : public UDreamUIPlayTween
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Property")
		FColor From = FColor::White;
	UPROPERTY(EditAnywhere, Category = "Property")
		FColor To = FColor::Green;
	/** parameter float is interpolated value From->To */
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnUpdateValue = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Color);

	virtual void OnUpdate(float progress)override
	{
		FColor color;
		color.R = FMath::Lerp(From.R, To.R, progress);
		color.G = FMath::Lerp(From.G, To.G, progress);
		color.B = FMath::Lerp(From.B, To.B, progress);
		color.A = FMath::Lerp(From.A, To.A, progress);
		OnUpdateValue.FireEvent(color);
	}
};

UCLASS(BlueprintType, meta = (DisplayName = "DreamUIPlayTween Int"))
class DREAMGUI_API UDreamUIPlayTween_Int : public UDreamUIPlayTween
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Property")
		int From = 0;
	UPROPERTY(EditAnywhere, Category = "Property")
		int To = 100;
	/** parameter float is interpolated value From->To */
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnUpdateValue = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Int32);

	virtual void OnUpdate(float progress)override
	{
		OnUpdateValue.FireEvent(FMath::Lerp(From, To, progress));
	}
};

UCLASS(BlueprintType, meta = (DisplayName = "DreamUIPlayTween LinearColor"))
class DREAMGUI_API UDreamUIPlayTween_LinearColor : public UDreamUIPlayTween
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Property")
		FLinearColor From = FLinearColor::White;
	UPROPERTY(EditAnywhere, Category = "Property")
		FLinearColor To = FLinearColor::Green;
	/** parameter float is interpolated value From->To */
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnUpdateValue = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::LinearColor);

	virtual void OnUpdate(float progress)override
	{
		OnUpdateValue.FireEvent(FMath::Lerp(From, To, progress));
	}
};

UCLASS(BlueprintType, meta = (DisplayName = "DreamUIPlayTween Quaternion"))
class DREAMGUI_API UDreamUIPlayTween_Quaternion : public UDreamUIPlayTween
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Property")
		FQuat From = FQuat::Identity;
	UPROPERTY(EditAnywhere, Category = "Property")
		FQuat To = FQuat(FVector(0.0f, 0.0f, 1.0f), HALF_PI);
	/** parameter float is interpolated value From->To */
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnUpdateValue = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Quaternion);

	virtual void OnUpdate(float progress)override
	{
		OnUpdateValue.FireEvent(FMath::Lerp(From, To, progress));
	}
};

UCLASS(BlueprintType, meta = (DisplayName = "DreamUIPlayTween Rotator"))
class DREAMGUI_API UDreamUIPlayTween_Rotator : public UDreamUIPlayTween
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Property")
		FRotator From = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, Category = "Property")
		FRotator To = FRotator(0.0f, 0.0f, 90.0f);
	/** parameter float is interpolated value From->To */
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnUpdateValue = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Rotator);

	virtual void OnUpdate(float progress)override
	{
		OnUpdateValue.FireEvent(FMath::Lerp(From, To, progress));
	}
};

UCLASS(BlueprintType, meta = (DisplayName = "DreamUIPlayTween Vector2"))
class DREAMGUI_API UDreamUIPlayTween_Vector2: public UDreamUIPlayTween
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector2D From = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector2D To = FVector2D(1.0f, 1.0f);
	/** parameter float is interpolated value From->To */
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnUpdateValue = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Vector2);

	virtual void OnUpdate(float progress)override
	{
		OnUpdateValue.FireEvent(FMath::Lerp(From, To, progress));
	}
};

UCLASS(BlueprintType, meta = (DisplayName = "DreamUIPlayTween Vector3"))
class DREAMGUI_API UDreamUIPlayTween_Vector3: public UDreamUIPlayTween
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector From = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector To = FVector::OneVector;
	/** parameter float is interpolated value From->To */
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnUpdateValue = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Vector3);

	virtual void OnUpdate(float progress)override
	{
		OnUpdateValue.FireEvent(FMath::Lerp(From, To, progress));
	}
};

UCLASS(BlueprintType, meta = (DisplayName = "DreamUIPlayTween Vector4"))
class DREAMGUI_API UDreamUIPlayTween_Vector4: public UDreamUIPlayTween
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector4 From = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	UPROPERTY(EditAnywhere, Category = "Property")
		FVector4 To = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	/** parameter float is interpolated value From->To */
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnUpdateValue = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Vector4);

	virtual void OnUpdate(float progress)override
	{
		OnUpdateValue.FireEvent(FMath::Lerp(From, To, progress));
	}
};
