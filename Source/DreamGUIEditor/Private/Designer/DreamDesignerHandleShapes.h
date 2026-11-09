// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Designer/DreamDesignerHandles.h"
#include "UObject/Object.h"
#include "UObject/UnrealType.h"

/**
 * What the effects' handle providers share: a rect's corners as handles, angles snapped, and a property read by its name --
 * the effects keep most of what the handles move protected, with setters for some of it and none for the rest.
 */
namespace DreamDesignerHandleShapes
{
	/** How far in from where it marks a handle sits at the least, in pixels, so that it is not on top of the selection's. */
	constexpr double MinInsetPixels = 12.0;
	/** The step Shift snaps an angle to, in degrees. */
	constexpr double SnapDegrees = 15.0;

	/** A corner's direction from a rect's centre, in a widget's corner order: right-bottom, right-top, left-top, left-bottom (+Y up). */
	inline FVector2D CornerSign(int32 InCorner)
	{
		static const FVector2D Signs[4] = { FVector2D(1.0, -1.0), FVector2D(1.0, 1.0), FVector2D(-1.0, 1.0), FVector2D(-1.0, -1.0) };
		return Signs[FMath::Clamp(InCorner, 0, 3)];
	}

	inline float GetCorner(const FVector4f& InRadii, int32 InCorner)
	{
		return InCorner == 0 ? InRadii.X : InCorner == 1 ? InRadii.Y : InCorner == 2 ? InRadii.Z : InRadii.W;
	}

	inline void SetCorner(FVector4f& InOutRadii, int32 InCorner, float InValue)
	{
		(InCorner == 0 ? InOutRadii.X : InCorner == 1 ? InOutRadii.Y : InCorner == 2 ? InOutRadii.Z : InOutRadii.W) = InValue;
	}

	/** A rounded corner's handle: in from the corner along its diagonal by its radius, or by MinInsetPixels while that is more. */
	inline FVector2D CornerHandle(const FVector2D& InCentre, const FVector2D& InHalf, float InRadius, int32 InCorner, double InPixelsPerUnit)
	{
		const FVector2D Sign = CornerSign(InCorner);
		const double Inset = FMath::Max(static_cast<double>(InRadius), MinInsetPixels / FMath::Max(InPixelsPerUnit, UE_KINDA_SMALL_NUMBER));
		return InCentre + Sign * InHalf - Sign * Inset;
	}

	/** A corner's radius as its handle is dragged along its diagonal from where it was grabbed, InRadius then. */
	inline float DragCorner(float InRadius, const FDreamDesignerHandleDrag& InDrag, int32 InCorner, float InMost)
	{
		const FVector2D Delta = InDrag.PointerLocal - InDrag.GrabLocal;
		const double Along = FVector2D::DotProduct(Delta, -CornerSign(InCorner)) * 0.5;
		return FMath::Clamp(static_cast<float>(InRadius + Along), 0.0f, FMath::Max(InMost, 0.0f));
	}

	/** Every corner set to one radius, or only InCorner. */
	inline FVector4f WithCorner(const FVector4f& InRadii, int32 InCorner, float InRadius, bool bInAlone)
	{
		if (!bInAlone)
		{
			return FVector4f(InRadius, InRadius, InRadius, InRadius);
		}
		FVector4f Radii = InRadii;
		SetCorner(Radii, InCorner, InRadius);
		return Radii;
	}

	inline double Snap(double InDegrees, bool bInSnap)
	{
		return bInSnap ? FMath::RoundToDouble(InDegrees / SnapDegrees) * SnapDegrees : InDegrees;
	}

	/** InObject's property InName, read as T: InDefault when there is none of T's size. Not for a bool; ReadBool is. */
	template <typename T>
	T Read(const UObject* InObject, FName InName, const T& InDefault = T())
	{
		const FProperty* Property = InObject != nullptr ? InObject->GetClass()->FindPropertyByName(InName) : nullptr;
		return Property != nullptr && Property->GetElementSize() == sizeof(T) ? *Property->ContainerPtrToValuePtr<T>(InObject) : InDefault;
	}

	inline bool ReadBool(const UObject* InObject, FName InName, bool bInDefault = false)
	{
		const FBoolProperty* Property = InObject != nullptr ? CastField<FBoolProperty>(InObject->GetClass()->FindPropertyByName(InName)) : nullptr;
		return Property != nullptr ? Property->GetPropertyValue_InContainer(InObject) : bInDefault;
	}

	/**
	 * Writes InValue into InObject's property InName when it differs, and tells the object, as the details panel does: for
	 * what has no setter. True when it wrote.
	 */
	template <typename T>
	bool Write(UObject* InObject, FName InName, const T& InValue)
	{
		FProperty* Property = InObject != nullptr ? InObject->GetClass()->FindPropertyByName(InName) : nullptr;
		if (Property == nullptr || Property->GetElementSize() != sizeof(T))
		{
			return false;
		}
		T* Value = Property->ContainerPtrToValuePtr<T>(InObject);
		if (*Value == InValue)
		{
			return false;
		}
		*Value = InValue;
		FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
		InObject->PostEditChangeProperty(Event);
		return true;
	}
}
