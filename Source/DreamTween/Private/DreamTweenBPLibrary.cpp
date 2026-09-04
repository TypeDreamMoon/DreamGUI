// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamTweenBPLibrary.h"
#include "DreamTween.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/MeshComponent.h"


UDreamTweener* UDreamTweenBPLibrary::FloatTo(UObject* WorldContextObject, const FDreamTweenFloatSetterDynamic& setter, float startValue, float endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenFloatGetterFunction::CreateLambda([startValue]
	{
		return startValue;
	}), FDreamTweenFloatSetterFunction::CreateLambda([setter](float value)
	{
		if (setter.IsBound())
			setter.Execute(value);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::DoubleTo(UObject* WorldContextObject, const FDreamTweenDoubleSetterDynamic& setter, double startValue, double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenDoubleGetterFunction::CreateLambda([startValue]
	{
		return startValue;
	}), FDreamTweenDoubleSetterFunction::CreateLambda([setter](auto value)
	{
		if (setter.IsBound())
			setter.Execute(value);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::IntTo(UObject* WorldContextObject, const FDreamTweenIntSetterDynamic& setter, int startValue, int endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenIntGetterFunction::CreateLambda([startValue]
	{
		return startValue;
	}), FDreamTweenIntSetterFunction::CreateLambda([setter](int value)
	{
		if (setter.IsBound())
			setter.Execute(value);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::Vector2To(UObject* WorldContextObject, const FDreamTweenVector2SetterDynamic& setter, FVector2D startValue, FVector2D endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenVector2DGetterFunction::CreateLambda([startValue]
	{
		return startValue;
	}), FDreamTweenVector2DSetterFunction::CreateLambda([setter](const FVector2D& value)
	{
		if (setter.IsBound())
			setter.Execute(value);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::Vector3To(UObject* WorldContextObject, const FDreamTweenVector3SetterDynamic& setter, FVector startValue, FVector endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenVectorGetterFunction::CreateLambda([startValue]
	{
		return startValue;
	}), FDreamTweenVectorSetterFunction::CreateLambda([setter](const FVector& value)
	{
		if (setter.IsBound())
			setter.Execute(value);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::Vector4To(UObject* WorldContextObject, const FDreamTweenVector4SetterDynamic& setter, FVector4 startValue, FVector4 endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenVector4GetterFunction::CreateLambda([startValue]
	{
		return startValue;
	}), FDreamTweenVector4SetterFunction::CreateLambda([setter](const FVector4& value)
	{
		if (setter.IsBound())
			setter.Execute(value);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::ColorTo(UObject* WorldContextObject, const FDreamTweenColorSetterDynamic& setter, FColor startValue, FColor endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenColorGetterFunction::CreateLambda([startValue]
	{
		return startValue;
	}), FDreamTweenColorSetterFunction::CreateLambda([setter](const FColor& value)
	{
		if (setter.IsBound())
			setter.Execute(value);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::LinearColorTo(UObject* WorldContextObject, const FDreamTweenLinearColorSetterDynamic& setter, FLinearColor startValue, FLinearColor endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenLinearColorGetterFunction::CreateLambda([startValue]
	{
		return startValue;
	}), FDreamTweenLinearColorSetterFunction::CreateLambda([setter](const FLinearColor& value)
	{
		if (setter.IsBound())
			setter.Execute(value);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::QuaternionTo(UObject* WorldContextObject, const FDreamTweenQuaternionSetterDynamic& setter, FQuat startValue, FQuat endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenQuaternionGetterFunction::CreateLambda([startValue]
	{
		return startValue;
	}), FDreamTweenQuaternionSetterFunction::CreateLambda([setter](const FQuat& value)
	{
		if (setter.IsBound())
			setter.Execute(value);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::RotatorTo(UObject* WorldContextObject, const FDreamTweenRotatorSetterDynamic& setter, FRotator startValue, FRotator endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenRotatorGetterFunction::CreateLambda([startValue]
	{
		return startValue;
	}), FDreamTweenRotatorSetterFunction::CreateLambda([setter](const FRotator& value)
	{
		if (setter.IsBound())
			setter.Execute(value);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}

#pragma region PositionXYZ
UDreamTweener* UDreamTweenBPLibrary::LocalPositionXTo(USceneComponent* target, double endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalPositionXTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenDoubleGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetRelativeLocation().X;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(target, [target](auto value) {
		auto location = target->GetRelativeLocation();
		location.X = value;
		target->SetRelativeLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::LocalPositionYTo(USceneComponent* target, double endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalPositionYTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenDoubleGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetRelativeLocation().Y;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(target, [target](auto value) {
		auto location = target->GetRelativeLocation();
		location.Y = value;
		target->SetRelativeLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::LocalPositionZTo(USceneComponent* target, double endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalPositionZTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenDoubleGetterFunction::CreateWeakLambda(target, [target] 
	{
		return target->GetRelativeLocation().Z;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(target, [=](auto value) {
		auto location = target->GetRelativeLocation();
		location.Z = value;
		target->SetRelativeLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::LocalPositionXTo_Sweep(USceneComponent* target, double endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalPositionXTo_Sweep] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenDoubleGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetRelativeLocation().X;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(target, [target, sweepHitResultStorage = MakeShared<FHitResult>(), sweep, teleport](auto value) {
		auto location = target->GetRelativeLocation();
		location.X = value;
		target->SetRelativeLocation(location, sweep, sweep ? &sweepHitResultStorage.Get() : nullptr, TeleportFlagToEnum(teleport));
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::LocalPositionYTo_Sweep(USceneComponent* target, double endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalPositionYTo_Sweep] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenDoubleGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetRelativeLocation().Y;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(target, [target, sweepHitResultStorage = MakeShared<FHitResult>(), sweep, teleport](auto value) {
		auto location = target->GetRelativeLocation();
		location.Y = value;
		target->SetRelativeLocation(location, sweep, sweep ? &sweepHitResultStorage.Get() : nullptr, TeleportFlagToEnum(teleport));
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::LocalPositionZTo_Sweep(USceneComponent* target, double endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalPositionZTo_Sweep] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenDoubleGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetRelativeLocation().Z;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(target, [target, sweepHitResultStorage = MakeShared<FHitResult>(), sweep, teleport](auto value) {
		auto location = target->GetRelativeLocation();
		location.Z = value;
		target->SetRelativeLocation(location, sweep, sweep ? &sweepHitResultStorage.Get() : nullptr, TeleportFlagToEnum(teleport));
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}



UDreamTweener* UDreamTweenBPLibrary::WorldPositionXTo(USceneComponent* target, double endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldPositionXTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenDoubleGetterFunction::CreateWeakLambda(target, [target] 
	{
		return target->GetComponentLocation().X;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(target, [target](auto value) {
		auto location = target->GetComponentLocation();
		location.X = value;
		target->SetWorldLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::WorldPositionYTo(USceneComponent* target, double endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldPositionYTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenDoubleGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetComponentLocation().Y;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(target, [target](auto value) {
		auto location = target->GetComponentLocation();
		location.Y = value;
		target->SetWorldLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::WorldPositionZTo(USceneComponent* target, double endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldPositionZTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenDoubleGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetComponentLocation().Z;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(target, [target](auto value) {
		auto location = target->GetComponentLocation();
		location.Z = value;
		target->SetWorldLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::WorldPositionXTo_Sweep(USceneComponent* target, double endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldPositionXTo_Sweep] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenDoubleGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetComponentLocation().X;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(target, [target, sweepHitResultStorage = MakeShared<FHitResult>(), sweep, teleport](auto value) {
		auto location = target->GetComponentLocation();
		location.X = value;
		target->SetWorldLocation(location, sweep, sweep ? &sweepHitResultStorage.Get() : nullptr, TeleportFlagToEnum(teleport));
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::WorldPositionYTo_Sweep(USceneComponent* target, double endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldPositionYTo_Sweep] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenDoubleGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetComponentLocation().Y;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(target, [target, sweepHitResultStorage = MakeShared<FHitResult>(), sweep, teleport](auto value) {
		auto location = target->GetComponentLocation();
		location.Y = value;
		target->SetWorldLocation(location, sweep, sweep ? &sweepHitResultStorage.Get() : nullptr, TeleportFlagToEnum(teleport));
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::WorldPositionZTo_Sweep(USceneComponent* target, double endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldPositionZTo_Sweep] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenDoubleGetterFunction::CreateWeakLambda(target, [target] 
	{
		return target->GetComponentLocation().Z;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(target, [target, sweepHitResultStorage = MakeShared<FHitResult>(), sweep, teleport](auto value) {
		auto location = target->GetComponentLocation();
		location.Z = value;
		target->SetWorldLocation(location, sweep, sweep ? &sweepHitResultStorage.Get() : nullptr, TeleportFlagToEnum(teleport));
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
#pragma endregion PositionXYZ




#pragma region Position
UDreamTweener* UDreamTweenBPLibrary::LocalPositionTo(USceneComponent* target, FVector endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalPositionTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target
	, FDreamTweenPositionGetterFunction::CreateUObject(target, &USceneComponent::GetRelativeLocation)
	, FDreamTweenPositionSetterFunction::CreateUObject(target, &USceneComponent::SetRelativeLocation)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::WorldPositionTo(USceneComponent* target, FVector endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldPositionTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target
	, FDreamTweenPositionGetterFunction::CreateUObject(target, &USceneComponent::GetComponentLocation)
	, FDreamTweenPositionSetterFunction::CreateUObject(target, &USceneComponent::SetWorldLocation)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::LocalPositionTo_Sweep(USceneComponent* target, FVector endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalPositionTo_Sweep] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target
	, FDreamTweenVectorGetterFunction::CreateUObject(target, &USceneComponent::GetRelativeLocation)
	, FDreamTweenPositionSetterFunction::CreateUObject(target, &USceneComponent::SetRelativeLocation)
	, endValue, duration, sweep, sweep ? &sweepHitResult : nullptr, TeleportFlagToEnum(teleport));
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::WorldPositionTo_Sweep(USceneComponent* target, FVector endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldPositionTo_Sweep] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target
	, FDreamTweenPositionGetterFunction::CreateUObject(target, &USceneComponent::GetComponentLocation)
	, FDreamTweenPositionSetterFunction::CreateUObject(target, &USceneComponent::SetWorldLocation)
	, endValue, duration, sweep, sweep ? &sweepHitResult : nullptr, TeleportFlagToEnum(teleport));
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
#pragma endregion Position



UDreamTweener* UDreamTweenBPLibrary::LocalScaleTo(USceneComponent* target, FVector endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalScaleTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target
	, FDreamTweenVectorGetterFunction::CreateUObject(target, &USceneComponent::GetRelativeScale3D)
	, FDreamTweenVectorSetterFunction::CreateUObject(target, &USceneComponent::SetRelativeScale3D)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}


#pragma region Rotation
UDreamTweener* UDreamTweenBPLibrary::LocalRotateEulerAngleTo(USceneComponent* target, FVector eulerAngle, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalRotateEulerAngleTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenRotationQuatGetterFunction::CreateWeakLambda(target, [target]
	{ 
		return target->GetRelativeRotationCache().GetCachedQuat();
	}), FDreamTweenRotationQuatSetterFunction::CreateUObject(target, &USceneComponent::SetRelativeRotation)
	, eulerAngle, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::LocalRotationQuaternionTo(USceneComponent* target, const FQuat& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalRotationTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenRotationQuatGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetRelativeRotationCache().GetCachedQuat();
	}), FDreamTweenRotationQuatSetterFunction::CreateUObject(target, &USceneComponent::SetRelativeRotation)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::LocalRotateEulerAngleTo_Sweep(USceneComponent* target, FVector eulerAngle, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalRotateEulerAngleTo_Sweep] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenRotationQuatGetterFunction::CreateWeakLambda(target, [target]
	{ 
		return target->GetRelativeRotationCache().GetCachedQuat();
	}), FDreamTweenRotationQuatSetterFunction::CreateUObject(target, &USceneComponent::SetRelativeRotation)
	, eulerAngle, duration, sweep, sweep ? &sweepHitResult : nullptr, TeleportFlagToEnum(teleport));
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::LocalRotationQuaternionTo_Sweep(USceneComponent* target, const FQuat& endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalRotationTo_Sweep] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenRotationQuatGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetRelativeRotationCache().GetCachedQuat();
	}), FDreamTweenRotationQuatSetterFunction::CreateUObject(target, &USceneComponent::SetRelativeRotation)
	, endValue, duration, sweep, sweep ? &sweepHitResult : nullptr, TeleportFlagToEnum(teleport));
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::LocalRotatorTo(USceneComponent* target, FRotator endValue, bool shortestPath, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalRotatorTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	if (shortestPath)
	{
		return LocalRotationQuaternionTo(target, endValue.Quaternion(), duration, delay, ease);
	}
	else
	{
		auto Tweener = UDreamTweenManager::To(target
		, FDreamTweenRotatorGetterFunction::CreateUObject(target, &USceneComponent::GetRelativeRotation)
		, FDreamTweenRotatorSetterFunction::CreateWeakLambda(target, [target] (FRotator value)
		{
			target->SetRelativeRotation(value);
		}), endValue, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->SetEase(ease);
		}
		return Tweener;
	}
}
UDreamTweener* UDreamTweenBPLibrary::LocalRotatorTo_Sweep(USceneComponent* target, FRotator endValue, bool shortestPath, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::LocalRotatorTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	if (shortestPath)
	{
		return LocalRotationQuaternionTo_Sweep(target, endValue.Quaternion(), sweepHitResult, sweep, teleport, duration, delay, ease);
	}
	else
	{
		auto Tweener = UDreamTweenManager::To(target
		, FDreamTweenRotatorGetterFunction::CreateUObject(target, &USceneComponent::GetRelativeRotation)
		, FDreamTweenRotatorSetterFunction::CreateWeakLambda(target, [target, sweepHitResultStorage = MakeShared<FHitResult>(), sweep, teleport](FRotator value)
		{
			target->SetRelativeRotation(value, sweep, sweep ? &sweepHitResultStorage.Get() : nullptr, TeleportFlagToEnum(teleport));
		}), endValue, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->SetEase(ease);
		}
		return Tweener;
	}
}



UDreamTweener* UDreamTweenBPLibrary::WorldRotateEulerAngleTo(USceneComponent* target, FVector eulerAngle, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldRotateEulerAngleTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenRotationQuatGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetComponentRotation().Quaternion();
	}), FDreamTweenRotationQuatSetterFunction::CreateUObject(target, &USceneComponent::SetWorldRotation)
	, eulerAngle, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::WorldRotationQuaternionTo(USceneComponent* target, const FQuat& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldRotationTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenRotationQuatGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetComponentRotation().Quaternion();
	}), FDreamTweenRotationQuatSetterFunction::CreateUObject(target, &USceneComponent::SetWorldRotation)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::WorldRotateEulerAngleTo_Sweep(USceneComponent* target, FVector eulerAngle, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldRotateEulerAngleTo_Sweep] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenRotationQuatGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetComponentRotation().Quaternion();
	}), FDreamTweenRotationQuatSetterFunction::CreateUObject(target, &USceneComponent::SetWorldRotation)
	, eulerAngle, duration, sweep, sweep ? &sweepHitResult : nullptr, TeleportFlagToEnum(teleport));
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::WorldRotationQuaternionTo_Sweep(USceneComponent* target, const FQuat& endValue, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldRotationTo_Sweep] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(target, FDreamTweenRotationQuatGetterFunction::CreateWeakLambda(target, [target]
	{
		return target->GetComponentRotation().Quaternion();
	}), FDreamTweenRotationQuatSetterFunction::CreateUObject(target, &USceneComponent::SetWorldRotation)
	, endValue, duration, sweep, sweep ? &sweepHitResult : nullptr, TeleportFlagToEnum(teleport));
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::WorldRotatorTo(USceneComponent* target, FRotator endValue, bool shortestPath, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldRotatorTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	if (shortestPath)
	{
		return WorldRotationQuaternionTo(target, endValue.Quaternion(), duration, delay, ease);
	}
	else
	{
		auto Tweener = UDreamTweenManager::To(target
		, FDreamTweenRotatorGetterFunction::CreateUObject(target, &USceneComponent::GetComponentRotation)
		, FDreamTweenRotatorSetterFunction::CreateWeakLambda(target, [target](FRotator value)
		{
			target->SetWorldRotation(value);
		}), endValue, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->SetEase(ease);
		}
		return Tweener;
	}
}
UDreamTweener* UDreamTweenBPLibrary::WorldRotatorTo_Sweep(USceneComponent* target, FRotator endValue, bool shortestPath, FHitResult& sweepHitResult, bool sweep, bool teleport, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::WorldRotatorTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	if (shortestPath)
	{
		return WorldRotationQuaternionTo_Sweep(target, endValue.Quaternion(), sweepHitResult, sweep, teleport, duration, delay, ease);
	}
	else
	{
		auto Tweener = UDreamTweenManager::To(target
		, FDreamTweenRotatorGetterFunction::CreateUObject(target, &USceneComponent::GetComponentRotation)
		, FDreamTweenRotatorSetterFunction::CreateWeakLambda(target, [target, sweepHitResultStorage = MakeShared<FHitResult>(), sweep, teleport](FRotator value)
		{
			target->SetWorldRotation(value, sweep, sweep ? &sweepHitResultStorage.Get() : nullptr, TeleportFlagToEnum(teleport));
		}), endValue, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->SetEase(ease);
		}
		return Tweener;
	}
}
#pragma endregion Rotation


#pragma region Material
UDreamTweener* UDreamTweenBPLibrary::MaterialScalarParameterTo(UObject* WorldContextObject, UMaterialInstanceDynamic* target, FName parameterName, float endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(WorldContextObject))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::MaterialScalarParameterTo] WorldContextObject is not valid!"));
		return nullptr;
	}
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::MaterialScalarParameterTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	float startValue = 0;
	int32 parameterIndex = 0;
	if (target->GetScalarParameterValue(parameterName, startValue))
	{
		target->InitializeScalarParameterAndGetIndex(parameterName, startValue, parameterIndex);
	}
	else
	{
		UE_LOG(DreamTween, Warning, TEXT("[UDreamTweenBPLibrary::MaterialScalarParameterTo]GetScalarParameterValue:%s error!"), *(parameterName.ToString()));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenMaterialScalarGetterFunction::CreateWeakLambda(target, [target, parameterName](float& result)
	{
		return target->GetScalarParameterValue(parameterName, result);
	}), FDreamTweenMaterialScalarSetterFunction::CreateUObject(target, &UMaterialInstanceDynamic::SetScalarParameterByIndex)
	, endValue, duration, parameterIndex);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::MaterialVectorParameterTo(UObject* WorldContextObject, UMaterialInstanceDynamic* target, FName parameterName, FLinearColor endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(WorldContextObject))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::MaterialVectorParameterTo] WorldContextObject is not valid!"));
		return nullptr;
	}
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::MaterialVectorParameterTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	FLinearColor startValue = FLinearColor();
	int32 parameterIndex = 0;
	if (target->GetVectorParameterValue(parameterName, startValue))
	{
		target->InitializeVectorParameterAndGetIndex(parameterName, startValue, parameterIndex);
	}
	else
	{
		UE_LOG(DreamTween, Warning, TEXT("[UDreamTweenBPLibrary::MaterialVectorParameterTo]GetVectorParameterValue:%s error!"), *(parameterName.ToString()));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenMaterialVectorGetterFunction::CreateWeakLambda(target, [target, parameterName](FLinearColor& result)
	{
		return target->GetVectorParameterValue(parameterName, result);
	}), FDreamTweenMaterialVectorSetterFunction::CreateUObject(target, &UMaterialInstanceDynamic::SetVectorParameterByIndex)
	, endValue, duration, parameterIndex);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}

UDreamTweener* UDreamTweenBPLibrary::MeshMaterialScalarParameterTo(UPrimitiveComponent* target, int materialIndex, FName parameterName, float endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::MeshMaterialScalarParameterTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	float startValue = 0;
	auto material = target->CreateAndSetMaterialInstanceDynamic(materialIndex);
	if (!IsValid(material))
	{
		//A slot the mesh does not have gives back nothing at all, not an empty material.
		UE_LOG(DreamTween, Warning, TEXT("[UDreamTweenBPLibrary::MeshMaterialScalarParameterTo]no material at index:%d on:%s"), materialIndex, *GetPathNameSafe(target));
		return nullptr;
	}
	int32 parameterIndex = 0;
	if (material->GetScalarParameterValue(parameterName, startValue))
	{
		material->InitializeScalarParameterAndGetIndex(parameterName, startValue, parameterIndex);
	}
	else
	{
		UE_LOG(DreamTween, Warning, TEXT("[UDreamTweenBPLibrary::MaterialScalarParameterTo]GetScalarParameterValue:%s error!"), *(parameterName.ToString()));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(material, FDreamTweenMaterialScalarGetterFunction::CreateWeakLambda(material, [material, parameterName](float& result)
	{
		return material->GetScalarParameterValue(parameterName, result);
	}), FDreamTweenMaterialScalarSetterFunction::CreateUObject(material, &UMaterialInstanceDynamic::SetScalarParameterByIndex)
	, endValue, duration, parameterIndex);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::MeshMaterialVectorParameterTo(UPrimitiveComponent* target, int materialIndex, FName parameterName, FLinearColor endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[UDreamTweenBPLibrary::MeshMaterialVectorParameterTo] target is not valid:%s"), *GetPathNameSafe(target));
		return nullptr;
	}
	FLinearColor startValue = FLinearColor();
	auto material = target->CreateAndSetMaterialInstanceDynamic(materialIndex);
	if (!IsValid(material))
	{
		//A slot the mesh does not have gives back nothing at all, not an empty material.
		UE_LOG(DreamTween, Warning, TEXT("[UDreamTweenBPLibrary::MeshMaterialVectorParameterTo]no material at index:%d on:%s"), materialIndex, *GetPathNameSafe(target));
		return nullptr;
	}
	int32 parameterIndex = 0;
	if (material->GetVectorParameterValue(parameterName, startValue))
	{
		material->InitializeVectorParameterAndGetIndex(parameterName, startValue, parameterIndex);
	}
	else
	{
		UE_LOG(DreamTween, Warning, TEXT("[UDreamTweenBPLibrary::MaterialVectorParameterTo]GetVectorParameterValue:%s error!"), *(parameterName.ToString()));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(material, FDreamTweenMaterialVectorGetterFunction::CreateWeakLambda(material, [material, parameterName](FLinearColor& result)
	{
		return material->GetVectorParameterValue(parameterName, result);
	}), FDreamTweenMaterialVectorSetterFunction::CreateUObject(material, &UMaterialInstanceDynamic::SetVectorParameterByIndex)
	, endValue, duration, parameterIndex);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
	}
	return Tweener;
}
#pragma endregion

#pragma region UMG
#include "Components/CanvasPanelSlot.h"
UDreamTweener* UDreamTweenBPLibrary::UMG_CanvasPanelSlot_PositionTo(UObject* WorldContextObject, UCanvasPanelSlot* target, const FVector2D& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject
	, FDreamTweenVector2DGetterFunction::CreateUObject(target, &UCanvasPanelSlot::GetPosition)
	, FDreamTweenVector2DSetterFunction::CreateUObject(target, &UCanvasPanelSlot::SetPosition)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::UMG_CanvasPanelSlot_SizeTo(UObject* WorldContextObject, class UCanvasPanelSlot* target, const FVector2D& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject
	, FDreamTweenVector2DGetterFunction::CreateUObject(target, &UCanvasPanelSlot::GetSize)
	, FDreamTweenVector2DSetterFunction::CreateUObject(target, &UCanvasPanelSlot::SetSize)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
#include "Components/HorizontalBoxSlot.h"
UDreamTweener* UDreamTweenBPLibrary::UMG_HorizontalBoxSlot_PaddingTo(UObject* WorldContextObject, UHorizontalBoxSlot* target, const FMargin& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto endValueVector4 = FVector4(endValue.Left, endValue.Top, endValue.Right, endValue.Bottom);
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenVector4GetterFunction::CreateWeakLambda(target, [=] 
	{
		auto padding = target->GetPadding();
		return FVector4(padding.Left, padding.Top, padding.Right, padding.Bottom);
	}), FDreamTweenVector4SetterFunction::CreateWeakLambda(target, [=](const FVector4& value) {
		target->SetPadding(FMargin(value.X, value.Y, value.Z, value.W));
	}), endValueVector4, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
#include "Components/VerticalBoxSlot.h"
UDreamTweener* UDreamTweenBPLibrary::UMG_VerticalBoxSlot_PaddingTo(UObject* WorldContextObject, UVerticalBoxSlot* target, const FMargin& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto endValueVector4 = FVector4(endValue.Left, endValue.Top, endValue.Right, endValue.Bottom);
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenVector4GetterFunction::CreateWeakLambda(target, [=] 
	{
		auto padding = target->GetPadding();
		return FVector4(padding.Left, padding.Top, padding.Right, padding.Bottom);
	}), FDreamTweenVector4SetterFunction::CreateWeakLambda(target, [=](const FVector4& value) {
		target->SetPadding(FMargin(value.X, value.Y, value.Z, value.W));
	}), endValueVector4, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
#include "Components/OverlaySlot.h"
UDreamTweener* UDreamTweenBPLibrary::UMG_OverlaySlot_PaddingTo(UObject* WorldContextObject, UOverlaySlot* target, const FMargin& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto endValueVector4 = FVector4(endValue.Left, endValue.Top, endValue.Right, endValue.Bottom);
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenVector4GetterFunction::CreateWeakLambda(target, [=]
	{
		auto padding = target->GetPadding();
		return FVector4(padding.Left, padding.Top, padding.Right, padding.Bottom);
	}), FDreamTweenVector4SetterFunction::CreateWeakLambda(target, [=](const FVector4& value) {
		target->SetPadding(FMargin(value.X, value.Y, value.Z, value.W));
	}), endValueVector4, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
#include "Components/ButtonSlot.h"
UDreamTweener* UDreamTweenBPLibrary::UMG_ButtonSlot_PaddingTo(UObject* WorldContextObject, UButtonSlot* target, const FMargin& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto endValueVector4 = FVector4(endValue.Left, endValue.Top, endValue.Right, endValue.Bottom);
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenVector4GetterFunction::CreateWeakLambda(target, [=]
	{
		auto padding = target->GetPadding();
		return FVector4(padding.Left, padding.Top, padding.Right, padding.Bottom);
	}), FDreamTweenVector4SetterFunction::CreateWeakLambda(target, [=](const FVector4& value) {
		target->SetPadding(FMargin(value.X, value.Y, value.Z, value.W));
	}), endValueVector4, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
#include "Components/BorderSlot.h"
UDreamTweener* UDreamTweenBPLibrary::UMG_BorderSlot_PaddingTo(UObject* WorldContextObject, UBorderSlot* target, const FMargin& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto endValueVector4 = FVector4(endValue.Left, endValue.Top, endValue.Right, endValue.Bottom);
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenVector4GetterFunction::CreateWeakLambda(target, [=]
	{
		auto padding = target->GetPadding();
		return FVector4(padding.Left, padding.Top, padding.Right, padding.Bottom);
	}), FDreamTweenVector4SetterFunction::CreateWeakLambda(target, [=](const FVector4& value) {
		target->SetPadding(FMargin(value.X, value.Y, value.Z, value.W));
	}), endValueVector4, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}

#include "Components/Widget.h"
UDreamTweener* UDreamTweenBPLibrary::UMG_RenderTransform_TranslationTo(UObject* WorldContextObject, class UWidget* target, const FVector2D& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenVector2DGetterFunction::CreateWeakLambda(target, [=] 
	{
		return target->GetRenderTransform().Translation;
	}), FDreamTweenVector2DSetterFunction::CreateUObject(target, &UWidget::SetRenderTranslation)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::UMG_RenderTransform_AngleTo(UObject* WorldContextObject, UWidget* target, float endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject
	, FDreamTweenFloatGetterFunction::CreateUObject(target, &UWidget::GetRenderTransformAngle)
	, FDreamTweenFloatSetterFunction::CreateUObject(target, &UWidget::SetRenderTransformAngle)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::UMG_RenderTransform_ScaleTo(UObject* WorldContextObject, UWidget* target, const FVector2D& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenVector2DGetterFunction::CreateWeakLambda(target, [=] 
	{
		return target->GetRenderTransform().Scale;
	}), FDreamTweenVector2DSetterFunction::CreateUObject(target, &UWidget::SetRenderScale)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::UMG_RenderTransform_ShearTo(UObject* WorldContextObject, UWidget* target, const FVector2D& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenVector2DGetterFunction::CreateWeakLambda(target, [=] 
	{
		return target->GetRenderTransform().Shear;
	}), FDreamTweenVector2DSetterFunction::CreateUObject(target, &UWidget::SetRenderShear)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
UDreamTweener* UDreamTweenBPLibrary::UMG_RenderOpacityTo(UObject* WorldContextObject, UWidget* target, float endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject
	, FDreamTweenFloatGetterFunction::CreateUObject(target, &UWidget::GetRenderOpacity)
	, FDreamTweenFloatSetterFunction::CreateUObject(target, &UWidget::SetRenderOpacity)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
#include "Blueprint/UserWidget.h"
UDreamTweener* UDreamTweenBPLibrary::UMG_UserWidget_ColorAndOpacityTo(UObject* WorldContextObject, UUserWidget* target, const FLinearColor& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenLinearColorGetterFunction::CreateWeakLambda(target, [=] 
	{
		return target->GetColorAndOpacity();
	}), FDreamTweenLinearColorSetterFunction::CreateUObject(target, &UUserWidget::SetColorAndOpacity)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
#include "Components/Image.h"
UDreamTweener* UDreamTweenBPLibrary::UMG_Image_ColorAndOpacityTo(UObject* WorldContextObject, UImage* target, const FLinearColor& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenLinearColorGetterFunction::CreateWeakLambda(target, [=] 
	{
		return target->GetColorAndOpacity();
	}), FDreamTweenLinearColorSetterFunction::CreateUObject(target, &UImage::SetColorAndOpacity)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
#include "Components/Button.h"
UDreamTweener* UDreamTweenBPLibrary::UMG_Button_ColorAndOpacityTo(UObject* WorldContextObject, UButton* target, const FLinearColor& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject, FDreamTweenLinearColorGetterFunction::CreateWeakLambda(target, [=] 
	{
		return target->GetColorAndOpacity();
	}), FDreamTweenLinearColorSetterFunction::CreateUObject(target, &UButton::SetColorAndOpacity)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
#include "Components/Border.h"
UDreamTweener* UDreamTweenBPLibrary::UMG_Border_ContentColorAndOpacityTo(UObject* WorldContextObject, UBorder* target, const FLinearColor& endValue, float duration, float delay, EDreamTweenEase ease)
{
	if (!IsValid(target))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s] target is not valid:%s"), ANSI_TO_TCHAR(__FUNCTION__), *GetPathNameSafe(target));
		return nullptr;
	}
	auto Tweener = UDreamTweenManager::To(WorldContextObject
	, FDreamTweenLinearColorGetterFunction::CreateUObject(target, &UBorder::GetContentColorAndOpacity)
	, FDreamTweenLinearColorSetterFunction::CreateUObject(target, &UBorder::SetContentColorAndOpacity)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease)->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	return Tweener;
}
#pragma endregion
