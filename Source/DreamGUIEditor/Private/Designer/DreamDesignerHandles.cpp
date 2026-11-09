// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamDesignerHandles.h"

#include "Core/Components/DreamWidget.h"
#include "UObject/UnrealType.h"

namespace DreamDesignerHandlesLocal
{
	TArray<TSharedRef<IDreamDesignerHandleProvider>>& GetRegistry()
	{
		static TArray<TSharedRef<IDreamDesignerHandleProvider>> Providers;
		return Providers;
	}
}

FDreamDesignerHandle& FDreamDesignerHandleSet::AddHandle(int32 InId, const FVector2D& InLocal)
{
	FDreamDesignerHandle& Handle = Handles.AddDefaulted_GetRef();
	Handle.Id = InId;
	Handle.Local = InLocal;
	return Handle;
}

void FDreamDesignerHandleSet::AddLine(const FVector2D& InFrom, const FVector2D& InTo, const FLinearColor& InColor, float InThickness)
{
	FDreamDesignerHandleLine& Line = Lines.AddDefaulted_GetRef();
	Line.From = InFrom;
	Line.To = InTo;
	Line.Color = InColor;
	Line.Thickness = InThickness;
}

void FDreamDesignerHandleSet::AddPolyline(TConstArrayView<FVector2D> InPoints, bool bInClosed, const FLinearColor& InColor, float InThickness)
{
	for (int32 Index = 0; Index + 1 < InPoints.Num(); ++Index)
	{
		AddLine(InPoints[Index], InPoints[Index + 1], InColor, InThickness);
	}
	if (bInClosed && InPoints.Num() > 2)
	{
		AddLine(InPoints.Last(), InPoints[0], InColor, InThickness);
	}
}

void FDreamDesignerHandleSet::AddEllipse(const FVector2D& InCenter, const FVector2D& InRadii, float InAngleDegrees, const FLinearColor& InColor, int32 InSegments)
{
	const int32 Segments = FMath::Clamp(InSegments, 8, 256);
	// Clockwise on screen is towards -Y here: local space runs up.
	double Sine = 0.0;
	double Cosine = 1.0;
	FMath::SinCos(&Sine, &Cosine, FMath::DegreesToRadians(static_cast<double>(InAngleDegrees)));
	const FVector2D AxisX(Cosine, -Sine);
	const FVector2D AxisY(Sine, Cosine);
	TArray<FVector2D> Points;
	Points.Reserve(Segments);
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		double PointSine = 0.0;
		double PointCosine = 1.0;
		FMath::SinCos(&PointSine, &PointCosine, UE_DOUBLE_TWO_PI * Index / Segments);
		Points.Add(InCenter + AxisX * (PointCosine * InRadii.X) + AxisY * (PointSine * InRadii.Y));
	}
	AddPolyline(Points, /*bInClosed*/ true, InColor);
}

void IDreamDesignerHandleProvider::OnRestored(UDreamWidget* InWidget, TConstArrayView<FDreamDesignerEditedProperty> InProperties)
{
	for (const FDreamDesignerEditedProperty& Edited : InProperties)
	{
		UObject* Object = Edited.Object.Get();
		FProperty* Property = Object != nullptr ? Object->GetClass()->FindPropertyByName(Edited.Property) : nullptr;
		if (Property != nullptr)
		{
			FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
			Object->PostEditChangeProperty(Event);
		}
	}
}

void FDreamDesignerHandles::Register(const TSharedRef<IDreamDesignerHandleProvider>& InProvider)
{
	DreamDesignerHandlesLocal::GetRegistry().AddUnique(InProvider);
}

void FDreamDesignerHandles::Unregister(const TSharedRef<IDreamDesignerHandleProvider>& InProvider)
{
	DreamDesignerHandlesLocal::GetRegistry().Remove(InProvider);
}

const TArray<TSharedRef<IDreamDesignerHandleProvider>>& FDreamDesignerHandles::GetProviders()
{
	return DreamDesignerHandlesLocal::GetRegistry();
}

TSharedPtr<IDreamDesignerHandleProvider> FDreamDesignerHandles::FindProvider(FName InName)
{
	for (const TSharedRef<IDreamDesignerHandleProvider>& Provider : DreamDesignerHandlesLocal::GetRegistry())
	{
		if (Provider->GetName() == InName)
		{
			return Provider;
		}
	}
	return nullptr;
}

FVector2D FDreamDesignerHandles::RectFractionToLocal(const UDreamWidget* InWidget, const FVector2D& InFromTopLeft)
{
	if (InWidget == nullptr)
	{
		return FVector2D::ZeroVector;
	}
	return FVector2D(
		InWidget->GetLocalSpaceLeft() + InFromTopLeft.X * InWidget->GetWidth(),
		InWidget->GetLocalSpaceTop() - InFromTopLeft.Y * InWidget->GetHeight());
}

FVector2D FDreamDesignerHandles::LocalToRectFraction(const UDreamWidget* InWidget, const FVector2D& InLocal)
{
	if (InWidget == nullptr)
	{
		return FVector2D::ZeroVector;
	}
	const double Width = FMath::Max(static_cast<double>(InWidget->GetWidth()), UE_KINDA_SMALL_NUMBER);
	const double Height = FMath::Max(static_cast<double>(InWidget->GetHeight()), UE_KINDA_SMALL_NUMBER);
	return FVector2D((InLocal.X - InWidget->GetLocalSpaceLeft()) / Width, (InWidget->GetLocalSpaceTop() - InLocal.Y) / Height);
}

FLinearColor FDreamDesignerHandles::GetGuideColor()
{
	return FLinearColor(0.95f, 0.95f, 0.95f, 0.9f);
}

FLinearColor FDreamDesignerHandles::GetActiveColor()
{
	return FLinearColor(1.0f, 0.78f, 0.2f, 1.0f);
}
